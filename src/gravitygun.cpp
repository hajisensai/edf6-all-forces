// The gravity gun (tools/calls.py EDF6VC_CALL_GRAVITY_GUN; README 重力枪; docs/gravity-re.md §6; the user, 2026-10-20: 「看看这里面的
// 重力枪，咱们也做一个。要做的话最好让其根据速度和质量造成伤害」): the Air Raider's Laser Guide Kit (Weapon_LaserMarker, vtable
// 0x17E45E8) told apart by its AmmoHitSizeAdjust (weapon +0x8C4, the bits kGravityMarkBits: tools/call_weapons.py gravity_sgo).
// The kit draws its beam while the trigger is held and fires no round (its fire slot 16 is a bare ret, docs/gravity-re.md §6),
// so the plugin adds the rest, through the gravity operations (gravity.h):
//  - held (+0x13A, the base update's copy of this frame's trigger; +0x13E: in hand): what the beam's line meets within the
//    kit's reach (AmmoSpeed x AmmoAlive, +0x894 x +0x898) from its muzzle along its aim (+0x170, the owner matrix's forward
//    row: the kit's own beam direction) is picked (gravity::Pick) and carried (gravity::HoldAt) to the point on that line
//    kHoldMin.. m out (from where it was met, drawn in to kCarry m past its own size), keeping pace with the player; while
//    carried it hits the enemies it is swung through (gravity::Track, held);
//  - let go: thrown along the aim (gravity::Throw: the speed for its mass from the ini's GravityThrowSpeed, plus the player's
//    own motion), then followed for its impacts, the damage times the kit's AmmoDamage (+0x89C: 1, tools/call_weapons.py);
//  - put away (+0x13E clear), or the gun off: what it carries is let fall, still followed.
// Online every move is told to the peers too (gravity_net.h): each machine carries and throws its own copy the same way, the
// damage dealt here alone (this machine's player is the thrower). The player's own Laser Guide Kit is never touched.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "gravity.h"
#include "gravity_net.h"
#include "memory.h"
#include "edf/weapon.h"
#include <cmath>
#include <cstring>

namespace crew {
namespace {
constexpr unsigned kLaserMarkerVtable=0x17E45E8;
constexpr std::uint32_t kGravityMarkBits=0x3F800E01;   // tools/calls.py: throw_mark(0xE01)
constexpr std::size_t kWeapons=0x1950,kWeaponCount=0x1960;          // the soldier's weapons (pointers) and their count
constexpr std::size_t kHeld=0x13A,kInHand=0x13E;                    // this frame's trigger, held; the weapon in hand
constexpr std::size_t kAim=0x170;                                   // the owner matrix's forward row (+0x150 the matrix)
constexpr std::size_t kHitSize=0x8C4,kSpeed=0x894,kAlive=0x898,kDamage=0x89C;
constexpr std::uint64_t kMostWeapons=16;
constexpr float kHoldMin=4.0f;        // m: nothing is held nearer the muzzle than this
constexpr float kCarry=6.0f;          // m: past its own size, where a thing is drawn in to
constexpr float kDrawIn=20.0f;        // m/s the hold distance closes at
constexpr float kLostAfter=25.0f;     // m from its hold point...
constexpr ULONGLONG kLostMs=2000;     // ...this long (stuck, or too heavy to follow): let go
constexpr float kLeastReach=10.0f,kMostReach=1000.0f;
constexpr float kMostCarrySpeed=30.0f;   // m/s: the player moving faster than this in a frame was moved (a ride, a warp)

struct Gun {
    ObjRef human;
    gravity::Target held;
    float holdDist=0.0f;
    float rayA[3]{},rayB[3]{},hit[3]{};   // the grab's ray and where it met the thing (gravity_net.h Hold)
    float lastPos[3]{};
    ULONGLONG lastMs=0,farSince=0;
    bool havePos=false;
    bool latched=false;   // the trigger is still the one that threw / let go: no new grab until it is released
    int grabs=0;          // logged this mission
};
Gun gun;

bool Ours(const unsigned char* w) noexcept {
    if(!w || !Readable(w,kHitSize+4) || At<const unsigned char*>(w,0)!=image+kLaserMarkerVtable)return false;
    return At<std::uint32_t>(w,kHitSize)==kGravityMarkBits;
}

// The soldier's gravity gun (the first one in its weapon list), nullptr: none.
const unsigned char* GravityGunOf(const unsigned char* human) noexcept {
    if(!Readable(human,kWeaponCount+8))return nullptr;
    const auto list=At<const unsigned char* const*>(human,kWeapons);
    const auto n=At<std::uint64_t>(human,kWeaponCount);
    if(!list || n==0 || n>kMostWeapons || !Readable(list,static_cast<std::size_t>(n)*8))return nullptr;
    for(std::uint64_t i=0;i<n;++i)if(Ours(list[i]))return list[i];
    return nullptr;
}

float Len(const float* v) noexcept { return std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]); }

