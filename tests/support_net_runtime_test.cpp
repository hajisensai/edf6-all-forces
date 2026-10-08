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
unsigned commandPackets=0,commandContexts=0;
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
void Log(const char*,...) noexcept {}
bool ReadNativeMissionLocation(unsigned* out) noexcept { *out=fixture::location;return fixture::locationReadable; }
void ResetCommandNetwork() noexcept {}
void UpdateCommandNetwork(const CommandNetworkContext&,std::uint64_t) noexcept {++fixture::commandContexts;}
bool ReceiveCommandNetwork(std::uint32_t,const char*,const void* bytes,std::size_t size,std::uint64_t) noexcept {
    if(size<4 || std::memcmp(bytes,"NCMD",4))return false;
    ++fixture::commandPackets;return true;
}
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
PlanResult PlanCall(std::uint32_t id,const float* target,Plan* plan) noexcept {
    *plan={};plan->catalogId=id;plan->count=1;std::memcpy(plan->target,target,12);
    auto& u=plan->units[0];u.resourceId=1;u.matrix[0]=u.matrix[5]=u.matrix[10]=u.matrix[15]=1;return PlanResult::ready;
}
bool Validate(const Plan& p) noexcept { return p.count==1 && p.units[0].resourceId==1; }
bool Spawn(std::uint64_t,const Plan&,bool remote) noexcept { Check(!remote,"host native spawn callback authority");++spawnCount;return true; }
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
    crew::ConfigureSupportNet({&PlanCall,&Validate,&Spawn,&Destroy,&Derive});
    wchar_t note[256]{};const float at[3]={0,1,2};
    Check(crew::SubmitSupportRequest(1,at,note,256),"production dynamic ABI accepts host request");Step();
    Check(spawnCount==1,"production frame runs planner and native callback");
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
    auto commandPacket=packet;std::memcpy(commandPacket.bytes,"NCMD",4);incoming.push_back(commandPacket);Step();
    Check(commandPackets==1 && commandContexts>0 && sendCount==1,"shared extension poll routes command packets once without consuming a support message");
    Check(crew::SubmitSupportRequest(1,at,note,256),"full handshake queues request");
    installed=false;Step();Check(!crew::SubmitSupportRequest(1,at,note,256),"unloaded bridge disables queued work");
    installed=true;peerCount=0;generation++;tick+=1000;Step();
    online=false;Step();Check(!crew::SubmitSupportRequest(1,at,note,256),"offline never sends multiplayer requests");
    crew::ResetSupportNet();
    WorldParticipants();
    std::printf("support runtime bridge: %d checks passed\n",checks);
}
