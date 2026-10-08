#define _CRT_RAND_S
#include "support_protocol.h"
#include "coop_extension_api.h"
#include "crew.h"
#include "online_authority.h"
#include <cstdlib>
#include <cstring>
#include <cwchar>

namespace crew {
namespace {
support_net::Session session;
support_net::Hooks hooks;
EDF6CoopExtensionApi api{};
EDF6CoopSnapshot snapshot{};
EDF6CoopPeer peers[support_net::kMaxPeers+1]{};
HMODULE module=nullptr;
bool running=false;
ULONGLONG nextResolve=0,lastTick=~0ULL;

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
bool PeerValid(const EDF6CoopPeer& peer) noexcept {
    return peer.id[0] && std::memchr(peer.id,0,sizeof(peer.id));
}
bool Resolve(ULONGLONG now) noexcept {
    const HMODULE current=GetModuleHandleW(L"EDF6Coop.dll");
    if(current && current==module && api.snapshot)return true;
    api={};module=nullptr;
    if(now<nextResolve)return false;nextResolve=now+1000;
    if(!current)return false;
    const auto get=reinterpret_cast<EDF6CoopGetExtensionApiFn>(GetProcAddress(current,"EDF6CoopGetExtensionApi"));
    if(!get || !get(EDF6COOP_EXTENSION_VERSION,sizeof(api),&api) || api.size!=sizeof(api) ||
       api.version!=EDF6COOP_EXTENSION_VERSION || !api.snapshot || !api.peer || !api.send || !api.poll) {
        api={};return false;
    }
    module=current;return true;
}
bool Start(const EDF6CoopSnapshot& next,ULONGLONG now) noexcept {
    session.Stop();running=false;
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
    session.Stop();running=false;snapshot={};lastTick=~0ULL;
}
void SupportNetTick() noexcept {
    const ULONGLONG now=GetTickCount64();
    if(lastTick==now)return;lastTick=now;
    if(!Cfg().enabled || !InSession() || !Resolve(now)) {if(running)ResetSupportNet();return;}
    EDF6CoopSnapshot next{};next.size=sizeof(next);
    if(!api.snapshot(&next) || !next.ready || next.size!=sizeof(next)) {if(running)ResetSupportNet();return;}
    if(!running || snapshot.generation!=next.generation)if(!Start(next,now))return;
    // Never spawn on the DirectNet worker. The map/crew game-thread frame owns
    // both deserialization and the native create/register/destroy callbacks.
    for(unsigned received=0;received<128;++received) {
        EDF6CoopPeer sender{};unsigned char bytes[EDF6COOP_EXTENSION_MAX_PAYLOAD]{};std::uint32_t count=0;
        if(!api.poll(snapshot.generation,&sender,bytes,sizeof(bytes),&count))break;
        if(!PeerValid(sender))continue;
        std::uint32_t peer=0;
        for(std::uint32_t i=1;i<=snapshot.peerCount;++i)if(!std::strcmp(sender.id,peers[i].id)){peer=i;break;}
        support_net::Message message;
        if(peer && support_net::Decode(bytes,count,message))session.Receive(peer,message,now);
    }
    session.Tick(now);
}
bool SubmitSupportRequest(int catalogId,const float* target,wchar_t* note,std::size_t size) noexcept {
    SupportNetTick();
    if(catalogId<0 || !running || !session.Ready()) {
        Note(note,size,L"联机支援尚未就绪：需全房同版全军出击与联机扩展，并完成关卡同步");return false;
    }
    if(!session.Submit(static_cast<std::uint32_t>(catalogId),target,GetTickCount64())) {
        Note(note,size,L"支援请求未受理：已有部署、调用过快或本关支援额度已满");return false;
    }
    Note(note,size,L"支援请求已排队，等待房主验证与全员确认");return true;
}
void ReportSupportFailure(std::uint64_t transaction) noexcept {
    if(transaction && transaction<=support_net::kMaxTransactions) {
        Log("SUPPORT NET rollback transaction=%llu",static_cast<unsigned long long>(transaction));
        session.Failed(static_cast<std::uint32_t>(transaction));
    }
}
std::uint64_t SubmitPreparedSupportPlan(const SupportPlan& plan) noexcept {
    SupportNetTick();
    return running && OnlineHostOnly() ? session.SubmitPrepared(plan,GetTickCount64()) : 0;
}
} // namespace crew

// The transport advertises af-support/1 only when this production protocol is
// actually loaded, not merely when the older room-isolation marker exists.
extern "C" __declspec(dllexport) std::uint32_t __cdecl EDF6AF_SupportProtocolVersion() noexcept { return 1; }