// Every way a hold ends but a throw (let go, put away, the target gone or contested, the player gone, the mission's
// physics gone, a fault): what was held falls, still followed for its impacts with its thrower (`human`; Track takes
// nothing too heavy to have been lifted), or, with no thrower left, is followed no more (never left in a held slot); the
// peers are told it was let go (gravity_net.h). A throw ends its hold in gravity::Throw (followed, not held) and
// gravity::net::Thrown.
void Release(gravity::Target& held,const unsigned char* human) noexcept {
    if(held) {
        if(human)gravity::Track(held,human,false);
        else gravity::Untrack(held);
        gravity::net::LetGo(held,human);
    }
    held=gravity::Target{};
}
void Release(const unsigned char* human) noexcept { Release(gun.held,human);gun.farSince=0; }

void Frame(unsigned char* human) noexcept {
    const ULONGLONG ms=GameMs();
    if(!gun.human.Is(human)){Release(nullptr);gun=Gun{};gun.human=ObjRef::Of(human);}   // another player object (a respawn)
    // The player's own motion (the hold keeps pace with it; a throw carries it).
    const float* pos=reinterpret_cast<const float*>(human+kPosition);
    float playerVel[3]{};
    const float dt=gun.lastMs ? static_cast<float>(ms-gun.lastMs)*0.001f : 0.0f;
    if(gun.havePos && dt>0.0f && dt<0.25f)for(int k=0;k<3;++k)playerVel[k]=(pos[k]-gun.lastPos[k])/dt;
    if(Len(playerVel)>kMostCarrySpeed)playerVel[0]=playerVel[1]=playerVel[2]=0.0f;
    std::memcpy(gun.lastPos,pos,12);gun.havePos=true;gun.lastMs=ms;

    const unsigned char* w=GravityGunOf(human);
    const bool inHand=w && w[kInHand];
    if(!inHand || !Cfg().gravityGun){Release(human);gun.latched=false;return;}
    const bool held=w[kHeld]!=0;
    float from[3],aim[3];
    if(!edf::MeanMuzzle(w,4,from,aim))std::memcpy(from,pos,12);
    std::memcpy(aim,w+kAim,12);
    const float al=Len(aim);
    if(!(al>0.5f))return;
    for(float& c:aim)c/=al;
    const float scale=std::isfinite(At<float>(w,kDamage)) && At<float>(w,kDamage)>0.0f ? At<float>(w,kDamage) : 1.0f;

    if(!held) {
        if(gun.held) {   // let go: thrown along the aim
            float vel[3];
            // A dying one (settling) is thrown too: gravity::Launch gives its corpse the velocity once it is on.
            const bool thrown=gravity::Update(gun.held)!=gravity::Liveness::gone &&
                              gravity::Throw(gun.held,aim,Cfg().gravityThrowSpeed,playerVel,human,scale,vel);
            if(thrown){gravity::net::Thrown(gun.held,human,vel);gun.held=gravity::Target{};}
            else Release(human);
        }
        gun.latched=false;
        return;
    }
    if(!gun.held) {
        if(gun.latched)return;
        const float reach=::gravmath::Clamp(At<float>(w,kSpeed)*static_cast<float>(At<std::int32_t>(w,kAlive)),kLeastReach,kMostReach);
        const float to[3]={from[0]+aim[0]*reach,from[1]+aim[1]*reach,from[2]+aim[2]*reach};
        gravity::Target t;float hit[3];
        if(!gravity::Pick(from,to,human,&t,hit))return;
        // A player (any machine's) or a vehicle a player rides is not taken unless the ini says so (GravityGrabPlayers).
        if(!Cfg().gravityGrabPlayers && gravity::IsPlayerOrRidden(t))return;
        float at[3];
        if(!gravity::Position(t,at))std::memcpy(at,hit,12);
        const float d[3]={at[0]-from[0],at[1]-from[1],at[2]-from[2]};
        gun.held=t;gun.holdDist=Len(d)>kHoldMin ? Len(d) : kHoldMin;gun.farSince=0;
        std::memcpy(gun.rayA,from,12);std::memcpy(gun.rayB,to,12);std::memcpy(gun.hit,hit,12);
        if(gun.grabs++<64)Log("GRAVITYGUN grab %p (kind %d, family %d): %.2f t, %.1f m out",t.obj.obj,static_cast<int>(t.kind),
                              static_cast<int>(t.family),t.kg*0.001f,gun.holdDist);
    }
    const gravity::Liveness live=gravity::Update(gun.held);
    if(live==gravity::Liveness::gone){Release(human);gun.latched=true;return;}
    // Drawn in to its carry distance (no faster than it can be carried), then held there on the aim's line.
    const float carry=kCarry+gun.held.radius*1.5f;
    const float cap=::gravmath::HoldCap(gun.held.kg);
    const float drawIn=cap*0.5f<kDrawIn ? cap*0.5f : kDrawIn;
    if(gun.holdDist>carry)gun.holdDist=std::fmax(carry,gun.holdDist-drawIn*dt);
    const float goal[3]={from[0]+aim[0]*gun.holdDist,from[1]+aim[1]*gun.holdDist,from[2]+aim[2]*gun.holdDist};
    // The peers told first: a target another player holds already (or outranks this one on) is not this machine's to hold.
    if(!gravity::net::Hold(gun.held,human,gun.hit,gun.rayA,gun.rayB,goal,playerVel)) {
        if(gun.grabs<64)Log("GRAVITYGUN %p is another player's grab: let go",gun.held.obj.obj);
        Release(human);gun.latched=true;return;
    }
    // A dying one between its body and its ragdoll (settling): held on, moved once its corpse is on.
    if(live==gravity::Liveness::settling)return;
    if(!gravity::HoldAt(gun.held,goal,playerVel)){Release(human);gun.latched=true;return;}
    gravity::Track(gun.held,human,true,scale);   // swung through enemies it hits them
    float at[3];
    if(gravity::Position(gun.held,at)) {
        const float d[3]={at[0]-goal[0],at[1]-goal[1],at[2]-goal[2]};
        if(Len(d)>kLostAfter) {
            if(!gun.farSince)gun.farSince=ms;
            else if(ms-gun.farSince>(cap<20.0f ? kLostMs*3 : kLostMs)){Release(human);gun.latched=true;}   // a heavy one is given longer
        } else gun.farSince=0;
    }
}
}  // namespace

