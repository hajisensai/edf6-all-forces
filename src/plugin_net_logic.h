// The plugin's online message channels, the pure part (plugin_net.h the game's; tests/plugin_net_test.cpp runs this).
// Every plugin-to-plugin message rides the one EDF6Coop extension poll support_net.cpp owns. Before 2026-10-20 each
// feature kept its own magic check, codec helpers, broadcast loop, sender check and roster reset, and support_net.cpp
// called them in a chain; an unknown message ended in the support parser, which logged it malformed every time.
// Now a channel is a row of this registry:
//  - its magic (the message's first little-endian word) or, for a channel that owns several, its own `owns` test;
//  - its version (the second word; 0: the channel checks its own). A message of another version is the channel's and
//    dropped here;
//  - its size bounds (outside: dropped);
//  - its receive, update (each frame, the transport's context), reset (the room changed: the peer indexes mean other
//    machines now) and joined (the room's handshake done: the channel sends each peer its whole state).
// Every machine of a room runs the same build's protocols (mod_room.h kAllForcesRoomProtocol: an older or newer build
// neither finds nor joins the room), so a channel sends to every peer and reads every peer's messages: no per-feature
// bits, no roster of who has what (net-6, 2026-11-24). `joined` waits for the support handshake alone (support_protocol.h
// Session::RoomReady: the host heard every guest's hello; a guest, its host's welcome saying so).
#pragma once
#include "command_net.h"
#include "wire.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace crew::plugin_net {
constexpr std::uint32_t kMaxPeers=1024;
constexpr std::size_t kMaxChannels=24;
constexpr std::size_t kMaxPayload=1024;   // EDF6COOP_EXTENSION_MAX_PAYLOAD

struct Channel {
    const char* name="";
    std::uint32_t magic=0;                                    // first word; 0: `owns` decides
    bool (*owns)(const void* bytes,std::size_t size) noexcept=nullptr;
    std::uint32_t version=0;                                  // second word; 0: the channel checks its own
    std::size_t minSize=4,maxSize=kMaxPayload;
    bool (*receive)(std::uint32_t peer,const char* puid,const void* bytes,std::size_t size,std::uint64_t now) noexcept=nullptr;
    void (*update)(const CommandNetworkContext& context,std::uint64_t now) noexcept=nullptr;
    void (*reset)() noexcept=nullptr;
    void (*joined)(std::uint32_t peer) noexcept=nullptr;
    // A state message (EDF6Coop sendState; plugin_net.h SendState) of this channel's that it takes: true for the kinds it
    // sends as state. nullptr: the channel sends none, and every state message of its magic is dropped.
    bool (*takesState)(const void* bytes,std::size_t size) noexcept=nullptr;
};

// State messages (EDF6Coop ABI 2 sendState, net-5 2026-11-24): "the latest replaces the previous" under a key the sender
// picks; never retransmitted, a receiver drops one not newer than the last it took of (sender, key). Their payload is the
// channel's own message, the same bytes a reliable one would carry (EDF6Coop's [key][sequence] header is its own and
// stripped before delivery), routed by its magic like any other; the channel's takesState says which kinds may come so.
// The key: one stream of one sender, StateKey(stream, object):
//  - stream: a kind of state of one channel (StateStream below; a fixed number, never a registry index, so a key does not
//    move when a channel is added);
//  - object: which one of them on the sending machine, a slot it keeps for the object's life (an enemy-control lease's
//    slot, a show's slot; 0 for a machine's one hold). A slot used again for the next object is the same stream: its
//    sequence goes on, and the receiver tells the objects apart by the payload (a lease, a grab serial, a show key).
// stream*kStateObjects+object < 64*1024 = EDF6COOP_EXTENSION_MAX_STATE_KEYS; key 0 is no key.
enum class StateStream : std::uint32_t {
    enemyPose=1,       // enemy control: the host's pose of a lease (object: its lease slot, < enemy_control_protocol::kMaxLeases)
    gravityHold=2,     // gravity: this machine's hold (object 0)
    gravityFlight=3,   // gravity: this machine's thrown thing's flight (object 0)
    ultimateKeep=4,    // ultimate strike: a playing show's keep (object: its show slot, < ultimate::kMaxShows)
    jetLever=5,        // a registered jet's engine lever (object: its slot on the machine that runs it, jet_lever_net.cpp)
};
constexpr std::uint32_t kStateObjects=1024,kStateStreams=64;
// 0: no key (an object out of range, a stream not one).
constexpr std::uint32_t StateKey(StateStream stream,std::uint32_t object) noexcept {
    const auto s=static_cast<std::uint32_t>(stream);
    return s && s<kStateStreams && object<kStateObjects ? s*kStateObjects+object : 0u;
}
static_assert(kStateStreams*kStateObjects<=65536u,"every key under EDF6COOP_EXTENSION_MAX_STATE_KEYS");

enum class Route : std::uint8_t {
    none,        // no channel's: the caller tries its own parser (support_net's v2 messages)
    delivered,   // given to the channel
    dropped,     // the channel's, but not taken: another version, a size out of bounds
};

