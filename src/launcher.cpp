// The Katyusha's impact point (the user, 2026-10-05: "show where the rockets land, as the plane's bombs do"): while the
// player rides a vehicle whose seat 0 holds a launcher marked kMarkLofted (tools/make_katyusha.py; common/edf/weapon.h),
// where a rocket fired now would come down, worked out the way the game flies it, and the ring its ripple spreads over.
//  - From the launcher's own muzzles (their mean: common/edf/weapon.h MeanMuzzle, as fire 0x6969A0 builds them), along
//    the rocket rail (the muzzle's row 2), at AmmoSpeed metres a frame (the live weapon's: what the request's tier and
//    the mission made of it), under the world gravity x AmmoGravityFactor (edf::WorldGravity, as each round's spawn
//    0x231E7B reads it).
//  - Stepped as the bullet core steps a round (0x233DC4: v += g/60, then p += v/60), a frame at a time, and a map ray
//    (heli.cpp MapRay: terrain and buildings, not water) along each kSegment frames of it: the first ray that hits is
//    the impact. None within the rocket's life (AmmoAlive): it would expire in the air, no mark.
//  - The ring: the rail turned by FireAccuracy (the cone's half angle, fire 0x691B02 -> 0x4E820) all the way round,
//    each of those arcs down to the impact's height (closed form of the same per-frame step: no rays).
// Computed on the game thread from the vehicle's input (crew.cpp InputHook), published with the HUD's frame
// (hud.cpp HudPublish) and drawn by it. Works whether EDF6AutoTurret is installed or not: it reads only the weapon.
#include "crew.h"
#include "layout.h"
#include "memory.h"
#include "edf/weapon.h"
#include <cmath>