// GravityAutoTest (a test of the gravity operations without the gun's trigger: the background test drive cannot aim): every
// kAutoGapMs the nearest enemy within kAutoReach is grabbed, carried to kAutoAhead m in front of the player and kAutoHigh m up
// (held there kAutoHoldMs at least, kAutoMostMs at most), then thrown ahead and down into the ground. Logged as the gun's grabs are.
// One that ends its carry still kAutoStuck m off (held back by a wall, a building) is passed over after (kMostSkipped kept).
constexpr ULONGLONG kAutoGapMs=5000,kAutoHoldMs=1500,kAutoMostMs=6000;
constexpr float kAutoReach=400.0f,kAutoAhead=10.0f,kAutoHigh=6.0f,kAutoStuck=20.0f;
constexpr int kMostSkipped=8;
struct Auto { gravity::Target held; float at[3]{}; ULONGLONG since=0,next=0; int grabs=0; const void* skipped[kMostSkipped]{}; int skips=0; };
Auto autoTest;
struct Nearest { const float* at; const void* object; float d2; };
void SeeNearest(void* ctx,const void* object,const float* lock) noexcept {
    auto& n=*static_cast<Nearest*>(ctx);
    if(!lock)return;
    for(const void* o:autoTest.skipped)if(o==object)return;
    const float d[3]={lock[0]-n.at[0],lock[1]-n.at[1],lock[2]-n.at[2]};
    const float d2=d[0]*d[0]+d[1]*d[1]+d[2]*d[2];
    if(d2>kAutoReach*kAutoReach || (n.object && d2>=n.d2))return;
    n.object=object;n.d2=d2;
}
void AutoTest(unsigned char* human) noexcept {
    const ULONGLONG ms=GameMs();
    const float* pos=reinterpret_cast<const float*>(human+kPosition);
    const float* fwd=reinterpret_cast<const float*>(human+kMatrix+32);
    float f[3]={fwd[0],0.0f,fwd[2]};
    const float fl=Len(f);
    if(!(fl>0.1f))return;
    for(float& c:f)c/=fl;
    if(!autoTest.held) {
        if(ms<autoTest.next)return;
        autoTest.next=ms+kAutoGapMs;
        Nearest n{pos,nullptr,0.0f};
        VisitEnemiesOf(At<std::int32_t>(human,kTeam),&SeeNearest,&n);
        gravity::Target t;
        if(!n.object || !gravity::TargetOf(n.object,0xFFFFFFu,&t) || !(::gravmath::HoldCap(t.kg)>0.0f) ||
           (!Cfg().gravityGrabPlayers && gravity::IsPlayerOrRidden(t))) {
            if(n.object && autoTest.grabs<64)Log("GRAVITYTEST nearest enemy %p (%.0f m) cannot be lifted",n.object,std::sqrt(n.d2));
            return;
        }
        autoTest.held=t;autoTest.since=ms;
        if(!gravity::Position(t,autoTest.at))std::memcpy(autoTest.at,pos,12);
        if(autoTest.grabs++<64)Log("GRAVITYTEST grab %p (kind %d, family %d): %.2f t, %.0f m away, HP %.0f / %.0f",t.obj.obj,
                                   static_cast<int>(t.kind),static_cast<int>(t.family),t.kg*0.001f,std::sqrt(n.d2),
                                   At<float>(n.object,0x2F8),At<float>(n.object,0x2F4));
    }
    const gravity::Liveness live=gravity::Update(autoTest.held);
    if(live==gravity::Liveness::gone){Release(autoTest.held,human);return;}
    if(live==gravity::Liveness::settling)return;
    const float goal[3]={pos[0]+f[0]*kAutoAhead,pos[1]+kAutoHigh,pos[2]+f[2]*kAutoAhead};
    float at[3];
    const bool known=gravity::Position(autoTest.held,at);
    const float gap[3]={known ? at[0]-goal[0] : 0.0f,known ? at[1]-goal[1] : 0.0f,known ? at[2]-goal[2] : 0.0f};
    const bool there=known && Len(gap)<3.0f && ms-autoTest.since>=kAutoHoldMs;
    if(!there && ms-autoTest.since<kAutoMostMs) {   // carried in, then held kAutoHoldMs at least
        // One grab told at a time (the gun's first); one another player holds is let go.
        if(!gun.held && !gravity::net::Hold(autoTest.held,human,autoTest.at,pos,autoTest.at,goal,nullptr)){Release(autoTest.held,human);return;}
        gravity::HoldAt(autoTest.held,goal,nullptr);
        gravity::Track(autoTest.held,human,true);
        return;
    }
    if(known)Log("GRAVITYTEST held at (%.0f,%.0f,%.0f), goal (%.0f,%.0f,%.0f)",at[0],at[1],at[2],goal[0],goal[1],goal[2]);
    if(known && Len(gap)>kAutoStuck) {   // held back: the next grab another
        autoTest.skipped[autoTest.skips++%kMostSkipped]=autoTest.held.obj.obj;
        Log("GRAVITYTEST %p held back %.0f m off its goal: passed over from now",autoTest.held.obj.obj,Len(gap));
    }
    const float down[3]={f[0],-0.6f,f[2]};
    float vel[3];
    const bool thrown=gravity::Throw(autoTest.held,down,Cfg().gravityThrowSpeed,nullptr,human,1.0f,vel);
    Log("GRAVITYTEST throw %p: %s",autoTest.held.obj.obj,thrown ? "thrown" : "not thrown");
    if(thrown && !gun.held){gravity::net::Thrown(autoTest.held,human,vel);autoTest.held=gravity::Target{};}
    else if(thrown){gravity::net::LetGo(autoTest.held,human);autoTest.held=gravity::Target{};}
    else Release(autoTest.held,human);
}

// From the shared tick (crew.cpp GravityTick), after the gravity operations' own frame.
// Every path that drops what is held goes through Release (never a bare reset: a held slot of gravity.cpp's followed
// table left behind, and the peers left carrying it till their silence timer).
void GravityGunTick() noexcept {
    if(!gravity::Ready()){Release(nullptr);Release(autoTest.held,nullptr);return;}
    unsigned char* human=PlayerHuman();
    if(!human){Release(nullptr);Release(autoTest.held,nullptr);gun=Gun{};return;}
    __try { Frame(human); } __except(EXCEPTION_EXECUTE_HANDLER){Release(nullptr);}
    if(Cfg().gravityAutoTest) {
        __try { AutoTest(human); } __except(EXCEPTION_EXECUTE_HANDLER){Release(autoTest.held,nullptr);Log("GRAVITYTEST faulted");}
    } else if(autoTest.held)Release(autoTest.held,human);
}

// A new mission: gravity::Reset has cleared the followed table already; the peers' copies go with the room's reset.
void ResetGravityGun() noexcept { gun=Gun{};autoTest=Auto{}; }
}  // namespace crew
