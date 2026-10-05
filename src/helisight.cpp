// A stock helicopter's gun sight (the user, 2026-10-05: "the heli's sight: ours too"; ini PlayerHeliGunSight). While
// the player flies or mans a stock helicopter (heli.cpp IsHelicopter with no plugin body: the N9 Eros 506, the Nereid
// 409, the Brute 410's door guns, the 602), its gun's red aim line is hidden (crew.cpp AimLines) and the HUD draws this
// instead (hud.cpp HeliGunSight, docs/hud-re.md §6):
//  - The gun: the first weapons of the player's seat with a stock aim line, that line hidden now (crew.cpp
//    HiddenAimGuns; the line is what a Weapon_VehicleShoot gun has, docs/aim-line-re.md), the second only when its
//    rounds are the first's (the 506's left and right guns: their mean). A seat whose gun makes no line (the 409's
//    gatling: custom_parameter []) has nothing to replace and no sight.
//  - The boresight: the way its muzzles point (common/edf/weapon.h MeanMuzzle, the frame fire builds a shot from:
//    a door gun's follows its turret), drawn as a direction (far).
//  - The pipper: the round's real arc, as the game spawns and steps it (autoturret/docs/re-notes.md "Rounds in
//    flight"): velocity = muzzle row 2 x AmmoSpeed + the shooter's velocity x AmmoOwnerMove / 60 (m/frame; 0x691FA0
//    reads weapon+0x190 and +0x24C, edf::kWeaponOwnerVel / kWeaponAmmoOwnerMove; the stock heli guns' AmmoOwnerMove is
//    0, read anyway), falling AmmoGravityFactor x the world gravity / 3600 each frame, for AmmoAlive frames. Where a
//    map ray first finds the ground along it (crew.h RoundImpact: terrain and buildings, not water or vehicles) is the
//    pipper; none within its life, the round's place at its end (sight::RoundAfter), drawn dim. Its distance from
//    the muzzle goes next to it. No lead: the stock heli guns lock nothing.
// Computed on the game thread from the vehicle's input (crew.cpp InputHook, after AimLines), published with the HUD's
// frame (hud.cpp HudPublish, the snapshot's triple buffer) and drawn by it.
#include "crew.h"
#include "layout.h"
#include "memory.h"
#include "sight.h"
#include "edf/weapon.h"
#include <cmath>

namespace crew {
namespace {
constexpr int kMostGuns=2;
constexpr std::uint64_t kMostMuzzles=64;
constexpr ULONGLONG kFreshMs=200;       // a readout not refreshed this long (game ms) is gone: the player got out

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

bool Solve(const unsigned char* const* guns,int n,HeliSightReadout& r) noexcept {
    Round round{};
    float pos[3],dir[3],g[3],owner[3];
    if(!RoundOf(guns[0],&round) || !Muzzle(guns,n,round,pos,dir) || !edf::WorldGravity(image,g))return false;
    Inherited(guns[0],owner);
    float vel[3],drop[3];
    for(int i=0;i<3;++i){vel[i]=dir[i]*round.speed+owner[i];drop[i]=g[i]*round.factor/3600.0f;}
    r=HeliSightReadout{};
    std::memcpy(r.bore,dir,12);
    float took=0.0f;
    r.hit=RoundImpact(pos,vel,drop,round.alive,r.pipper,&took);
    if(!r.hit)sight::RoundAfter(pos,vel,drop,static_cast<float>(round.alive),r.pipper);
    r.range=vec::Dist(pos,r.pipper);
    return true;
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
    Log("HELISIGHT v=%p guns=%d speed=%.2fm/f alive=%d gravity=x%.2f ownerMove=%.2f hit=%d range=%.0fm bore=(%.2f,%.2f,%.2f)",v,guns,
        At<float>(gun,edf::kWeaponAmmoSpeed),At<std::int32_t>(gun,edf::kWeaponAmmoAlive),At<float>(gun,edf::kWeaponAmmoGravity),
        At<float>(gun,edf::kWeaponAmmoOwnerMove),r.hit,r.range,r.bore[0],r.bore[1],r.bore[2]);
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
    const int n=HiddenAimGuns(seat,guns,kMostGuns);
    HeliSightReadout r{};
    if(!n || !Solve(guns,n,r))return;
    latest=r;latestMs=GameMs();
    DebugLog(v,guns[0],n,r);
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
