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
// The loft (LoftWant): the player aims the launcher with the camera at a ground point; the elevation that drops the
// rockets there (the high arc while the launcher can reach it, else the low one) goes to katyusha.cpp, which lifts the
// launcher's bone alone, so the camera keeps looking where the player looks (katyusha.cpp's file comment).
#include "crew.h"
#include "layout.h"
#include "memory.h"
#include "rounds.h"
#include "edf/weapon.h"
#include <cmath>

namespace crew {
namespace {
constexpr int kSegment=15;              // frames a map ray covers (0.25 s: the arc's sag over it is ~0.1 m)
constexpr std::uint64_t kMostMuzzles=64;
constexpr ULONGLONG kFreshMs=200;       // a readout not refreshed this long (game ms) is gone: the player got out
constexpr float kPi=3.14159265f;
constexpr float kSightFar=3000.0f;      // m: the farthest the camera's ground point is looked for (the stock far clip)
constexpr float kNoReach=1e9f;          // RoundImpact: the round's life alone ends the search

LauncherReadout latest{};
ULONGLONG latestMs=0;
const void* latestVehicle=nullptr;

// The seat's launcher marked kMarkLofted, or nullptr.
const unsigned char* LoftedLauncher(const unsigned char* vehicle,const unsigned char* seat) noexcept {
    const auto selected=PayloadSightPicked(vehicle,0);
    if(!selected)return nullptr;
    const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
    const auto count=At<std::uint64_t>(seat,kSeatWeaponCount);
    if(!count || count>16 || !Readable(holders,count*8))return nullptr;
    for(std::uint64_t i=0;i<count;++i) {
        if(!Readable(holders[i],kHolderWeapon+8))continue;
        const auto ctrl=At<const unsigned char*>(holders[i],kHolderCtrl);
        if(!Readable(ctrl,12) || At<std::int32_t>(ctrl,8)<=0)continue;
        const auto w=At<const unsigned char*>(holders[i],kHolderWeapon);
        if(w==selected && Readable(w,edf::kWeaponAccuracyScale+4) && At<std::int32_t>(w,edf::kWeaponMark)==edf::kMarkLofted)return w;
    }
    return nullptr;
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
    r.reach=RoundImpact(pos,vel,drop,alive,r.impact,&took);
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

// The elevation (rad up, on the vehicle) the launcher needs to drop its rockets where the player's camera looks (the
// first ground along the screen's centre: CameraRay + MapRay): the high arc while the launcher's stops allow it, else
// the low one, else (out of reach) 45 deg, the farthest it throws. Solved as EDF6AutoTurret solves (edf::BallisticArc:
// from the launcher's muzzles, in the vehicle's frame, gravity along the vehicle's down). False when the camera looks
// at no ground: the launcher then stays where the player aims it (the stock pose).
bool LoftWant(const unsigned char* v,const unsigned char* seat,const unsigned char* weapon,float* want,float* range) noexcept {
    float eye[3],dir[3],pos[3],rail[3],g[3],hit[3];
    if(!CameraRay(eye,dir) || !edf::MeanMuzzle(weapon,kMostMuzzles,pos,rail) || !edf::WorldGravity(image,g))return false;
    const float end[3]={eye[0]+dir[0]*kSightFar,eye[1]+dir[1]*kSightFar,eye[2]+dir[2]*kSightFar};
    if(MapRay(eye,end,hit)<0.0f)return false;
    const float speed=At<float>(weapon,edf::kWeaponAmmoSpeed),factor=At<float>(weapon,edf::kWeaponAmmoGravity);
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    const float d[3]={hit[0]-pos[0],hit[1]-pos[1],hit[2]-pos[2]};
    const float across=d[0]*m[0]+d[1]*m[1]+d[2]*m[2],up=d[0]*m[4]+d[1]*m[5]+d[2]*m[6],ahead=d[0]*m[8]+d[1]*m[9]+d[2]*m[10];
    const double x=std::sqrt(across*across+ahead*ahead);
    const double drop=factor*-(g[0]*m[4]+g[1]*m[5]+g[2]*m[6])/3600.0;
    const auto pitch=seat+kSeatAim+kAimAxes+kAxisStride;   // the pitch axis' stops, negative up
    const float lowest=-At<float>(pitch,kAxisMax),highest=-At<float>(pitch,kAxisMin);
    float e=0.0f,frames=0.0f;
    if(!(edf::BallisticArc(x,up,speed,drop,true,e,frames) && e>=lowest && e<=highest)
       && !edf::BallisticArc(x,up,speed,drop,false,e,frames))e=0.25f*kPi;
    if(!std::isfinite(e) || !(highest>lowest))return false;
    *want=e<lowest ? lowest : e>highest ? highest : e;
    *range=static_cast<float>(x);
    return true;
}

void DebugLog(const unsigned char* v,const unsigned char* weapon,const LauncherReadout& r,bool aim,float want,float sight) noexcept {
    static ULONGLONG at=0;
    const ULONGLONG now=GetTickCount64();
    if(!Cfg().debug || now-at<2000)return;
    at=now;
    Log("LAUNCHER v=%p elev=%.1f speed=%.3fm/f alive=%d cone=%.4f(x%.3f) reach=%d range=%.0fm flight=%.1fs ring=%d",v,r.elevation,
        At<float>(weapon,edf::kWeaponAmmoSpeed),At<std::int32_t>(weapon,edf::kWeaponAmmoAlive),At<float>(weapon,edf::kWeaponAccuracy),
        At<float>(weapon,edf::kWeaponAccuracyScale),r.reach,r.range,r.flight,r.rings);
    // The camera's pitch against the axis' (the stock pose's) and the held launcher's: the camera following the axes,
    // not the bone, is what keeps it on the ground while the launcher is lofted (katyusha.cpp).
    float eye[3],dir[3];
    const float cam=CameraRay(eye,dir) ? std::asin(dir[1]<-1.0f ? -1.0f : dir[1]>1.0f ? 1.0f : dir[1])*180.0f/kPi : 0.0f;
    LoftReadout l{};
    if(LauncherLoft(v,&l))
        Log("LOFT v=%p sight=%d %.0fm want=%.1f held=%d bone=%.1f stock(axis)=%.1f camera=%.1f ram=%+.1fdeg %.2fm",v,aim,sight,
            want*180.0f/kPi,l.held,l.elevation*180.0f/kPi,l.stock*180.0f/kPi,cam,l.ramTurn*180.0f/kPi,l.ramLength);
}
}  // namespace

// The first ground along the arc from `pos` at `vel` (m/frame), `drop` m/frame^2 added a frame, for at most `frames`
// frames: `hit` and the frames it took. Shared with playerjet.cpp's bomb impact (crew.h).
bool RoundImpact(const float* pos,const float* vel,const float* drop,int frames,float* hit,float* took) noexcept {
    rounds::Arc arc{};
    for(int c=0;c<3;++c){arc.vel[c]=vel[c];arc.drop[c]=drop[c];}
    float end[3];
    return rounds::FirstHit(arc,pos,frames,kSegment,kNoReach,&MapRay,hit,end,took);
}

void LauncherFrame(unsigned char* v) noexcept {
    const auto clear=[&](bool local) {
        if(local || latestVehicle==v) {
            SetLauncherLoft(v,false,0.0f);
            latest=LauncherReadout{};latestMs=0;latestVehicle=nullptr;
        }
    };
    if(!Cfg().enabled || v[kDead] || SeatCount(v)==0){clear(false);return;}
    const auto seat=SeatAt(v,0);
    if(SeatRider(seat)!=Rider::player || At<const void*>(seat,kSeatRider)!=PlayerHuman()){clear(false);return;}
    const unsigned char* weapon=LoftedLauncher(v,seat);
    if(!weapon){clear(true);return;}
    float want=0.0f,sight=0.0f;
    // The high camera observes the real rail's projectile. Feeding that observation back into LoftWant would
    // override the player's native pitch and move the rail again as the camera blends. Release the held loft;
    // katyusha.cpp eases it back to the native pose at its stock rate, which the impact prediction follows.
    const bool aim=!HighCamOn(v) && !TurretCamHighTransition(v) && LoftWant(v,seat,weapon,&want,&sight);
    SetLauncherLoft(v,aim,want);
    LauncherReadout r{};
    if(!Solve(weapon,r)) {
        static ULONGLONG at=0;
        const ULONGLONG now=GetTickCount64();
        if(Cfg().debug && now-at>=2000) {
            at=now;
            Log("LAUNCHER v=%p no solve: muzzles=%llu (at most %llu read) gravity/speed/life unreadable otherwise",v,
                static_cast<unsigned long long>(At<std::uint64_t>(weapon,edf::kMuzzleCount)),static_cast<unsigned long long>(kMostMuzzles));
        }
        clear(true);return;
    }
    latest=r;latestMs=GameMs();latestVehicle=v;
    DebugLog(v,weapon,r,aim,want,sight);
}

bool PlayerLauncher(LauncherReadout* out) noexcept {
    if(!Cfg().enabled || !latestMs || GameMs()-latestMs>kFreshMs)return false;
    *out=latest;
    return true;
}

void ResetLauncher() noexcept {
    latest=LauncherReadout{};latestMs=0;latestVehicle=nullptr;
}
}  // namespace crew
