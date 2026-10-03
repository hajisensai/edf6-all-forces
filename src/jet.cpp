// NPC jets (docs/jet-plan.md). The game has no fighter, so a jet is a Vehicle506_Helicopter body (rigid
// body and .cas collision, HP, weapons on the "body" bone, the stock crash and wreck) spawned from a
// derived SGO (testrange/gen.py: edf6tr_jet_*), told apart by its speed gain k (veh+0x162C), which the
// derived SGO sets to kStrikeMark or kFighterMark (no stock heli is anywhere near). Its NPC pilot does
// nothing; the plugin flies it in two stages a frame:
//  - input (slot 55, from HeliFrame): the target, the guidance step (JetSteer: the velocity turns at most
//    kMaxG and speeds up or slows down at most kAccel/kDecel), and the fire bytes slot 57 reads
//    (0x2020 both guns, 0x2021 the missile; vehicle_weapon_setting puts them along the body's nose);
//  - physics (slot 57, after the stock code wrote the heli's velocity and spin): the body's linear
//    velocity (0x11B18F0) and an angular velocity (0x11B1760) that turns the nose onto that velocity,
//    banked into the turn.
// Roles: strike (dive attacks on ground targets) and fighter (flying targets first). Neither reloads; out
// of ammo, out of fuel (cfg.jetFuelSec) or below kWithdrawHp of its HP it leaves and is deleted.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "memory.h"
#include <cmath>