class Registry {
public:
    // False: full, no receive, no magic and no owns, a magic taken already, bounds that make no sense.
    bool Register(const Channel& c) noexcept {
        if(count_>=kMaxChannels || !c.receive || (!c.magic && !c.owns) || c.minSize<4 || c.minSize>c.maxSize ||
           c.maxSize>kMaxPayload)return false;
        if(c.magic)for(std::size_t i=0;i<count_;++i)if(rows_[i].magic==c.magic)return false;
        rows_[count_++]=c;
        return true;
    }
    std::size_t Count() const noexcept { return count_; }
    const Channel& At(std::size_t i) const noexcept { return rows_[i<count_ ? i : 0]; }
    // The channel a message is for: its magic first, then the channels that test their own.
    const Channel* Find(const void* bytes,std::size_t size,std::size_t* index=nullptr) const noexcept {
        if(!bytes || size<4)return nullptr;
        const std::uint32_t magic=wire::HeadWord(bytes,size,0);
        for(std::size_t i=0;i<count_;++i)if(rows_[i].magic && rows_[i].magic==magic){if(index)*index=i;return &rows_[i];}
        for(std::size_t i=0;i<count_;++i)if(!rows_[i].magic && rows_[i].owns && rows_[i].owns(bytes,size)){if(index)*index=i;return &rows_[i];}
        return nullptr;
    }
    // Every channel's reset (the room changed: RoomWatch; or the transport reset whole).
    void ResetAll() const noexcept { for(std::size_t i=0;i<count_;++i)if(rows_[i].reset)rows_[i].reset(); }
    // state: a state message (EDF6Coop sendState); none of a channel's: dropped, never the support parser's.
    Route Dispatch(std::uint32_t peer,const char* puid,const void* bytes,std::size_t size,std::uint64_t now,
                   bool state=false) const noexcept {
        const Channel* c=Find(bytes,size);
        if(!c)return state ? Route::dropped : Route::none;
        if(size<c->minSize || size>c->maxSize)return Route::dropped;
        if(state && (!c->takesState || !c->takesState(bytes,size)))return Route::dropped;
        if(c->version && wire::HeadWord(bytes,size,1)!=c->version)return Route::dropped;
        // A channel's receive returning false would hand its bytes to another parser: it never may (they are its).
        c->receive(peer,puid,bytes,size,now);
        return Route::delivered;
    }
private:
    Channel rows_[kMaxChannels]{};
    std::size_t count_=0;
};

// Which channels were told each peer joined (the room's handshake done: plugin_net.cpp Update calls Joins only then).
class Peers {
public:
    void Reset() noexcept { std::memset(told_,0,sizeof(told_)); }
    // For each channel with a joined callback and each peer 1..peers not told yet: `joined(channel index, peer)`, once
    // until Reset.
    template<class Joined>
    void Joins(const Registry& reg,std::uint32_t peers,Joined joined) noexcept {
        if(peers>kMaxPeers)peers=kMaxPeers;
        for(std::uint32_t p=1;p<=peers;++p) {
            for(std::size_t i=0;i<reg.Count() && i<32;++i) {
                const Channel& c=reg.At(i);
                const std::uint32_t mask=1u<<i;
                if(!c.joined || (told_[p]&mask))continue;
                told_[p]|=mask;
                joined(i,p);
            }
        }
    }
private:
    std::uint32_t told_[kMaxPeers+1]{};
};
static_assert(kMaxChannels<=32,"Peers::told_ keeps one bit a channel");

constexpr std::size_t kPuidChars=64;   // a PUID is at most 64 printable characters
inline bool ValidPuid(const char* p) noexcept {
    if(!p || !p[0])return false;
    std::size_t n=0;
    while(n<=kPuidChars && p[n])++n;
    if(n>kPuidChars)return false;
    for(std::size_t i=0;i<n;++i)if(static_cast<unsigned char>(p[i])<0x21 || static_cast<unsigned char>(p[i])>0x7E)return false;
    return true;
}

// Which room the channels' per-peer state is of (plugin_net.cpp Update, every frame). The room is
// its epoch, roster size, host peer, whether this machine hosts and this machine's PUID: another of any of them and the
// peer indexes mean other machines now. Before 2026-10-20 only the peer table was forgotten then; the channels'
// reset (the contract above) was called by the transport's whole reset alone, so a member joining or leaving kept each
// channel's state of the old indexes (2026-10-20 review).
//  - forgetPeers: the joins forgotten (not up, or another room): each channel told `joined` again once ready;
//  - resetChannels: every channel's reset, once a change, never each frame. A room left for none (offline, the
//    transport gone) is a change; none to a first room is not (no channel holds a peer's state yet). A short drop with
//    the room unchanged (the same epoch and roster when it comes back) is none: the peers are told `joined` again and
//    each channel's own update sees that it was not ready (an update must recover from frames it was not ready in).
struct RoomStep { bool forgetPeers=false,resetChannels=false; };
class RoomWatch {
public:
    RoomStep See(const CommandNetworkContext& c,bool up) noexcept {
        const char* local=ValidPuid(c.localPuid) ? c.localPuid : "";
        const bool same=c.epoch==epoch_ && c.peerCount==peers_ && c.hostPeer==hostPeer_ && c.host==host_ && !std::strcmp(local,local_);
        RoomStep step;
        step.forgetPeers=!up || !same;
        if(!same) {
            step.resetChannels=epoch_!=0 || peers_!=0;   // there was a room
            epoch_=c.epoch;peers_=c.peerCount;hostPeer_=c.hostPeer;host_=c.host;
            std::memcpy(local_,local,std::strlen(local)+1);
        }
        return step;
    }
    void Reset() noexcept { epoch_=0;peers_=0;hostPeer_=0;host_=false;local_[0]=0; }
private:
    std::uint64_t epoch_=0;
    std::uint32_t peers_=0,hostPeer_=0;
    bool host_=false;
    char local_[kPuidChars+1]{};
};

// A sender's run (a channel's `origin`: a new one tells receivers its state starts over): random, never zero.
constexpr std::uint32_t Origin(std::uint32_t random,std::uint64_t fallback) noexcept {
    return (random ? random : static_cast<std::uint32_t>(fallback ^ (fallback>>32)))|1u;
}
}  // namespace crew::plugin_net
