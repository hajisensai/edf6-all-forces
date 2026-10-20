// The plugin's gravity operations (gravity.cpp; docs/gravity-re.md; the user, 2026-10-20: 「把"操作重力"做成函数之类的方便
// 操作，以后要经常使用它」): one way to grab, carry, push, pull and throw whatever the game's physics moves, and to deal what a
// thrown thing hits as its mass and speed say (gravity_logic.h; the user: 「根据速度和质量造成伤害」). The gravity gun
// (gravitygun.cpp) is its first user.
//  - A Target is what can be moved: a live character through its own controller (an insect / berserker's cling, a walker's
//    walk: the game's own way of launching them), a corpse or a vehicle through its ragdoll's bodies, anything else the
//    physics simulates through its rigid body. TargetOf(object) / Pick(ray) make one; it is a weak reference (an ObjRef
//    and a body id), so a target gone since is simply not live.
//  - Every move is a velocity (m/s, world): SetVelocity, AddVelocity / Impulse, HoldAt (a frame of the carry spring), Throw
//    (the throw speed for its mass, then followed for its impacts), Radial (a pull or a push of everything round a point).
//  - What is thrown (Throw, Radial, or Track) is followed each frame (Frame, crew.cpp's shared tick): an impact (the speed
//    it loses beyond gravity, or the fall it lands with) deals gravity_logic.h ImpactDamage of its mass at the point; an
//    enemy it passes through is hit at its closing speed. Both through the impact charges (jet_bay.cpp ImpactDamage):
//    the thrower's side's enemies only, the thrower's kills. One charge a frame at most per thing followed: an impact and the
//    enemies it touches that frame are one blow (the charge reaches all of them), each thing's next at kContactGapMs.
// Game thread only. Every operation moves this machine's copy alone (the game replicates none of these velocities: an
// insect in the air, a corpse, a container, debris are each machine's own physics; docs/gravity-re.md §11): online, a move
// every machine must see goes through gravity_net.h (the gravity gun's do), which has each machine's copy moved the same way.
#pragma once
#include "crew.h"
#include "gravity_logic.h"
#include <cstdint>

