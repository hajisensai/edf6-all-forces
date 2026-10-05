// A stock helicopter's weapons' sight (the user, 2026-10-05: "the heli's sight: ours too", and "the heli's attack point
// seems missing": no impact point for the 409's gun or any heli's missiles / rockets; ini PlayerHeliGunSight). While
// the player flies or mans a stock helicopter (heli.cpp IsHelicopter with no plugin body: the N9 Eros 506, the Nereid
// 409, the Brute 410's door guns, the 602), its gun's red aim line is hidden (crew.cpp AimLines) and the HUD draws this
// instead (hud.cpp HeliGunSight, docs/hud-re.md §6). A stock heli has no weapon to select: its input fires the guns on
// the primary trigger and the missile holder on the secondary button (506 slot 55, docs/heli-input-re.md §2b; the 409
// the same), so the sight shows both at once, the gun's and the other weapon's (Weapons sorts the seat's weapons by
// heli.cpp Arms's rule for the NPC: homing, the slow rockets, the guns):
//  - The gun: the first weapons of the player's seat with a stock aim line, that line hidden now (crew.cpp
//    HiddenAimGuns; the line is what a Weapon_VehicleShoot gun has, docs/aim-line-re.md), the second only when its
//    rounds are the first's (the 506's left and right guns: their mean). A seat whose gun makes no line (the 409's
//    turret gatling: custom_parameter []) gets its fastest gun instead (until 2026-10-05 it got no sight at all).
//  - The boresight: the way its muzzles point (common/edf/weapon.h MeanMuzzle, the frame fire builds a shot from:
//    a door gun's follows its turret, the 409's its turret), drawn as a direction (far).
//  - The pipper: the round's real arc, as the game spawns and steps it (autoturret/docs/re-notes.md "Rounds in
//    flight"): velocity = muzzle row 2 x AmmoSpeed + the shooter's velocity x AmmoOwnerMove / 60 (m/frame; 0x691FA0
//    reads weapon+0x190 and +0x24C, edf::kWeaponOwnerVel / kWeaponAmmoOwnerMove; the stock heli guns' AmmoOwnerMove is
//    0, read anyway), falling AmmoGravityFactor x the world gravity / 3600 each frame, for AmmoAlive frames. Where a
//    map ray first finds the ground along it (crew.h RoundImpact: terrain and buildings, not water or vehicles) is the
//    pipper; none within its life, the round's place at its end (sight::RoundAfter), drawn dim. Its distance from
//    the muzzle goes next to it. No lead: the stock heli guns lock nothing.
//  - The missile (the 506's and 602's: LockonType 1, a MissileBullet01 homing on what the weapon's own lock list
//    holds): the lock as the jets' stores read it (stores.h StoreLock: the weapon's lock list +0xC60 and the lock in
//    progress +0xC70, the same in every weapon, docs/stores-re.md §7), on the target's lock point; with no lock, its
//    boresight and LockonRange (+0x6D0), the reach it locks within. It homes: no impact point to show.
//  - The rockets (the 409's V_409HELI_MISSILE01: a MissileBullet01 with no lock, starting at 0.5 m/frame and speeding
//    up; heli.cpp Arms's rule: no lock and a round slower than kRocketBelow): they fly straight along their muzzle
//    (heli.cpp kRocketStart: its NPC leads them so), so their mark is where that line first meets the map within
//    kRocketSight, its distance beside it. Not an arc: the weapon's copy of the round's acceleration
//    (Ammo_CustomParameter[4]) has no known offset, so a drop on the way is not shown. The call variants' dropped
//    weapons (V_506HELI_UNDER_NAPALM01, the 409's bomb) fall under the same rule and get the same straight mark.
// Computed on the game thread from the vehicle's input (crew.cpp InputHook, after AimLines), published with the HUD's
// frame (hud.cpp HudPublish, the snapshot's triple buffer) and drawn by it.
#include "crew.h"
#include "layout.h"
#include "memory.h"
#include "sight.h"
#include "stores.h"
#include "edf/weapon.h"
#include <cmath>

