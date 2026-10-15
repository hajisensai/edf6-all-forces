// EDF4.1 classes online (UfoMother301 edf41mother.cpp, UfoMother401 edf41mother401.cpp, UfoRobo edf41robo.cpp): the shared
// network layer (src/edf41net.cpp; docs/edf41-classes.md section 联机). EDF.dll TimeDateStamp 0x678CCB46, RVAs.
//
// How EDF6 runs its own script-made enemies online (H unless noted; docs/edf41-classes.md 联机 has the addresses):
//  - every machine runs the mission script, so every machine makes the object (CreateEnemy 2000 / 2001 / CreateNeutral 1020
//    all call CreateFromPath and then 0x7834F0): the script native gives it a network identity derived from the script's
//    bytecode position, owned by the host (NetworkObject +8 bit 1 on the host, bit 0 on a guest). Our factories run on
//    every machine, and the hull / robot is such a registered object.
//  - every machine runs the object's own update (AI included); the owner corrects: its pose through the class's
//    NetworkObject slot 5 / 6 writer / reader (UfoCarrier508: position every 60 frames), its target (message 1), and the
//    durability: damage is judged by the shooter's machine (slot 34 0x54F8D0) and its HP change broadcast (event type 2);
//    the kill replayed everywhere (M).
//  - objects a boss makes (MovingFortress's parts 0x4C79C0, DropBoat's cargo, nests' spawns) get a child identity derived
//    from the parent's (0x781630: index, sub, the parent's hash), owned as the parent is, on every machine alike: the copies
//    are bound without any spawn message, as long as every machine makes the same children in the same order.
//  - per-object events travel on the object's own NetworkObject: send slot 16 (+0x80, 0x773DA0, a broadcast of event 7),
//    receive slot 17 (GameObjectBase 0x54D770 reads a small-int tag and knows 0..3; vehicles 4..5; coop 13 / 14; the
//    plugin's drill / Proteus / Sazabi 15 on vehicles). This layer uses tag 12 on its own copy of the classes' NetworkObject
//    vtable (Attach), so no stock table is touched.
//
// What this layer gives the classes:
//  - roles: Guest(obj) (online, registered, another machine runs it: show what the host decides) and Decides(obj);
//  - Child(parent, child, index, sub): the plugin's parts and spawns get the native child identity (queued until the
//    parent has its own; Pump(parent) from the parent's update registers what waits): durability, damage, kills and the
//    target then travel natively;
//  - Send / Drain: the host's decisions (the robot's body states, a part's attack decision) as small versioned events, read
//    back on the guest's copy at its next update;
//  - a development loopback (ini Edf41NetMirror, offline only): one object's events go through the game's own stream writer
//    and reader into another object's receiver, which then acts as a guest copy (the classes make the mirror).
// Off (ini Edf41Net=0): nothing is attached, sent or registered; every machine decides for its own copy (the state before).
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace crew::edf41net {

// ---- the wire (pure; tests/edf41net_test.cpp) ---------------------------------------------------------------------------
constexpr std::int8_t kTag=12;                 // free on GameObjectBase's slot 17 (0..3 stock, 13 / 14 coop, 15 the plugin's vehicles)
constexpr std::uint32_t kMagic=0x4E313445u;    // "E41N"
constexpr std::uint16_t kVersion=1;
constexpr std::size_t kMostPayload=192;
struct Header {
    std::uint32_t magic=kMagic;
    std::uint16_t version=kVersion;
    std::uint8_t kind=0;
    std::uint8_t size=0;          // payload bytes after the header
    std::uint64_t sender=0;       // the sending process (random, never 0)
    std::uint32_t sequence=0;     // the sender's count, never 0
    std::uint32_t reserved=0;     // 0 on the wire
};
static_assert(sizeof(Header)==24,"fixed x64 little-endian wire layout");
constexpr std::size_t kMostMessage=sizeof(Header)+kMostPayload;

// Header + payload into `out` (room bytes); false on a bad size or kind 0.
inline bool Encode(const Header& h,const void* payload,std::size_t size,unsigned char* out,std::size_t room,std::size_t* written) noexcept {
    if(!out || !written || h.kind==0 || size>kMostPayload || (size && !payload) || room<sizeof(Header)+size || !h.sender || !h.sequence)return false;
    Header w=h;
    w.magic=kMagic; w.version=kVersion; w.size=static_cast<std::uint8_t>(size); w.reserved=0;
    std::memcpy(out,&w,sizeof(w));
    if(size)std::memcpy(out+sizeof(w),payload,size);
    *written=sizeof(w)+size;
    return true;
}
// A message as it came: the header checked (magic, version, kind, sender, sequence, its size = the bytes there are).
inline bool Decode(const unsigned char* data,std::size_t n,Header* h,const unsigned char** payload) noexcept {
    if(!data || !h || !payload || n<sizeof(Header) || n>kMostMessage)return false;
    Header r;
    std::memcpy(&r,data,sizeof(r));
    if(r.magic!=kMagic || r.version!=kVersion || r.kind==0 || !r.sender || !r.sequence || r.reserved || sizeof(Header)+r.size!=n)return false;
    *h=r;
    *payload=data+sizeof(Header);
    return true;
}
// Per object: the newest sequence seen from each sender (serial arithmetic, so the sender's wrap is accepted): a repeat or an
// older message is refused, a new sender is taken (the host's process changes only with a new session). Bounded: a ninth
// sender is refused rather than an old watermark forgotten.
struct Gate {
    struct Seen { std::uint64_t sender=0; std::uint32_t sequence=0; };
    static constexpr int kSenders=8;
    Seen seen[kSenders]{};
    bool Admit(std::uint64_t sender,std::uint32_t sequence) noexcept {
        if(!sender || !sequence)return false;
        Seen* empty=nullptr;
        for(Seen& s:seen) {
            if(!s.sender) { if(!empty)empty=&s; continue; }
            if(s.sender!=sender)continue;
            const std::uint32_t delta=sequence-s.sequence;
            if(!delta || delta>=0x80000000u)return false;
            s.sequence=sequence;
            return true;
        }
        if(!empty)return false;
        *empty=Seen{sender,sequence};
        return true;
    }
};
// Finite and within the world (a corrupt or hostile float never reaches a matrix).
inline bool Sane(const float* v,int n,float limit=1.0e7f) noexcept {
    for(int i=0;i<n;++i)if(!std::isfinite(v[i]) || std::fabs(v[i])>limit)return false;
    return true;
}

// The child identities the classes use (0x781630's u16 index, u16 sub under the parent's hash): one (index, sub) per child.
constexpr std::uint16_t kChildPart=1;          // a hull's part by its creation order (301: h.parts; 401: its panels' serial)
constexpr std::uint16_t kChildBattery=0x4000;  // + the mount index; sub = the panel's serial (401)
constexpr std::uint16_t kChildSpawn=0x100;     // 301: + the hatch index, sub = the hatch's count; 401: kChildSpawn401
constexpr std::uint16_t kChildSpawn401=0x8000; // | (the generator panel's serial & 0x7FFF), sub = its count

// ---- the game side (src/edf41net.cpp) ------------------------------------------------------------------------------------
bool Enabled() noexcept;                           // ini Edf41Net and the code checked
bool Online() noexcept;                            // a session is on (netprobe.cpp InSession)
bool Registered(const void* object) noexcept;      // it has a network identity (+0x128 bits 0 / 1)
bool Guest(const void* object) noexcept;           // online, registered, run by another machine (or the mirror test's copy)
inline bool Decides(const void* object) noexcept { return !Guest(object); }
// The classes' NetworkObject vtable swapped for this layer's copy (the stock one with slot 17 wrapped): call from the factory.
void Attach(void* object) noexcept;
// One event of `kind` (1..255) from `object` to its copies on the other machines: online where it Decides and is registered,
// or through the development loopback. False when nothing was sent.
bool Send(void* object,std::uint8_t kind,const void* payload,std::size_t size) noexcept;
// The events that arrived for `object` since the last call, oldest first (game thread; call at its update).
using Visit=void(*)(void* context,std::uint8_t kind,const unsigned char* payload,std::size_t size) noexcept;
int Drain(const void* object,Visit visit,void* context) noexcept;
// The native child identity for `child` under `parent` (MovingFortress 0x4C79C0's call of 0x781630), online only; queued while
// the parent has none yet. `targetSync`: +0x591 = 1, the owner sends its target (message 1, GameObjectBase::Update 0x54C15D).
void Child(void* parent,void* child,std::uint16_t index,std::uint16_t sub,bool targetSync) noexcept;
void Pump(void* parent) noexcept;                  // registers the children waiting for `parent` (call at its update)
// Development (ini Edf41NetMirror, offline): `from`'s events reach `to` through the game's stream writer and reader; `to` is
// then a guest. Mirror() says whether the switch is on (and the game offline).
bool Mirror() noexcept;
void Loopback(void* from,void* to) noexcept;
bool IsMirror(const void* object) noexcept;
// Counters for the classes' periodic lines (sent, received, refused, children registered / waiting).
struct Stats { std::uint32_t sent=0,received=0,refused=0,looped=0,children=0,waiting=0,childFailed=0; };
Stats Counters() noexcept;
}  // namespace crew::edf41net
