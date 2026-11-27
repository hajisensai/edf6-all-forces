// The plugin's online message channels (plugin_net.h; the rules plugin_net_logic.h). Game thread only: support_net.cpp's one
// extension poll calls Update and Receive.
#define _CRT_RAND_S
#include <cstdlib>
#include "plugin_net.h"
#include "net_counters.h"
#include "edf_layout.h"
#include "plugin_log.h"

namespace crew::plugin_net {
namespace {
Registry registry;
Peers peers;
Room room{};
bool ready=false;
// The room the joins and the channels' state are of (plugin_net_logic.h RoomWatch): a change (another epoch, host,
// roster size, this machine) forgets the joins and resets every channel.
RoomWatch roomWatch;
std::uint32_t roomGeneration=0;   // RoomGeneration: each change and each whole reset
}  // namespace

bool RegisterChannel(const Channel& channel) noexcept {
    const bool ok=registry.Register(channel);
    if(!ok)Log("PLUGIN NET channel %s not registered (full, or its magic taken)",channel.name ? channel.name : "?");
    return ok;
}

void Update(const Room& next,std::uint64_t now) noexcept {
    room=next;
    const CommandNetworkContext& c=room.context;
    const bool up=c.ready && c.send && c.peerCount && c.peerCount<=kMaxPeers;
    const RoomStep step=roomWatch.See(c,up);
    if(step.forgetPeers)peers.Reset();
    if(step.resetChannels) {
        Log("PLUGIN NET the room changed (epoch %llu, %u peers, host peer %u%s): every channel reset",
            static_cast<unsigned long long>(c.epoch),c.peerCount,c.hostPeer,c.host ? ", hosting" : "");
        ++roomGeneration;
        ready=false;           // a reset sends nothing: the indexes are the new room's already
        registry.ResetAll();   // before any channel's update of the new room
    }
    ready=up;
    for(std::size_t i=0;i<registry.Count();++i)if(registry.At(i).update)registry.At(i).update(c,now);
    if(ready && room.peersReady)peers.Joins(registry,c.peerCount,[](std::size_t i,std::uint32_t p) noexcept {registry.At(i).joined(p);});
}

bool Receive(std::uint32_t peer,const char* puid,const void* bytes,std::size_t size,std::uint64_t now,bool state) noexcept {
    if(!bytes || size<4)return false;
    netcount::In(wire::HeadWord(bytes,size,0),size);   // counted only (PERF NET CHANNELS, net_counters.h)
    return registry.Dispatch(peer,puid,bytes,size,now,state)!=Route::none;
}

void Reset() noexcept {
    ++roomGeneration;
    ready=false;
    registry.ResetAll();
    peers.Reset();
    roomWatch.Reset();
}

bool Ready() noexcept { return ready; }
std::uint32_t RoomGeneration() noexcept { return roomGeneration; }
bool Host() noexcept { return ready && room.context.host; }
std::uint32_t HostPeer() noexcept { return ready && !room.context.host ? room.context.hostPeer : 0u; }
std::uint32_t PeerCount() noexcept { return ready ? room.context.peerCount : 0u; }
bool Send(std::uint32_t peer,const void* bytes,std::size_t size) noexcept {
    if(!(ready && peer && peer<=room.context.peerCount && bytes && size && size<=kMaxPayload && room.context.send(peer,bytes,size)))return false;
    netcount::Out(wire::HeadWord(bytes,size,0),size);   // counted only (PERF NET CHANNELS, net_counters.h)
    return true;
}
std::uint32_t Broadcast(const void* bytes,std::size_t size) noexcept {
    std::uint32_t sent=0;
    for(std::uint32_t p=1;ready && p<=room.context.peerCount;++p)if(Send(p,bytes,size))++sent;
    return sent;
}
bool SendHost(const void* bytes,std::size_t size) noexcept {
    const std::uint32_t host=HostPeer();
    return host && Send(host,bytes,size);
}
bool SendState(std::uint32_t peer,std::uint32_t key,const void* bytes,std::size_t size) noexcept {
    if(!(ready && key && peer && peer<=room.context.peerCount && bytes && size && size<=kMaxPayload && room.context.sendState &&
         room.context.sendState(peer,key,bytes,size)))return false;
    netcount::Out(wire::HeadWord(bytes,size,0),size);   // counted only (PERF NET CHANNELS, net_counters.h)
    return true;
}
std::uint32_t BroadcastState(std::uint32_t key,const void* bytes,std::size_t size) noexcept {
    std::uint32_t sent=0;
    for(std::uint32_t p=1;ready && p<=room.context.peerCount;++p)if(SendState(p,key,bytes,size))++sent;
    return sent;
}
std::uint32_t NewOrigin() noexcept {
    unsigned r=0;
    if(rand_s(&r))r=0;
    return Origin(r,GetTickCount64());
}
}  // namespace crew::plugin_net