namespace crew {
namespace {
constexpr int kMostGuns=2;
constexpr std::uint64_t kMostMuzzles=64;
constexpr ULONGLONG kFreshMs=200;       // a readout not refreshed this long (game ms) is gone: the player got out
constexpr std::size_t kWeaponLockRange=0x6D0;   // LockonRange (autoturret/docs/re-notes.md "Weapon")
// A round slower than this (m/frame: 120 m/s, heli.cpp Arms) with no lock is a rocket; a round that reaches less than
// kNoReach m in its life is no weapon to aim (the 506's fuel tank v_fuel01: 1 m/frame for 1 frame).
constexpr float kRocketBelow=2.0f,kNoReach=10.0f;
constexpr float kRocketSight=1500.0f,kRocketStep=10.0f;   // m along the rockets' line; m a step of it (RoundImpact)

HeliSightReadout latest{};
ULONGLONG latestMs=0;

// A gun's round: AmmoSpeed (m/frame), AmmoGravityFactor, AmmoAlive (frames); false when unreadable or none.
struct Round { float speed,factor; std::int32_t alive; };
bool RoundOf(const unsigned char* gun,Round* r) noexcept {
    if(!Readable(gun,edf::kWeaponAmmoGravity+4) || !Readable(gun+edf::kWeaponMatrix,0x40))return false;
    r->speed=At<float>(gun,edf::kWeaponAmmoSpeed);r->factor=At<float>(gun,edf::kWeaponAmmoGravity);
    r->alive=At<std::int32_t>(gun,edf::kWeaponAmmoAlive);
    return std::isfinite(r->speed) && r->speed>0.0f && std::isfinite(r->factor) && r->alive>0;
}

// The guns' mean muzzle (position and unit direction): the first gun's, and the second's with the same round.
bool Muzzle(const unsigned char* const* guns,int n,const Round& first,float* pos,float* dir) noexcept {
    float p[3]={0.0f,0.0f,0.0f},d[3]={0.0f,0.0f,0.0f};
    int used=0;
    for(int k=0;k<n;++k) {
        Round r{};
        if(!RoundOf(guns[k],&r) || r.speed!=first.speed || r.factor!=first.factor || r.alive!=first.alive)continue;
        float mp[3],md[3];
        if(!edf::MeanMuzzle(guns[k],kMostMuzzles,mp,md))continue;
        for(int i=0;i<3;++i){p[i]+=mp[i];d[i]+=md[i];}
        ++used;
    }
    if(!used || !vec::Normalize(d))return false;
    for(int i=0;i<3;++i)pos[i]=p[i]/static_cast<float>(used);
    std::memcpy(dir,d,12);
    return true;
}

// The shooter's velocity a round takes on (m/frame): 0x691FA0's weapon+0x190 x AmmoOwnerMove / 60.
void Inherited(const unsigned char* gun,float* out) noexcept {
    const float share=At<float>(gun,edf::kWeaponAmmoOwnerMove);
    const float* v=reinterpret_cast<const float*>(gun+edf::kWeaponOwnerVel);
    for(int i=0;i<3;++i)out[i]=std::isfinite(share) && std::isfinite(v[i]) ? v[i]*share/60.0f : 0.0f;
}

// The gun's part of the readout (see the top).
bool SolveGun(const unsigned char* const* guns,int n,HeliSightReadout& r) noexcept {
    Round round{};
    float pos[3],dir[3],g[3],owner[3];
    if(!RoundOf(guns[0],&round) || !Muzzle(guns,n,round,pos,dir) || !edf::WorldGravity(image,g))return false;
    Inherited(guns[0],owner);
    float vel[3],drop[3];
    for(int i=0;i<3;++i){vel[i]=dir[i]*round.speed+owner[i];drop[i]=g[i]*round.factor/3600.0f;}
    std::memcpy(r.bore,dir,12);
    float took=0.0f;
    r.hit=RoundImpact(pos,vel,drop,round.alive,r.pipper,&took);
    if(!r.hit)sight::RoundAfter(pos,vel,drop,static_cast<float>(round.alive),r.pipper);
    r.range=vec::Dist(pos,r.pipper);
    r.gun=true;
    return true;
}

// The other weapon's part (see the top): `w` the missile (`homing`) or the rockets.
bool SolveArm(unsigned char* w,bool homing,HeliSightReadout& r) noexcept {
    float pos[3],dir[3];
    if(!edf::MeanMuzzle(w,kMostMuzzles,pos,dir) || !vec::Normalize(dir))return false;
    std::memcpy(r.armBore,dir,12);
    if(homing) {
        r.arm=HeliArm::missile;
        const float range=At<float>(w,kWeaponLockRange);
        r.lockRange=std::isfinite(range) && range>0.0f ? range : 0.0f;
        r.lock=StoreLock(Store{w,nullptr,0,0,r.lockRange},r.armAt,&r.lockProgress);
        if(r.lock)r.armRange=vec::Dist(pos,r.armAt);
        return true;
    }
    r.arm=HeliArm::rockets;
    const float step[3]={dir[0]*kRocketStep,dir[1]*kRocketStep,dir[2]*kRocketStep},none[3]={0.0f,0.0f,0.0f};
    float took=0.0f;
    r.armHit=RoundImpact(pos,step,none,static_cast<int>(kRocketSight/kRocketStep),r.armAt,&took);
    if(r.armHit)r.armRange=vec::Dist(pos,r.armAt);
    return true;
}

// The seat's weapons sorted (heli.cpp Arms's rule): `gun` the fastest straight one, `arm` the first homing one, else
// the first rocket (`homing` says which). Rounds that reach nowhere (kNoReach) are passed by.
void Weapons(const unsigned char* seat,unsigned char** gun,unsigned char** arm,bool* homing) noexcept {
    *gun=*arm=nullptr;*homing=false;
    const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
    const auto n=At<std::uint64_t>(seat,kSeatWeaponCount);
    if(n>8 || !Readable(holders,n*8))return;
    float fastest=0.0f;
    unsigned char* rocket=nullptr;
    for(std::uint64_t i=0;i<n;++i) {
        if(!Readable(holders[i],kHolderWeapon+8))continue;
        unsigned char* const w=At<unsigned char*>(holders[i],kHolderWeapon);
        Round round{};
        if(!Readable(w,kWeaponLockRange+4) || !RoundOf(w,&round) || round.speed*static_cast<float>(round.alive)<kNoReach)continue;
        if(At<std::int32_t>(w,kWeaponLockon)==kHoming){if(!*homing){*arm=w;*homing=true;}continue;}
        if(round.speed<kRocketBelow){if(!rocket)rocket=w;continue;}
        if(round.speed>fastest){fastest=round.speed;*gun=w;}
    }
    if(!*homing)*arm=rocket;
}

// The seat the player sits in, or nullptr.
const unsigned char* PlayerSeat(unsigned char* v) noexcept {
    const unsigned count=SeatCount(v);
    for(unsigned i=0;i<count;++i)if(SeatRider(SeatAt(v,i))==Rider::player)return SeatAt(v,i);
    return nullptr;
}

void DebugLog(const unsigned char* v,const unsigned char* gun,int guns,const HeliSightReadout& r) noexcept {
    static ULONGLONG at=0;
    const ULONGLONG now=GetTickCount64();
    if(!Cfg().debug || now-at<2000)return;
    at=now;
    if(gun)Log("HELISIGHT v=%p guns=%d speed=%.2fm/f alive=%d gravity=x%.2f ownerMove=%.2f hit=%d range=%.0fm bore=(%.2f,%.2f,%.2f)",v,guns,
               At<float>(gun,edf::kWeaponAmmoSpeed),At<std::int32_t>(gun,edf::kWeaponAmmoAlive),At<float>(gun,edf::kWeaponAmmoGravity),
               At<float>(gun,edf::kWeaponAmmoOwnerMove),r.hit,r.range,r.bore[0],r.bore[1],r.bore[2]);
    if(r.arm!=HeliArm::none)
        Log("HELISIGHT v=%p %s: lock=%d (%.2f) lockRange=%.0fm mark=%d %.0fm",v,r.arm==HeliArm::missile ? "missile" : "rockets",r.lock,
            r.lockProgress,r.lockRange,r.armHit,r.armRange);
}
}  // namespace

bool PlayerHeliOwnSight(const void* vehicle) noexcept {
    if(!Cfg().enabled || !Cfg().playerHeliGunSight)return false;
    __try { return IsHelicopter(vehicle) && BodyOf(vehicle)==PluginBody::none; }
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

void HeliSightFrame(unsigned char* v) noexcept {
    if(v[kDead] || !PlayerHeliOwnSight(v))return;
    const unsigned char* const seat=PlayerSeat(v);
    if(!seat)return;
    const unsigned char* guns[kMostGuns]{};
    int n=HiddenAimGuns(seat,guns,kMostGuns);
    unsigned char *gun=nullptr,*arm=nullptr;
    bool homing=false;
    Weapons(seat,&gun,&arm,&homing);
    if(!n && gun){guns[0]=gun;n=1;}   // no stock line to replace (the 409's turret gun): its fastest gun
    HeliSightReadout r{};
    const bool gunOk=n && SolveGun(guns,n,r);
    const bool armOk=arm && SolveArm(arm,homing,r);
    if(!gunOk && !armOk)return;
    latest=r;latestMs=GameMs();
    DebugLog(v,gunOk ? guns[0] : nullptr,n,r);
}

bool PlayerHeliSight(HeliSightReadout* out) noexcept {
    if(!latestMs || GameMs()-latestMs>kFreshMs)return false;
    *out=latest;
    return true;
}

void ResetHeliSight() noexcept {
    latest=HeliSightReadout{};latestMs=0;
}
}  // namespace crew