namespace crew {
namespace {
constexpr int kSegment=15;              // frames a map ray covers (0.25 s: the arc's sag over it is ~0.1 m)
constexpr std::uint64_t kMostMuzzles=64;
constexpr ULONGLONG kFreshMs=200;       // a readout not refreshed this long (game ms) is gone: the player got out
constexpr float kPi=3.14159265f;

LauncherReadout latest{};
ULONGLONG latestMs=0;

// The seat's launcher marked kMarkLofted, or nullptr.
const unsigned char* LoftedLauncher(const unsigned char* seat) noexcept {
    const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
    const auto count=At<std::uint64_t>(seat,kSeatWeaponCount);
    if(!count || count>8 || !Readable(holders,count*8))return nullptr;
    for(std::uint64_t i=0;i<count;++i) {
        if(!Readable(holders[i],kHolderWeapon+8))continue;
        const auto w=At<const unsigned char*>(holders[i],kHolderWeapon);
        if(Readable(w,edf::kWeaponAccuracyScale+4) && At<std::int32_t>(w,edf::kWeaponMark)==edf::kMarkLofted)return w;
    }
    return nullptr;
}

// The first ground along the arc from `pos` at `vel` (m/frame), `drop` m/frame^2 added a frame, for at most `frames`
// frames: `hit` and the frames it took.
bool Impact(const float* pos,const float* vel,const float* drop,int frames,float* hit,float* took) noexcept {
    float p[3]={pos[0],pos[1],pos[2]},v[3]={vel[0],vel[1],vel[2]};
    for(int n=0;n<frames;n+=kSegment) {
        const float from[3]={p[0],p[1],p[2]};
        const int steps=frames-n<kSegment ? frames-n : kSegment;
        for(int k=0;k<steps;++k)for(int c=0;c<3;++c){v[c]+=drop[c];p[c]+=v[c];}
        const float d[3]={p[0]-from[0],p[1]-from[1],p[2]-from[2]};
        const float length=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
        const float at=MapRay(from,p,hit);
        if(at<0.0f)continue;
        *took=static_cast<float>(n)+(length>1e-3f ? at/length : 0.0f)*static_cast<float>(steps);
        return true;
    }
    return false;
}

// Where an arc from `pos` at `vel` (m/frame) comes down to height `y` (the per-frame step in closed form: after n
// frames p = pos + n vel + drop n(n+1)/2), on its way down. False when it never gets that low.
bool DownTo(const float* pos,const float* vel,const float* drop,float y,float* at) noexcept {
    const float a=0.5f*drop[1],b=vel[1]+0.5f*drop[1],c0=pos[1]-y;
    if(!(a<0.0f))return false;
    const float disc=b*b-4.0f*a*c0;
    if(disc<0.0f)return false;
    const float n=(-b-std::sqrt(disc))/(2.0f*a);   // a < 0: the larger root
    if(!(n>0.0f))return false;
    const float fall=0.5f*n*(n+1.0f);
    for(int c=0;c<3;++c)at[c]=pos[c]+n*vel[c]+drop[c]*fall;
    at[1]=y;
    return true;
}

// The rail turned `cone` rad off itself, `turn` rad round it.
void ConeEdge(const float* dir,float cone,float turn,float* out) noexcept {
    const float pick[3]={std::fabs(dir[1])<0.9f ? 0.0f : 1.0f,std::fabs(dir[1])<0.9f ? 1.0f : 0.0f,0.0f};
    float u[3]={dir[1]*pick[2]-dir[2]*pick[1],dir[2]*pick[0]-dir[0]*pick[2],dir[0]*pick[1]-dir[1]*pick[0]};
    const float lu=std::sqrt(u[0]*u[0]+u[1]*u[1]+u[2]*u[2]);
    for(float& x:u)x/=lu;
    const float w[3]={dir[1]*u[2]-dir[2]*u[1],dir[2]*u[0]-dir[0]*u[2],dir[0]*u[1]-dir[1]*u[0]};
    const float s=std::sin(cone),k=std::cos(cone),cs=std::cos(turn),sn=std::sin(turn);
    for(int c=0;c<3;++c)out[c]=dir[c]*k+(u[c]*cs+w[c]*sn)*s;
}

// FireAccuracy as fire uses it: times weapon+0xE14 (a per-weapon scale; the only writer found, 0x69DB11, is another
// weapon class's: not traced for Weapon_VehicleShoot, Debug logs it); one outside (0, 4] counts as 1.
float Cone(const unsigned char* weapon) noexcept {
    const float accuracy=At<float>(weapon,edf::kWeaponAccuracy),scale=At<float>(weapon,edf::kWeaponAccuracyScale);
    const float k=std::isfinite(scale) && scale>0.0f && scale<=4.0f ? scale : 1.0f;
    return std::isfinite(accuracy) && accuracy>0.0f && accuracy<0.5f ? accuracy*k : 0.0f;
}

bool Solve(const unsigned char* weapon,LauncherReadout& r) noexcept {
    float pos[3],dir[3],g[3];
    if(!Readable(weapon+edf::kWeaponMatrix,0x40) || !edf::MeanMuzzle(weapon,kMostMuzzles,pos,dir))return false;
    if(!edf::WorldGravity(image,g))return false;
    const float speed=At<float>(weapon,edf::kWeaponAmmoSpeed),factor=At<float>(weapon,edf::kWeaponAmmoGravity);
    const std::int32_t alive=At<std::int32_t>(weapon,edf::kWeaponAmmoAlive);
    if(!std::isfinite(speed) || speed<=0.0f || !std::isfinite(factor) || alive<=0)return false;
    const float vel[3]={dir[0]*speed,dir[1]*speed,dir[2]*speed};
    const float drop[3]={g[0]*factor/3600.0f,g[1]*factor/3600.0f,g[2]*factor/3600.0f};
    r=LauncherReadout{};
    r.elevation=std::asin(dir[1]<-1.0f ? -1.0f : dir[1]>1.0f ? 1.0f : dir[1])*180.0f/kPi;
    float took=0.0f;
    r.reach=Impact(pos,vel,drop,alive,r.impact,&took);
    if(!r.reach)return true;
    r.flight=took/60.0f;
    r.range=std::sqrt((r.impact[0]-pos[0])*(r.impact[0]-pos[0])+(r.impact[2]-pos[2])*(r.impact[2]-pos[2]));
    const float cone=Cone(weapon);
    for(int i=0;cone>0.0f && i<kLauncherRing;++i) {
        float edge[3];
        ConeEdge(dir,cone,2.0f*kPi*static_cast<float>(i)/static_cast<float>(kLauncherRing),edge);
        const float v[3]={edge[0]*speed,edge[1]*speed,edge[2]*speed};
        if(DownTo(pos,v,drop,r.impact[1],r.ring[r.rings]))++r.rings;
    }
    return true;
}

void DebugLog(const unsigned char* v,const unsigned char* weapon,const LauncherReadout& r) noexcept {
    static ULONGLONG at=0;
    const ULONGLONG now=GetTickCount64();
    if(!Cfg().debug || now-at<2000)return;
    at=now;
    Log("LAUNCHER v=%p elev=%.1f speed=%.3fm/f alive=%d cone=%.4f(x%.3f) reach=%d range=%.0fm flight=%.1fs ring=%d",v,r.elevation,
        At<float>(weapon,edf::kWeaponAmmoSpeed),At<std::int32_t>(weapon,edf::kWeaponAmmoAlive),At<float>(weapon,edf::kWeaponAccuracy),
        At<float>(weapon,edf::kWeaponAccuracyScale),r.reach,r.range,r.flight,r.rings);
}
}  // namespace

void LauncherFrame(unsigned char* v) noexcept {
    if(v[kDead] || SeatCount(v)==0)return;
    const auto seat=SeatAt(v,0);
    if(SeatRider(seat)!=Rider::player)return;
    const unsigned char* weapon=LoftedLauncher(seat);
    if(!weapon)return;
    LauncherReadout r{};
    if(!Solve(weapon,r)) {
        static ULONGLONG at=0;
        const ULONGLONG now=GetTickCount64();
        if(Cfg().debug && now-at>=2000) {
            at=now;
            Log("LAUNCHER v=%p no solve: muzzles=%llu (at most %llu read) gravity/speed/life unreadable otherwise",v,
                static_cast<unsigned long long>(At<std::uint64_t>(weapon,edf::kMuzzleCount)),static_cast<unsigned long long>(kMostMuzzles));
        }
        return;
    }
    latest=r;latestMs=GameMs();
    DebugLog(v,weapon,r);
}

bool PlayerLauncher(LauncherReadout* out) noexcept {
    if(!latestMs || GameMs()-latestMs>kFreshMs)return false;
    *out=latest;
    return true;
}

void ResetLauncher() noexcept {
    latest=LauncherReadout{};latestMs=0;
}
}  // namespace crew
