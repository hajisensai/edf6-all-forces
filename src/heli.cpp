// Helicopter pilot. EDF.dll has no helicopter flight AI (docs/heli-input-re.md): a heli with the
// stock NPC rider in seat 0 just sits there. So for every heli with an NPC pilot (crewed by this plugin
// or spawned with one by the mission), after the
// stock input (slot 55) has filled the input block, we overwrite it:
//   veh+0x1540 lateral  (+ = along heading row 0)     veh+0x1548 forward (+ = along heading row 2)
//   veh+0x1544 throttle (target rotor speed, 0..1)    veh+0x154C 1.0     veh+0x1550 yaw rate
//   byte veh+0x2020 both gatlings, byte veh+0x2021 missile (506 and 409; 410 fires per gunner seat)
// Slot 57 (physics + weapons) consumes them the same frame.
//
// Flight: every mode yields a wanted horizontal velocity, and the stick tracks it (Stick): the orbit
// round a standing player and the escort of a moving one are pure velocity tracking (they never stop,
// so they never brake); landing, holding and the attack station arrive at a point as fast as the heli
// can still stop there (Arrive). The stick works on the heading rows, so its signs are right by
// construction. Altitude = climb-rate loop on the rotor speed (the lift, which lags the throttle by
// seconds) with a throttle loop under it that drives the rotor there. The world sign of yaw was
// not provable statically, so it is learned online from how the heading actually turns.
// When the player stands still for heliLandMs it lands next to them and stays down while they are
// close, so they can walk up and bump the NPC pilot.
// With the player aboard (in a gunner seat) it does not follow: it attacks the enemies around itself.
// Attack: low overwatch (see Engage). It fires only when the 3D nose line is within the fire cone of
// the target's lead point and that is within the guns' reach (the 506 gatling's rounds die at 160 m).
#include "crew.h"
#include "memory.h"
#include <cmath>

