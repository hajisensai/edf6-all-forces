// Helicopter pilot. EDF.dll has no helicopter flight AI (docs/heli-input-re.md): a heli with the
// stock NPC rider in seat 0 just sits there. So for the helicopters this plugin crewed, after the
// stock input (slot 55) has filled the input block, we overwrite it:
//   veh+0x1540 lateral  (+ = along heading row 0)     veh+0x1548 forward (+ = along heading row 2)
//   veh+0x1544 throttle (target rotor speed, 0..1)    veh+0x154C 1.0     veh+0x1550 yaw rate
//   byte veh+0x2020 both gatlings, byte veh+0x2021 missile (506 and 409; 410 fires per gunner seat)
// Slot 57 (physics + weapons) consumes them the same frame.
//
// Flight: the heli holds a point heliFollow metres from the player (on the enemy side when it is
// engaging), heliHeight metres up. Horizontal = PD on position projected onto the heading rows, so its
// signs are right by construction. Altitude = vertical-speed loop on the throttle with an integrator
// that learns the hover throttle (the rotor lags the throttle by seconds). The world sign of yaw was
// not provable statically, so it is learned online from how the heading actually turns.
// When the player stands still for heliLandMs it lands next to them and stays down while they are
// close, so they can walk up and bump the NPC pilot.
// With the player aboard (in a gunner seat) it does not follow itself: it holds position and closes in
// on the nearest enemy instead.
#include "crew.h"
#include "memory.h"
#include <cmath>

