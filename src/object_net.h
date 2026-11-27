// The plugin's object messages over the game's own NetworkObject (object +0x120: the stock descriptor routes a message to
// the same object's copy on every machine; no connection of our own). One layer (object_net.cpp) carries every kind under
// one tag: the drill's flight (drill_net.h), the Proteus's control and shield (proteus_net.h), the Sazabi's pose
// (sazabi_net.h), the Dreadnought's turrets (dreadnought_net.h), the Andryusha's fuse (andryusha_net.h), a vehicle's own
// turrets (seat_turret_net.h), a seat's weapon pick (seat_net.h) and the EDF4.1 classes' events (edf41net.h). Each kind's
// block starts with the same Head: its magic names the kind (the receiver dispatches on it through the kinds' registry),
// its sender and sequence order one machine's messages (one sender identity and one sequence counter for every kind), and
// its controller is the control epoch (the seat rider's ReferenceId; -1 for the host's NPC, an empty seat or an object
// without a rider) a receiver compares with its own view of who controls it. The classes whose NetworkObject slot 17 the
// layer chains by name are in kClasses below; a stock vehicle class is chained the first time one of its vehicles carries a
// message (ChainVehicle). Pure: tests/object_net_test.cpp checks it off the game; docs/seat-net.md.
//
// Old builds: the stock receivers (vehicles 0x6325B0, GameObjectBase 0x54D770) read tag 15's type and return without
// touching the object (tests/drill_net_native_test.cpp, tests/edf41net_native_test.cpp run them). Every member of a room
// runs the same build (kAllForcesRoomProtocol), so a kind's version is checked, never adapted.
#pragma once
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace crew::object_net {
constexpr std::int8_t kTag=15;   // coop reserves 13 (hits) and 14 (RNG); GameObjectBase knows 0..3, vehicles 4..5

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

// One stream's duplicate / reorder gate (a vehicle's, a seat's, an EDF4.1 object's): each sender's last sequence. Kept
// inside the state of the ObjRef-bound object, never keyed by a raw address alone. Each sender's watermark survives a
// driver change, so a delayed message from the previous driver cannot undo the new driver's; bounded (`N` senders), never
// evicting a watermark (which would re-admit an old message).
template<std::size_t N> struct BasicGate {
    struct Seen { std::uint64_t sender=0;std::uint32_t sequence=0; };
    static constexpr std::size_t kMaxSenders=N;
    Seen seen[N]{};
    // `online` in a session now; `runsHere` this machine runs the stream itself (its own messages never apply);
    // `epoch` the controller this machine sees now.
    bool Admit(const Head& h,bool online,bool runsHere,std::int32_t epoch) noexcept {
        if(!online || runsHere || h.controller!=epoch)return false;
        return AdmitSequence(h.sender,h.sequence);
    }
    // The ordering alone, for a kind that judges by itself who may send (edf41net.cpp: guest copies only).
    bool AdmitSequence(std::uint64_t sender,std::uint32_t sequence) noexcept {
        if(!sender || !sequence)return false;
        Seen* empty=nullptr;
        for(auto& x:seen) {
            if(!x.sender && !empty)empty=&x;
            if(x.sender!=sender)continue;
            // Serial-number arithmetic also accepts the sender's uint32 wrap, while rejecting older messages.
            const std::uint32_t delta=sequence-x.sequence;
            if(!delta || delta>=0x80000000u)return false;
            x.sequence=sequence;return true;
        }
        if(!empty)return false;
        *empty=Seen{sender,sequence};return true;
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

// An object copy that is replicated (registered: its network flags +0x128 bit 0 or 1) in a session.
constexpr bool Replicated(bool session,std::uint16_t flags) noexcept { return session && (flags&3)!=0; }

// The next sequence of a sender's counter: never 0.
inline std::uint32_t NextSequence(std::uint32_t& counter) noexcept {
    if(++counter==0)++counter;
    return counter;
}

// --- The kinds' registry: a block's magic -> its receiver (`object` the object the message came for, `block` its bytes,
// the Head first). A magic is taken once: a second receiver for it is refused (two files claiming one kind), the same one
// again is fine (an install run twice). Written at install, read by the receive hook: an entry is complete before the
// count that publishes it.
using Handler=void(*)(unsigned char* object,const void* block,std::size_t size) noexcept;
template<std::size_t N> class Kinds {
public:
    static constexpr std::size_t kMost=N;
    bool Add(std::uint32_t magic,Handler fn) noexcept {
        if(!magic || !fn)return false;
        const std::size_t n=count_.load(std::memory_order_acquire);
        for(std::size_t i=0;i<n;++i)if(kinds_[i].magic==magic)return kinds_[i].fn==fn;
        if(n>=N)return false;
        kinds_[n]=Kind{magic,fn};
        count_.store(n+1,std::memory_order_release);
        return true;
    }
    Handler Find(std::uint32_t magic) const noexcept {
        if(!magic)return nullptr;
        const std::size_t n=count_.load(std::memory_order_acquire);
        for(std::size_t i=0;i<n;++i)if(kinds_[i].magic==magic)return kinds_[i].fn;
        return nullptr;
    }
    std::size_t Count() const noexcept { return count_.load(std::memory_order_acquire); }
private:
    struct Kind { std::uint32_t magic=0; Handler fn=nullptr; };
    Kind kinds_[N]{};
    std::atomic<std::size_t> count_{0};
};
constexpr std::size_t kMostKinds=16;

// --- The classes whose NetworkObject receive slot the layer chains by name (ChainClass): each class's NetworkObject vtable
// (its RVA in EDF.dll 0x678CCB46), the stock receiver its slot 17 holds, a name for the log. Slot 16 (+0x80) is the stock
// send 0x773DA0 on each. docs/drill-re.md section 8.5 and docs/edf41-classes.md 联机 have the readings; the native tests
// (tests/drill_net_native_test.cpp, proteus_net_native_test.cpp, sazabi_net_native_test.cpp, edf41net_native_test.cpp)
// check every row on the real EDF.dll.
enum class Class : std::uint8_t { drill505, bigBegaruta, v506, eventAnimationObject, ufoCarrier508 };
constexpr unsigned kVehicleReceive=0x6325B0,kObjectReceive=0x54D770,kStockSend=0x773DA0;
constexpr unsigned kSendSlot=16,kReceiveSlot=17;
struct ClassSlot { Class id; unsigned table; unsigned stockReceive; const char* name; };
constexpr ClassSlot kClasses[]={
    {Class::drill505,0x17DB048,kVehicleReceive,"drill (505)"},
    {Class::bigBegaruta,0x17DEE10,kVehicleReceive,"Proteus (BigBegaruta)"},
    {Class::v506,0x17DB440,kVehicleReceive,"506 (Sazabi, Dreadnought)"},
    {Class::eventAnimationObject,0x17D4E90,kObjectReceive,"EventAnimationObject (EDF4.1 parts, UfoRobo)"},
    {Class::ufoCarrier508,0x17C9F50,kObjectReceive,"UfoCarrier508 (UfoMother301 / 401)"},
};
constexpr std::size_t kClassCount=sizeof(kClasses)/sizeof(kClasses[0]);
constexpr const ClassSlot* ClassOf(Class c) noexcept {
    for(const ClassSlot& s:kClasses)if(s.id==c)return &s;
    return nullptr;
}
// The RVA of slot `slot` of a class's NetworkObject vtable.
constexpr unsigned SlotRva(const ClassSlot& c,unsigned slot) noexcept { return c.table+slot*8u; }
}  // namespace crew::object_net
