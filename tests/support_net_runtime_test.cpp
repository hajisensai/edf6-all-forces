#define _CRT_RAND_S
// Real production bridge with an in-process implementation of the public C ABI.
// No game process, game installation, socket, or DLL entry point is used.
#include "../src/crew.h"
#include "../src/coop_extension_api.h"
#include <deque>
#include <cstring>
#include <cstdio>
#include <cstdlib>
namespace fixture {
ULONGLONG tick=10000;
bool installed=true,ready=true,host=true,online=true,stale=false;
bool gateReady=false;
bool locationReadable=true;
unsigned location=4;
unsigned actors=0,expectedActors=0;
const char* actorPuids[4]={"host","client","host","lobby-only"};
std::uint64_t generation=1;
unsigned spawnCount=0,destroyCount=0,sendCount=0,peerCount=0;
unsigned boundedSendCalls=0;std::uint32_t boundedSendResult=EDF6COOP_EXTENSION_SEND_ACCEPTED;
unsigned commandPackets=0,commandContexts=0;
unsigned enemyControlPackets=0,enemyControlContexts=0,enemyControlResets=0,subPackets=0,subContexts=0,subResets=0;
unsigned carrierPackets=0,carrierContexts=0,carrierResets=0;bool reenterCarrier=false;
char enemyControlSender[65]{},subSender[65]{};bool enemyControlReady=false,subReady=false;
unsigned pvpPackets=0,pvpContexts=0,pvpResets=0;bool pvpReady=false;char pvpSender[65]{};
unsigned rangePlans=0,rangeValidations=0,rangeSpawns=0,rangeDestroys=0,rangeTicks=0,rangeResets=0,rangeNotices=0;
std::uint64_t rangeToken=0;
int pvpTeam=0;bool identityMismatch=false,nativeIdMissing=false;
unsigned lastSupportTeam=0,lastSupportOwner=0;
unsigned gravityPackets=0;
unsigned qmarkPackets=0,qmarkContexts=0,qmarkResets=0;std::uint32_t qmarkPeers=0;bool qmarkReady=false;
EDF6CoopPeer Peer(const char* id) { EDF6CoopPeer p{};strcpy_s(p.id,id);return p; }
struct Packet { EDF6CoopPeer sender;unsigned char bytes[176]; };
std::deque<Packet> incoming;
HMODULE WINAPI Module(LPCWSTR name) {
    if(!wcscmp(name,L"EOSSDK-Win64-Shipping.dll"))return reinterpret_cast<HMODULE>(2);
    return installed && !wcscmp(name,L"EDF6Coop.dll") ? reinterpret_cast<HMODULE>(1) : nullptr;
}
ULONGLONG WINAPI Clock() { return tick; }
FARPROC WINAPI Proc(HMODULE,LPCSTR);
}
#define GetModuleHandleW fixture::Module
#define GetTickCount64 fixture::Clock
#define GetProcAddress fixture::Proc
#include "../src/support_net.cpp"
#undef GetModuleHandleW
#undef GetTickCount64
#undef GetProcAddress
namespace crew {
Config config;
const Config& Cfg() noexcept { config.enabled=true;return config; }
bool InSession() noexcept { return fixture::online; }
bool OnlineHostOnly() noexcept { return fixture::host; }
unsigned char* PlayerHuman() noexcept { return nullptr; }   // the rescue requester lookup (no player in this fixture)
void Log(const char*,...) noexcept {}
bool ReadNativeMissionLocation(unsigned* out) noexcept { *out=fixture::location;return fixture::locationReadable; }
void ResetCommandNetwork() noexcept {}
void UpdateCommandNetwork(const CommandNetworkContext&,std::uint64_t) noexcept {++fixture::commandContexts;}
bool ReceiveCommandNetwork(std::uint32_t,const char*,const void* bytes,std::size_t size,std::uint64_t) noexcept {
    if(size<4 || std::memcmp(bytes,"NCMD",4))return false;
    ++fixture::commandPackets;return true;
}
// The Q marks (qmark.cpp): the room's state each tick, their packets before the command and support parsers.
void UpdateQMarkNetwork(bool ready,std::uint32_t roomPeers,bool (*)(std::uint32_t,const void*,std::size_t) noexcept,std::uint64_t) noexcept {
    ++fixture::qmarkContexts;fixture::qmarkReady=ready;fixture::qmarkPeers=roomPeers;
}
bool ReceiveQMarkNetwork(std::uint32_t,const char*,const void* bytes,std::size_t size,std::uint64_t) noexcept {
    if(size<4 || std::memcmp(bytes,"QMRK",4))return false;
    ++fixture::qmarkPackets;return true;
}
void ResetQMarkNetwork() noexcept { ++fixture::qmarkResets; }
void UpdatePvpNetwork(const CommandNetworkContext& context,std::uint64_t) noexcept {
    ++fixture::pvpContexts;fixture::pvpReady=context.ready;
}
bool ReceivePvpNetwork(std::uint32_t,const char* sender,const void* bytes,std::size_t size,std::uint64_t) noexcept {
    if(size<4 || std::memcmp(bytes,"AFPV",4))return false;
    ++fixture::pvpPackets;strcpy_s(fixture::pvpSender,sender);return true;
}
void UpdateEnemyControlNetwork(const CommandNetworkContext& context,std::uint64_t) noexcept {
    ++fixture::enemyControlContexts;fixture::enemyControlReady=context.ready;
}
bool ReceiveEnemyControlNetwork(std::uint32_t,const char* sender,const void* bytes,std::size_t size,std::uint64_t) noexcept {
    if(size<4 || std::memcmp(bytes,"ECTL",4))return false;
    ++fixture::enemyControlPackets;strcpy_s(fixture::enemyControlSender,sender);return true;
}
void ResetEnemyControlNetwork() noexcept {++fixture::enemyControlResets;}
void UpdateSubSupportNetwork(const CommandNetworkContext& context,std::uint64_t) noexcept {
    ++fixture::subContexts;fixture::subReady=context.ready;
}
bool ReceiveSubSupportNetwork(std::uint32_t,const char* sender,const void* bytes,std::size_t size,std::uint64_t) noexcept {
    if(size<4 || std::memcmp(bytes,"AFSB",4))return false;
    ++fixture::subPackets;strcpy_s(fixture::subSender,sender);return true;
}
void ResetSubSupportNetwork() noexcept {++fixture::subResets;}
void UpdateCarrierDroneNetwork(const CommandNetworkContext&,std::uint64_t) noexcept {++fixture::carrierContexts;}
bool ReceiveCarrierDroneNetwork(std::uint32_t,const char*,const void* bytes,std::size_t size,std::uint64_t) noexcept {
    if(size<4 || std::memcmp(bytes,"AFDQ",4))return false;
    ++fixture::carrierPackets;
    if(fixture::reenterCarrier){fixture::tick+=1;SupportNetTick();}
    return true;
}
void ResetCarrierDroneNetwork() noexcept {++fixture::carrierResets;}
// The gravity operations' and the float guns' (gravity_net.cpp, floatgun.cpp): not under test here.
// Their channels (plugin_net.h) as registered: GRVT for kExtGravity peers, FGUN for kExtFloatGun ones.
bool ReceiveGravity(std::uint32_t,const char*,const void*,std::size_t,std::uint64_t) noexcept {++fixture::gravityPackets;return true;}
plugin_net::Channel GravityNetChannel() noexcept {
    plugin_net::Channel c;c.name="gravity";c.magic=0x54565247u;c.version=1;c.featureBit=support_net::kExtGravity;
    c.minSize=c.maxSize=160;c.receive=&ReceiveGravity;return c;
}
bool ReceiveFloatGun(std::uint32_t,const char*,const void*,std::size_t,std::uint64_t) noexcept {return true;}
plugin_net::Channel FloatGunNetChannel() noexcept {
    plugin_net::Channel c;c.name="float guns";c.magic=0x4E554746u;c.version=1;c.featureBit=support_net::kExtFloatGun;
    c.minSize=c.maxSize=96;c.receive=&ReceiveFloatGun;return c;
}
// The ultimate strike's (ultimate.cpp): ULTS for kExtUltimate peers, not under test here.
bool ReceiveUltimate(std::uint32_t,const char*,const void*,std::size_t,std::uint64_t) noexcept {return true;}
plugin_net::Channel UltimateNetChannel() noexcept {
    plugin_net::Channel c;c.name="ultimate";c.magic=0x53544C55u;c.version=1;c.featureBit=support_net::kExtUltimate;
    c.minSize=c.maxSize=96;c.receive=&ReceiveUltimate;return c;
}
void ResetPvpNetwork() noexcept { ++fixture::pvpResets; }
void ResetPvp() noexcept { ResetPvpNetwork(); }
int PvpSupportTeam(const char*) noexcept {return fixture::pvpTeam;}
bool PvpValidateSupportPlan(const support_net::Plan& plan) noexcept {
    return fixture::pvpTeam>=0 && plan.team==static_cast<unsigned>(fixture::pvpTeam);
}
bool ReadNativeObjectId(const unsigned char* object,unsigned char* id) noexcept {
    if(fixture::nativeIdMissing)return false;
    for(unsigned i=0;i<fixture::actors;++i)if(object==reinterpret_cast<const unsigned char*>(fixture::actorPuids[i])) {
        std::memset(id,0,32);id[0]=static_cast<unsigned char>(0x40+i);return true;
    }
    return false;
}
bool ResolveMarkIdentities(const unsigned char* marker,const unsigned char*,MarkIdentities* out) noexcept {
    if(!marker || !out || marker[0]<0x40 || marker[0]>=0x40+fixture::actors)return false;
    const unsigned i=marker[0]-0x40;
    *out={};out->player={fixture::actorPuids[i],reinterpret_cast<const void*>(static_cast<std::uintptr_t>(i+100))};
    out->slot=static_cast<int>(i);out->puid=const_cast<char*>(fixture::identityMismatch ? "wrong-member" : fixture::actorPuids[i]);
    return true;
}
// The native spawn functions are isolated below the bridge. Their production descriptor/plan contract still runs,
// so this fixture verifies the real bridge routes only the reserved catalog through all range callbacks.
support_net::PlanResult PlanRangeEnemy(std::uint32_t catalog,const float* at,std::uint64_t descriptor,support_net::Plan* out) noexcept {
    ++fixture::rangePlans;
    const int row=debugspawn::NetworkRow(descriptor);
    float matrix[16]{};matrix[0]=matrix[5]=matrix[10]=matrix[15]=1;
    for(unsigned i=0;i<3;++i)matrix[12+i]=at[i];
    return catalog==debugspawn::kNetworkCatalog && debugspawn::BuildNetworkPlan(row,matrix,out) ?
        support_net::PlanResult::ready : support_net::PlanResult::refused;
}
support_net::PlanResult PrepareRangeEnemy(const support_net::Plan& plan) noexcept {
    return debugspawn::NetworkPlanRow(plan)>=0 ? support_net::PlanResult::ready : support_net::PlanResult::refused;
}
bool ValidateRangeEnemy(const support_net::Plan& plan) noexcept {
    ++fixture::rangeValidations;return debugspawn::NetworkPlanRow(plan)>=0;
}
bool SpawnRangeEnemy(std::uint64_t token,const support_net::Plan&,bool) noexcept {
    fixture::rangeToken=token;++fixture::rangeSpawns;return true;
}
void DestroyRangeEnemy(std::uint64_t token) noexcept {
    if(token && token==fixture::rangeToken){++fixture::rangeDestroys;fixture::rangeToken=0;}
}
void RangeEnemyNetworkTick() noexcept { ++fixture::rangeTicks; }
void ResetRangeEnemyNetwork() noexcept { ++fixture::rangeResets;fixture::rangeToken=0; }
bool RangeEnemyRequestNotice(std::uint32_t,support_net::RequestStatus) noexcept { ++fixture::rangeNotices;return false; }
}
namespace fixture {
using namespace crew::support_net;
int checks=0;
void Check(bool ok,const char* why) { ++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);} }
std::uint32_t EDF6COOP_CALL Snapshot(EDF6CoopSnapshot* out) {
    out->size=sizeof(*out);out->ready=ready;out->isHost=host;out->generation=generation;out->peerCount=peerCount;
    out->local=Peer(host ? "host" : "client");out->host=Peer("host");return 1;
}
std::uint32_t EDF6COOP_CALL Member(std::uint64_t expected,std::uint32_t index,EDF6CoopPeer* out) {
    if(expected!=generation || index>=peerCount || stale)return 0;*out=Peer(host ? "client" : "host");return 1;
}
std::uint32_t EDF6COOP_CALL Send(std::uint64_t expected,const EDF6CoopPeer*,const void* bytes,std::uint32_t size) {
    Check(expected==generation,"send bound to observed transport generation");
    Message m;Check(Decode(bytes,size,m),"production sends canonical protocol bytes");++sendCount;return 1;
}
std::uint32_t EDF6COOP_CALL TrySend(std::uint64_t expected,const EDF6CoopPeer* target,const void* bytes,std::uint32_t size,std::uint32_t byteLimit,std::uint32_t packetLimit) {
    Check(expected==generation && target && !std::strcmp(target->id,"client"),"bounded send binds the actual roster identity and current SDK generation");
    Check(bytes && size==4 && byteLimit==768*1024 && packetLimit==768,"bounded send forwards the caller's real SDK ceilings exactly");
    ++boundedSendCalls;return boundedSendResult;
}
std::uint32_t EDF6COOP_CALL Poll(std::uint64_t expected,EDF6CoopPeer* sender,void* bytes,std::uint32_t cap,std::uint32_t* out) {
    Check(expected==generation,"poll bound to observed transport generation");
    *out=0;if(incoming.empty() || cap<kWireSize)return 0;
    *sender=incoming.front().sender;std::memcpy(bytes,incoming.front().bytes,kWireSize);*out=kWireSize;incoming.pop_front();return 1;
}
std::uint32_t EDF6COOP_CALL Api(std::uint32_t version,std::uint32_t size,EDF6CoopExtensionApi* out) {
    if(version!=1 || size!=sizeof(*out))return 0;
    *out={sizeof(*out),1,&Snapshot,&Member,&Send,&Poll};return 1;
}
int __cdecl PuidToString(void* id,char* out,std::int32_t* length) {
    const char* text=static_cast<const char*>(id);const auto needed=static_cast<std::int32_t>(std::strlen(text)+1);
    if(*length<needed)return 1;std::memcpy(out,text,needed);*length=needed;return 0;
}
std::uint32_t __cdecl ResolvePlayer(std::int32_t index,EDF6CoopPeer* out) {
    if(index<0 || index>2)return 0;*out=Peer(index==0 ? "host" : index==1 ? "client" : "lobby-only");return 1;
}
std::uint32_t __cdecl TransportGateReady() { return 1; }
FARPROC WINAPI Proc(HMODULE,LPCSTR name) {
    if(!std::strcmp(name,"EDF6CoopGetExtensionApi"))return reinterpret_cast<FARPROC>(&Api);
    if(!std::strcmp(name,"EDF6CoopTrySendExtension"))return reinterpret_cast<FARPROC>(&TrySend);
    if(!std::strcmp(name,"EOS_ProductUserId_ToString"))return reinterpret_cast<FARPROC>(&PuidToString);
    if(!std::strcmp(name,"EDF6Coop_ResolveMissionPlayerPuid"))return reinterpret_cast<FARPROC>(&ResolvePlayer);
    if(!std::strcmp(name,"EDF6Coop_MissionAdmissionReady"))return reinterpret_cast<FARPROC>(&TransportGateReady);
    return nullptr;
}
bool Participants(void** out,std::uint32_t capacity,std::uint32_t* count,std::uint32_t* expected) noexcept {
    if(actors>capacity)return false;
    for(unsigned i=0;i<actors;++i)out[i]=const_cast<char*>(actorPuids[i]);
    *count=actors;*expected=expectedActors;return true;
}
bool AdmissionReady() noexcept { return gateReady; }
bool CreationsMatch(const crew::ObjRef* created,std::uint32_t count) noexcept {
    for(std::uint32_t i=0;i<count;++i)if(created[i].obj!=actorPuids[i] ||
        created[i].ctrl!=reinterpret_cast<const void*>(static_cast<std::uintptr_t>(i+100)))return false;
    return count==actors;
}
void NoteCreations() {
    for(unsigned i=0;i<actors;++i)EDF6AF_MissionPlayerCreated(static_cast<int>(i),actorPuids[i],
        reinterpret_cast<const void*>(static_cast<std::uintptr_t>(i+100)));
}
PlanResult PlanCall(std::uint32_t id,const float* target,std::uint64_t,Plan* plan) noexcept {
    *plan={};plan->catalogId=id;plan->count=1;std::memcpy(plan->target,target,12);
    auto& u=plan->units[0];u.resourceId=1;u.matrix[0]=u.matrix[5]=u.matrix[10]=u.matrix[15]=1;return PlanResult::ready;
}
bool Validate(const Plan& p) noexcept { return p.count==1 && (p.units[0].resourceId==1 ||
    (p.catalogId==kPreparedDroneCatalog && (p.units[0].resourceId==crew::kSupportSubDroneResource || crew::IsCarrierDroneResource(p.units[0].resourceId)))); }
