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
// Offline only (gravity::Ready): online the kit is its stock self. The player's own Laser Guide Kit is never touched.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "gravity.h"
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

void LetGo(const unsigned char* human) noexcept {
    // Falls on its own, still followed for its impacts (Track takes nothing too heavy to have been lifted).
    if(gun.held && human)gravity::Track(gun.held,human,false);
    gun.held=gravity::Target{};
    gun.farSince=0;
}

void Frame(unsigned char* human) noexcept {
    const ULONGLONG ms=GameMs();
    if(!gun.human.Is(human)){gun=Gun{};gun.human=ObjRef::Of(human);}
    // The player's own motion (the hold keeps pace with it; a throw carries it).
    const float* pos=reinterpret_cast<const float*>(human+kPosition);
    float playerVel[3]{};
    const float dt=gun.lastMs ? static_cast<float>(ms-gun.lastMs)*0.001f : 0.0f;
    if(gun.havePos && dt>0.0f && dt<0.25f)for(int k=0;k<3;++k)playerVel[k]=(pos[k]-gun.lastPos[k])/dt;
    if(Len(playerVel)>kMostCarrySpeed)playerVel[0]=playerVel[1]=playerVel[2]=0.0f;
    std::memcpy(gun.lastPos,pos,12);gun.havePos=true;gun.lastMs=ms;

    const unsigned char* w=GravityGunOf(human);
    const bool inHand=w && w[kInHand];
    if(!inHand || !Cfg().gravityGun){LetGo(human);gun.latched=false;return;}
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
            const bool thrown=gravity::Refresh(gun.held) &&
                              gravity::Throw(gun.held,aim,Cfg().gravityThrowSpeed,playerVel,human,scale);
            if(!thrown)LetGo(human);
            gun.held=gravity::Target{};
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
        float at[3];
        if(!gravity::Position(t,at))std::memcpy(at,hit,12);
        const float d[3]={at[0]-from[0],at[1]-from[1],at[2]-from[2]};
        gun.held=t;gun.holdDist=Len(d)>kHoldMin ? Len(d) : kHoldMin;gun.farSince=0;
        if(gun.grabs++<64)Log("GRAVITYGUN grab %p (kind %d, family %d): %.2f t, %.1f m out",t.obj.obj,static_cast<int>(t.kind),
                              static_cast<int>(t.family),t.kg*0.001f,gun.holdDist);
    }
    if(!gravity::Refresh(gun.held)){gun.held=gravity::Target{};gun.latched=true;return;}
    // Drawn in to its carry distance (no faster than it can be carried), then held there on the aim's line.
    const float carry=kCarry+gun.held.radius*1.5f;
    const float cap=::gravmath::HoldCap(gun.held.kg);
    const float drawIn=cap*0.5f<kDrawIn ? cap*0.5f : kDrawIn;
    if(gun.holdDist>carry)gun.holdDist=std::fmax(carry,gun.holdDist-drawIn*dt);
    const float goal[3]={from[0]+aim[0]*gun.holdDist,from[1]+aim[1]*gun.holdDist,from[2]+aim[2]*gun.holdDist};
    if(!gravity::HoldAt(gun.held,goal,playerVel)){LetGo(human);gun.latched=true;return;}
    gravity::Track(gun.held,human,true,scale);   // swung through enemies it hits them
    float at[3];
    if(gravity::Position(gun.held,at)) {
        const float d[3]={at[0]-goal[0],at[1]-goal[1],at[2]-goal[2]};
        if(Len(d)>kLostAfter) {
            if(!gun.farSince)gun.farSince=ms;
            else if(ms-gun.farSince>(cap<20.0f ? kLostMs*3 : kLostMs)){LetGo(human);gun.latched=true;}   // a heavy one is given longer
        } else gun.farSince=0;
    }
}
}  // namespace

// From the shared tick (crew.cpp GravityTick), after the gravity operations' own frame.
void GravityGunTick() noexcept {
    if(!gravity::Ready()){gun.held=gravity::Target{};return;}
    unsigned char* human=PlayerHuman();
    if(!human){gun=Gun{};return;}
    __try { Frame(human); } __except(EXCEPTION_EXECUTE_HANDLER){gun.held=gravity::Target{};}
}

void ResetGravityGun() noexcept { gun=Gun{}; }
}  // namespace crew
