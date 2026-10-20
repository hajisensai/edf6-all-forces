// The plugin's vehicle messages over the game's own NetworkObject (vehicle +0x120: the stock descriptor routes a message
// to the same vehicle's copy on every machine; no connection of our own). One transport (seat_net.cpp) carries every
// kind under one tag: the drill's flight (drill_net.h), the Proteus's control and shield (proteus_net.h), the Sazabi's
// pose (sazabi_net.h) and a seat's weapon pick (Pick below). Each kind's block starts with the same Head: its magic
// names the kind (the receiver dispatches on it), its sender and sequence order one machine's messages, and its
// controller is the control epoch (the seat rider's ReferenceId, -1 for the host's NPC or an empty seat) a receiver
// compares with its own view of who sits there. Pure: tests/seat_net_test.cpp checks it off the game.
//
// Old builds: the stock receiver (0x6325B0) reads tag 15's type and returns without touching the vehicle
// (tests/drill_net_native_test.cpp runs it); an older plugin's drill receiver takes every tag 15 block on a 505 and
// drops one it cannot decode, its Proteus and Sazabi receivers pass a block of another magic on to the stock one. So a
// kind an older peer does not know is ignored there, silently, never logged as malformed.
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace crew::seat_net {
constexpr std::int8_t kTag=15;   // coop reserves 13 (hits) and 14 (RNG)

struct Head {
    std::uint32_t magic=0,version=0;
    std::uint64_t sender=0;
    std::uint32_t sequence=0;
    std::int32_t controller=-1;
};
static_assert(sizeof(Head)==24 && offsetof(Head,sender)==8 && offsetof(Head,controller)==20,"the kinds' common wire head");

// A block's head (`size` the block's bytes): false when it is shorter than one.
inline bool ReadHead(const void* bytes,std::size_t size,Head* into) noexcept {
    if(!bytes || size<sizeof(Head) || !into)return false;
    std::memcpy(into,bytes,sizeof(Head));
    return true;
}
// A block's magic (the kind), 0 when it has none.
inline std::uint32_t MagicOf(const void* bytes,std::size_t size) noexcept {
    std::uint32_t magic=0;
    if(bytes && size>=sizeof(magic))std::memcpy(&magic,bytes,sizeof(magic));
    return magic;
}
// The head of a kind `magic` of version `version`: a sender, a sequence (0 is never sent), an epoch.
constexpr bool HeadValid(const Head& h,std::uint32_t magic,std::uint32_t version) noexcept {
    return h.magic==magic && h.version==version && h.sender!=0 && h.sequence!=0 && h.controller>=-1;
}

// One stream's duplicate / reorder gate (a vehicle's, a seat's): each sender's last sequence. Kept inside the state of
// the ObjRef-bound object, never keyed by a raw address alone. Each sender's watermark survives a driver change, so a
// delayed message from the previous driver cannot undo the new driver's; bounded (`N` senders), never evicting a
// watermark (which would re-admit an old message).
template<std::size_t N> struct BasicGate {
    struct Seen { std::uint64_t sender=0;std::uint32_t sequence=0; };
    static constexpr std::size_t kMaxSenders=N;
    Seen seen[N]{};
    // `online` in a session now; `runsHere` this machine runs the stream itself (its own messages never apply);
    // `epoch` the controller this machine sees now.
    bool Admit(const Head& h,bool online,bool runsHere,std::int32_t epoch) noexcept {
        if(!online || runsHere || h.controller!=epoch || !h.sender || !h.sequence)return false;
        Seen* empty=nullptr;
        for(auto& x:seen) {
            if(!x.sender && !empty)empty=&x;
            if(x.sender!=h.sender)continue;
            // Serial-number arithmetic also accepts the sender's uint32 wrap, while rejecting older messages.
            const std::uint32_t delta=h.sequence-x.sequence;
            if(!delta || delta>=0x80000000u)return false;
            x.sequence=h.sequence;return true;
        }
        if(!empty)return false;
        *empty=Seen{h.sender,h.sequence};return true;
    }
};
using Gate=BasicGate<1024>;   // the coop room's supported peer capacity

// A receiver's gates of many streams (a vehicle's seat each: payload.cpp's picks), kept apart from the records the
// messages fill. A record is forgotten when not refreshed for a while (kPickStaleMs) or taken for another stream; its
// gate is not: before 2026-10-20 the gate lived in the record, so 2 s after a pick stopped coming a delayed older pick
// of the same sender was admitted again and undid the newer choice (2026-10-20 review). A stream's gate is dropped only
// with its object (`live` false: a slot is taken from a gone object when no free one is left; never from a live one,
// which would re-admit an old message) or with the room's members (Reset: another room, a mission's end).
template<std::size_t Streams,std::size_t Senders> class StreamGates {
public:
    struct Key { const void* obj=nullptr; const void* ctrl=nullptr; unsigned stream=0; };
    // The gate of `k` (made when new), null when every slot is a live object's other stream (the message refused).
    // `live(obj,ctrl)`: whether that object still lives.
    template<class Live> BasicGate<Senders>* For(const Key& k,Live live) noexcept {
        if(!k.obj)return nullptr;
        Slot* free=nullptr;
        for(auto& s:slots_) {
            if(s.used && s.key.obj==k.obj && s.key.ctrl==k.ctrl && s.key.stream==k.stream)return &s.gate;
            if(!s.used && !free)free=&s;
        }
        if(!free)for(auto& s:slots_)if(!live(s.key.obj,s.key.ctrl)){free=&s;break;}
        if(!free)return nullptr;
        *free=Slot{};free->used=true;free->key=k;
        return &free->gate;
    }
    // An object gone (every stream of it).
    void Forget(const void* obj) noexcept { for(auto& s:slots_)if(s.used && s.key.obj==obj)s=Slot{}; }
    void Reset() noexcept { for(auto& s:slots_)s=Slot{}; }
    std::size_t Used() const noexcept { std::size_t n=0;for(const auto& s:slots_)n+=s.used ? 1u : 0u;return n; }
private:
    struct Slot { bool used=false; Key key{}; BasicGate<Senders> gate{}; };
    Slot slots_[Streams]{};
};

// A vehicle copy that is replicated (registered: its network flags +0x128 bit 0 or 1) in a session.
constexpr bool Replicated(bool session,std::uint16_t flags) noexcept { return session && (flags&3)!=0; }

// The next sequence of a sender's counter: never 0.
inline std::uint32_t NextSequence(std::uint32_t& counter) noexcept {
    if(++counter==0)++counter;
    return counter;
}

// --- A seat's weapon pick (payload.cpp): which of the seat's weapons its fire lands on (a stock vehicle's switchable
// stores, its native ammunition alternatives). The machine whose choice it is sends it (the seat's player on theirs;
// for an NPC gunner, the machine that runs it: IsOnlineAuthority); every other machine lands that seat's pulls on the
// same weapon (PullHook). `holder` is the weapon's holder in the vehicle's holder array (veh +0x638, NativeHolderIndex),
// -1 none picked; `seat` the seat; `flags` kNpc when an NPC gunner chose it.
constexpr std::uint32_t kPickMagic=0x4B435053,kPickVersion=1;   // 'SPCK'
constexpr std::uint32_t kNpc=1,kPickFlags=1;
constexpr std::uint32_t kMostSeats=16,kMostHolders=64;
struct Pick {
    Head head{kPickMagic,kPickVersion,0,0,-1};
    std::uint32_t seat=0;
    std::int32_t holder=-1;
    std::uint32_t flags=0;
    std::uint32_t reserved=0;   // explicit: no uninitialized padding goes on the wire
};
static_assert(sizeof(Pick)==40 && offsetof(Pick,reserved)==36,"pointer-free, explicit wire layout");
constexpr bool Valid(const Pick& p) noexcept {
    return HeadValid(p.head,kPickMagic,kPickVersion) && p.seat<kMostSeats && p.holder>=-1 &&
        p.holder<static_cast<std::int32_t>(kMostHolders) && !(p.flags&~kPickFlags) && p.reserved==0;
}
inline bool Decode(const void* bytes,std::size_t size,Pick& into) noexcept {
    if(!bytes || size!=sizeof(Pick))return false;
    Pick p;std::memcpy(&p,bytes,sizeof(p));
    if(!Valid(p))return false;
    into=p;return true;
}
// When a machine sends its seat's pick: at once when it changes, else every kPickRefreshMs (a lost message, a copy that
// joined late, a receiver whose record expired). A receiver forgets a pick not refreshed for kPickStaleMs.
constexpr unsigned long long kPickRefreshMs=500,kPickStaleMs=2000;
constexpr bool PickDue(bool changed,unsigned long long now,unsigned long long sentAt) noexcept {
    return changed || sentAt==0 || now-sentAt>=kPickRefreshMs;
}
}  // namespace crew::seat_net