namespace crew::gravity {
enum class Kind : std::uint8_t { none, body, cling, walk, ragdoll };
struct Target {
    ObjRef obj;                // the scene object (none: an ownerless body, a debris piece)
    std::uint32_t body=0xFFFFFFu;   // a Havok body id: the body moved (body), the one read for a ragdoll's place and speed
    std::size_t ctrl=0;        // cling / walk: the controller's offset in the object; ragdoll: the ragdoll's
    bool ragdollPtr=false;     // ragdoll: the object holds a pointer to it at `ctrl` (a humanoid's), not the ragdoll itself
    Kind kind=Kind::none;
    ::gravmath::Family family=::gravmath::Family::walker;
    float kg=0.0f;             // its mass (gravity_logic.h: the physics' own, or the family's estimate)
    float radius=1.0f;         // m: its size (the reach of its contacts, the blast of its impacts)
    // Update's bookkeeping. settlingSince: a live character found dead with no ragdoll on yet (its game time, 0: not);
    // lastAt / lastAtMs: an ownerless body's place when last updated (its id may be another body's once it is freed).
    std::uint64_t settlingSince=0,lastAtMs=0;
    float lastAt[3]{};
    explicit operator bool() const noexcept { return kind!=Kind::none; }
};
// What Update finds (docs/gravity-re.md §5, §12 A): a character that dies has its ground body taken out of the world at
// once (0x6DAE80, the death's damage handler 0x4A8E4D) and its ragdoll put in only later, in its death state (0x4A8570 ->
// 0x6E4160): between the two there is nothing to move. `settling` is that gap: the target is kept (no move), its corpse
// taken over once the ragdoll is on, given up after kSettleMs.
enum class Liveness : std::uint8_t { live, settling, gone };
constexpr std::uint64_t kSettleMs=500;   // about 30 frames

bool Install() noexcept;        // the code checks (plugin.cpp, at load); false: every operation refuses
// Whether `obj`'s class has the MSVC RTTI type descriptor `td` (an RVA) among its bases: its primary vtable's complete object
// locator (vt[-1]), class hierarchy and base class array, read as memory (no call; docs/gravity-re.md §1.3). Under the
// caller's __try. E.g. InsectBase 0x202A270, HumanoidBase 0x203B5B0, HumanBase 0x2016A98, VehicleBase 0x2006480.
bool IsA(const void* obj,std::uint32_t td) noexcept;
bool Ready() noexcept;          // installed, in a mission (the physics world there)
void Reset() noexcept;          // a new mission (mission.cpp): nothing is followed any more
void Frame() noexcept;          // once a frame (crew.cpp SharedFrameTick): the followed throws' impacts and contacts

// What `object` is to gravity (false: nothing it can move: the map, a static body, an unknown class). `body`: the body a ray
// hit on it (0xFFFFFF: none; a ragdoll's place is then its first body's).
bool TargetOf(const void* object,std::uint32_t body,Target* out) noexcept;
// The first thing along a->b gravity can move, `self` (and what it rides) never; `hit` the point the ray met it at (or its
// own place for one found near the ray: an enemy's lock point within a few metres of it counts, the aim's help). False:
// nothing there.
bool Pick(const float* a,const float* b,const void* self,Target* out,float* hit) noexcept;
// Still there (a corpse is; a deleted object is not; an ownerless body whose mass is no longer its own is another's).
bool Live(const Target& t) noexcept;
// Each frame for what a caller keeps (Frame does it for what it follows; a carrier for what it holds; gravity_net.h for the
// peers' grabs): a live character that has died since is its corpse now (its ragdoll, the family's, takes over), or
// `settling` while that ragdoll is not on yet; an ownerless body that jumped further than anything flies in a frame is
// `gone` (its id freed and taken by another). Every caller treats the three alike: live moves it, settling keeps it unmoved.
Liveness Update(Target& t) noexcept;
inline bool Refresh(Target& t) noexcept { return Update(t)==Liveness::live; }
// A player character (any machine's), or a vehicle with a player aboard: not grabbed unless the ini's GravityGrabPlayers.
bool IsPlayerOrRidden(const Target& t) noexcept;
bool Position(const Target& t,float* at) noexcept;      // its centre (a ragdoll's: its read body's), world
bool Velocity(const Target& t,float* vel) noexcept;     // m/s
bool Landed(const Target& t) noexcept;                  // a character on the ground (a body: never)

bool SetVelocity(const Target& t,const float* vel) noexcept;
// SetVelocity now, or (a dying character, `settling`) once its corpse's ragdoll is on, within kSettleMs. False: gone.
bool Launch(const Target& t,const float* vel) noexcept;
bool AddVelocity(const Target& t,const float* dv) noexcept;
bool Impulse(const Target& t,const float* impulse) noexcept;   // N s: dv = impulse / kg
// One frame of carrying it to `goal` (moving at `goalVel`, may be null): gravity_logic.h HoldVelocity for its mass. False:
// too heavy to move, or gone.
bool HoldAt(const Target& t,const float* goal,const float* goalVel) noexcept;

// Throws it along `dir` (any length) at the speed gravity_logic.h ThrowSpeed gives `baseSpeed` for its mass (plus `carry`,
// the carrier's own motion, may be null), and follows it for `by` (Track). `vel`: the velocity it was given (may be null;
// gravity_net.h Thrown takes it). False: not thrown.
bool Throw(const Target& t,const float* dir,float baseSpeed,const float* carry,const unsigned char* by,float scale=1.0f,
           float* vel=nullptr) noexcept;
// Follows `t` for `by`'s side: its impacts and the enemies it passes through are hit (`held`: being carried, contacts only:
// its own velocity is the carrier's to change). A target already followed is taken over (its flight goes on). What it deals
// is the ini's GravityDamage times `scale` (the gravity gun's: its weapon's AmmoDamage). Nothing too heavy to lift
// (gravity_logic.h kMostLiftKg) is followed: it was never moved, so it hits nothing.
void Track(const Target& t,const unsigned char* by,bool held,float scale=1.0f) noexcept;
void Untrack(const Target& t) noexcept;
// Every enemy of `by`'s side within `radius` of `centre` given `speed` m/s along the line from the centre (negative: toward
// it, a pull), less with the distance (none at the edge), and (`track`) followed for `by`. How many were moved. This
// machine's copies alone: online use gravity::net::Radial (every machine moves its copies, the damage dealt here only).
int Radial(const float* centre,float radius,float speed,const unsigned char* by,float scale=1.0f,bool track=true) noexcept;
}  // namespace crew::gravity

namespace crew {
// gravitygun.cpp: the gravity gun (the shared tick, after gravity::Frame; a new mission forgets what it held).
void GravityGunTick() noexcept;
void ResetGravityGun() noexcept;
}  // namespace crew