namespace crew {
namespace {
constexpr unsigned kHeliVtables[]={0x17DB238,0x17DEF98,0x17DF338,0x17DF790};   // 506, 409, 410, base
constexpr unsigned kHeli506=0x17DB238,kHeli410=0x17DF338;
// Input block, heading basis, contact byte, rotor speed
constexpr std::size_t kInLateral=0x1540,kInThrottle=0x1544,kInForward=0x1548,kInW=0x154C,kInYaw=0x1550;
constexpr std::size_t kFireGun=0x2020,kFireMissile=0x2021;
constexpr std::size_t kHeadRight=0x15C0,kHeadForward=0x15E0,kContact=0x1580,kRotor=0x1BF8;
constexpr unsigned char kContactGround=2;
// Lock-target registry and team relations (same as EDF6AutoTurret's turret.h)
constexpr std::size_t kRegistry=0x20B2AB0,kRegList=0x8,kNodeTarget=0x10;
constexpr std::size_t kTargetObject=0x8,kTargetAim=0x10,kTargetValid=0x29,kTargetLockable=0x2A;
constexpr std::size_t kTeams=0x20B2978,kTeamArray=0x38,kTeamStride=0x38,kTeamRelation=0x18;
constexpr std::int32_t kEnemyRelation=2,kMaxTeam=64;
constexpr int kMaxNodes=8192;
constexpr float kPi=3.14159265f;
constexpr float kBoardRange=25.0f;   // a landed heli stays down while the player is this close
constexpr float kLandDistance=20.0f; // it lands this far from a player standing still
constexpr float kRotorGain=4.0f;     // throttle per unit the rotor is off the wanted rotor
// Take-off: no horizontal stick on the ground and for kLiftOffMs after leaving it, so it climbs straight
// up. (With full forward stick before the rotor spun up, a heli slid along the ground into a tank and both
// blew up.)
constexpr ULONGLONG kLiftOffMs=1500;
// The 506 gatling's rounds live 40 frames at 4 m/frame (V_506HELI_GATLING01_*.SGO): 160 m. The 409 and
// 410 guns reach 480 m and 720 m, so for those heliRange is the limit.
constexpr float kGun506Range=150.0f;
constexpr float kBulletSpeed=240.0f; // m/s, the 506 gatling (4 m/frame); used for the lead of all guns
// Speed. Slot 57 drives the horizontal velocity towards k * stick along the heading rows, and the nose
// pitches with the forward stick: the logs (14:33-14:45, VEH positions 5 s apart at fwd=1.00) show
// pitch = -35 deg * stick (lagging ~3 s) and the speed creeping up to 16-17 m/s over ~30 s, so
// kTopSpeed is the speed of full stick. Back stick from 15 m/s stops it at 1.5-2 m/s^2; kStopDecel
// leaves margin for the ~1.5 s (kStopLag) it travels before the nose comes up and the brake bites.
constexpr float kTopSpeed=17.0f;
constexpr float kStopDecel=1.2f,kStopLag=1.5f;
// Attack: low overwatch. The guns are fixed along the nose, the nose dips 35 deg * forward stick, and
// steady forward stick s flies the heli at kTopSpeed * s. So a fixed glide slope at dip d means a speed of
// ~0.49 m/s per degree and a sink rate of speed * tan(d); the rotor sinks at most ~1.8 m/s (logs), which
// caps the slope at ~14 deg and ~7 m/s: slower than the ants it chases (runs: 0-5 shots per round).
// Instead it hovers heliFireHeight above the target at a station beside the player (whom the ants come
// for), turns the nose onto the target and dips it just as far as the target is below: 12 m up, a
// target 40-150 m out needs 5-17 deg, half stick at most, which creeps forward at a few m/s. When the
// creep has carried it kLeash beyond the station, or the target is so close below that the dip would
// pass kMaxDip, it flies back to the station (no aiming) and starts over.
constexpr float kMaxTilt=0.61f;      // rad: nose dip at full forward stick (35 deg in the logs)
constexpr float kPitchGain=1.0f;     // extra dip asked per rad the nose lags the wanted dip (halves the ~3 s lag)
constexpr float kMaxDip=0.52f;       // rad (30 deg): the deepest dip it aims with, short of full stick
constexpr float kDipMargin=0.09f;    // rad: back at the station it aims again once the dip is this far under kMaxDip
constexpr float kLeash=30.0f;        // m the aiming creep may carry it beyond the station
constexpr float kStationReached=10.0f;
constexpr float kTransit=60.0f;      // beyond this from the station it faces the way it flies, not the target
constexpr float kStandoff=0.6f;      // gunship mode (player aboard): the station is this share of the gun range out
constexpr float kStandoffMax=120.0f;
constexpr float kHitRadius=3.0f;     // m: the cone widens up close so a miss of this much at the target still fires
constexpr float kMissileCone=10.0f;  // deg: the missile homes (LockonType 1), so a rough aim is enough
constexpr float kMissileMin=50.0f;   // m: no missile closer than this
constexpr float kKeepTarget=30.0f;   // m: the current target counts this much nearer (less switching)
constexpr float kTooClose=1000.0f;   // m: a target too close below to aim at counts this much farther
// Formation: a V, kWingGap metres per place; all flown helis keep kSeparation metres apart, pushed by
// kSeparationGain m/s per metre of overlap.
constexpr float kWingGap=25.0f,kSeparation=25.0f,kSeparationGain=0.4f;
// With no enemy: while the player moves (a 3 m step within kMovingMs) the helis escort in a V on their
// flank, kEscortAhead metres forward: the player's velocity plus kSlotGain m/s per metre off the slot
// (at most kSlotCatch). While they stand they orbit them heliFollow metres out at kOrbitSpeed: tangent
// speed plus kRadialGain m/s per metre off the radius; wingmen hold their share of the circle from the
// leader by speeding up or slowing down kPhaseGain of kOrbitSpeed per rad behind or ahead.
constexpr ULONGLONG kMovingMs=2000;
constexpr float kEscortAhead=15.0f,kSlotGain=0.33f,kSlotCatch=kTopSpeed;
constexpr float kOrbitSpeed=10.0f,kRadialGain=0.3f,kPhaseGain=1.0f,kPhaseMax=0.5f;
constexpr float kFaceSpeed=3.0f;     // m/s: slower than this it faces the player instead of the way it flies
// Yaw input per rad/s of turn rate. Full yaw turns about 45-70 deg/s and the turn lags the input by about
// 0.8 s (the logs: 0.6 overshot by 38 deg), so it has to start easing off about 36 deg early.
// On a moving target the damping alone holds the nose rate/1.5 rad behind it (8 deg for an ant crossing
// 100 m out at 10 m/s, the 14:42 runs' |off| of 5-15 deg), so the target's bearing rate is fed forward:
// damping acts on the rate relative to it, plus kYawFeed input per rad/s of it.
constexpr float kYawDamp=1.2f,kYawFeed=1.1f;

struct Signature { std::size_t rva; unsigned char bytes[16]; std::size_t size; };
const Signature kHeliSignatures[]={
    {0x6543A0,{0x48,0x8B,0xC4,0x48,0x89,0x58,0x08,0x48,0x89,0x68,0x10,0x48,0x89,0x70,0x18,0x48},16},   // base input
    {0x61B8F0,{0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x6C,0x24,0x20,0x56,0x48,0x83,0xEC,0x20,0x48},16},   // 506 input
    {0x65451A,{0x4C,0x89,0xB7,0x40,0x15,0x00,0x00,0x44,0x89,0xB7,0x48,0x15,0x00,0x00,0x48,0xC7},16},   // input block reset
    {0x61B71E,{0x80,0xBB,0x20,0x20,0x00,0x00,0x00},7},                                                 // 506 gatlings read
    {0x61B743,{0x80,0xBB,0x21,0x20,0x00,0x00,0x00},7},                                                 // 506 missile read
};
bool profileOk=false;

// Obstacle avoidance casts rays at the map: terrain and buildings, never units (docs/raycast-re.md).
// EDF.dll's wrapper of the Havok ray cast takes the world wrapper *(image+kHavokGlobal)+0x10, the ray
// (filter 0x16: layer 22, the game's own map-only layer) and a stack hknpClosestHitCollector, which keeps
// the nearest hit: count at +0x0C, point at +0x30, fraction of the segment at +0x50.
constexpr std::size_t kHavokGlobal=0x20B2958,kCastRay=0x11A7EE0,kHitVtbl=0x1768B78,kHitReset=0xFDF00;
constexpr std::size_t kHitSlot0=0x978880,kHitAdd=0xD93980;
const Signature kRaySignatures[]={
    {kCastRay,{0x40,0x53,0x56,0x57,0x48,0x81,0xEC,0xA0,0x00,0x00,0x00,0x48,0x8B,0x05,0x66,0x71},16},
    {kHitReset,{0x33,0xD2,0xB8,0xFF,0xFF,0x00,0x00,0x89,0x51,0x0C,0x0F,0x28,0x05,0x1F,0x4B,0xE8},16},
    {kHitAdd,{0xF3,0x0F,0x10,0x4A,0x20,0x0F,0x10,0x41,0x10,0x0F,0xC6,0xC9,0x00,0x0F,0x2E,0xC1},16},
};
bool rayOk=false;
struct alignas(16) RayInput { float from[4],to[4]; std::uint32_t filter,unk24; std::uint64_t pad; };
static_assert(sizeof(RayInput)==0x30,"EdfRayInput");
struct alignas(16) RayHits { unsigned char raw[0xA0]; };

// Metres along a->b to the nearest terrain/building, or -1 with none (or no physics world). `hit`
// receives the point.
float CastRay(const float* a,const float* b,float* hit=nullptr) noexcept {
    if(!rayOk)return -1.0f;
    const auto g=At<unsigned char*>(image,kHavokGlobal);
    if(!Readable(g,0x70) || !At<const void*>(g,0x68))return -1.0f;
    const RayInput in{{a[0],a[1],a[2],1.0f},{b[0],b[1],b[2],1.0f},0x16,0,0};
    RayHits col{};
    *reinterpret_cast<const void**>(col.raw)=image+kHitVtbl;
    reinterpret_cast<void(*)(void*)>(image+kHitReset)(&col);
    reinterpret_cast<void(*)(void*,void*,const RayInput*)>(image+kCastRay)(g+0x10,&col,&in);
    if(*reinterpret_cast<const std::int32_t*>(col.raw+0x0C)==0)return -1.0f;
    const float f=*reinterpret_cast<const float*>(col.raw+0x50);
    if(!std::isfinite(f) || f<0.0f || f>1.0f)return -1.0f;
    if(hit)std::memcpy(hit,col.raw+0x30,12);
    const float d[3]={b[0]-a[0],b[1]-a[1],b[2]-a[2]};
    return f*std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
}

struct Heli {
    const void* vehicle;
    ULONGLONG crewedAt,seen,loggedAt,missileAt,targetAt,groundAt;
    LARGE_INTEGER last;
    float prev[3],vel[3];
    float hover;          // learned rotor speed that holds height
    float prevHeading,lastYaw,yawRate;
    int yawSign,votes;    // +1: a positive yaw input increases atan2(fwd.x, fwd.z)
    bool yawLocked,started;
    const void* target;
    float hold[3];        // where it holds when it has nobody to follow
    float pos[3];         // last position, for the other helis' formation and separation
    bool back;            // engaged: flying back to the station (see Engage), not aiming
    const void* tracked;  // the target tgtPrev/tgtVel belong to
    float tgtPrev[3],tgtVel[3];
    ULONGLONG playerAt;   // the player fix pVel was last updated from
    float pPrev[3],pVel[3];
};
Heli helis[16]{};
// The player's last move: they count as standing still once within 3 m of `still` since `stillAt`.
// moveDir is the horizontal direction of that last 3 m step.
float still[3]{},moveDir[3]{0,0,1};ULONGLONG stillAt=0;

void TrackPlayerStill() noexcept {
    const float d[3]={player.pos[0]-still[0],player.pos[1]-still[1],player.pos[2]-still[2]};
    if(stillAt && d[0]*d[0]+d[1]*d[1]+d[2]*d[2]>9.0f) {
        const float len=std::sqrt(d[0]*d[0]+d[2]*d[2]);
        if(len>1.0f){moveDir[0]=d[0]/len;moveDir[1]=0;moveDir[2]=d[2]/len;}
    }
    if(!stillAt || d[0]*d[0]+d[1]*d[1]+d[2]*d[2]>9.0f){std::memcpy(still,player.pos,12);stillAt=GetTickCount64();}
}

Heli* Find(const void* vehicle) noexcept {
    for(auto& h:helis)if(h.vehicle==vehicle)return &h;
    return nullptr;
}

float Dot2(const float* a,const float* b) noexcept { return a[0]*b[0]+a[2]*b[2]; }
float Clamp(float v,float lo,float hi) noexcept { return v<lo ? lo : v>hi ? hi : v; }
float Wrap(float a) noexcept { while(a>kPi)a-=2*kPi; while(a<-kPi)a+=2*kPi; return a; }

// Scales the horizontal part of `v` down to at most `limit` long.
void Limit2(float* v,float limit) noexcept {
    const float len=std::sqrt(Dot2(v,v));
    if(len>limit){v[0]*=limit/len;v[2]*=limit/len;}
}

// A horizontal unit vector from the heading basis row at `offset`, or false.
bool Row(const unsigned char* v,std::size_t offset,float* out) noexcept {
    const float* r=reinterpret_cast<const float*>(v+offset);
    const float len=std::sqrt(r[0]*r[0]+r[2]*r[2]);
    if(!std::isfinite(len) || len<0.1f)return false;
    out[0]=r[0]/len;out[1]=0;out[2]=r[2]/len;
    return true;
}

const std::int32_t* Relations(std::int32_t team) noexcept {
    if(team<0 || team>=kMaxTeam)return nullptr;
    const auto manager=At<const unsigned char*>(image,kTeams);
    if(!Readable(manager,kTeamArray+8))return nullptr;
    const auto rows=At<const unsigned char*>(manager,kTeamArray);
    if(!Readable(rows+team*kTeamStride,kTeamStride))return nullptr;
    const auto relation=At<const std::int32_t*>(rows+team*kTeamStride,kTeamRelation);
    return Readable(relation,kMaxTeam*4) ? relation : nullptr;
}

// The horizontal distance below which a target sits too steeply under a heli heliFireHeight above it.
float MinAimHoriz() noexcept { return cfg.heliFireHeight/std::tan(kMaxDip); }

// The enemy lock point to engage, among the enemies within `range` of `around`: the one nearest to
// `from` (the heli: the shortest turn and flight), the current one counting kKeepTarget nearer and one
// too close below to aim at kTooClose farther. Returns false with none.
bool PickTarget(Heli& h,const unsigned char* v,const float* around,const float* from,float range,float* aim) noexcept {
    auto team=At<std::int32_t>(v,kTeam);
    if(team==kTeamVehicle)team=player.team;   // nobody's vehicle: fight the player's enemies
    const auto relation=Relations(team);
    const auto registry=At<const unsigned char*>(image,kRegistry);
    if(!relation || !Readable(registry,kRegList+0x10))return false;
    const auto head=At<const unsigned char*>(registry,kRegList);
    if(!Readable(head,0x10))return false;
    const float minHoriz=MinAimHoriz();
    float best=0.0f,bestAim[3]{};const void* bestObject=nullptr;
    int n=0;
    for(auto node=At<const unsigned char*>(head,0);node!=head && n<kMaxNodes;node=At<const unsigned char*>(node,0),++n) {
        const auto target=At<const unsigned char*>(node,kNodeTarget);
        if(!target || target[0]!=0 || !target[kTargetValid] || !target[kTargetLockable])continue;
        const auto object=At<const unsigned char*>(target,kTargetObject);
        if(!object || object==v || object[kDead])continue;
        const auto other=At<std::int32_t>(object,kTeam);
        if(other<0 || other>=kMaxTeam || relation[other]!=kEnemyRelation)continue;
        const float* a=reinterpret_cast<const float*>(target+kTargetAim);
        if(!std::isfinite(a[0]) || !std::isfinite(a[1]) || !std::isfinite(a[2]))continue;
        const float d[3]={a[0]-around[0],a[1]-around[1],a[2]-around[2]};
        if(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]>range*range)continue;
        const float f[3]={a[0]-from[0],a[1]-from[1],a[2]-from[2]};
        float score=std::sqrt(f[0]*f[0]+f[1]*f[1]+f[2]*f[2]);
        if(object==h.target)score-=kKeepTarget;
        if(std::sqrt(Dot2(f,f))<minHoriz)score+=kTooClose;
        if(!bestObject || score<best){best=score;bestObject=object;std::memcpy(bestAim,a,12);}
    }
    if(!bestObject)return false;
    if(bestObject!=h.target)h.targetAt=GetTickCount64();
    h.target=bestObject;std::memcpy(aim,bestAim,12);
    return true;
}

float GunRange(const unsigned char* v) noexcept {
    return At<const unsigned char*>(v,0)==image+kHeli506 && kGun506Range<cfg.heliRange ? kGun506Range : cfg.heliRange;
}

// Would a burst from `from` towards `to` pass within 8 m of the player before reaching the target?
bool PlayerInLine(const float* from,const float* to) noexcept {
    if(!player.at || GetTickCount64()-player.at>2000)return false;
    const float d[3]={to[0]-from[0],to[1]-from[1],to[2]-from[2]};
    const float p[3]={player.pos[0]-from[0],player.pos[1]-from[1],player.pos[2]-from[2]};
    const float dd=d[0]*d[0]+d[1]*d[1]+d[2]*d[2];
    if(dd<1.0f)return false;
    const float t=(p[0]*d[0]+p[1]*d[1]+p[2]*d[2])/dd;
    if(t<0.0f || t>1.0f)return false;
    const float c[3]={p[0]-d[0]*t,p[1]-d[1]*t,p[2]-d[2]*t};
    return c[0]*c[0]+c[1]*c[1]+c[2]*c[2]<64.0f;
}

// This heli's place in the formation: how many helis flown right now come before it in `helis`.
int WingIndex(const Heli& h,ULONGLONG ms) noexcept {
    int n=0;
    for(const auto& o:helis){if(&o==&h)break;if(o.vehicle && ms-o.seen<2000)++n;}
    return n;
}

float Dist2(const float* a,const float* b) noexcept {
    const float d[3]={a[0]-b[0],0,a[2]-b[2]};
    return std::sqrt(Dot2(d,d));
}

// Adds to the wanted velocity `vel` a push away from the other helis flown right now, so they keep
// kSeparation metres apart.
void Separate(const Heli& h,const float* pos,float* vel,ULONGLONG ms) noexcept {
    for(const auto& o:helis) {
        if(&o==&h || !o.vehicle || ms-o.seen>2000)continue;
        const float d[3]={pos[0]-o.pos[0],0,pos[2]-o.pos[2]};
        const float len=std::sqrt(Dot2(d,d));
        if(len<0.1f || len>kSeparation)continue;
        vel[0]+=d[0]/len*(kSeparation-len)*kSeparationGain;vel[2]+=d[2]/len*(kSeparation-len)*kSeparationGain;
    }
}

// Filtered velocity of `point` (the player or the target), from its moves between fixes `dt` seconds
// apart; a jump faster than `limit` (a new target, a teleport) restarts it at rest.
void TrackVelocity(float* prev,float* vel,const float* point,float dt,float limit,bool restart) noexcept {
    const float raw[3]={(point[0]-prev[0])/dt,(point[1]-prev[1])/dt,(point[2]-prev[2])/dt};
    if(restart || raw[0]*raw[0]+raw[1]*raw[1]+raw[2]*raw[2]>limit*limit)vel[0]=vel[1]=vel[2]=0.0f;
    else for(int i=0;i<3;++i)vel[i]+=(raw[i]-vel[i])*0.2f;
    std::memcpy(prev,point,12);
}

// Arrive at `goal`, moving at `goalVel`, as fast as it can still stop there: the speed towards it is the
// least of kTopSpeed, what kStopDecel stops in the distance left after kStopLag at the closing speed,
// and heliMoveGain/heliBrakeGain per metre near it (the settle of the old position PD).
void Arrive(const Heli& h,const float* pos,const float* goal,const float* goalVel,float* out) noexcept {
    const float e[3]={goal[0]-pos[0],0,goal[2]-pos[2]};
    const float len=std::sqrt(Dot2(e,e));
    out[0]=goalVel[0];out[1]=0;out[2]=goalVel[2];
    if(len<0.1f)return;
    const float rel[3]={h.vel[0]-goalVel[0],0,h.vel[2]-goalVel[2]};
    const float closing=Dot2(rel,e)/len>0.0f ? Dot2(rel,e)/len : 0.0f;
    const float left=len-closing*kStopLag>0.0f ? len-closing*kStopLag : 0.0f;
    const float settle=len*cfg.heliMoveGain/(cfg.heliBrakeGain>0.01f ? cfg.heliBrakeGain : 0.01f);
    float want=std::sqrt(2.0f*kStopDecel*left);
    if(want>settle)want=settle;
    if(want>kTopSpeed)want=kTopSpeed;
    out[0]+=e[0]/len*want;out[2]+=e[2]/len*want;
}

// The helis flown right now: how many, and the first of them (the formation leader).
int ActiveHelis(ULONGLONG ms,const Heli** leader) noexcept {
    int n=0;*leader=nullptr;
    for(const auto& o:helis)if(o.vehicle && ms-o.seen<2000){if(!n)*leader=&o;++n;}
    return n;
}

// No target: escort the moving player, or orbit the standing one (see kMovingMs). Sets the wanted
// velocity and height; returns how far it is off its slot (escort) or radius (orbit).
float Formation(const Heli& h,const float* pos,const float* fwd,int wing,ULONGLONG ms,float* vel,float* height) noexcept {
    const Heli* leader=nullptr;
    const int count=ActiveHelis(ms,&leader);
    const float* lead=leader ? leader->pos : pos;
    *height=player.pos[1]+cfg.heliHeight+4.0f*static_cast<float>(wing);
    if(ms-stillAt<kMovingMs) {
        // A V on the side the leader is on, heliFollow out and kEscortAhead forward; wing n takes
        // place (n+1)/2 on alternating sides, kWingGap metres back and out per place.
        const float perp[2]={moveDir[2],-moveDir[0]};
        const float toLead[2]={lead[0]-player.pos[0],lead[2]-player.pos[2]};
        const float flank=toLead[0]*perp[0]+toLead[1]*perp[1]<0 ? -1.0f : 1.0f;
        const float place=static_cast<float>((wing+1)/2),side=wing%2 ? 1.0f : -1.0f;
        const float out=cfg.heliFollow+side*place*kWingGap,ahead=kEscortAhead-place*kWingGap;
        float fix[3]={player.pos[0]+perp[0]*flank*out+moveDir[0]*ahead-pos[0],0,
                      player.pos[2]+perp[1]*flank*out+moveDir[2]*ahead-pos[2]};
        const float off=std::sqrt(Dot2(fix,fix));
        fix[0]*=kSlotGain;fix[2]*=kSlotGain;Limit2(fix,kSlotCatch);
        vel[0]=h.pVel[0]+fix[0];vel[1]=0;vel[2]=h.pVel[2]+fix[2];
        return off;
    }
    // Orbit (counterclockwise in atan2(x, z)): tangent at kOrbitSpeed, pulled onto the radius; a wingman
    // runs faster or slower until it sits its share of the circle behind the leader.
    const float r=cfg.heliFollow>10.0f ? cfg.heliFollow : 10.0f;
    float out[3]={pos[0]-player.pos[0],0,pos[2]-player.pos[2]};
    float dist=std::sqrt(Dot2(out,out));
    if(dist<1.0f){out[0]=-fwd[0];out[2]=-fwd[2];dist=1.0f;}
    else{out[0]/=dist;out[2]/=dist;}
    const float tangent[3]={out[2],0,-out[0]};   // d/d(angle) of (sin, cos)
    const float angle=std::atan2(out[0],out[2]);
    const float want=std::atan2(lead[0]-player.pos[0],lead[2]-player.pos[2])+
        2.0f*kPi*static_cast<float>(wing)/static_cast<float>(count>0 ? count : 1);
    const float speed=kOrbitSpeed*(1.0f+Clamp(Wrap(want-angle)*kPhaseGain,-kPhaseMax,kPhaseMax));
    const float radial=Clamp((r-dist)*kRadialGain,-kOrbitSpeed,kOrbitSpeed);
    vel[0]=h.pVel[0]+tangent[0]*speed+out[0]*radial;vel[1]=0;vel[2]=h.pVel[2]+tangent[2]*speed+out[2]*radial;
    return std::fabs(dist-r);
}

// Engaged (see kMaxTilt): the station is beside the player, heliFollow out across the line from them to
// the target on the leader's side (wingmen alternate sides, kWingGap farther out per place), so the
// rounds pass clear of them; with the player aboard it is kStandoff of the gun range out from the target
// on the heli's side. Either way at least 1.5 x the closest aimable distance from the target. Sets the
// wanted velocity (arrive at the station), the height (heliFireHeight above the target, or the player if
// higher) and h.back; returns the distance to the station.
float Engage(Heli& h,const float* pos,const float* aim,float range,float dip,int wing,ULONGLONG ms,bool follow,
             float* vel,float* height) noexcept {
    float station[3];
    if(follow) {
        const Heli* leader=nullptr;ActiveHelis(ms,&leader);
        const float* lead=leader ? leader->pos : pos;
        float axis[3]={aim[0]-player.pos[0],0,aim[2]-player.pos[2]};
        float len=std::sqrt(Dot2(axis,axis));
        if(len<1.0f){axis[0]=0;axis[2]=1;len=1.0f;}
        const float perp[3]={axis[2]/len,0,-axis[0]/len};
        const float toLead[3]={lead[0]-player.pos[0],0,lead[2]-player.pos[2]};
        const float side=(Dot2(toLead,perp)<0 ? -1.0f : 1.0f)*(wing%2 ? -1.0f : 1.0f);
        const float out=cfg.heliFollow+kWingGap*static_cast<float>((wing+1)/2);
        station[0]=player.pos[0]+perp[0]*side*out;station[2]=player.pos[2]+perp[2]*side*out;
    } else {
        float from[3]={pos[0]-aim[0],0,pos[2]-aim[2]};
        float len=std::sqrt(Dot2(from,from));
        if(len<1.0f){from[0]=0;from[2]=1;len=1.0f;}
        const float out=Clamp(range*kStandoff,0.0f,kStandoffMax);
        station[0]=aim[0]+from[0]/len*out;station[2]=aim[2]+from[2]/len*out;
    }
    float away[3]={station[0]-aim[0],0,station[2]-aim[2]};
    const float awayLen=std::sqrt(Dot2(away,away)),minAway=1.5f*MinAimHoriz();
    if(awayLen<minAway) {
        if(awayLen<0.1f){away[0]=pos[0]-aim[0];away[2]=pos[2]-aim[2];}
        const float l=std::sqrt(Dot2(away,away))>0.1f ? std::sqrt(Dot2(away,away)) : 1.0f;
        station[0]=aim[0]+away[0]/l*minAway;station[2]=aim[2]+away[2]/l*minAway;
    }
    const float ground=follow && player.pos[1]>aim[1] ? player.pos[1] : aim[1];
    *height=ground+cfg.heliFireHeight+3.0f*static_cast<float>(wing);
    const float off=Dist2(pos,station);
    if(off>kLeash || dip>kMaxDip)h.back=true;
    else if(h.back && off<kStationReached && dip<kMaxDip-kDipMargin)h.back=false;
    const float still2[3]={0,0,0};
    Arrive(h,pos,station,follow ? h.pVel : still2,vel);
    return off;
}

// Obstacle avoidance, applied to the wanted velocity and height of every mode but landing:
// - the height stays kGroundClear over the ground below and over whatever lies kLookAhead seconds ahead
//   (a ray down from high above that point finds hill tops and roofs alike);
// - a ray straight along the way it flies (and two kAvoidSide to the sides) finds walls: the speed into
//   one is cut to what stops it kAvoidStop short, the height goes kRoofClear over the wall's top, and it
//   veers to the clearer side.
constexpr float kLookAhead=3.0f,kLookMin=25.0f,kAvoidSide=0.52f;   // s; m; rad (30 deg)
constexpr float kGroundClear=6.0f,kRoofClear=10.0f,kRoofProbe=150.0f,kAvoidStop=12.0f,kAvoidSteer=8.0f;
struct Avoidance { float ahead,clear; };   // metres to the wall ahead and over the ground; -1: none seen

float RoofBelow(const float* at,float top) noexcept {
    const float a[3]={at[0],top,at[2]},b[3]={at[0],top-kRoofProbe*2.0f,at[2]};
    float hit[3];
    return CastRay(a,b,hit)>=0.0f ? hit[1] : -1e9f;
}

Avoidance Avoid(const float* pos,const float* vel,float* want,float* height,bool land) noexcept {
    Avoidance r{-1.0f,-1.0f};
    if(!cfg.heliAvoid || !rayOk)return r;
    const float down[3]={pos[0],pos[1]-kRoofProbe*2.0f,pos[2]};
    float hit[3];
    if(CastRay(pos,down,hit)>=0.0f) {
        r.clear=pos[1]-hit[1];
        if(!land && *height<hit[1]+kGroundClear)*height=hit[1]+kGroundClear;
    }
    if(land)return r;
    // The way it flies: where it wants to go, or where it drifts.
    float dir[3]={want[0],0,want[2]};
    if(Dot2(dir,dir)<4.0f){dir[0]=vel[0];dir[2]=vel[2];}
    const float dl=std::sqrt(Dot2(dir,dir));
    if(dl<2.0f)return r;
    dir[0]/=dl;dir[2]/=dl;
    const float speed=std::sqrt(Dot2(vel,vel));
    const float look=speed*kLookAhead>kLookMin ? speed*kLookAhead : kLookMin;
    const float top=(pos[1]>*height ? pos[1] : *height)+kRoofProbe;
    const float ahead[3]={pos[0]+dir[0]*look,0,pos[2]+dir[2]*look};
    const float roof=RoofBelow(ahead,top);
    if(*height<roof+kGroundClear)*height=roof+kGroundClear;
    // Walls: straight ahead and to both sides.
    float dist[3];
    for(int i=0;i<3;++i) {
        const float a=static_cast<float>(i-1)*kAvoidSide,c=std::cos(a),s=std::sin(a);
        const float d[3]={dir[0]*c+dir[2]*s,0,dir[2]*c-dir[0]*s};
        const float end[3]={pos[0]+d[0]*look,pos[1],pos[2]+d[2]*look};
        dist[i]=CastRay(pos,end,i==1 ? hit : nullptr);
    }
    const float centre=dist[1];
    if(centre>=0.0f) {
        r.ahead=centre;
        const float beyond[3]={hit[0]+dir[0]*3.0f,0,hit[2]+dir[2]*3.0f};
        const float wall=RoofBelow(beyond,top);
        if(*height<wall+kRoofClear)*height=wall+kRoofClear;
        const float room=centre-kAvoidStop;
        const float allowed=room>0.0f ? std::sqrt(2.0f*kStopDecel*room) : 0.0f;
        const float into=Dot2(want,dir);
        if(into>allowed){want[0]-=dir[0]*(into-allowed);want[2]-=dir[2]*(into-allowed);}
    }
    // Veer to the clearer side (a side with no hit is clear for the whole look).
    const float left=dist[0]<0.0f ? look : dist[0],rightSide=dist[2]<0.0f ? look : dist[2];
    if(centre>=0.0f || left<look || rightSide<look) {
        const float side=left>rightSide ? -1.0f : 1.0f;   // -1: toward the first ray (negative angle)
        const float s=std::sin(side*kAvoidSide*3.0f),c=std::cos(side*kAvoidSide*3.0f);   // 90 deg off
        const float perp[3]={dir[0]*c+dir[2]*s,0,dir[2]*c-dir[0]*s};
        const float nearest=centre>=0.0f ? centre : (left<rightSide ? left : rightSide);
        const float push=kAvoidSteer*(1.0f-nearest/look);
        want[0]+=perp[0]*push;want[2]+=perp[2]*push;
    }
    return r;
}

void Fly(Heli& h,unsigned char* v,bool playerAboard) noexcept {
    LARGE_INTEGER now,freq;QueryPerformanceCounter(&now);QueryPerformanceFrequency(&freq);
    const float* pos=reinterpret_cast<const float*>(v+kPosition);
    float right[3],fwd[3];
    if(!Row(v,kHeadRight,right) || !Row(v,kHeadForward,fwd))return;
    const float heading=std::atan2(fwd[0],fwd[2]);
    std::memcpy(h.pos,pos,12);
    if(!h.started) {
        h.started=true;h.last=now;std::memcpy(h.prev,pos,12);std::memcpy(h.hold,pos,12);
        const float rotor=At<float>(v,kRotor);
        h.hover=std::isfinite(rotor) && rotor>0.2f && rotor<1.0f ? rotor : 0.5f;
        h.prevHeading=heading;h.yawSign=1;
        return;
    }
    const float dt=Clamp(static_cast<float>(now.QuadPart-h.last.QuadPart)/static_cast<float>(freq.QuadPart),0.004f,0.1f);
    h.last=now;
    for(int i=0;i<3;++i){const float raw=(pos[i]-h.prev[i])/dt;h.vel[i]+= (raw-h.vel[i])*0.3f;h.prev[i]=pos[i];}
    const bool grounded=(v[kContact]&kContactGround)!=0;

    // Learn the yaw sign from the turn the last input produced.
    const float turned=Wrap(heading-h.prevHeading);h.prevHeading=heading;
    h.yawRate+=(turned/dt-h.yawRate)*0.3f;
    if(!grounded && !h.yawLocked && std::fabs(h.lastYaw)>0.3f && std::fabs(turned)>0.0005f) {
        h.votes+=(turned>0)==(h.lastYaw*static_cast<float>(h.yawSign)>0) ? 1 : -1;   // did it turn the way we meant?
        if(h.votes<=-15){h.yawSign=-h.yawSign;h.votes=0;Log("HELI v=%p yaw sign flipped to %d",v,h.yawSign);}
        else if(h.votes>=30){h.yawLocked=true;Log("HELI v=%p yaw sign locked at %d",v,h.yawSign);}
    }
    // The nose (the guns are fixed along it), pitch included.
    const float* noseRow=reinterpret_cast<const float*>(v+kMatrix+0x20);
    const float noseLen=std::sqrt(noseRow[0]*noseRow[0]+noseRow[1]*noseRow[1]+noseRow[2]*noseRow[2]);
    const bool noseOk=std::isfinite(noseLen) && noseLen>0.5f;
    const float nose[3]={noseOk ? noseRow[0]/noseLen : fwd[0],noseOk ? noseRow[1]/noseLen : 0.0f,noseOk ? noseRow[2]/noseLen : fwd[2]};
    const float dip=-std::asin(Clamp(nose[1],-1.0f,1.0f));

    // Who to follow, what to shoot, and how they move.
    const ULONGLONG ms=GetTickCount64();
    const int wing=WingIndex(h,ms);
    const bool follow=!playerAboard && player.at && ms-player.at<10000;
    if(follow)TrackPlayerStill();
    if(follow && player.at!=h.playerAt) {
        const float pdt=h.playerAt ? static_cast<float>(player.at-h.playerAt)*0.001f : 0.0f;
        TrackVelocity(h.pPrev,h.pVel,player.pos,pdt>0.005f ? pdt : 0.005f,40.0f,!h.playerAt || pdt>0.5f);
        h.playerAt=player.at;
    }
    if(!follow){h.pVel[0]=h.pVel[1]=h.pVel[2]=0.0f;h.playerAt=0;}
    const float* anchor=follow ? player.pos : pos;
    float aim[3]{};
    const bool engage=PickTarget(h,v,anchor,pos,cfg.heliRange,aim);
    if(engage) {
        TrackVelocity(h.tgtPrev,h.tgtVel,aim,dt,40.0f,h.tracked!=h.target);
        h.tracked=h.target;
    } else {
        h.tracked=nullptr;h.back=false;
    }
    // Lead the target by the rounds' flight time; the nose, the dip and the fire test all use the lead point.
    float lead[3]{},dist=0.0f,dipWant=0.0f,losRate=0.0f;
    if(engage) {
        const float d0[3]={aim[0]-pos[0],aim[1]-pos[1],aim[2]-pos[2]};
        const float t=std::sqrt(d0[0]*d0[0]+d0[1]*d0[1]+d0[2]*d0[2])/kBulletSpeed;
        for(int i=0;i<3;++i)lead[i]=aim[i]+h.tgtVel[i]*t;
        const float d[3]={lead[0]-pos[0],lead[1]-pos[1],lead[2]-pos[2]};
        dist=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
        dipWant=std::atan2(-d[1],std::sqrt(Dot2(d,d)));
        // Bearing rate of the target: d/dt atan2(dx, dz) with the relative velocity.
        const float rel[3]={h.tgtVel[0]-h.vel[0],0,h.tgtVel[2]-h.vel[2]};
        if(Dot2(d,d)>1.0f)losRate=(d[2]*rel[0]-d[0]*rel[2])/Dot2(d,d);
    }
    const float toPlayer[3]={player.pos[0]-pos[0],0,player.pos[2]-pos[2]};
    const bool byPlayer=follow && Dot2(toPlayer,toPlayer)<kBoardRange*kBoardRange;
    // Land by a player who stands still with no enemy about (so they can walk up and take it over),
    // and stay down while they are next to it.
    const bool land=follow && !engage && cfg.heliLandMs && (ms-stillAt>cfg.heliLandMs || (grounded && byPlayer));

    // The wanted horizontal velocity and height.
    const float range=GunRange(v);
    float want[3]={0,0,0},height=pos[1],off=0.0f;
    const float rest[3]={0,0,0};
    if(land) {
        float dir[3]={pos[0]-player.pos[0],0,pos[2]-player.pos[2]};
        float len=std::sqrt(Dot2(dir,dir));
        if(len<1.0f){dir[0]=-fwd[0];dir[2]=-fwd[2];len=1.0f;}
        const float r=cfg.heliFollow<kLandDistance ? cfg.heliFollow : kLandDistance;
        const float spot[3]={player.pos[0]+dir[0]/len*(r+kWingGap*static_cast<float>(wing)),0,
                             player.pos[2]+dir[2]/len*(r+kWingGap*static_cast<float>(wing))};
        Arrive(h,pos,spot,rest,want);
        off=Dist2(pos,spot);
        height=player.pos[1]-10.0f;   // below the ground: it descends until it touches down
    } else if(engage) {
        off=Engage(h,pos,aim,range,dipWant,wing,ms,follow,want,&height);
    } else if(follow) {
        off=Formation(h,pos,fwd,wing,ms,want,&height);
    } else {
        Arrive(h,pos,h.hold,rest,want);
        off=Dist2(pos,h.hold);height=h.hold[1];
    }
    if(!land)Separate(h,pos,want,ms);
    const Avoidance avoid=Avoid(pos,h.vel,want,&height,land);
    if(follow || engage)std::memcpy(h.hold,pos,12);
    const bool aiming=engage && !h.back && dist<range;

    // Horizontal: the stick for the wanted velocity (full stick flies kTopSpeed) plus heliBrakeGain per
    // m/s it is off, on the heading rows.
    float c[3]={want[0]/kTopSpeed+(want[0]-h.vel[0])*cfg.heliBrakeGain,0,
                want[2]/kTopSpeed+(want[2]-h.vel[2])*cfg.heliBrakeGain};
    Limit2(c,1.0f);
    float forward=Clamp(Dot2(c,fwd),-1.0f,1.0f);
    const float lateral=Clamp(Dot2(c,right),-1.0f,1.0f);
    // Aiming, the forward stick is the nose dip (kMaxTilt at full stick), and it creeps along the nose.
    if(aiming)forward=Clamp((dipWant+(dipWant-dip)*kPitchGain)/kMaxTilt,-1.0f,1.0f);

    // Vertical: the rotor (kRotor) is the lift, and it trails the throttle by seconds (spooling down
    // slower than up). So altitude -> climb rate -> wanted rotor -> a throttle that drives the rotor
    // there. h.hover is the rotor that holds height; it is learned only near the goal, where the
    // climb rate is not saturated (learning on the climb winds it up to 1 and it overshoots by 20 m).
    const float dy=height-pos[1];
    const float climb=Clamp(dy*0.25f,-3.0f,3.0f);
    const float err=climb-h.vel[1];
    if(std::fabs(dy)<6.0f)h.hover=Clamp(h.hover+err*cfg.heliHoverLearn*dt,0.1f,1.0f);
    const float wantRotor=Clamp(h.hover+err*cfg.heliClimbGain,0.0f,1.0f);
    const float rotor=At<float>(v,kRotor);
    float throttle=std::isfinite(rotor) ? Clamp(wantRotor+(wantRotor-rotor)*kRotorGain,0.0f,1.0f) : wantRotor;
    float stickF=forward,stickL=lateral;
    if(grounded)h.groundAt=ms;
    if(land && grounded){throttle=0.0f;stickF=stickL=0.0f;}
    else if(ms-h.groundAt<kLiftOffMs){stickF=stickL=0.0f;}

    // Yaw: engaged, onto the target (with its bearing rate fed forward) unless transiting back from
    // far off; else the way it flies, or the player when slow.
    float face[3]={0,0,0},faceRate=0.0f;
    if(engage && !(h.back && off>kTransit)){face[0]=lead[0]-pos[0];face[2]=lead[2]-pos[2];faceRate=losRate;}
    else if(std::sqrt(Dot2(want,want))>kFaceSpeed){face[0]=want[0];face[2]=want[2];}
    else if(follow){face[0]=player.pos[0]-pos[0];face[2]=player.pos[2]-pos[2];}
    float yaw=0.0f,offYaw=kPi;
    if(Dot2(face,face)>1.0f) {
        offYaw=Wrap(std::atan2(face[0],face[2])-heading);
        yaw=Clamp(offYaw*1.5f-(h.yawRate-faceRate)*kYawDamp+faceRate*kYawFeed,-1.0f,1.0f)*static_cast<float>(h.yawSign);
    }
    h.lastYaw=yaw;

    Put<float>(v,kInLateral,stickL);Put<float>(v,kInForward,stickF);Put<float>(v,kInThrottle,throttle);
    Put<float>(v,kInW,1.0f);Put<float>(v,kInYaw,yaw);

    // Fire: the nose within the cone of the lead point (heliFireCone, wider up close so kHitRadius at
    // the target still counts), the lead point within the guns' reach, and not through the player.
    // The missile homes, so it goes with a rough aim and farther.
    bool gun=false,missile=false;float miss=180.0f,cone=cfg.heliFireCone;
    if(engage && dist>1.0f) {
        const float d[3]={lead[0]-pos[0],lead[1]-pos[1],lead[2]-pos[2]};
        miss=std::acos(Clamp((d[0]*nose[0]+d[1]*nose[1]+d[2]*nose[2])/dist,-1.0f,1.0f))*180.0f/kPi;
        const float wide=std::atan(kHitRadius/dist)*180.0f/kPi;
        if(wide>cone)cone=wide;
    }
    if(engage && cfg.heliFire && !grounded && !land && !PlayerInLine(pos,lead)) {
        gun=miss<cone && dist<range;
        missile=cfg.heliMissile && miss<kMissileCone && dist>kMissileMin && dist<cfg.heliRange && ms-h.missileAt>cfg.heliMissileMs;
        if(missile)h.missileAt=ms;
    }
    if(At<const unsigned char*>(v,0)!=image+kHeli410) {   // 410 fires through per-gunner-seat blocks
        v[kFireGun]=gun;v[kFireMissile]=missile;
    }

    if(cfg.debug && ms-h.loggedAt>1000) {
        h.loggedAt=ms;
        const float speed=std::sqrt(Dot2(h.vel,h.vel)),aimedLead=Dist2(aim,lead);
        Log("HELI v=%p %s wing=%d y=%.1f goal=%.1f vy=%.2f thr=%.3f hover=%.3f rotor=%.3f fwd=%.2f lat=%.2f yaw=%.2f rate=%.0fdeg/s sign=%d%s votes=%d dGoal=%.0f ground=%d target=%p dist=%.0f off=%.0fdeg miss=%.1fdeg pitch=%.0fdeg gun=%d msl=%d spd=%.1f want=%.1f dipWant=%.0fdeg cone=%.1fdeg lead=%.1f tv=%.1f los=%.0fdeg/s ahead=%.0f clear=%.0f",
            v,land ? "land" : engage ? (h.back ? "back" : aiming ? "aim" : "wait") : follow ? (ms-stillAt<kMovingMs ? "escort" : "orbit") : "hold",
            wing,pos[1],height,h.vel[1],throttle,h.hover,rotor,
            stickF,stickL,yaw,h.yawRate*180.0f/kPi,h.yawSign,h.yawLocked ? "(locked)" : "",h.votes,off,grounded,
            engage ? h.target : nullptr,dist,offYaw*180.0f/kPi,miss,-dip*180.0f/kPi,gun,missile,
            speed,std::sqrt(Dot2(want,want)),dipWant*180.0f/kPi,cone,aimedLead,std::sqrt(Dot2(h.tgtVel,h.tgtVel)),losRate*180.0f/kPi,avoid.ahead,avoid.clear);
    }
}
}  // namespace

