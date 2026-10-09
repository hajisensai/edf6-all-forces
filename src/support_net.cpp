#define _CRT_RAND_S
#include "support_protocol.h"
#include "coop_extension_api.h"
#include "crew.h"
#include "online_authority.h"
#include "mission_participants.h"
#include "command_net.h"
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <algorithm>

namespace crew {
namespace {
support_net::Session session;
support_net::Hooks hooks;
EDF6CoopExtensionApi api{};
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
    return support_net::Encode(message,bytes,sizeof(bytes)) &&
        api.send(snapshot.generation,&peers[peer],bytes,static_cast<std::uint32_t>(sizeof(bytes)))!=0;
}
bool SendCommand(std::uint32_t peer,const void* bytes,std::size_t count) noexcept {
    return running && peer && peer<=snapshot.peerCount && bytes && count &&
        count<=EDF6COOP_EXTENSION_MAX_PAYLOAD && api.send &&
        api.send(snapshot.generation,&peers[peer],bytes,static_cast<std::uint32_t>(count))!=0;
}
void CommandContext(ULONGLONG now) noexcept {
    UpdateCommandNetwork({running && session.Ready(),snapshot.isHost!=0,session.Epoch(),commandHostPeer,
        snapshot.peerCount,snapshot.local.id,&SendCommand},now);
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
    api={};module=nullptr;transportAdmissionReady=nullptr;
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
    return true;
}
void Note(wchar_t* note,std::size_t size,const wchar_t* text) noexcept {
    if(note && size)swprintf_s(note,size,L"%ls",text);
}
}
void ConfigureSupportNet(const support_net::Hooks& configured) noexcept {
    ResetSupportNet();hooks=configured;
    session.Configure({nullptr,&Send,&Nonce,hooks});
}
void ResetSupportNet() noexcept {
    session.Stop();running=false;blockedUntilMission=false;snapshot={};lastTick=~0ULL;ResetWorld();
    commandHostPeer=0;ResetCommandNetwork();
}
void SuspendSupportNet() noexcept {
    session.Suspend();running=false;blockedUntilMission=true;
    CommandContext(GetTickCount64());
    Log("SUPPORT NET suspended: existing actors retained; mission participant resynchronization required");
}
void SupportNetTick() noexcept {
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
    for(unsigned received=0;received<128;++received) {
        EDF6CoopPeer sender{};unsigned char bytes[EDF6COOP_EXTENSION_MAX_PAYLOAD]{};std::uint32_t count=0;
        if(!api.poll(snapshot.generation,&sender,bytes,sizeof(bytes),&count))break;
        if(!PeerValid(sender))continue;
        std::uint32_t peer=0;
        for(std::uint32_t i=1;i<=snapshot.peerCount;++i)if(!std::strcmp(sender.id,peers[i].id)){peer=i;break;}
        if(peer) {
            CommandContext(now);
            if(ReceiveCommandNetwork(peer,sender.id,bytes,count,now))continue;
            support_net::Message message;
            if(support_net::Decode(bytes,count,message))session.Receive(peer,message,now);
        }
    }
    session.Tick(now);
    CommandContext(now);
    if(session.Suspended())SuspendSupportNet();
}
bool SubmitSupportRequest(int catalogId,const float* target,wchar_t* note,std::size_t size) noexcept {
    SupportNetTick();
    if(blockedUntilMission) {
        Note(note,size,L"联机参与者同步已暂停，现有支援保留；重新开始关卡后可再呼叫");return false;
    }
    if(catalogId<0 || !running || !session.Ready()) {
        Note(note,size,L"联机支援尚未就绪：需全房同版全军出击与联机扩展，并完成关卡同步");return false;
    }
    if(!session.Submit(static_cast<std::uint32_t>(catalogId),target,GetTickCount64())) {
        Note(note,size,L"支援请求未受理：已有部署、调用过快或本关支援额度已满");return false;
    }
    Note(note,size,L"支援请求已排队，等待房主验证与全员确认");return true;
}
void ReportSupportFailure(std::uint64_t transaction) noexcept {
    if(transaction) {
        Log("SUPPORT NET rollback transaction=%llu",static_cast<unsigned long long>(transaction));
        session.Failed(transaction);
    }
}
bool SupportTransactionActive(std::uint64_t transaction) noexcept { return session.IsActive(transaction); }
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
    return running && OnlineHostOnly() ? session.SubmitPrepared(plan,GetTickCount64()) : 0;
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