bool Spawn(std::uint64_t,const Plan& plan,bool remote) noexcept {
    Check(!remote,"host native spawn callback authority");++spawnCount;
    lastSupportTeam=plan.team;lastSupportOwner=plan.requester[0];return true;
}
void Destroy(std::uint64_t) noexcept { ++destroyCount; }
bool Derive(std::uint32_t ordinal,unsigned char* bytes) noexcept { std::memset(bytes,0,32);bytes[12]=5;std::memcpy(bytes+4,&ordinal,4);return true; }
void Step() { tick+=100;crew::SupportNetTick(); }
void WorldParticipants() {
    installed=true;online=true;ready=false;actors=2;expectedActors=3;gateReady=false;
    crew::ConfigureSupportNet({&PlanCall,&Validate,&Spawn,&Destroy,&Derive,&Participants,&AdmissionReady,&CreationsMatch});
    EDF6AFMissionParticipants state{};
    gateReady=true;actors=3;Step();EDF6AF_GetMissionParticipants(1,sizeof(state),&state);
    Check(!state.ready,"preload cannot seal leftover actors before player creation begins");
    gateReady=false;actors=2;
    Check(crew::SupportMissionPlayerAllowed(2),"initial mission creation remains allowed before roster seal");
    Step();Check(EDF6AF_GetMissionParticipants(1,sizeof(state),&state) && !state.ready,"unverified native admission gate cannot publish ready world");
    gateReady=true;tick+=300;Step();EDF6AF_GetMissionParticipants(1,sizeof(state),&state);
    Check(!state.ready,"partial player construction cannot become ACK quorum");
    actors=3;tick+=300;Step();EDF6AF_GetMissionParticipants(1,sizeof(state),&state);
    Check(!state.ready,"creation attempt alone cannot seal old world actors");
    NoteCreations();tick+=300;Step();EDF6AF_GetMissionParticipants(1,sizeof(state),&state);
    Check(state.ready && state.participantCount==2 && !std::strcmp(state.participants[0].id,"client"),"complete native world deduplicates split-screen PUIDs");
    Check(crew::SupportCommandRequesterMatches(const_cast<char*>(actorPuids[1]),"client"),"command authentication accepts native PUID of an admitted world player");
    Check(!crew::SupportCommandRequesterMatches(const_cast<char*>(actorPuids[1]),"host"),"command authentication cannot borrow another transport identity");
    Check(!crew::SupportCommandRequesterMatches(const_cast<char*>(actorPuids[3]),"lobby-only"),"lobby-only identity cannot issue world commands");
    const auto epoch=state.worldEpoch;
    Check(crew::SupportMissionPlayerAllowed(0) && crew::SupportMissionPlayerAllowed(1),"existing mission players may respawn");
    Check(!crew::SupportMissionPlayerAllowed(2),"lobby-only player cannot enter sealed current world");
    Check(!crew::SupportMissionPlayerAllowed(99),"failed pre-create identity read fails closed");
    actors=0;tick+=300;Step();EDF6AF_GetMissionParticipants(1,sizeof(state),&state);
    Check(state.ready && state.participantCount==2 && state.worldEpoch==epoch,"death/removal observations cannot redefine frozen quorum");
    EDF6AFMissionAdmissionState policy{};
    Check(EDF6AF_GetMissionAdmissionState(1,sizeof(policy),&policy) && policy.phase==3 && policy.participantCount==2,
        "host admission policy exposes sealed world independently of transport readiness");
    location=5;EDF6AF_GetMissionAdmissionState(1,sizeof(policy),&policy);
    Check(policy.phase==2 && !policy.participantCount,"native loading cannot leak previous sealed roster");
    location=3;EDF6AF_GetMissionAdmissionState(1,sizeof(policy),&policy);
    Check(policy.phase==1 && !policy.participantCount,"verified native MENU_ROOM yields lobby before another preload");
    EDF6AF_GetMissionParticipants(1,sizeof(state),&state);Check(!state.ready,"room menu is not an active mission quorum");
    locationReadable=false;EDF6AF_GetMissionAdmissionState(1,sizeof(policy),&policy);
    Check(policy.phase==0,"native phase read failure is unknown, never lobby");
    locationReadable=true;location=4;
    Check(!EDF6AF_GetMissionParticipants(2,sizeof(state),&state),"world snapshot ABI rejects unsupported version");
    crew::ResetSupportNet();actors=4;expectedActors=4;crew::SupportMissionPlayerAllowed(0);
    NoteCreations();
    tick+=300;Step();EDF6AF_GetMissionParticipants(1,sizeof(state),&state);
    Check(state.ready && state.participantCount==3 && state.worldEpoch!=epoch && crew::SupportMissionPlayerAllowed(2),
        "next explicit mission admits prior lobby-only player into new frozen world");
    crew::SupportMissionReturnedToLobby();location=3;EDF6AF_GetMissionAdmissionState(1,sizeof(policy),&policy);
    Check(policy.phase==1 && !policy.participantCount && policy.worldEpoch!=state.worldEpoch,
        "only explicit verified lobby notification clears sealed admission policy");
    crew::ResetSupportNet();
}
}
int main() {
    using namespace fixture;
    EDF6AFMissionAdmissionState initial{};location=3;
    Check(EDF6AF_GetMissionAdmissionState(1,sizeof(initial),&initial) && initial.phase==1 && initial.worldEpoch,
        "initial native lobby is visible before dispatcher preload configuration");location=4;
    actors=expectedActors=1;gateReady=true;
    crew::ConfigureSupportNet({&PlanCall,&Validate,&Spawn,&Destroy,&Derive,&Participants,&AdmissionReady,&CreationsMatch});
    NoteCreations();Step(); // a real sealed native actor/PUID, independent of lobby order
    wchar_t note[256]{};const float at[3]={0,1,2};
    Check(crew::SubmitSupportRequest(1,at,note,256),"production dynamic ABI accepts host request");Step();
    Check(spawnCount==1,"production frame runs planner and native callback");
    Check(lastSupportOwner==0x40 && lastSupportTeam==0,"mode-off support caches the authenticated native requester with stock team zero");
    pvpTeam=1;tick+=2100;
    Check(crew::SubmitSupportRequest(1,at,note,256),"committed PVP owner can request support");Step();
    Check(spawnCount==2 && lastSupportTeam==1 && lastSupportOwner==0x40,"PVP support carries the real requester's committed team to the plan");
    Plan probed{};pvpTeam=-1;
    Check(crew::PlanForRequester(0,1,at,0,&probed)==PlanResult::refused,"pending PVP transition refuses support instead of choosing team two");
    pvpTeam=1;identityMismatch=true;
    Check(crew::PlanForRequester(0,1,at,0,&probed)==PlanResult::refused,"native requester PUID must match authenticated local sender and sealed world");
    identityMismatch=false;nativeIdMissing=true;
    Check(crew::PlanForRequester(0,1,at,0,&probed)==PlanResult::refused,"missing canonical native player ID never drops back to stock-team PVP support");
    nativeIdMissing=false;pvpTeam=0;
    const unsigned before=destroyCount;
    ready=false;Step();Check(destroyCount==before && crew::SupportTransactionActive(1),"not-ready snapshot preserves delivered actors and activation");
    ready=true;generation++;Step();Check(!crew::SubmitSupportRequest(1,at,note,256),"restored link does not silently reopen a changed participant epoch");
    crew::ResetSupportNet();Check(destroyCount>before,"explicit mission reset owns actor teardown");
    generation++;peerCount=1;stale=true;Step();
    Check(!crew::SubmitSupportRequest(1,at,note,256),"peer roster changed during snapshot fails closed");
    stale=false;generation++;Step();
    Message hello;hello.kind=Kind::hello;hello.challenge=9;hello.request=1;
    Packet packet{};packet.sender=Peer("intruder");Check(Encode(hello,packet.bytes,sizeof(packet.bytes)),"encode handshake");
    incoming.push_back(packet);Step();Check(sendCount==0,"unknown authenticated sender absent from roster cannot handshake");
    packet.sender=Peer("client");incoming.push_back(packet);Step();Check(sendCount==1,"roster peer receives welcome");
    static_assert(sizeof(EDF6CoopExtensionApi)==40,"bounded send preserves the legacy support ABI");
    const unsigned char flowPayload[4]={'E','C','T','L'};
    for(auto status:{EDF6COOP_EXTENSION_SEND_ACCEPTED,EDF6COOP_EXTENSION_SEND_WOULD_BLOCK,EDF6COOP_EXTENSION_SEND_UNAVAILABLE}) {
        boundedSendResult=status;Check(crew::TrySendCommand(1,flowPayload,sizeof(flowPayload),768*1024,768)==status,"production wrapper retains all three SDK outcomes");
    }
    Check(boundedSendCalls==3 && crew::running && crew::snapshot.generation==generation,"wouldBlock never changes the room or consumes the ordinary support sender");
    const auto optional=crew::trySendExtension;crew::trySendExtension=nullptr;
    Check(crew::TrySendCommand(1,flowPayload,sizeof(flowPayload),768*1024,768)==EDF6COOP_EXTENSION_SEND_UNAVAILABLE && crew::running,"missing optional flow ABI disables only high-volume sends");
    crew::trySendExtension=optional;

    auto commandPacket=packet;std::memcpy(commandPacket.bytes,"NCMD",4);incoming.push_back(commandPacket);Step();
    Check(commandPackets==1 && commandContexts>0 && sendCount==1,"shared extension poll routes command packets once without consuming a support message");
    auto markPacket=packet;std::memcpy(markPacket.bytes,"QMRK",4);incoming.push_back(markPacket);Step();
    Check(qmarkPackets==1 && commandPackets==1 && sendCount==1,"a Q mark packet goes to qmark.cpp alone");
    Check(qmarkContexts>0 && qmarkReady && qmarkPeers==1,"the Q marks get the room (ready, its peers) each tick");
    auto pvpPacket=packet;std::memcpy(pvpPacket.bytes,"AFPV",4);incoming.push_back(pvpPacket);Step();
    Check(pvpPackets==1 && commandPackets==1 && qmarkPackets==1 && !std::strcmp(pvpSender,"client"),
        "PVP receives the shared poll's authenticated sender exactly once without reaching other parsers");
    Check(pvpContexts>0 && pvpReady,"PVP receives the ready sealed-world context");
    auto enemyControlPacket=packet;std::memcpy(enemyControlPacket.bytes,"ECTL",4);incoming.push_back(enemyControlPacket);Step();
    Check(enemyControlPackets==1 && !std::strcmp(enemyControlSender,"client") && commandPackets==1 && pvpPackets==1,
        "enemy control receives the sole poll's authenticated sender exactly once");
    Check(enemyControlContexts>0 && enemyControlReady,"enemy control receives the ready support epoch context");
    auto subPacket=packet;std::memcpy(subPacket.bytes,"AFSB",4);incoming.push_back(subPacket);Step();
    Check(subPackets==1 && !std::strcmp(subSender,"client") && enemyControlPackets==1 && pvpPackets==1,
        "submarine subsystem state receives only its own shared-poll datagram");
    Check(subContexts>0 && subReady,"submarine state receives the ready support epoch context");
    auto carrierPacket=packet;std::memcpy(carrierPacket.bytes,"AFDQ",4);incoming.push_back(carrierPacket);
    reenterCarrier=true;const auto carrierContextsBefore=carrierContexts;Step();reenterCarrier=false;
    Check(carrierPackets==1 && enemyControlPackets==1 && subPackets==1,"carrier drone request is consumed once by its own shared-poll handler");
    Check(carrierContexts-carrierContextsBefore==3,"nested prepared-plan enqueue cannot recursively poll/advance native support");
    // The newer channels (plugin_net.h): a peer that did not announce kExtGravity (this fixture's hellos carry no extension
    // word) is never heard on it; a channel this build does not have is dropped, never fed to the support parser.
    auto gravityPacket=packet;std::memset(gravityPacket.bytes,0,sizeof(gravityPacket.bytes));std::memcpy(gravityPacket.bytes,"GRVT",5);
    const unsigned sendsBefore=sendCount;incoming.push_back(gravityPacket);Step();
    Check(gravityPackets==0 && sendCount==sendsBefore && carrierPackets==1,"a gravity message from a peer without the bit: dropped");
    auto unknownPacket=packet;std::memcpy(unknownPacket.bytes,"ZZZZ",4);incoming.push_back(unknownPacket);Step();
    Check(sendCount==sendsBefore && commandPackets==1 && qmarkPackets==1 && crew::running,"a channel this build does not have: dropped quietly");
    Check(crew::SubmitSupportRequest(1,at,note,256),"full handshake queues request");
    installed=false;Step();Check(!crew::SubmitSupportRequest(1,at,note,256),"unloaded bridge disables queued work");
    installed=true;peerCount=0;generation++;tick+=1000;Step();
    online=false;Step();Check(!crew::SubmitSupportRequest(1,at,note,256),"offline never sends multiplayer requests");
    const unsigned resetsBefore=qmarkResets,pvpResetsBefore=pvpResets,enemyResetsBefore=enemyControlResets,subResetsBefore=subResets,carrierResetsBefore=carrierResets;crew::ResetSupportNet();
    Check(qmarkResets>resetsBefore,"the session reset drops the teammates' marks");
    Check(pvpResets>pvpResetsBefore,"the session reset cancels PVP's network epoch");
    Check(enemyControlResets>enemyResetsBefore && subResets>subResetsBefore && carrierResets>carrierResetsBefore,"session reset invalidates enemy-control, submarine state and carrier request epochs");
    // Same production session, reserved range catalog, with no remote peer: every wrapper must select the range
    // callbacks rather than the ordinary support dispatcher. The multi-peer transaction is tested independently.
    online=true;host=true;ready=true;installed=true;peerCount=0;generation++;tick+=1000;
    crew::ConfigureSupportNet({&PlanCall,&Validate,&Spawn,&Destroy,&Derive});
    const auto ordinarySpawns=spawnCount;
    int row=-1;for(int i=0;i<crew::debugspawn::kEntryCount;++i)
        if(crew::debugspawn::kEntries[i].how==crew::debugspawn::How::enemy){row=i;break;}
    Check(row>=0,"range bridge has a generated enemy descriptor");
    Check(crew::SubmitSupportRequest(static_cast<int>(crew::debugspawn::kNetworkCatalog),at,note,256,
        crew::debugspawn::Descriptor(crew::debugspawn::kEntries[row])),"range request reaches the production bridge");
    Step();
    Check(rangePlans==1 && rangeValidations==1 && rangeSpawns==1 && rangeToken && spawnCount==ordinarySpawns,
        "range catalog traverses its plan, validate and native spawn wrappers alone");
    Check(rangeTicks>0 && rangeNotices>0,"range activation/status ticks are supplied by the production bridge");
    const auto rangeResetBefore=rangeResets;crew::ResetSupportNet();
    Check(rangeDestroys==1 && rangeResets>rangeResetBefore && !rangeToken,"mission reset rolls back range ownership and request identity");
    // Real production wrapper -> actual Session::SubmitPrepared: ordinary mission default must not silently reject child plans.
    online=true;host=true;ready=true;installed=true;peerCount=0;generation++;tick+=1000;pvpTeam=0;
    crew::ConfigureSupportNet({&PlanCall,&Validate,&Spawn,&Destroy,&Derive});Step();
    for(unsigned kind=0;kind<4;++kind) {
        Plan child{};child.catalogId=kPreparedDroneCatalog;child.count=1;child.requester[0]=0x40;
        auto& unit=child.units[0];unit.resourceId=kind==0 ? crew::kSupportSubDroneResource : crew::CarrierDroneResource(kind-1,kind,true);
        unit.variant=0x1122334400ull+kind;unit.matrix[0]=unit.matrix[5]=unit.matrix[10]=unit.matrix[15]=1;
        const auto beforeSpawn=spawnCount;const auto token=crew::SubmitPreparedSupportPlan(child);
        Check(token!=0 && spawnCount==beforeSpawn,"real wrapper admits only explicitly classified dynamic prepared child without early spawn");
        Step();Check(crew::SupportTransactionActive(token) && spawnCount==beforeSpawn+1 && lastSupportOwner==0x40,
            "actual production wrapper and Session derive native ID commit and activate each exact dynamic child");
        const auto beforeDestroy=destroyCount;crew::ReportSupportFailure(token);
        Check(destroyCount==beforeDestroy+1 && !crew::SupportTransactionActive(token),"real dynamic wrapper token follows Session Destroy cancellation");
    }
    Plan rejected{};rejected.catalogId=kPreparedDroneCatalog;rejected.count=1;rejected.requester[0]=0x40;
    rejected.units[0].resourceId=crew::kSupportSubDroneResource;rejected.units[0].variant=99;
    rejected.units[0].matrix[0]=rejected.units[0].matrix[5]=rejected.units[0].matrix[10]=rejected.units[0].matrix[15]=1;
    rejected.units[0].netId[0]=1;Check(!crew::SubmitPreparedSupportPlan(rejected),"prepared wrapper cannot smuggle an existing native child ID");
    rejected.units[0].netId[0]=0;rejected.requester[0]=0;
    Check(!crew::SubmitPreparedSupportPlan(rejected),"dynamic wrapper refuses missing canonical owner");
    crew::ResetSupportNet();
    WorldParticipants();
    std::printf("support runtime bridge: %d checks passed\n",checks);
}