bool IsHelicopter(const void* vehicle) noexcept {
    if(!Readable(vehicle,8))return false;
    const auto vtable=At<const unsigned char*>(vehicle,0);
    for(auto rva:kHeliVtables)if(vtable==image+rva)return true;
    return false;
}

void HeliCrewed(const void* vehicle) noexcept {
    Heli* slot=Find(vehicle);
    if(!slot){slot=&helis[0];for(auto& h:helis)if(h.seen<slot->seen)slot=&h;}
    *slot=Heli{};slot->vehicle=vehicle;slot->crewedAt=slot->seen=GetTickCount64();
    // The flight gains slot 57 uses (docs/heli-input-re.md; set from the SGO, values not traced): per frame
    // v = damp*v + blend*(speedGain*stick - damp*v), so full stick tops out at blend*speedGain/(1-(1-blend)*damp).
    const auto c=static_cast<const unsigned char*>(vehicle);
    Log("HELI v=%p crewed: the plugin flies it; speedGain=%.4f blend=%.4f damp=%.4f maxTilt=%.3f maxYaw=%.4f",vehicle,
        At<float>(c,0x162C),At<float>(c,0x1630),At<float>(c,0x1614),At<float>(c,0x1640),At<float>(c,0x1634));
}

void HeliFrame(unsigned char* vehicle) noexcept {
    if(!profileOk || !cfg.heliPilot || vehicle[kDead])return;
    if(SeatCount(vehicle)==0 || SeatRider(SeatAt(vehicle,0))!=Rider::dummy)return;   // only NPC pilots
    Heli* h=Find(vehicle);
    if(!h){HeliCrewed(vehicle);h=Find(vehicle);}   // a mission-spawned NPC heli (CreateFriend): fly it too
    h->seen=GetTickCount64();
    bool playerAboard=false;
    for(unsigned i=1;i<SeatCount(vehicle);++i)playerAboard=playerAboard || SeatRider(SeatAt(vehicle,i))==Rider::player;
    Fly(*h,vehicle,playerAboard);
}

bool CheckHeliProfile() noexcept {
    __try {
        for(const auto& s:kHeliSignatures)if(!Matches(s.rva,s.bytes,s.size)){Log("HELI profile mismatch at %#zx",s.rva);return false;}
        profileOk=true;
        // Avoidance has its own check: without it the helis still fly, just blind.
        rayOk=Readable(image+kHitVtbl,0x28) && At<const unsigned char*>(image,kHitVtbl)==image+kHitSlot0 &&
              At<const unsigned char*>(image,kHitVtbl+0x20)==image+kHitAdd;
        for(const auto& s:kRaySignatures)rayOk=rayOk && Matches(s.rva,s.bytes,s.size);
        Log("HELI ray=%d (obstacle avoidance %s)",rayOk,rayOk ? "on" : "off: unexpected EDF.dll code");
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}  // namespace crew
