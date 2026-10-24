#define _CRT_RAND_S
#include "support_protocol.h"
#include "coop_extension_api.h"
#include "crew.h"
#include "online_authority.h"
#include "mission_participants.h"
#include "command_net.h"
#include "command_protocol.h"
#include "debug_spawn_net.h"
#include "pvp.h"
#include "enemy_control_net.h"
#include "sub_support_state.h"
#include "carrier_drone_net.h"
#include "floatgun.h"
#include "gravity_net.h"
#include "ultimate_net.h"
#include "support_submarine.h"
#include "support_soldier.h"
#include "command_identity.h"
#include "version_notice.h"
#include "hudtext.h"
#include "plugin_net.h"
#include "qmark.h"
#include "qmark_protocol.h"
#include "enemy_control_protocol.h"
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <algorithm>

namespace crew {
namespace {
support_net::Session session;
support_net::Hooks hooks;
EDF6CoopExtensionApi api{};
EDF6CoopTrySendExtensionFn trySendExtension=nullptr;
EDF6CoopSnapshot snapshot{};
EDF6CoopPeer peers[support_net::kMaxPeers+1]{};
HMODULE module=nullptr;
using AdmissionReadyFn=std::uint32_t(__cdecl*)();
AdmissionReadyFn transportAdmissionReady=nullptr;
bool running=false,blockedUntilMission=false;
std::uint32_t commandHostPeer=0;
ULONGLONG nextResolve=0,lastTick=~0ULL;
SRWLOCK worldLock=SRWLOCK_INIT;
EDF6AFMissionParticipants world{sizeof(EDF6AFMissionParticipants),0,0,0,1,{}};
bool worldFrozen=false,worldCreationSeen=false;
std::uint64_t worldSerial=1;
std::uint32_t worldPhase=0;
ObjRef worldCreated[support_net::kMaxPeers]{};
ULONGLONG nextWorldRead=0;
// The room's builds (version_notice.h): peers whose map command messages carry another protocol version, and the notice
// last said (logged and shown once a change).
std::uint32_t commandVersionOf[support_net::kMaxPeers+1]{};
versionnote::Notice versionShown{};
SRWLOCK versionLock=SRWLOCK_INIT;
wchar_t versionText[200]{};
ULONGLONG versionAt=0;
// Why the transport is (not) usable, logged once a change: an online request refused as "not ready" has its cause in
// the log (the 2026-10-09 report had none).
const char* transportState="";
void NoteTransport(const char* state) noexcept {
    if(std::strcmp(state,transportState)==0)return;
    transportState=state;
    Log("SUPPORT NET transport: %s",state);
}

bool PuidText(void* puid,EDF6CoopPeer& out) noexcept {
    out={};
    const HMODULE eos=GetModuleHandleW(L"EOSSDK-Win64-Shipping.dll");
    if(!puid || !eos)return false;
    using Convert=int(__cdecl*)(void*,char*,std::int32_t*);
    const auto convert=reinterpret_cast<Convert>(GetProcAddress(eos,"EOS_ProductUserId_ToString"));
    if(!convert)return false;
    std::int32_t length=sizeof(out.id);
    __try {return convert(puid,out.id,&length)==0 && length>1 && length<=sizeof(out.id) &&
        out.id[0] && std::memchr(out.id,0,sizeof(out.id));}
    __except(EXCEPTION_EXECUTE_HANDLER){out={};return false;}
}
void ResetWorld() noexcept {
    AcquireSRWLockExclusive(&worldLock);
    world={};world.size=sizeof(world);worldFrozen=false;worldCreationSeen=false;worldPhase=2;
    for(auto& actor:worldCreated)actor={};
    if(worldSerial!=UINT64_MAX)world.worldEpoch=++worldSerial;
    ReleaseSRWLockExclusive(&worldLock);nextWorldRead=0;
}
void WorldTick(ULONGLONG now) noexcept {
    if(!hooks.participants || !hooks.createdMatches || !hooks.admissionReady || !hooks.admissionReady() ||
       !world.worldEpoch || !worldCreationSeen || now<nextWorldRead)return;
    nextWorldRead=now+250;
    // Only the game thread writes worldFrozen. Once sealed, deaths, respawns,
    // team changes and lobby churn do not redefine current-world membership.
    if(worldFrozen)return;
    void* puids[support_net::kMaxPeers]{};
    std::uint32_t count=0,expected=0;
    if(!hooks.participants(puids,support_net::kMaxPeers,&count,&expected) ||
       !expected || expected>support_net::kMaxPeers || count!=expected)return;
    if(!hooks.createdMatches(worldCreated,expected))return;
    EDF6AFMissionParticipants next{};next.size=sizeof(next);next.worldEpoch=world.worldEpoch;
    for(std::uint32_t i=0;i<count;++i)if(!PuidText(puids[i],next.participants[i]))return;
    auto less=[](const EDF6CoopPeer& a,const EDF6CoopPeer& b){return std::strcmp(a.id,b.id)<0;};
    auto equal=[](const EDF6CoopPeer& a,const EDF6CoopPeer& b){return std::strcmp(a.id,b.id)==0;};
    std::sort(next.participants,next.participants+count,less);
    const auto end=std::unique(next.participants,next.participants+count,equal);
    next.participantCount=static_cast<std::uint32_t>(end-next.participants);
    // Split-screen humans share a PUID, but all expected native player actors
    // must have existed before deduplicating them into a network ACK quorum.
    for(std::uint32_t i=next.participantCount;i<count;++i)next.participants[i]={};
    next.ready=1;
    AcquireSRWLockExclusive(&worldLock);world=next;worldFrozen=true;worldPhase=3;ReleaseSRWLockExclusive(&worldLock);
    Log("SUPPORT world participants sealed: actors=%u peers=%u epoch=%llu",count,next.participantCount,
        static_cast<unsigned long long>(next.worldEpoch));
}

std::uint64_t Nonce(void*) noexcept {
    unsigned lo=0,hi=0;if(rand_s(&lo) || rand_s(&hi))return 0;
    return (static_cast<std::uint64_t>(hi)<<32 | lo) | 1;
}
bool Send(void*,std::uint32_t peer,const support_net::Message& message) noexcept {
    if(!running || !peer || peer>snapshot.peerCount || !api.send)return false;
    unsigned char bytes[support_net::kWireSize]{};
    const bool encoded=support_net::Encode(message,bytes,sizeof(bytes));
    const bool sent=encoded && api.send(snapshot.generation,&peers[peer],bytes,static_cast<std::uint32_t>(sizeof(bytes)))!=0;
    if(!sent)Log("SUPPORT NET send failed: kind=%u peer=%u encoded=%d generation=%llu",
        static_cast<unsigned>(message.kind),peer,encoded,static_cast<unsigned long long>(snapshot.generation));
    return sent;
}
bool SendCommand(std::uint32_t peer,const void* bytes,std::size_t count) noexcept {
    return running && peer && peer<=snapshot.peerCount && bytes && count &&
        count<=EDF6COOP_EXTENSION_MAX_PAYLOAD && api.send &&
        api.send(snapshot.generation,&peers[peer],bytes,static_cast<std::uint32_t>(count))!=0;
}
std::uint32_t TrySendCommand(std::uint32_t peer,const void* bytes,std::size_t count,std::uint32_t byteLimit,std::uint32_t packetLimit) noexcept {
    if(!running || !peer || peer>snapshot.peerCount || !bytes || !count ||
       count>EDF6COOP_EXTENSION_MAX_PAYLOAD || !trySendExtension)return EDF6COOP_EXTENSION_SEND_UNAVAILABLE;
    return trySendExtension(snapshot.generation,&peers[peer],bytes,static_cast<std::uint32_t>(count),byteLimit,packetLimit);
}
const char* PeerPuid(std::uint32_t peer) noexcept {
    return peer<=snapshot.peerCount && peer<=support_net::kMaxPeers ? peers[peer].id : nullptr;
}
std::uint32_t HeardExtension(std::uint32_t peer) noexcept { return session.PeerExtension(peer); }
std::uint32_t OwnExtension() noexcept {
    std::uint32_t ext=0;unsigned char bloom[32]{};
    if(hooks.variants)hooks.variants(&ext,bloom);
    return ext|support_net::kExtBuilt;
}
// Every plugin channel's frame (plugin_net.h: the registry, the roster, the joins). The Q marks go peer to peer with no
// host step: the context's `linked` (the transport alone) is theirs.
void CommandContext(ULONGLONG now) noexcept {
    plugin_net::Room room;
    room.context=CommandNetworkContext{running && session.Ready(),snapshot.isHost!=0,session.Epoch(),commandHostPeer,
        snapshot.peerCount,snapshot.local.id,&SendCommand,&TrySendCommand,running};
    room.puid=&PeerPuid;room.heard=&HeardExtension;room.ownExt=OwnExtension();
    plugin_net::Update(room,now);
}
void QMarkUpdate(const CommandNetworkContext& c,std::uint64_t now) noexcept { UpdateQMarkNetwork(c.linked,c.peerCount,c.send,now); }
bool PvpOwns(const void* bytes,std::size_t size) noexcept {
    const auto* b=static_cast<const unsigned char*>(bytes);
    return bytes && size>=4 && b[0]=='A' && b[1]=='F' && b[2]=='P' && b[3]=='V';
}
// The channels, in the old chain's order (qmark, command, PvP, enemy control, the submarine's and the carrier's drones,
// then the gravity operations, the float guns and the ultimate strike). The older ones read their own versions (version 0) and are read by
// every build that has them (no feature bit); the newer ones are only sent to the peers that announced theirs.
void RegisterPluginChannels() noexcept {
    static bool registered=false;
    if(registered)return;
    registered=true;
    using plugin_net::Channel;
    Channel c;
    c=Channel{};c.name="qmark";c.magic=qmark_net::kMagic;c.receive=&ReceiveQMarkNetwork;c.update=&QMarkUpdate;c.reset=&ResetQMarkNetwork;
    plugin_net::RegisterChannel(c);
    c=Channel{};c.name="command";c.magic=command_net::kMagic;c.receive=&ReceiveCommandNetwork;c.update=&UpdateCommandNetwork;
    c.reset=&ResetCommandNetwork;plugin_net::RegisterChannel(c);
    c=Channel{};c.name="pvp";c.owns=&PvpOwns;c.receive=&ReceivePvpNetwork;c.update=&UpdatePvpNetwork;c.reset=&ResetPvpNetwork;
    plugin_net::RegisterChannel(c);
    c=Channel{};c.name="enemy control";c.magic=enemy_control_protocol::kMagic;c.receive=&ReceiveEnemyControlNetwork;
    c.update=&UpdateEnemyControlNetwork;c.reset=&ResetEnemyControlNetwork;plugin_net::RegisterChannel(c);
    c=Channel{};c.name="submarine drones";c.magic=sub_support::kMagic;c.receive=&ReceiveSubSupportNetwork;
    c.update=&UpdateSubSupportNetwork;c.reset=&ResetSubSupportNetwork;plugin_net::RegisterChannel(c);
    c=Channel{};c.name="carrier drones";c.magic=carrier_drone_net::kMagic;c.receive=&ReceiveCarrierDroneNetwork;
    c.update=&UpdateCarrierDroneNetwork;c.reset=&ResetCarrierDroneNetwork;plugin_net::RegisterChannel(c);
    plugin_net::RegisterChannel(GravityNetChannel());
    plugin_net::RegisterChannel(FloatGunNetChannel());
    plugin_net::RegisterChannel(UltimateNetChannel());
}
// A message no channel and not the support protocol owns (a newer build's channel this one does not have): dropped,
// said once a magic (a few magics), never once a message.
void NoteUnknown(std::uint32_t peer,const unsigned char* bytes,std::uint32_t count) noexcept {
    static std::uint32_t seen[8]{};static unsigned seenCount=0;
    std::uint32_t magic=0;if(count>=4)std::memcpy(&magic,bytes,4);
    for(unsigned i=0;i<seenCount;++i)if(seen[i]==magic)return;
    if(seenCount>=_countof(seen))return;
    seen[seenCount++]=magic;
    Log("SUPPORT NET peer %u sends messages of a channel this build does not have (magic 0x%08X, %u bytes): ignored",peer,magic,count);
}
bool PeerValid(const EDF6CoopPeer& peer) noexcept {
    return peer.id[0] && std::memchr(peer.id,0,sizeof(peer.id));
}
bool Resolve(ULONGLONG now) noexcept {
    const HMODULE current=GetModuleHandleW(L"EDF6Coop.dll");
    if(current && current==module && api.snapshot) {
        const bool gate=transportAdmissionReady && transportAdmissionReady()!=0;
        if(!gate)NoteTransport("EDF6Coop mission admission gate not ready");
        return gate;
    }
    api={};module=nullptr;transportAdmissionReady=nullptr;trySendExtension=nullptr;
    if(now<nextResolve)return false;nextResolve=now+1000;
    if(!current){NoteTransport("no EDF6Coop.dll loaded (map support online needs the companion EDF6Coop on every player)");return false;}
    const auto get=reinterpret_cast<EDF6CoopGetExtensionApiFn>(GetProcAddress(current,"EDF6CoopGetExtensionApi"));
    transportAdmissionReady=reinterpret_cast<AdmissionReadyFn>(GetProcAddress(current,"EDF6Coop_MissionAdmissionReady"));
    if(!get || !get(EDF6COOP_EXTENSION_VERSION,sizeof(api),&api) || api.size!=sizeof(api) ||
       api.version!=EDF6COOP_EXTENSION_VERSION || !api.snapshot || !api.peer || !api.send || !api.poll ||
       !transportAdmissionReady || !transportAdmissionReady() || !GetProcAddress(current,"EDF6Coop_ResolveMissionPlayerPuid")) {
        NoteTransport("EDF6Coop present but without a ready af-support extension (older EDF6Coop, or its admission gate is off)");
        api={};return false;
    }
    trySendExtension=reinterpret_cast<EDF6CoopTrySendExtensionFn>(GetProcAddress(current,"EDF6CoopTrySendExtension"));
    module=current;return true;
}
bool Start(const EDF6CoopSnapshot& next,ULONGLONG now) noexcept {
    running=false;
    if(!PeerValid(next.local) || !PeerValid(next.host) || next.peerCount>support_net::kMaxPeers ||
       !next.generation || next.isHost>1 || (next.isHost!=0)!=OnlineHostOnly())return false;
    peers[0]=next.local;
    std::uint32_t hostPeer=0;
    for(std::uint32_t i=1;i<=next.peerCount;++i) {
        if(!api.peer(next.generation,i-1,&peers[i]) || !PeerValid(peers[i]))return false;
        for(std::uint32_t j=0;j<i;++j)if(!std::strcmp(peers[i].id,peers[j].id))return false;
        if(!std::strcmp(peers[i].id,next.host.id))hostPeer=i;
    }
    if((next.isHost && std::strcmp(next.local.id,next.host.id)) || (!next.isHost && !hostPeer))return false;
    snapshot=next;running=true;
    commandHostPeer=hostPeer;
    session.Start(next.isHost!=0,next.peerCount,hostPeer,now);
    Log("SUPPORT NET application started: host=%u peers=%u pending=%u",next.isHost,next.peerCount,session.PendingHandshakes());
    return true;
}
void Note(wchar_t* note,std::size_t size,const wchar_t* text) noexcept {
    if(note && size)swprintf_s(note,size,L"%ls",text);
}
// A header of another build: the map command protocol's magic with another version (support's own wire is v2 in every
// build that has the extension). Only read: the message is still dropped as before.
void NoteForeignVersion(std::uint32_t peer,const unsigned char* bytes,std::uint32_t count) noexcept {
    if(!peer || peer>support_net::kMaxPeers || count<8)return;
    std::uint32_t magic=0,version=0;std::memcpy(&magic,bytes,4);std::memcpy(&version,bytes+4,4);
    if(magic!=command_net::kMagic)return;
    const std::uint32_t seen=command_net::Compatible(version) ? 0u : version;   // v6 and v7 negotiate (command_protocol.h)
    if(commandVersionOf[peer]!=seen && seen)
        Log("SUPPORT NET peer %u sends map commands of protocol v%u (this build: v%u): its map orders are not synchronised",peer,version,
            command_net::kVersion);
    commandVersionOf[peer]=seen;
}
// The features a set of feature bits (support_protocol.h Features: capabilities and extensions) names, in the HUD's language.
void Features(std::uint32_t caps,bool command,wchar_t* out,std::size_t size) noexcept {
    using hudtext::Tr;using hudtext::Tx;using support_net::FeatureExt;
    out[0]=0;
    const struct { std::uint32_t bit; Tx text; } kNames[]={{FeatureExt(support_net::kExtSeaRescue),Tx::versionFeatRescue},
        {FeatureExt(support_net::kExtRescueChannel),Tx::versionFeatRescueChannel},
        {FeatureExt(support_net::kExtOutfit41),Tx::versionFeatOutfit41},
        {FeatureExt(support_net::kExtBvmPlayers),Tx::versionFeatBvmPlayers},
        {FeatureExt(support_net::kExtBackpack),Tx::versionFeatBackpack},
        {FeatureExt(support_net::kExtGravity),Tx::versionFeatGravity},
        {FeatureExt(support_net::kExtFloatGun),Tx::versionFeatFloatGun},
        {FeatureExt(support_net::kExtUltimate),Tx::versionFeatUltimate},
        {support_net::kCapAirborneAir,Tx::versionFeatAir},{support_net::kCapTransports,Tx::versionFeatTransport},
        {support_net::kCapLoadout,Tx::versionFeatCompose},{FeatureExt(support_net::kExtVariants),Tx::versionFeatVariants},
        {support_net::kCapSoldierVariants,Tx::versionFeatLoadout}};
    // At most kNamed named, then "and more": the line stays within the HUD's one line.
    constexpr int kNamed=3;
    int named=0;bool more=false;
    const auto add=[&](const wchar_t* word) noexcept {
        if(named>=kNamed){more=true;return;}
        const std::size_t used=std::wcslen(out);
        _snwprintf_s(out+used,size-used,_TRUNCATE,L"%ls%ls",used ? Tr(Tx::versionListSep) : L"",word);
        ++named;
    };
    // Without the rescue at all, its channel goes unsaid (the line stays within the HUD's one line).
    if(caps&FeatureExt(support_net::kExtSeaRescue))caps&=~FeatureExt(support_net::kExtRescueChannel);
    for(const auto& n:kNames)if(caps&n.bit)add(Tr(n.text));
    if(command)add(Tr(Tx::versionFeatCommand));
    if(!out[0])add(Tr(Tx::versionFeatSome));
    if(more){const std::size_t used=std::wcslen(out);_snwprintf_s(out+used,size-used,_TRUNCATE,L"%ls",Tr(Tx::versionListMore));}
}
// The room's builds compared (version_notice.h), said once a change: logged, and shown on the HUD a while.
void VersionTick(bool online) noexcept {
    versionnote::State s;
    s.online=online;s.host=snapshot.isHost!=0;s.mine=support_net::kFeatures;
    s.hostKnown=session.HostCapsKnown();s.hostCaps=session.HostCaps();s.roomBehind=session.RoomBehindHost();
    // The newest build whose welcome carried nothing: the one before the sea rescue (every capability, the variants).
    s.silentHost=support_net::Features(support_net::kCapabilities,support_net::kExtVariants);
    s.peersBehind=session.PeersBehind(&s.peersMissing);s.peersAhead=session.PeersAhead();
    for(std::uint32_t i=1;online && i<=snapshot.peerCount && i<=support_net::kMaxPeers;++i)if(commandVersionOf[i])++s.commandPeers;
    const versionnote::Notice n=versionnote::Compare(s);
    if(n==versionShown)return;
    versionShown=n;
    using hudtext::Tr;using hudtext::Tx;
    // Two lines: who is older or newer, then what does not work in this room because of it.
    wchar_t text[200]{},features[120]{};
    Features(n.missing,n.command,features,_countof(features));
    switch(n.kind) {
    case versionnote::Kind::none: break;
    case versionnote::Kind::hostOlder: _snwprintf_s(text,_TRUNCATE,L"%ls",Tr(Tx::versionHostOlder));break;
    case versionnote::Kind::selfOlder: _snwprintf_s(text,_TRUNCATE,L"%ls",Tr(Tx::versionSelfOlder));break;
    case versionnote::Kind::peersOlder: _snwprintf_s(text,_TRUNCATE,Tr(Tx::versionPeersOlder),static_cast<int>(n.count));break;
    case versionnote::Kind::peersNewer: _snwprintf_s(text,_TRUNCATE,Tr(Tx::versionPeersNewer),static_cast<int>(n.count));break;
    case versionnote::Kind::roomOlder: _snwprintf_s(text,_TRUNCATE,L"%ls",Tr(Tx::versionRoomOlder));break;
    case versionnote::Kind::commandOnly: _snwprintf_s(text,_TRUNCATE,L"%ls",Tr(Tx::versionCommandOnly));break;
    }
    if(n.kind!=versionnote::Kind::none && n.kind!=versionnote::Kind::selfOlder && n.kind!=versionnote::Kind::peersNewer) {
        const std::size_t used=std::wcslen(text);
        _snwprintf_s(text+used,_countof(text)-used,_TRUNCATE,L"\n");
        const std::size_t more=std::wcslen(text);
        _snwprintf_s(text+more,_countof(text)-more,_TRUNCATE,Tr(Tx::versionUnavailable),features);
    }
    Log("SUPPORT NET builds: kind=%d host=%d mine=0x%X hostKnown=%d hostCaps=0x%X roomBehind=%d peersBehind=%u missing=0x%X ahead=%u "
        "commandPeers=%u%s",static_cast<int>(n.kind),s.host,s.mine,s.hostKnown,s.hostCaps,s.roomBehind,s.peersBehind,s.peersMissing,
        s.peersAhead,s.commandPeers,n.kind==versionnote::Kind::none ? " (the same build everywhere)" : "");
    AcquireSRWLockExclusive(&versionLock);
    std::memcpy(versionText,text,sizeof(text));versionAt=n.kind==versionnote::Kind::none ? 0 : GetTickCount64();
    ReleaseSRWLockExclusive(&versionLock);
}
}
namespace {
bool PlanRequester(std::uint32_t peer,unsigned char* out,ObjRef* outActor=nullptr) noexcept {
    if(!out || !running || peer>snapshot.peerCount)return false;
    const char* authenticated=peers[peer].id;
    AcquireSRWLockShared(&worldLock);const bool sealed=worldFrozen && world.ready;ReleaseSRWLockShared(&worldLock);
    if(!sealed)return false;
    const HMODULE coop=GetModuleHandleW(L"EDF6Coop.dll");
    const auto resolve=coop ? reinterpret_cast<EDF6CoopResolveMissionPlayerPuidFn>(GetProcAddress(coop,"EDF6Coop_ResolveMissionPlayerPuid")) : nullptr;
    if(!resolve)return false;
    for(std::uint32_t i=0;i<support_net::kMaxPeers;++i) {
        AcquireSRWLockShared(&worldLock);const ObjRef actor=worldCreated[i];ReleaseSRWLockShared(&worldLock);
        if(!actor.obj)continue;
        EDF6CoopPeer who{};
        if(!resolve(static_cast<std::int32_t>(i),&who) || !PeerValid(who) || std::strcmp(who.id,authenticated))continue;
        __try {
            unsigned char candidate[32]{};MarkIdentities identities{};
            if(ReadNativeObjectId(static_cast<const unsigned char*>(actor.obj),candidate) &&
               ResolveMarkIdentities(candidate,nullptr,&identities) && identities.player.obj==actor.obj &&
               identities.player.ctrl==actor.ctrl && SupportCommandRequesterMatches(identities.puid,authenticated)) {
                std::memcpy(out,candidate,32);if(outActor)*outActor=actor;return true;
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    return false;
}
support_net::PlanResult PlanForRequester(std::uint32_t peer,std::uint32_t catalog,const float* target,
                                         std::uint64_t loadout,support_net::Plan* out) noexcept {
    if(catalog==debugspawn::kNetworkCatalog)return PlanRangeEnemy(catalog,target,loadout,out);
    if(!out || !running || peer>snapshot.peerCount)return support_net::PlanResult::refused;
    const int team=PvpSupportTeam(peers[peer].id);
    if(team<0)return support_net::PlanResult::refused; // an unconfirmed transition must not silently create stock-team support
    const bool ownership=session.PeersHaveDetails(support_net::kDetailPvpSupport);
    if(team && !ownership)return support_net::PlanResult::refused;
    const auto result=hooks.planForRequester ? hooks.planForRequester(peer,catalog,target,loadout,out) :
        hooks.plan ? hooks.plan(catalog,target,loadout,out) : support_net::PlanResult::refused;
    if(result!=support_net::PlanResult::ready)return result;
    if(ownership) {
        if(!PlanRequester(peer,out->requester))return support_net::PlanResult::refused;
        out->team=static_cast<std::uint32_t>(team);
    }
    return PvpValidateSupportPlan(*out) ? result : support_net::PlanResult::refused;
}
}
bool SupportAuthenticatedRequester(std::uint32_t peer,ObjRef* out) noexcept {
    if(!out)return false;
    *out={};unsigned char identity[32]{};
    return PlanRequester(peer,identity,out);
}
void ConfigureSupportNet(const support_net::Hooks& configured) noexcept {
    ResetSupportNet();hooks=configured;
    auto combined=hooks;
    combined.plan=[](std::uint32_t catalog,const float* target,std::uint64_t loadout,support_net::Plan* out) noexcept {
        if(catalog==debugspawn::kNetworkCatalog)return PlanRangeEnemy(catalog,target,loadout,out);
        return hooks.plan ? hooks.plan(catalog,target,loadout,out) : support_net::PlanResult::refused;
    };
    combined.planForRequester=&PlanForRequester;
    combined.prepare=[](const support_net::Plan& plan) noexcept {   // the range's rows load before READY (debug_spawn.cpp)
        return plan.catalogId==debugspawn::kNetworkCatalog ? PrepareRangeEnemy(plan) :
            hooks.prepare ? hooks.prepare(plan) : support_net::PlanResult::ready;
    };
    combined.validate=[](const support_net::Plan& plan) noexcept {
        return plan.catalogId==debugspawn::kNetworkCatalog ? ValidateRangeEnemy(plan) :
            PvpValidateSupportPlan(plan) && hooks.validate && hooks.validate(plan);
    };
    combined.spawn=[](std::uint64_t token,const support_net::Plan& plan,bool remote) noexcept {
        return plan.catalogId==debugspawn::kNetworkCatalog ? SpawnRangeEnemy(token,plan,remote) : hooks.spawn && hooks.spawn(token,plan,remote);
    };
    combined.destroy=[](std::uint64_t token) noexcept {
        DestroyRangeEnemy(token);
        if(hooks.destroy)hooks.destroy(token);
    };
    combined.notice=[](std::uint32_t request,support_net::RequestStatus status) noexcept {
        if(!RangeEnemyRequestNotice(request,status) && hooks.notice)hooks.notice(request,status);
    };
    session.Configure({nullptr,&Send,&Nonce,combined});
}
void ResetSupportNet() noexcept {
    session.Stop();ResetRangeEnemyNetwork();running=false;blockedUntilMission=false;snapshot={};lastTick=~0ULL;ResetWorld();
    for(auto& v:commandVersionOf)v=0;
    versionShown={};AcquireSRWLockExclusive(&versionLock);versionAt=0;ReleaseSRWLockExclusive(&versionLock);
    commandHostPeer=0;RegisterPluginChannels();plugin_net::Reset();
}
void SuspendSupportNet() noexcept {
    session.Suspend();running=false;blockedUntilMission=true;
    CommandContext(GetTickCount64());
    Log("SUPPORT NET suspended: existing actors retained; mission participant resynchronization required");
}
void RunSupportNetTick() noexcept {
    const ULONGLONG now=GetTickCount64();
    if(lastTick==now)return;lastTick=now;
    if(InSession())WorldTick(now);
    if(blockedUntilMission)return;
    if(!Cfg().enabled || !InSession() || !Resolve(now)) {
        if(!InSession())NoteTransport("offline");
        if(running)SuspendSupportNet();
        return;
    }
    EDF6CoopSnapshot next{};next.size=sizeof(next);
    if(!api.snapshot(&next) || !next.ready || next.size!=sizeof(next)) {
        NoteTransport("EDF6Coop extension room not ready (direct link, every participant's af-support/2 marker, or the sealed world)");
        if(running)SuspendSupportNet();
        return;
    }
    if(running && snapshot.generation!=next.generation){SuspendSupportNet();return;}
    if(!running && !Start(next,now)){NoteTransport("EDF6Coop snapshot rejected (peer list / host identity)");return;}
    NoteTransport(next.isHost ? "ready (host)" : "ready (client)");
    CommandContext(now);
    // Never spawn on the DirectNet worker. The map/crew game-thread frame owns
    // both deserialization and the native create/register/destroy callbacks.
    for(unsigned received=0;received<512;++received) {
        EDF6CoopPeer sender{};unsigned char bytes[EDF6COOP_EXTENSION_MAX_PAYLOAD]{};std::uint32_t count=0;
        if(!api.poll(snapshot.generation,&sender,bytes,sizeof(bytes),&count))break;
        if(!PeerValid(sender))continue;
        std::uint32_t peer=0;
        for(std::uint32_t i=1;i<=snapshot.peerCount;++i)if(!std::strcmp(sender.id,peers[i].id)){peer=i;break;}
        if(peer) {
            NoteForeignVersion(peer,bytes,count);
            CommandContext(now);
            if(plugin_net::Receive(peer,sender.id,bytes,count,now))continue;   // a channel's (plugin_net.h), before the support parser
            support_net::Message message;
            if(support_net::Decode(bytes,count,message)) {
                const auto pending=session.PendingHandshakes();
                session.Receive(peer,message,now);
                if(session.PendingHandshakes()!=pending)Log("SUPPORT NET handshake: kind=%u peer=%u pending=%u ready=%d",
                    static_cast<unsigned>(message.kind),peer,session.PendingHandshakes(),session.Ready());
            } else if(count>=4 && wire::HeadWord(bytes,count,0)==support_net::kMagic) {
                Log("SUPPORT NET malformed application packet: peer=%u bytes=%u",peer,count);
            } else NoteUnknown(peer,bytes,count);
        }
    }
    session.SetRescueCooldown(static_cast<std::uint64_t>(Cfg().seaRescueCooldownSec)*1000);   // the host's own setting decides
    session.Tick(now);
    RangeEnemyNetworkTick();
    VersionTick(running);
    CommandContext(now);
    if(session.Suspended())SuspendSupportNet();
}
void SupportNetTick() noexcept {
    // An authenticated carrier request may enqueue a prepared Session plan from inside this sole poll.
    // Do not recursively poll/advance the Session when SubmitPreparedSupportPlan asks for a current context.
    static bool ticking=false; // game thread only, just like the native spawn/owner callbacks
    if(ticking)return;
    ticking=true;
    __try {RunSupportNetTick();}
    __finally {ticking=false;}
}
bool SubmitSupportRequest(int catalogId,const float* target,wchar_t* note,std::size_t size,std::uint64_t loadout) noexcept {
    SupportNetTick();
    if(blockedUntilMission) {
        Note(note,size,L"联机参与者同步已暂停，现有支援保留；重新开始关卡后可再呼叫");return false;
    }
    if(catalogId<0 || !running || !session.Ready()) {
        Log("SUPPORT NET request refused: catalog=%d transport=%d pending=%u ready=%d state=%s",catalogId,running,
            session.PendingHandshakes(),session.Ready(),transportState);
        Note(note,size,running ? L"支援自动握手尚未完成：联机通道已连接，但部署协议未就绪" :
            L"支援联机通道未就绪：当前参与者或认证直连尚未同步");return false;
    }
    if(!session.Submit(static_cast<std::uint32_t>(catalogId),target,GetTickCount64(),loadout)) {
        Log("SUPPORT NET request not admitted: catalog=%d pending=%u",catalogId,session.PendingHandshakes());
        Note(note,size,L"支援请求未受理：已有部署、调用过快或本关支援额度已满");return false;
    }
    Log("SUPPORT NET request accepted: catalog=%d host=%u",catalogId,snapshot.isHost);
    Note(note,size,L"支援调度中：规划入场路线并同步部署");return true;
}
void ReportSupportFailure(std::uint64_t transaction) noexcept {
    if(transaction) {
        Log("SUPPORT NET rollback transaction=%llu",static_cast<unsigned long long>(transaction));
        session.Failed(transaction);
    }
}
bool SupportTransactionActive(std::uint64_t transaction) noexcept { return session.IsActive(transaction); }
bool SupportPeersHaveDetails(std::uint32_t details) noexcept { return !InSession() || (running && session.PeersHaveDetails(details)); }
bool SupportPeersAcceptVariants() noexcept { return !running || session.PeersHave(support_net::kCapSoldierVariants); }
bool SupportPeersAcceptAirborne() noexcept { return !running || session.PeersHave(support_net::kCapAirborneAir); }
bool SupportPeersAcceptTransports() noexcept { return !running || session.PeersHave(support_net::kCapTransports); }
bool SupportPeersAcceptLoadout() noexcept { return !running || session.PeersHave(support_net::kCapLoadout); }
bool SupportPeersHaveVariantFile(std::uint64_t hash) noexcept {
    return !running || session.PeersHaveVariant(support_net::kExtVariants,hash);
}
bool SupportPeersApplyVariants() noexcept { return !running || session.PeersHaveVariant(support_net::kExtVariants,0); }
bool SupportPeersApplyBackpacks() noexcept { return !running || session.PeersHaveExtension(support_net::kExtBackpack); }
bool SupportPeersAcceptRescue() noexcept { return !running || session.PeersHaveExtension(support_net::kExtSeaRescue); }
bool SupportVersionCue(wchar_t* out,std::size_t capacity) noexcept {
    constexpr ULONGLONG kShowMs=12000;
    AcquireSRWLockShared(&versionLock);
    const bool fresh=versionAt && GetTickCount64()-versionAt<=kShowMs && out && capacity;
    if(fresh)_snwprintf_s(out,capacity,_TRUNCATE,L"%ls",versionText);
    ReleaseSRWLockShared(&versionLock);
    return fresh;
}
bool SupportTransactionRequester(std::uint64_t token,ObjRef* out) noexcept {
    std::uint32_t peer=0;
    if(!out || !session.RequesterOf(token,&peer))return false;
    *out=ObjRef{};
    if(!peer) {   // the host's own request
        if(unsigned char* own=PlayerHuman())*out=ObjRef::Of(own);
        return static_cast<bool>(*out);
    }
    if(peer>snapshot.peerCount)return false;
    // The requester's stable identity is the transport's authenticated PUID; its player actor is the mission player whose
    // index EDF6Coop resolves to that PUID (the same resolution the admission gate uses).
    const HMODULE coop=GetModuleHandleW(L"EDF6Coop.dll");
    using ResolvePlayer=std::uint32_t(__cdecl*)(std::int32_t,EDF6CoopPeer*);
    const auto resolve=coop ? reinterpret_cast<ResolvePlayer>(GetProcAddress(coop,"EDF6Coop_ResolveMissionPlayerPuid")) : nullptr;
    if(!resolve)return false;
    for(std::uint32_t i=0;i<support_net::kMaxPeers && !*out;++i) {
        AcquireSRWLockShared(&worldLock);const ObjRef actor=worldCreated[i];ReleaseSRWLockShared(&worldLock);
        EDF6CoopPeer who{};
        if(actor.obj && resolve(static_cast<std::int32_t>(i),&who) && PeerValid(who) && !std::strcmp(who.id,peers[peer].id))*out=actor;
    }
    return static_cast<bool>(*out);
}
bool SupportCommandRequesterMatches(void* puid,const char* authenticatedPuid) noexcept {
    if(!authenticatedPuid || !std::memchr(authenticatedPuid,0,65))return false;
    EDF6CoopPeer peer;
    if(!PuidText(puid,peer) || std::strcmp(peer.id,authenticatedPuid))return false;
    AcquireSRWLockShared(&worldLock);
    bool accepted=false;
    if(worldFrozen && world.ready)for(std::uint32_t i=0;i<world.participantCount;++i)
        accepted=accepted || !std::strcmp(peer.id,world.participants[i].id);
    ReleaseSRWLockShared(&worldLock);return accepted;
}
bool SupportParticipantAllowed(void* puid) noexcept {
    AcquireSRWLockShared(&worldLock);
    const bool frozen=worldFrozen;ReleaseSRWLockShared(&worldLock);
    if(!frozen)return true;
    EDF6CoopPeer peer;if(!PuidText(puid,peer))return false;
    AcquireSRWLockShared(&worldLock);
    bool allowed=!worldFrozen;
    for(std::uint32_t i=0;worldFrozen && i<world.participantCount;++i)
        allowed=allowed || !std::strcmp(peer.id,world.participants[i].id);
    ReleaseSRWLockShared(&worldLock);return allowed;
}
std::uint32_t GetMissionParticipants(std::uint32_t version,std::uint32_t size,EDF6AFMissionParticipants* out) noexcept {
    if(!out || version!=EDF6AF_MISSION_PARTICIPANTS_VERSION || size!=sizeof(*out))return 0;
    AcquireSRWLockShared(&worldLock);*out=world;ReleaseSRWLockShared(&worldLock);
    unsigned location=0;
    if(!ReadNativeMissionLocation(&location) || location!=4)out->ready=0;
    return 1;
}
std::uint32_t AllowMissionPlayer(std::int32_t index) noexcept {
    AcquireSRWLockShared(&worldLock);const bool frozen=worldFrozen;ReleaseSRWLockShared(&worldLock);
    if(!frozen) {
        // MissionStart runs during preload. Do not seal a leftover old-world
        // actor table before this lifetime actually starts creating players.
        if(index>=0) {AcquireSRWLockExclusive(&worldLock);worldCreationSeen=true;ReleaseSRWLockExclusive(&worldLock);}
        return 1;
    }
    EDF6CoopPeer peer{};
    const HMODULE coop=GetModuleHandleW(L"EDF6Coop.dll");
    using ResolvePlayer=std::uint32_t(__cdecl*)(std::int32_t,EDF6CoopPeer*);
    const auto resolve=coop ? reinterpret_cast<ResolvePlayer>(GetProcAddress(coop,"EDF6Coop_ResolveMissionPlayerPuid")) : nullptr;
    bool allowed=false;
    if(index>=0 && resolve && resolve(index,&peer) && PeerValid(peer)) {
        AcquireSRWLockShared(&worldLock);
        for(std::uint32_t i=0;i<world.participantCount;++i)allowed=allowed || !std::strcmp(peer.id,world.participants[i].id);
        ReleaseSRWLockShared(&worldLock);
    }
    if(!allowed)Log("SUPPORT admission: mission player %d waits until the next mission (not in sealed world)",index);
    return allowed ? 1u : 0u;
}
bool SupportMissionPlayerAllowed(int index) noexcept { return AllowMissionPlayer(index)!=0; }
bool SupportSoloHostWorld() noexcept {
    if(!InSession() || !OnlineHostOnly())return false;
    AcquireSRWLockShared(&worldLock);
    const bool solo=worldFrozen && world.ready && world.participantCount==1;
    ReleaseSRWLockShared(&worldLock);
    return solo;
}
void NoteSupportMissionPlayerCreated(int index,const ObjRef& object) noexcept {
    if(index<0 || static_cast<unsigned>(index)>=support_net::kMaxPeers || !object.obj || !object.ctrl)return;
    AcquireSRWLockExclusive(&worldLock);
    worldCreated[index]=object;worldCreationSeen=true;
    ReleaseSRWLockExclusive(&worldLock);
}
void SupportMissionReturnedToLobby() noexcept {
    SuspendSupportNet();
    ResetPvp(); // verified native scene release, unlike a transient transport readiness loss
    AcquireSRWLockExclusive(&worldLock);
    world={};world.size=sizeof(world);worldPhase=1;worldFrozen=false;worldCreationSeen=false;
    if(worldSerial!=UINT64_MAX)world.worldEpoch=++worldSerial;
    for(auto& actor:worldCreated)actor={};
    ReleaseSRWLockExclusive(&worldLock);
}
std::uint32_t GetMissionAdmissionState(std::uint32_t version,std::uint32_t size,EDF6AFMissionAdmissionState* out) noexcept {
    if(!out || version!=EDF6AF_MISSION_ADMISSION_VERSION || size!=sizeof(*out))return 0;
    AcquireSRWLockShared(&worldLock);
    *out={};out->size=sizeof(*out);out->phase=worldPhase;out->worldEpoch=world.worldEpoch;
    const bool sealed=worldFrozen;
    out->participantCount=world.participantCount;
    std::memcpy(out->participants,world.participants,sizeof(world.participants));
    ReleaseSRWLockShared(&worldLock);
    // This verified script-owned location is available in menus before any
    // preload/configuration callback. Never infer Lobby from !ready or deaths.
    unsigned location=0;
    if(!ReadNativeMissionLocation(&location))out->phase=0;
    else if(location==2 || location==3)out->phase=1;
    else if(location==5)out->phase=2;
    else if(location==4)out->phase=sealed ? 3u : 2u;
    else out->phase=0;
    if(out->phase!=3) {out->participantCount=0;std::memset(out->participants,0,sizeof(out->participants));}
    return 1;
}
std::uint64_t SubmitPreparedSupportPlan(const SupportPlan& plan) noexcept {
    SupportNetTick();
    if(!running || !OnlineHostOnly())return 0;
    const bool dynamicDrone=plan.catalogId==support_net::kPreparedDroneCatalog && plan.count==1 &&
        (plan.units[0].resourceId==kSupportSubDroneResource || IsCarrierDroneResource(plan.units[0].resourceId));
    return session.SubmitPrepared(plan,GetTickCount64(),dynamicDrone ? support_net::PreparedKind::dynamicDrone :
        support_net::PreparedKind::missionCrew);
}
} // namespace crew

// The transport advertises af-support/2 only when this production protocol is
// actually loaded, not merely when the older room-isolation marker exists.
extern "C" __declspec(dllexport) std::uint32_t __cdecl EDF6AF_SupportProtocolVersion() noexcept { return 2; }
extern "C" __declspec(dllexport) std::uint32_t __cdecl EDF6AF_GetMissionParticipants(
    std::uint32_t version,std::uint32_t size,EDF6AFMissionParticipants* out) noexcept {
    return crew::GetMissionParticipants(version,size,out);
}
extern "C" __declspec(dllexport) std::uint32_t __cdecl EDF6AF_AllowMissionPlayer(std::int32_t index) noexcept {
    return crew::AllowMissionPlayer(index);
}
extern "C" __declspec(dllexport) void __cdecl EDF6AF_MissionPlayerCreated(std::int32_t index,const void* object,const void* ctrl) noexcept {
    crew::NoteSupportMissionPlayerCreated(index,crew::ObjRef{object,ctrl});
}
extern "C" __declspec(dllexport) std::uint32_t __cdecl EDF6AF_GetMissionAdmissionState(
    std::uint32_t version,std::uint32_t size,EDF6AFMissionAdmissionState* out) noexcept {
    return crew::GetMissionAdmissionState(version,size,out);
}