namespace crew {
namespace {
constexpr unsigned kHeli506=0x17DB238,kPhysics506=0x61B710;   // slot 57 of the 506
constexpr std::size_t kSlotPhysics=57;
constexpr std::size_t kSpeedGain=0x162C,kBody=0x1650;
constexpr float kStrikeMark=7001.0f,kFighterMark=7002.0f;
constexpr unsigned kSetLinearVelocity=0x11B18F0,kSetAngularVelocity=0x11B1760,kDelete=0x118A1B0;
constexpr std::size_t kInLateral=0x1540,kInThrottle=0x1544,kInForward=0x1548,kInW=0x154C,kInYaw=0x1550;
constexpr std::size_t kFireGun=0x2020,kFireMissile=0x2021;
constexpr std::size_t kHpMax=0x2F4,kHp=0x2F8;
constexpr std::size_t kSeatWeapons=0xC8,kSeatWeaponCount=0xD8,kHolderWeapon=0x10;
constexpr std::size_t kWeaponLockon=0x6B0,kWeaponSpeed=0x894,kWeaponAlive=0x898,kWeaponGravity=0x8E0,kWeaponAmmo=0xBE8;
constexpr std::int32_t kHoming=1;
// The stock input (0x6543A0) pushes the body under the ceiling *(*(image+kCeiling)+0x3C) every frame.
constexpr std::size_t kCeiling=0x20B2998,kCeilingY=0x3C;
constexpr float kPi=3.14159265f,kG=9.8f,kGravity=14.7f;

// Flight. Speeds m/s, heights m above the target (or the anchor: the player, or where it first flew).
constexpr float kCruise=70.0f,kAttack=85.0f,kMinSpeed=45.0f,kAccel=12.0f,kDecel=15.0f;
constexpr float kMaxG=5.0f;            // turn: at most this many g of lateral acceleration
constexpr float kAttGain=6.0f;         // 1/s: the nose closes on where it should point this fast
constexpr float kMaxOmega=4.0f;        // rad/s
constexpr float kMinAlt=25.0f;         // never lower over the ground than this
constexpr float kLookAhead=2.5f;       // s: the ground and the ceiling are checked this far ahead too
constexpr float kCeilingGap=12.0f;
constexpr float kStrikeAlt=160.0f,kFighterAlt=220.0f;
constexpr float kPatrolRadius=300.0f,kPatrolStep=60.0f;   // per jet, so they do not share one circle
constexpr float kTakeoffClear=30.0f;   // m over the ground: done taking off
// Strike: approach at kStrikeAlt; from kDiveStart out with the target within kDiveCone of the nose, dive
// onto the gun's lead point (kStrikeAlt over kDiveStart: ~20-30 deg), guns from kGunOpen in to kGunClose;
// pull out under kPullAlt over the target or kGunClose from it, climb back, fly on kExtendOut and turn in.
constexpr float kDiveStart=450.0f,kDiveCone=0.52f,kPullAlt=55.0f,kExtendOut=750.0f;
constexpr float kGunOpen=420.0f,kGunClose=110.0f;
constexpr float kGunCone=0.035f,kHitRadius=4.0f;   // rad (2 deg), or what puts kHitRadius on the target
constexpr float kMissileCone=0.2f,kMissileMin=120.0f,kMissileMax=500.0f;
constexpr ULONGLONG kMissileMs=2500,kPullMs=7000,kExtendMs=12000;
// Fighter: lead pursuit at the target's speed plus kChaseOver; closer than kOverrun it breaks off
// (extends kRunOutMs) so it does not ram or sit on its tail.
constexpr float kChaseOver=25.0f,kOverrun=70.0f,kFlyerClear=15.0f;
constexpr ULONGLONG kRunOutMs=3000;
constexpr float kStrikeRange=600.0f,kFighterRange=900.0f;   // m from the anchor it takes targets in
// Withdrawing it climbs toward the ceiling and flies away from the player at full speed; it is deleted
// only out there (never in front of the player): kGone from the player, or, held in by the map's edge,
// kGoneStuck after kStuckMs of withdrawing.
constexpr float kWithdrawHp=0.25f,kGone=1000.0f,kGoneStuck=600.0f,kWithdrawClimb=300.0f;
constexpr ULONGLONG kStuckMs=60000;
constexpr ULONGLONG kStaleMs=1500;   // a table entry not flown this long is another object's now

enum class Mode { takeoff, patrol, approach, dive, pull, extend, chase, runOut, withdraw };
const char* const kModeNames[]={"takeoff","patrol","approach","dive","pull","extend","chase","runOut","withdraw"};

struct Jet {
    unsigned char* vehicle;
    bool fighter;
    Mode mode;
    ULONGLONG bornAt,seen,modeAt,missileAt,loggedAt;
    LARGE_INTEGER last;
    float vel[3],omega[3];   // what the physics stage writes
    bool ready;              // vel/omega hold this frame's command
    float anchor[3];         // where it patrols when there is no player
    const void* target;
    bool flyer;
    float aim[3],tgtPrev[3],tgtVel[3];
    float out[3];            // extend / run-out direction
    bool reap;               // withdrawn: delete from another vehicle's update (JetReap)
    const char* why;         // why it withdrew
};
Jet jets[16]{};

float Dot(const float* a,const float* b) noexcept { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
float Len(const float* a) noexcept { return std::sqrt(Dot(a,a)); }
float Clamp(float v,float lo,float hi) noexcept { return v<lo ? lo : v>hi ? hi : v; }
void Cross(const float* a,const float* b,float* out) noexcept {
    const float c[3]={a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
    std::memcpy(out,c,12);
}
// Scales `a` to unit length; false (and `a` untouched) when it is too short.
bool Normalize(float* a) noexcept {
    const float l=Len(a);
    if(!std::isfinite(l) || l<1e-4f)return false;
    a[0]/=l;a[1]/=l;a[2]/=l;
    return true;
}
float HorizDist(const float* a,const float* b) noexcept {
    const float dx=a[0]-b[0],dz=a[2]-b[2];
    return std::sqrt(dx*dx+dz*dz);
}

bool IsJetVehicle(const unsigned char* v,bool* fighter) noexcept {
    if(!Readable(v,kSpeedGain+4) || At<const unsigned char*>(v,0)!=image+kHeli506)return false;
    const float k=At<float>(v,kSpeedGain);
    if(fighter)*fighter=k==kFighterMark;
    return k==kStrikeMark || k==kFighterMark;
}

Jet* FindJet(const void* v) noexcept {
    for(auto& j:jets)if(j.vehicle==v)return &j;
    return nullptr;
}

void SetMode(Jet& j,Mode m,ULONGLONG ms) noexcept {
    if(j.mode==m)return;
    if(cfg.debug)Log("JET v=%p %s -> %s",j.vehicle,kModeNames[static_cast<int>(j.mode)],kModeNames[static_cast<int>(m)]);
    j.mode=m;j.modeAt=ms;
}

// Metres of ground (terrain, buildings) under `p`, or -1 with none seen.
float Clearance(const float* p) noexcept {
    const float down[3]={p[0],p[1]-400.0f,p[2]};
    float hit[3];
    return MapRay(p,down,hit)>=0.0f ? p[1]-hit[1] : -1.0f;
}

float Ceiling() noexcept {
    const auto p=At<const unsigned char*>(image,kCeiling);
    if(!p || !Readable(p+kCeilingY,4))return 1e9f;
    const float y=At<float>(p,kCeilingY);
    return std::isfinite(y) ? y : 1e9f;
}

// The pilot's seat weapons: guns (straight, fastest round speed for the lead), the homing missile.
struct Arms { float gunSpeed,gunGravity,gunRange; std::int32_t guns,missiles; bool hasGun,hasMissile; };
Arms ReadArms(unsigned char* v) noexcept {
    Arms a{240.0f,0.0f,400.0f,0,0,false,false};
    if(SeatCount(v)==0)return a;
    const auto seat=SeatAt(v,0);
    const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
    const auto count=At<std::uint64_t>(seat,kSeatWeaponCount);
    if(count>8 || !Readable(holders,count*8))return a;
    for(std::uint64_t i=0;i<count;++i) {
        if(!Readable(holders[i],kHolderWeapon+8))continue;
        const auto w=At<const unsigned char*>(holders[i],kHolderWeapon);
        if(!Readable(w,kWeaponAmmo+4))continue;
        const std::int32_t ammo=At<std::int32_t>(w,kWeaponAmmo);
        if(At<std::int32_t>(w,kWeaponLockon)==kHoming){a.hasMissile=true;a.missiles+=ammo>0 ? ammo : 0;continue;}
        const float speed=At<float>(w,kWeaponSpeed)*60.0f,reach=At<float>(w,kWeaponSpeed)*static_cast<float>(At<std::int32_t>(w,kWeaponAlive));
        if(!std::isfinite(speed) || speed<=1.0f)continue;
        a.guns+=ammo>0 ? ammo : 0;
        if(!a.hasGun || speed>a.gunSpeed) {
            a.hasGun=true;a.gunSpeed=speed;
            const float g=At<float>(w,kWeaponGravity);
            a.gunGravity=std::isfinite(g) && g>0.0f ? g : 0.0f;
            a.gunRange=std::isfinite(reach) && reach>0.0f ? reach : 400.0f;
        }
    }
    return a;
}

// Where to point the guns to hit `aim` moving at `tv` from `from` (round flight time and drop).
void Lead(const float* from,const float* aim,const float* tv,const Arms& a,float* out) noexcept {
    std::memcpy(out,aim,12);
    for(int pass=0;pass<2;++pass) {
        const float d[3]={out[0]-from[0],out[1]-from[1],out[2]-from[2]};
        const float t=Len(d)/a.gunSpeed;
        for(int i=0;i<3;++i)out[i]=aim[i]+tv[i]*t;
        out[1]+=0.5f*a.gunGravity*kGravity*t*t;
    }
}

// The target: flyers first for a fighter (any enemy more than kFlyerClear over the ground), ground
// enemies first for a strike jet; nearest to the jet among those within `range` of `anchor`, the current
// one counting 100 m nearer.
struct Pick { Jet* j; const float* pos; const float* anchor; float range; const void* best; float score,aim[3]; bool flyer; };
void VisitTarget(void* ctx,const void* object,const float* p) noexcept {
    auto& k=*static_cast<Pick*>(ctx);
    const float d[3]={p[0]-k.anchor[0],p[1]-k.anchor[1],p[2]-k.anchor[2]};
    if(Dot(d,d)>k.range*k.range)return;
    const float clear=Clearance(p);
    const bool flyer=clear>kFlyerClear;
    const float f[3]={p[0]-k.pos[0],p[1]-k.pos[1],p[2]-k.pos[2]};
    float score=Len(f);
    if(object==k.j->target)score-=100.0f;
    if(flyer!=k.j->fighter)score+=2000.0f;   // the other role's kind of target: only with none of its own
    if(!k.best || score<k.score){k.best=object;k.score=score;std::memcpy(k.aim,p,12);k.flyer=flyer;}
}

// Turns the velocity toward `want` (unit) at most kMaxG, and its speed toward `speed`; fills the
// commanded velocity and the bank (the up the lift points along).
void JetSteer(Jet& j,const float* fwd,const float* want,float speed,float dt,float* up) noexcept {
    float dir[3]={j.vel[0],j.vel[1],j.vel[2]};
    float s=Len(dir);
    if(s<1.0f || !Normalize(dir)){std::memcpy(dir,fwd,12);s=s<1.0f ? s : 0.0f;}
    const float turn=kMaxG*kG/(s>kMinSpeed ? s : kMinSpeed)*dt;
    const float c=Clamp(Dot(dir,want),-1.0f,1.0f),angle=std::acos(c);
    float next[3];
    if(angle<=turn)std::memcpy(next,want,12);
    else {
        float axis[3];Cross(dir,want,axis);
        if(!Normalize(axis)){axis[0]=0;axis[1]=1;axis[2]=0;}
        float side[3];Cross(axis,dir,side);   // in the turn plane, toward `want`
        for(int i=0;i<3;++i)next[i]=dir[i]*std::cos(turn)+side[i]*std::sin(turn);
        Normalize(next);
    }
    const float ds=speed-s;
    s+=Clamp(ds,-kDecel*dt,kAccel*dt);
    // Lift: gravity held up plus the turn's acceleration (the change of direction times speed).
    float lift[3]={0,kG,0};
    for(int i=0;i<3;++i)lift[i]+=(next[i]-dir[i])/dt*s;
    const float along=Dot(lift,next);
    for(int i=0;i<3;++i)up[i]=lift[i]-next[i]*along;
    if(!Normalize(up)){up[0]=0;up[1]=1;up[2]=0;}
    for(int i=0;i<3;++i)j.vel[i]=next[i]*s;
}

// The angular velocity that turns the body's rows (right, up, forward at veh+0x60) onto `nose` and
// `up`: sin(angle) * axis from the three rows, times kAttGain.
void Attitude(Jet& j,const unsigned char* v,const float* nose,const float* up) noexcept {
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    const float* r=m;const float* u=m+4;const float* f=m+8;
    float rx[3];Cross(u,f,rx);
    const float hand=Dot(rx,r)>=0.0f ? 1.0f : -1.0f;   // right = hand * up x forward
    float right[3];Cross(up,nose,right);
    for(int i=0;i<3;++i)right[i]*=hand;
    float w[3]={0,0,0},c[3];
    Cross(r,right,c);for(int i=0;i<3;++i)w[i]+=c[i];
    Cross(u,up,c);for(int i=0;i<3;++i)w[i]+=c[i];
    Cross(f,nose,c);for(int i=0;i<3;++i)w[i]+=c[i];
    for(int i=0;i<3;++i)w[i]*=0.5f*kAttGain;
    const float l=Len(w);
    if(l>kMaxOmega)for(int i=0;i<3;++i)w[i]*=kMaxOmega/l;
    std::memcpy(j.omega,w,12);
}

// A unit direction to `goal` whose climb is limited to `maxClimb` (sine).
void Toward(const float* pos,const float* goal,float* out) noexcept {
    out[0]=goal[0]-pos[0];out[1]=goal[1]-pos[1];out[2]=goal[2]-pos[2];
    if(!Normalize(out)){out[0]=0;out[1]=0;out[2]=1;}
}

// Level flight (climbing or sinking toward `height`) along the horizontal `dir`.
void Level(const float* pos,const float* dir,float height,float* out) noexcept {
    float h[3]={dir[0],0,dir[2]};
    if(!Normalize(h)){h[0]=0;h[2]=1;}
    const float climb=Clamp((height-pos[1])/150.0f,-0.45f,0.6f);
    out[0]=h[0];out[1]=climb;out[2]=h[2];
    Normalize(out);
}

// Keeps `want` off the ground and under the ceiling, now and kLookAhead seconds out.
void Guard(const Jet& j,const float* pos,float* want) noexcept {
    const float s=Len(j.vel);
    const float ahead[3]={pos[0]+j.vel[0]*kLookAhead,pos[1]+j.vel[1]*kLookAhead,pos[2]+j.vel[2]*kLookAhead};
    const float probe[3]={ahead[0],pos[1]>ahead[1] ? pos[1] : ahead[1],ahead[2]};
    const float here=Clearance(pos),there=Clearance(probe);
    const float lowest=probe[1]-(there>=0.0f ? there : 1e9f);
    const float floorY=(here>=0.0f ? pos[1]-here : -1e9f)>lowest ? pos[1]-here : lowest;
    const float predicted=pos[1]+(s>1.0f ? j.vel[1]*kLookAhead : 0.0f);
    if(predicted<floorY+kMinAlt || pos[1]<floorY+kMinAlt) {
        const float need=Clamp((floorY+kMinAlt-predicted)/40.0f,0.3f,0.8f);
        if(want[1]<need){want[1]=need;Normalize(want);}
    }
    const float top=Ceiling()-kCeilingGap;
    if(predicted>top && want[1]>-0.15f){want[1]=-0.15f;Normalize(want);}
}

// The circle round the anchor, kPatrolRadius out (plus kPatrolStep per jet), counterclockwise.
void Patrol(const Jet& j,const float* pos,const float* anchor,float height,float* want) noexcept {
    const float r=kPatrolRadius+kPatrolStep*static_cast<float>(&j-jets);
    float out[3]={pos[0]-anchor[0],0,pos[2]-anchor[2]};
    float dist=Len(out);
    if(!Normalize(out)){out[0]=1;out[2]=0;dist=0.0f;}
    const float tangent[3]={out[2],0,-out[0]};
    const float pull=Clamp((r-dist)/r,-1.5f,1.5f);
    const float dir[3]={tangent[0]+out[0]*pull,0,tangent[2]+out[2]*pull};
    Level(pos,dir,height,want);
}

// Strike attack (see kDiveStart). Returns whether the guns may fire (diving at the lead point).
bool Strike(Jet& j,const float* pos,const float* lead,float height,ULONGLONG ms,float* want,float* speed) noexcept {
    const float dh=HorizDist(pos,lead),over=pos[1]-lead[1];
    const float to[3]={lead[0]-pos[0],0,lead[2]-pos[2]};
    float vdir[3]={j.vel[0],0,j.vel[2]};
    if(!Normalize(vdir)){vdir[0]=to[0];vdir[2]=to[2];Normalize(vdir);}
    float toN[3]={to[0],0,to[2]};Normalize(toN);
    const float off=std::acos(Clamp(Dot(vdir,toN),-1.0f,1.0f));
    *speed=kAttack;
    switch(j.mode) {
    case Mode::dive:
        if(over<kPullAlt || dh<kGunClose*0.7f || Len(to)<kGunClose){SetMode(j,Mode::pull,ms);break;}
        Toward(pos,lead,want);
        return true;
    case Mode::pull:
        if(pos[1]>=height-20.0f || ms-j.modeAt>kPullMs) {
            std::memcpy(j.out,vdir,12);SetMode(j,Mode::extend,ms);break;
        }
        want[0]=vdir[0];want[1]=0.7f;want[2]=vdir[2];Normalize(want);
        return false;
    case Mode::extend:
        if(dh>kExtendOut || ms-j.modeAt>kExtendMs){SetMode(j,Mode::approach,ms);break;}
        Level(pos,j.out,height,want);
        return false;
    default:
        if(j.mode!=Mode::approach)SetMode(j,Mode::approach,ms);
        break;
    }
    // Approach: at the target at height; dive once in the window, else fly out and come round.
    if(dh<=kDiveStart && dh>kGunClose*2.0f && off<kDiveCone && over>kPullAlt+30.0f){SetMode(j,Mode::dive,ms);Toward(pos,lead,want);return true;}
    if(dh<=kDiveStart*0.8f && off>kDiveCone){std::memcpy(j.out,vdir,12);SetMode(j,Mode::extend,ms);Level(pos,vdir,height,want);return false;}
    Level(pos,to,height,want);
    *speed=kCruise;
    return false;
}

// Air-to-air: lead pursuit; breaks off when it overruns.
bool Chase(Jet& j,const float* pos,const float* lead,ULONGLONG ms,float* want,float* speed) noexcept {
    const float d[3]={lead[0]-pos[0],lead[1]-pos[1],lead[2]-pos[2]};
    if(j.mode==Mode::runOut) {
        if(ms-j.modeAt<kRunOutMs){std::memcpy(want,j.out,12);*speed=kAttack;return false;}
        SetMode(j,Mode::chase,ms);
    }
    if(j.mode!=Mode::chase)SetMode(j,Mode::chase,ms);
    if(Len(d)<kOverrun) {
        float dir[3]={j.vel[0],j.vel[1]+Len(j.vel)*0.3f,j.vel[2]};
        if(!Normalize(dir)){dir[0]=0;dir[1]=0.3f;dir[2]=1;Normalize(dir);}
        std::memcpy(j.out,dir,12);SetMode(j,Mode::runOut,ms);
        std::memcpy(want,dir,12);*speed=kAttack;return false;
    }
    Toward(pos,lead,want);
    const float s=Len(j.tgtVel)+kChaseOver;
    *speed=Clamp(s,kMinSpeed+10.0f,kAttack);
    return true;
}

void Withdraw(Jet& j,const char* why,ULONGLONG ms) noexcept {
    if(j.mode==Mode::withdraw)return;
    j.why=why;SetMode(j,Mode::withdraw,ms);
    Log("JET v=%p withdraws: %s",j.vehicle,why);
}

// The fire bytes: guns while the nose is on the lead point within reach, the missile on a rough aim.
void Fire(Jet& j,unsigned char* v,const float* pos,const float* nose,const float* lead,bool gunsOk,const Arms& a,ULONGLONG ms) noexcept {
    bool gun=false,missile=false;
    if(j.target && cfg.heliFire && j.mode!=Mode::withdraw) {
        const float d[3]={lead[0]-pos[0],lead[1]-pos[1],lead[2]-pos[2]};
        const float dist=Len(d);
        const float miss=dist>1.0f ? std::acos(Clamp(Dot(d,nose)/dist,-1.0f,1.0f)) : 0.0f;
        const float wide=dist>1.0f ? std::atan(kHitRadius/dist) : 1.0f;
        const float reach=a.gunRange<kGunOpen ? a.gunRange : kGunOpen;
        gun=gunsOk && a.guns>0 && dist<reach && dist>kGunClose*0.8f && miss<(wide>kGunCone ? wide : kGunCone) && !BurstHitsPlayer(pos,lead);
        missile=a.missiles>0 && dist>kMissileMin && dist<kMissileMax && miss<kMissileCone && ms-j.missileAt>kMissileMs && !BurstHitsPlayer(pos,lead);
        if(missile)j.missileAt=ms;
    }
    v[kFireGun]=gun;v[kFireMissile]=missile;
}

void JetLog(const Jet& j,const unsigned char* v,const float* pos,const Arms& a,float speed,float clear,ULONGLONG ms) noexcept {
    const float hp=At<float>(v,kHp),hpMax=At<float>(v,kHpMax);
    const float d=j.target ? std::sqrt((j.aim[0]-pos[0])*(j.aim[0]-pos[0])+(j.aim[1]-pos[1])*(j.aim[1]-pos[1])+(j.aim[2]-pos[2])*(j.aim[2]-pos[2])) : 0.0f;
    Log("JET v=%p %s %s y=%.0f clear=%.0f ceil=%.0f spd=%.0f/%.0f vy=%.1f target=%p%s dist=%.0f guns=%d msl=%d hp=%.0f/%.0f fuel=%.0fs fire=%d/%d",
        v,j.fighter ? "fighter" : "strike",kModeNames[static_cast<int>(j.mode)],pos[1],clear,Ceiling(),Len(j.vel),speed,j.vel[1],
        j.target,j.flyer ? "(air)" : "",d,a.guns,a.missiles,hp,hpMax,
        static_cast<float>(cfg.jetFuelSec)-static_cast<float>(ms-j.bornAt)*0.001f,v[kFireGun],v[kFireMissile]);
}

using PhysicsFn=void(__fastcall*)(void*);   // slot 57: void(vehicle)
PhysicsFn nextPhysics=nullptr;
using SetVecFn=void(*)(void*,const float*);
using DeleteFn=void(*)(void*);
using KickFn=void(*)(void*,void*);

// Slot 57 of the 506, after the stock step: the jet's velocity and spin replace the heli's.
void __fastcall PhysicsHook(void* vehicle) {
    nextPhysics(vehicle);
    __try {
        auto v=static_cast<unsigned char*>(vehicle);
        Jet* j=FindJet(v);
        if(!j || !j->ready || v[kDead] || GetTickCount64()-j->seen>200 || !IsJetVehicle(v,nullptr))return;
        const auto body=At<void*>(v,kBody);
        if(!body)return;
        alignas(16) float lin[4]={j->vel[0],j->vel[1],j->vel[2],0.0f},ang[4]={j->omega[0],j->omega[1],j->omega[2],0.0f};
        reinterpret_cast<SetVecFn>(image+kSetLinearVelocity)(body,lin);
        reinterpret_cast<SetVecFn>(image+kSetAngularVelocity)(body,ang);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

bool physicsOk=false;
const unsigned char kPhysicsSig[]={0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0xE8};   // 0x61B710: push rbx; sub rsp,20h; mov rbx,rcx; call
// Both jump through the Havok world interface; only the slot differs (0xA8 linear, 0xB0 angular).
const unsigned char kSetLinSig[]={0x48,0x8B,0x81,0x00,0x01,0x00,0x00,0x4C,0x8B,0xC2,0x8B,0x91,0xF0,0x00,0x00,0x00,0x45,0x33,0xC9,0x4C,0x8B,0x50,0x58,0x49,0x8B,0x42,0x18,0x49,0x8D,0x4A,0x18,0x48,0xFF,0xA0,0xA8,0x00,0x00};
const unsigned char kSetAngSig[]={0x48,0x8B,0x81,0x00,0x01,0x00,0x00,0x4C,0x8B,0xC2,0x8B,0x91,0xF0,0x00,0x00,0x00,0x45,0x33,0xC9,0x4C,0x8B,0x50,0x58,0x49,0x8B,0x42,0x18,0x49,0x8D,0x4A,0x18,0x48,0xFF,0xA0,0xB0,0x00,0x00};
const unsigned char kDeleteSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x60,0x48};
}  // namespace

bool IsJet(const void* vehicle) noexcept {
    return IsJetVehicle(static_cast<const unsigned char*>(vehicle),nullptr);
}

void JetFrame(unsigned char* v) noexcept {
    if(!physicsOk)return;
    const ULONGLONG ms=GetTickCount64();
    Jet* j=FindJet(v);
    if(j && ms-j->seen>kStaleMs)*j=Jet{};   // a new object at a dead jet's address
    if(j && !j->vehicle)j=nullptr;
    LARGE_INTEGER now,freq;QueryPerformanceCounter(&now);QueryPerformanceFrequency(&freq);
    const float* pos=reinterpret_cast<const float*>(v+kPosition);
    if(!j) {
        j=&jets[0];
        for(auto& o:jets)if(!o.vehicle || o.seen<j->seen)j=&o;
        if(j->vehicle && !j->reap)Log("JET table full: dropping v=%p",j->vehicle);
        *j=Jet{};j->vehicle=v;IsJetVehicle(v,&j->fighter);j->bornAt=j->modeAt=ms;j->last=now;
        std::memcpy(j->anchor,pos,12);j->mode=Mode::takeoff;
        Log("JET v=%p crewed: %s, hp=%.0f, ceiling=%.0f",v,j->fighter ? "fighter" : "strike",At<float>(v,kHp),Ceiling());
    }
    j->seen=ms;
    const float dt=Clamp(static_cast<float>(now.QuadPart-j->last.QuadPart)/static_cast<float>(freq.QuadPart),0.004f,0.1f);
    j->last=now;
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    float nose[3]={m[8],m[9],m[10]};
    if(!Normalize(nose)){nose[0]=0;nose[1]=0;nose[2]=1;}
    // The stock input stays out of it: rotor spinning, no stick.
    Put<float>(v,kInLateral,0.0f);Put<float>(v,kInForward,0.0f);Put<float>(v,kInYaw,0.0f);
    Put<float>(v,kInThrottle,1.0f);Put<float>(v,kInW,1.0f);

    const Arms arms=ReadArms(v);
    const bool follow=player.at && ms-player.at<10000;
    const float* anchor=follow ? player.pos : j->anchor;
    const float hp=At<float>(v,kHp),hpMax=At<float>(v,kHpMax);
    if(ms-j->bornAt>static_cast<ULONGLONG>(cfg.jetFuelSec)*1000)Withdraw(*j,"fuel",ms);
    else if(hpMax>0.0f && hp<hpMax*kWithdrawHp)Withdraw(*j,"damaged",ms);
    else if(arms.guns<=0 && arms.missiles<=0 && (arms.hasGun || arms.hasMissile))Withdraw(*j,"out of ammo",ms);

    // The target and its motion.
    Pick pick{j,pos,anchor,j->fighter ? kFighterRange : kStrikeRange,nullptr,0.0f,{},false};
    if(j->mode!=Mode::withdraw && j->mode!=Mode::takeoff)VisitEnemies(v,&VisitTarget,&pick);
    if(pick.best) {
        const bool same=pick.best==j->target;
        for(int i=0;i<3;++i) {
            const float raw=(pick.aim[i]-j->tgtPrev[i])/dt;
            j->tgtVel[i]=same && std::fabs(raw)<80.0f ? j->tgtVel[i]+(raw-j->tgtVel[i])*0.2f : 0.0f;
        }
        std::memcpy(j->tgtPrev,pick.aim,12);std::memcpy(j->aim,pick.aim,12);
        j->target=pick.best;j->flyer=pick.flyer;
    } else j->target=nullptr;
    float lead[3];
    if(j->target)Lead(pos,j->aim,j->tgtVel,arms,lead);
    else std::memcpy(lead,pos,12);

    // Guidance.
    const float clear=Clearance(pos);
    const float base=j->target && !j->flyer ? j->aim[1] : anchor[1];
    const float height=base+(j->fighter ? kFighterAlt : kStrikeAlt);
    float want[3]={nose[0],0,nose[2]},speed=kCruise;
    bool gunsOk=false;
    switch(j->mode) {
    case Mode::takeoff:
        want[0]=nose[0];want[1]=0.6f;want[2]=nose[2];Normalize(want);
        if(clear>kTakeoffClear || clear<0.0f)SetMode(*j,Mode::patrol,ms);
        break;
    case Mode::withdraw: {
        float away[3]={pos[0]-anchor[0],0,pos[2]-anchor[2]};
        if(!Normalize(away)){away[0]=nose[0];away[2]=nose[2];}
        const float top=Ceiling()-kCeilingGap*2.0f,climb=anchor[1]+kWithdrawClimb;
        Level(pos,away,climb<top ? climb : top,want);speed=kAttack+10.0f;
        const float d[3]={pos[0]-anchor[0],pos[1]-anchor[1],pos[2]-anchor[2]};
        const float gone=Len(d);
        if(gone>kGone || (ms-j->modeAt>kStuckMs && gone>kGoneStuck)){
            if(!j->reap)Log("JET v=%p out of sight (%.0f m from the player): deleting",v,gone);
            j->reap=true;
        }
        break;
    }
    default:
        if(!j->target) {
            if(j->mode!=Mode::patrol)SetMode(*j,Mode::patrol,ms);
            Patrol(*j,pos,anchor,height,want);
        } else if(j->flyer)gunsOk=Chase(*j,pos,lead,ms,want,&speed);
        else gunsOk=Strike(*j,pos,lead,height,ms,want,&speed);
        break;
    }
    Guard(*j,pos,want);
    float up[3];
    JetSteer(*j,nose,want,speed,dt,up);
    float dir[3]={j->vel[0],j->vel[1],j->vel[2]};
    if(!Normalize(dir))std::memcpy(dir,nose,12);
    Attitude(*j,v,dir,up);
    j->ready=true;
    Fire(*j,v,pos,nose,lead,gunsOk,arms,ms);
    if(cfg.debug && ms-j->loggedAt>1000){j->loggedAt=ms;JetLog(*j,v,pos,arms,speed,clear,ms);}
}

void JetReap(const void* self) noexcept {
    for(auto& j:jets) {
        if(!j.vehicle || !j.reap || j.vehicle==self)continue;
        unsigned char* v=j.vehicle;
        // Only one flown a moment ago: a jet shot down meanwhile is the game's to clean up (and may be gone).
        const bool live=GetTickCount64()-j.seen<200;
        j=Jet{};
        if(!live || !Readable(v,kSeats+8) || v[kDead] || !IsJetVehicle(v,nullptr))continue;
        if(SeatCount(v)>0 && SeatRider(SeatAt(v,0))==Rider::dummy)reinterpret_cast<KickFn>(image+kSeatKick)(v,SeatAt(v,0));
        reinterpret_cast<DeleteFn>(image+kDelete)(v);
        Log("JET v=%p gone (deleted)",v);
    }
}

bool InstallJets() noexcept {
    __try {
        const bool sig=Matches(kPhysics506,kPhysicsSig,sizeof(kPhysicsSig)) && Matches(kSetLinearVelocity,kSetLinSig,sizeof(kSetLinSig)) &&
                       Matches(kSetAngularVelocity,kSetAngSig,sizeof(kSetAngSig)) && Matches(kDelete,kDeleteSig,sizeof(kDeleteSig));
        if(!sig){Log("JET profile mismatch: jets off");return false;}
        const auto slot=reinterpret_cast<void**>(image+kHeli506)+kSlotPhysics;
        void* const current=*slot;
        if(current!=image+kPhysics506)Log("JET physics: chaining onto %p (another plugin)",current);
        nextPhysics=reinterpret_cast<PhysicsFn>(current);
        physicsOk=PatchVtableSlot(slot,current,reinterpret_cast<void*>(&PhysicsHook));
        Log("HOOK jets physics=%d",physicsOk);
        return physicsOk;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}  // namespace crew