namespace crew {
namespace {
constexpr unsigned kHeliVtables[]={0x17DB238,0x17DEF98,0x17DF338,0x17DF790};   // 506, 409, 410, base
constexpr unsigned kHeli410=0x17DF338;
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

struct Signature { std::size_t rva; unsigned char bytes[16]; std::size_t size; };
const Signature kHeliSignatures[]={
    {0x6543A0,{0x48,0x8B,0xC4,0x48,0x89,0x58,0x08,0x48,0x89,0x68,0x10,0x48,0x89,0x70,0x18,0x48},16},   // base input
    {0x61B8F0,{0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x6C,0x24,0x20,0x56,0x48,0x83,0xEC,0x20,0x48},16},   // 506 input
    {0x65451A,{0x4C,0x89,0xB7,0x40,0x15,0x00,0x00,0x44,0x89,0xB7,0x48,0x15,0x00,0x00,0x48,0xC7},16},   // input block reset
    {0x61B71E,{0x80,0xBB,0x20,0x20,0x00,0x00,0x00},7},                                                 // 506 gatlings read
    {0x61B743,{0x80,0xBB,0x21,0x20,0x00,0x00,0x00},7},                                                 // 506 missile read
};
bool profileOk=false;

struct Heli {
    const void* vehicle;
    ULONGLONG crewedAt,seen,loggedAt,missileAt,targetAt;
    LARGE_INTEGER last;
    float prev[3],vel[3];
    float hover;          // learned hover throttle
    float prevHeading,lastYaw;
    int yawSign,votes;    // +1: a positive yaw input increases atan2(fwd.x, fwd.z)
    bool yawLocked,started;
    const void* target;
    float hold[3];        // where it holds when it has nobody to follow
};
Heli helis[16]{};
// The player's last move: they count as standing still once within 3 m of `still` since `stillAt`.
float still[3]{};ULONGLONG stillAt=0;

void TrackPlayerStill() noexcept {
    const float d[3]={player.pos[0]-still[0],player.pos[1]-still[1],player.pos[2]-still[2]};
    if(!stillAt || d[0]*d[0]+d[1]*d[1]+d[2]*d[2]>9.0f){std::memcpy(still,player.pos,12);stillAt=GetTickCount64();}
}

Heli* Find(const void* vehicle) noexcept {
    for(auto& h:helis)if(h.vehicle==vehicle)return &h;
    return nullptr;
}

float Dot2(const float* a,const float* b) noexcept { return a[0]*b[0]+a[2]*b[2]; }
float Clamp(float v,float lo,float hi) noexcept { return v<lo ? lo : v>hi ? hi : v; }
float Wrap(float a) noexcept { while(a>kPi)a-=2*kPi; while(a<-kPi)a+=2*kPi; return a; }

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

// The enemy lock point to engage: the current target while it stays in range, else the nearest
// one within `range` of `around`. Returns false with none.
bool PickTarget(Heli& h,const unsigned char* v,const float* around,float range,float* aim) noexcept {
    auto team=At<std::int32_t>(v,kTeam);
    if(team==kTeamVehicle)team=player.team;   // nobody's vehicle: fight the player's enemies
    const auto relation=Relations(team);
    const auto registry=At<const unsigned char*>(image,kRegistry);
    if(!relation || !Readable(registry,kRegList+0x10))return false;
    const auto head=At<const unsigned char*>(registry,kRegList);
    if(!Readable(head,0x10))return false;
    float best=range*range,bestAim[3]{};const void* bestObject=nullptr;bool kept=false;
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
        const float d2=d[0]*d[0]+d[1]*d[1]+d[2]*d[2];
        if(d2>range*range)continue;
        if(object==h.target && !kept){kept=true;bestObject=object;std::memcpy(bestAim,a,12);best=-1.0f;continue;}
        if(!kept && d2<best){best=d2;bestObject=object;std::memcpy(bestAim,a,12);}
    }
    if(!bestObject)return false;
    if(bestObject!=h.target)h.targetAt=GetTickCount64();
    h.target=bestObject;std::memcpy(aim,bestAim,12);
    return true;
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

void Fly(Heli& h,unsigned char* v,bool playerAboard) noexcept {
    LARGE_INTEGER now,freq;QueryPerformanceCounter(&now);QueryPerformanceFrequency(&freq);
    const float* pos=reinterpret_cast<const float*>(v+kPosition);
    float right[3],fwd[3];
    if(!Row(v,kHeadRight,right) || !Row(v,kHeadForward,fwd))return;
    const float heading=std::atan2(fwd[0],fwd[2]);
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
    if(!grounded && !h.yawLocked && std::fabs(h.lastYaw)>0.3f && std::fabs(turned)>0.0005f) {
        h.votes+=(turned>0)==(h.lastYaw*static_cast<float>(h.yawSign)>0) ? 1 : -1;   // did it turn the way we meant?
        if(h.votes<=-15){h.yawSign=-h.yawSign;h.votes=0;Log("HELI v=%p yaw sign flipped to %d",v,h.yawSign);}
        else if(h.votes>=30){h.yawLocked=true;Log("HELI v=%p yaw sign locked at %d",v,h.yawSign);}
    }

    // Where to be.
    const ULONGLONG ms=GetTickCount64();
    const bool follow=!playerAboard && player.at && ms-player.at<10000;
    if(follow)TrackPlayerStill();
    const float toPlayer[3]={player.pos[0]-pos[0],0,player.pos[2]-pos[2]};
    const bool byPlayer=follow && Dot2(toPlayer,toPlayer)<kBoardRange*kBoardRange;
    // Land by a player who stands still (so they can walk up and take it over), and stay down while
    // they are next to it.
    const bool land=follow && cfg.heliLandMs && (ms-stillAt>cfg.heliLandMs || (grounded && byPlayer));
    const float* anchor=follow ? player.pos : pos;
    float aim[3]{};
    const bool engage=PickTarget(h,v,anchor,cfg.heliRange,aim);
    float goal[3];
    if(follow) {
        // heliFollow metres from the player: towards the enemy when engaging, else on our current bearing.
        float dir[3]={(engage ? aim[0] : pos[0])-player.pos[0],0,(engage ? aim[2] : pos[2])-player.pos[2]};
        float len=std::sqrt(Dot2(dir,dir));
        if(len<1.0f){dir[0]=-fwd[0];dir[2]=-fwd[2];len=1.0f;}
        goal[0]=player.pos[0]+dir[0]/len*cfg.heliFollow;goal[2]=player.pos[2]+dir[2]/len*cfg.heliFollow;
        goal[1]=player.pos[1]+cfg.heliHeight;
        if(land) {
            const float r=cfg.heliFollow<kLandDistance ? cfg.heliFollow : kLandDistance;
            goal[0]=player.pos[0]+dir[0]/len*r;goal[2]=player.pos[2]+dir[2]/len*r;
            goal[1]=player.pos[1]-10.0f;   // below the ground: it descends until it touches down
        }
        std::memcpy(h.hold,pos,12);
    } else if(engage) {
        // Gunship: close to heliStandoff metres of the target, heliHeight above it.
        float dir[3]={pos[0]-aim[0],0,pos[2]-aim[2]};
        float len=std::sqrt(Dot2(dir,dir));
        if(len<1.0f){dir[0]=-fwd[0];dir[2]=-fwd[2];len=1.0f;}
        goal[0]=aim[0]+dir[0]/len*cfg.heliStandoff;goal[2]=aim[2]+dir[2]/len*cfg.heliStandoff;
        goal[1]=aim[1]+cfg.heliHeight;
        std::memcpy(h.hold,goal,12);
    } else {
        std::memcpy(goal,h.hold,12);
    }

    // Horizontal: PD on position, projected onto the heading rows.
    float c[3]={(goal[0]-pos[0])*cfg.heliMoveGain-h.vel[0]*cfg.heliBrakeGain,0,
                (goal[2]-pos[2])*cfg.heliMoveGain-h.vel[2]*cfg.heliBrakeGain};
    const float cl=std::sqrt(Dot2(c,c));
    if(cl>1.0f){c[0]/=cl;c[2]/=cl;}

    // Vertical: climb-rate loop on the throttle; the integrator settles on the hover throttle.
    const float climb=Clamp((goal[1]-pos[1])*0.3f,-4.0f,5.0f);
    const float err=climb-h.vel[1];
    h.hover=Clamp(h.hover+err*cfg.heliHoverLearn*dt,0.1f,1.0f);
    float throttle=Clamp(h.hover+err*cfg.heliClimbGain,0.0f,1.0f);
    if(land && grounded){throttle=0.0f;h.hover=0.1f;c[0]=c[2]=0.0f;}

    // Yaw: face the target, else where it is going, else the player.
    float face[3]={0,0,0};
    if(engage){face[0]=aim[0]-pos[0];face[2]=aim[2]-pos[2];}
    else if(std::sqrt(Dot2(c,c))>0.3f){face[0]=c[0];face[2]=c[2];}
    else if(follow){face[0]=player.pos[0]-pos[0];face[2]=player.pos[2]-pos[2];}
    const float forward=Clamp(Dot2(c,fwd),-1.0f,1.0f),lateral=Clamp(Dot2(c,right),-1.0f,1.0f);
    float yaw=0.0f,off=kPi;
    if(Dot2(face,face)>1.0f) {
        off=Wrap(std::atan2(face[0],face[2])-heading);
        yaw=Clamp(off*1.5f,-1.0f,1.0f)*static_cast<float>(h.yawSign);
    }
    h.lastYaw=yaw;

    Put<float>(v,kInLateral,lateral);Put<float>(v,kInForward,forward);Put<float>(v,kInThrottle,throttle);
    Put<float>(v,kInW,1.0f);Put<float>(v,kInYaw,yaw);

    // Fire: lined up, in range, not too steep, not through the player.
    bool gun=false,missile=false;float dist=0.0f;
    if(engage && cfg.heliFire && !grounded && !land) {
        const float d[3]={aim[0]-pos[0],aim[1]-pos[1],aim[2]-pos[2]};
        const float flat=std::sqrt(Dot2(d,d));dist=std::sqrt(flat*flat+d[1]*d[1]);
        const bool lined=std::fabs(off)<cfg.heliFireCone*kPi/180.0f && std::atan2(-d[1],flat)<0.7f;
        gun=lined && dist<cfg.heliRange && !PlayerInLine(pos,aim);
        missile=gun && cfg.heliMissile && dist>50.0f && ms-h.missileAt>cfg.heliMissileMs;
        if(missile)h.missileAt=ms;
    }
    if(At<const unsigned char*>(v,0)!=image+kHeli410) {   // 410 fires through per-gunner-seat blocks
        v[kFireGun]=gun;v[kFireMissile]=missile;
    }

    if(cfg.debug && ms-h.loggedAt>1000) {
        h.loggedAt=ms;
        Log("HELI v=%p %s y=%.1f goal=%.1f vy=%.2f thr=%.3f hover=%.3f rotor=%.3f fwd=%.2f lat=%.2f yaw=%.2f sign=%d%s votes=%d dGoal=%.0f ground=%d target=%p dist=%.0f off=%.0fdeg gun=%d msl=%d",
            v,land ? "land" : follow ? "follow" : engage ? "gunship" : "hold",pos[1],goal[1],h.vel[1],throttle,h.hover,At<float>(v,kRotor),
            forward,lateral,yaw,h.yawSign,h.yawLocked ? "(locked)" : "",h.votes,
            std::sqrt((goal[0]-pos[0])*(goal[0]-pos[0])+(goal[2]-pos[2])*(goal[2]-pos[2])),grounded,
            engage ? h.target : nullptr,dist,off*180.0f/kPi,gun,missile);
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
    Log("HELI v=%p crewed: the plugin flies it",vehicle);
}

void HeliFrame(unsigned char* vehicle) noexcept {
    if(!profileOk || !cfg.heliPilot || vehicle[kDead])return;
    Heli* h=Find(vehicle);
    if(!h || SeatCount(vehicle)==0 || SeatRider(SeatAt(vehicle,0))!=Rider::dummy)return;   // only our NPC pilots
    h->seen=GetTickCount64();
    bool playerAboard=false;
    for(unsigned i=1;i<SeatCount(vehicle);++i)playerAboard=playerAboard || SeatRider(SeatAt(vehicle,i))==Rider::player;
    Fly(*h,vehicle,playerAboard);
}

bool CheckHeliProfile() noexcept {
    __try {
        for(const auto& s:kHeliSignatures)if(!Matches(s.rva,s.bytes,s.size)){Log("HELI profile mismatch at %#zx",s.rva);return false;}
        profileOk=true;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}  // namespace crew
