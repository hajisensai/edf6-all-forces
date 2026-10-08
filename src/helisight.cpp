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
//    real muzzles are flown independently (the 506's left and right guns are never averaged). A seat whose gun makes no line (the 409's
//    turret gatling: custom_parameter []) gets its fastest gun instead (until 2026-10-05 it got no sight at all).
//  - The boresight: the first actual muzzle direction (common/edf/weapon.h MuzzleFrame, the frame fire builds a shot from:
//    a door gun's follows its turret, the 409's its turret), drawn as a direction (far).
//  - The pipper: the round's real arc, as the game spawns and steps it (autoturret/docs/re-notes.md "Rounds in
//    flight"): velocity = muzzle row 2 x AmmoSpeed + the shooter's velocity x AmmoOwnerMove / 60 (m/frame; 0x691FA0
//    reads weapon+0x190 and +0x24C, edf::kWeaponOwnerVel / kWeaponAmmoOwnerMove; the stock heli guns' AmmoOwnerMove is
//    0, read anyway), falling AmmoGravityFactor x the world gravity / 3600 each frame, for AmmoAlive frames. Where a
//    map ray first finds the ground along it (crew.h RoundImpact: terrain and buildings, not water or vehicles) is the
//    predicted ground cross; none within its life, the round's place at its end (sight::RoundAfter), drawn as dim open dashes. Its distance from
//    the muzzle goes next to it. No lead: the stock heli guns lock nothing.
//  - The missile (the 506's and 602's: LockonType 1, a MissileBullet01 homing on what the weapon's own lock list
//    holds): the lock as the jets' stores read it (lockon.h WeaponLock: the weapon's lock list +0xC60 and the lock in
//    progress +0xC70, the same in every weapon, docs/stores-re.md §7), on the target's lock point; with no lock, its
//    boresight and LockonRange (+0x6D0), the reach it locks within. It homes: no impact point to show.
//  - The stores (the jets' rocket pod, Hellfires and AIM-9X the installer gives the helis' requests, ini StockVehicleStores): the
//    secondary fires the one picked (payload.cpp), and that one is the other weapon here (PayloadPicked), a Hellfire's lock
//    as the missile's, the rocket pod's mark as the rockets'.
//  - The rockets (the 409's V_409HELI_MISSILE01: a MissileBullet01 with no lock, starting at 0.5 m/frame and speeding
//    up; heli.cpp Arms's rule: no lock and a round slower than kRocketBelow): their mark is where their path, flown as
//    the game flies them (vhud.h RoundLands: rounds.h Motor, the weapon's own Ammo_CustomParameter; the 409's coast
//    1.5 s, falling, before the motor lights), first meets the map within kRocketSight, its distance beside it. Until
//    2026-10-06 it was their straight line, the drop on the way left out (the user: "the rockets' point is off"). The
//    call variants' dropped weapons (V_506HELI_UNDER_NAPALM01, the 409's bomb) fall under the same rule and are flown
//    as the arcs they are.
// Computed on the game thread from the vehicle's input (crew.cpp InputHook, after AimLines), published with the HUD's
// frame (hud.cpp HudPublish, the snapshot's triple buffer) and drawn by it.
#include "crew.h"
#include "layout.h"
#include "memory.h"
#include "sight.h"
#include "lockon.h"
#include "stores.h"
#include "edf/weapon.h"
#include "weapon_mount.h"
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
constexpr float kRocketSight=3000.0f;   // m: the farthest the rockets' point is looked for (the near camera's far clip)

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
    if(!guns || n<=0 || !RoundOf(guns[0],&round))return false;
    const auto firstMuzzle=At<const unsigned char*>(guns[0],edf::kMuzzles);
    if(!RoundOf(guns[0],&round) || !At<std::uint64_t>(guns[0],edf::kMuzzleCount) ||
       !Readable(firstMuzzle,edf::kMuzzleStride) || !edf::MuzzleFrame(guns[0],firstMuzzle,pos,dir) ||
       !vec::Normalize(dir) || !edf::WorldGravity(image,g))return false;
    Inherited(guns[0],owner);
    float drop[3];
    for(int i=0;i<3;++i)drop[i]=g[i]*round.factor/3600.0f;
    std::memcpy(r.bore,dir,12);
    float took=0.0f;
    // Each real gun/muzzle has its own path. The old mean point could lie between two shots, on neither one.
    for(int k=0;k<n && r.paths<roundaim::kSightPaths;++k) {
        Round own{};if(!RoundOf(guns[k],&own))continue;
        float ownDrop[3];for(int c=0;c<3;++c)ownDrop[c]=g[c]*own.factor/3600.0f;
        const auto muzzles=At<const unsigned char*>(guns[k],edf::kMuzzles);const auto count=At<std::uint64_t>(guns[k],edf::kMuzzleCount);
        if(!count || count>kMostMuzzles || !Readable(muzzles,count*edf::kMuzzleStride))continue;
        for(std::uint64_t i=0;i<count && r.paths<roundaim::kSightPaths;++i) {
            float p[3],d[3],inherited[3];
            if(!edf::MuzzleFrame(guns[k],muzzles+i*edf::kMuzzleStride,p,d) || !vec::Normalize(d))continue;
            Inherited(guns[k],inherited);
            float velocity[3];for(int c=0;c<3;++c)velocity[c]=d[c]*own.speed+inherited[c];
            auto& path=r.path[r.paths++];
            path.hit=RoundImpact(p,velocity,ownDrop,own.alive,path.at,&took);
            if(!path.hit){took=static_cast<float>(own.alive);sight::RoundAfter(p,velocity,ownDrop,took,path.at);}
            path.range=vec::Dist(p,path.at);path.seconds=took/60.0f;
            if(r.paths==1){std::memcpy(r.bore,d,12);std::memcpy(r.pipper,path.at,12);r.hit=path.hit;r.range=path.range;}
        }
    }
    if(!r.paths)return false;
    // Its range ladder: the same round (the inherited velocity already in m/frame: as a shooter at 60x it with all of it kept).
    const roundaim::Round ladder{round.speed,{drop[0],drop[1],drop[2]},1.0f,round.alive};
    const float shooter[3]={owner[0]*60.0f,owner[1]*60.0f,owner[2]*60.0f};
    r.ladder=gunsight::Of(ladder,pos,dir,shooter);
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
        r.lock=WeaponLock(w,r.armAt,&r.lockProgress);
        if(r.lock)r.armRange=vec::Dist(pos,r.armAt);
        return true;
    }
    r.arm=HeliArm::rockets;
    RoundModel m{};
    float sec=0.0f;
    if(!ReadRound(w,&m))return true;   // unreadable: only its boresight (dim)
    r.armLabel=m.label;
    r.armHit=RoundLands(w,m,pos,dir,kRocketSight,r.armAt,&sec);
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
    // The stores switched on the pilot's secondary (payload.cpp, ini StockVehicleStores): the mark is the one it fires now.
    if(unsigned char* const picked=PayloadPicked(v);picked && seat==SeatAt(v,0)){arm=picked;homing=At<std::int32_t>(picked,kWeaponLockon)==kHoming;}
    if(!n && gun){guns[0]=gun;n=1;}   // no stock line to replace (the 409's turret gun): its fastest gun
    HeliSightReadout r{};
    const bool gunOk=n && SolveGun(guns,n,r);
    if(gunOk) {
        const auto freedom=weaponmount::OfWeapon(v,seat,guns[0]);
        r.physicalOnly=!(freedom.known && freedom.yaw && freedom.pitch);
    }
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
