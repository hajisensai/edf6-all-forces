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
std::uint64_t generation=1;
unsigned spawnCount=0,destroyCount=0,sendCount=0,peerCount=0;
EDF6CoopPeer Peer(const char* id) { EDF6CoopPeer p{};strcpy_s(p.id,id);return p; }
struct Packet { EDF6CoopPeer sender;unsigned char bytes[176]; };
std::deque<Packet> incoming;
HMODULE WINAPI Module(LPCWSTR name) { return installed && !wcscmp(name,L"EDF6Coop.dll") ? reinterpret_cast<HMODULE>(1) : nullptr; }
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
FARPROC WINAPI Proc(HMODULE,LPCSTR name) { return !std::strcmp(name,"EDF6CoopGetExtensionApi") ? reinterpret_cast<FARPROC>(&Api) : nullptr; }
PlanResult PlanCall(std::uint32_t id,const float* target,Plan* plan) noexcept {
    *plan={};plan->catalogId=id;plan->count=1;std::memcpy(plan->target,target,12);
    auto& u=plan->units[0];u.resourceId=1;u.matrix[0]=u.matrix[5]=u.matrix[10]=u.matrix[15]=1;return PlanResult::ready;
}
bool Validate(const Plan& p) noexcept { return p.count==1 && p.units[0].resourceId==1; }
bool Spawn(std::uint64_t,const Plan&,bool remote) noexcept { Check(!remote,"host native spawn callback authority");++spawnCount;return true; }
void Destroy(std::uint64_t) noexcept { ++destroyCount; }
bool Derive(std::uint32_t ordinal,unsigned char* bytes) noexcept { std::memset(bytes,0,32);bytes[12]=5;std::memcpy(bytes+4,&ordinal,4);return true; }
void Step() { tick+=100;crew::SupportNetTick(); }
}
int main() {
    using namespace fixture;
    crew::ConfigureSupportNet({&PlanCall,&Validate,&Spawn,&Destroy,&Derive});
    wchar_t note[256]{};const float at[3]={0,1,2};
    Check(crew::SubmitSupportRequest(1,at,note,256),"production dynamic ABI accepts host request");Step();
    Check(spawnCount==1,"production frame runs planner and native callback");
    ready=false;Step();Check(destroyCount>0,"not-ready snapshot rolls back active objects");
    ready=true;generation++;peerCount=1;stale=true;Step();
    Check(!crew::SubmitSupportRequest(1,at,note,256),"peer roster changed during snapshot fails closed");
    stale=false;generation++;Step();
    Message hello;hello.kind=Kind::hello;hello.challenge=9;hello.request=1;
    Packet packet{};packet.sender=Peer("intruder");Check(Encode(hello,packet.bytes,sizeof(packet.bytes)),"encode handshake");
    incoming.push_back(packet);Step();Check(sendCount==0,"unknown authenticated sender absent from roster cannot handshake");
    packet.sender=Peer("client");incoming.push_back(packet);Step();Check(sendCount==1,"roster peer receives welcome");
    Check(crew::SubmitSupportRequest(1,at,note,256),"full handshake queues request");
    installed=false;Step();Check(!crew::SubmitSupportRequest(1,at,note,256),"unloaded bridge disables queued work");
    installed=true;peerCount=0;generation++;tick+=1000;Step();
    online=false;Step();Check(!crew::SubmitSupportRequest(1,at,note,256),"offline never sends multiplayer requests");
    crew::ResetSupportNet();
    std::printf("support runtime bridge: %d checks passed\n",checks);
}
