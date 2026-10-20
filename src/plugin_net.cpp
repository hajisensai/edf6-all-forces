// The plugin's online message channels (plugin_net.h; the rules plugin_net_logic.h). Game thread only: support_net.cpp's one
// extension poll calls Update and Receive.
#define _CRT_RAND_S
#include <cstdlib>
#include "plugin_net.h"
#include "crew.h"
#include "support_protocol.h"
#include <cstring>

namespace crew::plugin_net {
namespace {
Registry registry;
Peers peers;
Room room{};
bool ready=false;
// The room the peers' words are of: a change (another epoch, host, roster size, this machine) forgets them.
std::uint64_t roomEpoch=0;
std::uint32_t roomPeers=0,roomHostPeer=0;
bool roomHost=false;
char roomLocal[kPuidChars+1]{};
// Host: the roster last sent, and when.
std::uint64_t rosterSum=0,rosterAt=0;
constexpr std::uint64_t kRosterGapMs=3000;   // sent again this often (and at once on a change): a guest that missed one
int rosterLogs=0;

std::uint64_t Sum(const RosterEntry* e,std::size_t n) noexcept {
    std::uint64_t h=1469598103934665603ull;
    for(std::size_t i=0;i<n;++i) {
        for(const char* c=e[i].puid;*c;++c)h=(h^static_cast<unsigned char>(*c))*1099511628211ull;
        h=(h^e[i].ext)*1099511628211ull;
    }
    return h^n;
}

void SendRoster(std::uint64_t now) noexcept {
    const std::uint32_t count=room.context.peerCount;
    static RosterEntry entries[kMaxPeers+1];
    std::size_t n=0;
    const char* own=room.context.localPuid;
    if(ValidPuid(own)){strcpy_s(entries[n].puid,own);entries[n].ext=room.ownExt;++n;}
    for(std::uint32_t p=1;p<=count && room.puid;++p) {
        const char* id=room.puid(p);
        if(!peers.Known(p) || !ValidPuid(id))continue;
        strcpy_s(entries[n].puid,id);entries[n].ext=peers.Ext(p);++n;
    }
    if(!n)return;
    const std::uint64_t sum=Sum(entries,n);
    if(sum==rosterSum && now-rosterAt<kRosterGapMs)return;
    rosterSum=sum;rosterAt=now;
    unsigned char bytes[kMaxPayload];
    for(std::size_t first=0;first<n;first+=kRosterPerMessage) {
        const std::size_t part=n-first<kRosterPerMessage ? n-first : kRosterPerMessage;
        const std::size_t used=EncodeRoster(entries+first,part,bytes,sizeof(bytes));
        if(used)Broadcast(support_net::kExtPeerRoster,bytes,used);
    }
}

void ReceiveRoster(std::uint32_t peer,const void* bytes,std::size_t size) noexcept {
    if(room.context.host || peer!=room.context.hostPeer || !room.puid)return;   // the host's word alone
    RosterEntry entries[kRosterPerMessage];
    std::size_t n=0;
    if(!DecodeRoster(bytes,size,entries,&n)) {
        if(rosterLogs++<4)Log("PLUGIN NET the host's roster could not be read (%zu bytes)",size);
        return;
    }
    for(std::size_t i=0;i<n;++i)for(std::uint32_t p=1;p<=room.context.peerCount;++p) {
        const char* id=room.puid(p);
        if(id && !std::strcmp(id,entries[i].puid)){peers.Set(p,entries[i].ext);break;}
    }
}

bool SameRoom(const CommandNetworkContext& c) noexcept {
    const char* local=c.localPuid ? c.localPuid : "";
    return c.epoch==roomEpoch && c.peerCount==roomPeers && c.hostPeer==roomHostPeer && c.host==roomHost &&
        !std::strcmp(local,roomLocal);
}
}  // namespace

bool RegisterChannel(const Channel& channel) noexcept {
    if(channel.magic==kRosterMagic)return false;
    const bool ok=registry.Register(channel);
    if(!ok)Log("PLUGIN NET channel %s not registered (full, or its magic taken)",channel.name ? channel.name : "?");
    return ok;
}

void Update(const Room& next,std::uint64_t now) noexcept {
    room=next;
    const CommandNetworkContext& c=room.context;
    const bool up=c.ready && c.send && c.peerCount && c.peerCount<=kMaxPeers;
    if(!up || !SameRoom(c)) {
        peers.Reset();rosterSum=0;rosterAt=0;
        roomEpoch=c.epoch;roomPeers=c.peerCount;roomHostPeer=c.hostPeer;roomHost=c.host;
        roomLocal[0]=0;
        if(c.localPuid && ValidPuid(c.localPuid))strcpy_s(roomLocal,c.localPuid);
    }
    ready=up;
    if(ready && c.host && room.heard) {
        for(std::uint32_t p=1;p<=c.peerCount;++p) {
            const std::uint32_t ext=room.heard(p);
            if(!ext){if(peers.Known(p))peers.Forget(p);continue;}
            if(!peers.Known(p) || peers.Ext(p)!=ext)peers.Set(p,ext);
        }
        SendRoster(now);
    }
    for(std::size_t i=0;i<registry.Count();++i)if(registry.At(i).update)registry.At(i).update(c,now);
    if(ready)peers.Joins(registry,c.peerCount,[](std::size_t i,std::uint32_t p) noexcept {registry.At(i).joined(p);});
}

bool Receive(std::uint32_t peer,const char* puid,const void* bytes,std::size_t size,std::uint64_t now) noexcept {
    if(!bytes || size<4)return false;
    if(wire::HeadWord(bytes,size,0)==kRosterMagic) {
        if(ready)ReceiveRoster(peer,bytes,size);
        return true;
    }
    return registry.Dispatch(peer,puid,bytes,size,now,[](std::uint32_t p,std::uint32_t bit) noexcept {return peers.Has(p,bit);})!=Route::none;
}

void Reset() noexcept {
    for(std::size_t i=0;i<registry.Count();++i)if(registry.At(i).reset)registry.At(i).reset();
    peers.Reset();ready=false;rosterSum=0;rosterAt=0;
    roomEpoch=0;roomPeers=0;roomHostPeer=0;roomHost=false;roomLocal[0]=0;
}

bool Ready() noexcept { return ready; }
bool Host() noexcept { return ready && room.context.host; }
std::uint32_t HostPeer() noexcept { return ready && !room.context.host ? room.context.hostPeer : 0u; }
std::uint32_t PeerCount() noexcept { return ready ? room.context.peerCount : 0u; }
bool PeerHas(std::uint32_t peer,std::uint32_t featureBit) noexcept {
    return ready && peer && peer<=room.context.peerCount && peers.Has(peer,featureBit);
}
bool Send(std::uint32_t peer,std::uint32_t featureBit,const void* bytes,std::size_t size) noexcept {
    return bytes && size && size<=kMaxPayload && PeerHas(peer,featureBit) && room.context.send(peer,bytes,size);
}
std::uint32_t Broadcast(std::uint32_t featureBit,const void* bytes,std::size_t size) noexcept {
    std::uint32_t sent=0;
    for(std::uint32_t p=1;ready && p<=room.context.peerCount;++p)if(Send(p,featureBit,bytes,size))++sent;
    return sent;
}
bool SendHost(std::uint32_t featureBit,const void* bytes,std::size_t size) noexcept {
    const std::uint32_t host=HostPeer();
    return host && Send(host,featureBit,bytes,size);
}
std::uint32_t NewOrigin() noexcept {
    unsigned r=0;
    if(rand_s(&r))r=0;
    return Origin(r,GetTickCount64());
}
}  // namespace crew::plugin_net
