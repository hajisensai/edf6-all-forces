// The round models of the vehicles' impact points (src/rounds.h), checked without the game (docs/hud-re.md §7):
//  - Arc against sight::RoundAfter's closed form (the per-frame step v += drop, p += v);
//  - Motor (MissileBullet01 flying straight) against the sums its update gives in closed form: before ignition the
//    inherited velocity h_n = k h_(n-1) + d, so the drop by ignition (a frames) is d/(1-k) (a - k (1 - k^a)/(1 - k)) + a d;
//    the motor's speed climbs CP[4] a frame from the ignition to CP[6] and stays; after it the drop a frame is d and
//    the inherited share, 0.9 a frame of what it had;
//  - FirstHit against flat ground (a ray onto y = 0): where the arc / the rocket cross it, within the segment's chord;
//  - the stock weapons' numbers (Root.cpk, docs/hud-re.md §7): the 409's rockets, the Depth Crawler's and the
//    Begaruta's, against the straight line the helis' sight drew before: where each lands from 50 m up, level, and
//    how far under its line it is when it gets there.
// Exit code 1 when a check fails. Built on request only: cmake --build build --target rounds_check && build\rounds_check.exe
#include "../src/rounds.h"
#include "../src/sight.h"
#include "../src/roundaim.h"
#include <cmath>
#include <cstdio>

namespace {
using namespace crew;
constexpr float kGravity=14.7f;   // m/s^2: the world's (autoturret/docs/re-notes.md "Gravity": measured about 14.7)
int failed=0;

void Expect(bool ok,const char* what,double got,double want) {
    std::printf("%s  %-58s got %.4f want %.4f\n",ok ? "ok  " : "FAIL",what,got,want);
    if(!ok)++failed;
}

// A ray onto the ground y = 0 (MapRay's contract: metres from a to the hit, < 0 none).
float Ground(const float* a,const float* b,float* hit) {
    if(!(a[1]>=0.0f && b[1]<0.0f))return -1.0f;
    const float t=a[1]/(a[1]-b[1]);
    for(int i=0;i<3;++i)hit[i]=a[i]+(b[i]-a[i])*t;
    const float d[3]={hit[0]-a[0],hit[1]-a[1],hit[2]-a[2]};
    return std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
}

void ArcChecks() {
    const float pos[3]={0.0f,50.0f,0.0f},vel[3]={0.0f,0.3f,6.0f},drop[3]={0.0f,-kGravity/3600.0f,0.0f};
    rounds::Arc a{{vel[0],vel[1],vel[2]},{drop[0],drop[1],drop[2]}};
    float p[3]={pos[0],pos[1],pos[2]},closed[3];
    float worst=0.0f;
    for(int n=1;n<=300;++n) {
        rounds::Step(a,p);
        sight::RoundAfter(pos,vel,drop,static_cast<float>(n),closed);
        for(int i=0;i<3;++i)worst=std::fmax(worst,std::fabs(p[i]-closed[i]));
    }
    Expect(worst<5e-3f,"arc: the step against RoundAfter (300 frames, max error m)",worst,0.0);
    // Flat ground: the closed-form crossing (the larger root of y + n vy + d n(n+1)/2 = 0), against FirstHit's.
    rounds::Arc b{{vel[0],vel[1],vel[2]},{drop[0],drop[1],drop[2]}};
    float hit[3],end[3],took=0.0f;
    const bool landed=rounds::FirstHit(b,pos,600,15,1e9f,&Ground,hit,end,&took);
    const double A=0.5*drop[1],B=vel[1]+0.5*drop[1],C=pos[1];
    const double n=(-B-std::sqrt(B*B-4.0*A*C))/(2.0*A);
    Expect(landed && std::fabs(hit[2]-n*vel[2])<0.5*vel[2],"arc: FirstHit lands where the arc meets y = 0 (z, m)",hit[2],n*vel[2]);
    Expect(std::fabs(took-n)<0.5,"arc: ...after the frames it flies",took,n);
}

struct Rocket { const char* name; float speed,factor,accel,top,keepInh; int ignite,alive; };
// Root.cpk WEAPON/*.SGO (docs/hud-re.md §7): AmmoSpeed, AmmoGravityFactor, Ammo_CustomParameter [4], [6], [7][1], [7][0],
// AmmoAlive; [7][2] not given (1).
const Rocket kRockets[]={
    {"V_409HELI_MISSILE01 (Nereid rockets)",0.5f,1.0f,0.03f,10.0f,0.98f,90,2400},
    {"V_502_GROUNDROBO_MISSILE01_L (Depth Crawler)",0.1f,0.75f,0.08f,1.5f,0.98f,0,480},
    {"V_504BEGARUTA_ROCKET01_L (Begaruta rockets)",0.1f,0.75f,0.01f,10.0f,0.98f,0,1200},
};

rounds::Motor MotorOf(const Rocket& r,const float* dir) {
    rounds::Motor m{};
    for(int i=0;i<3;++i){m.own[i]=dir[i]*r.speed;m.drop[i]=0.0f;}
    m.drop[1]=-kGravity*r.factor/3600.0f;
    m.accel=r.accel;m.top=r.top;m.keepInh=r.keepInh;m.keepOwn=1.0f;m.ignite=r.ignite;
    return m;
}

void MotorChecks() {
    const float dir[3]={0.0f,0.0f,1.0f};
    const Rocket& r=kRockets[0];
    rounds::Motor m=MotorOf(r,dir);
    float p[3]={0.0f,0.0f,0.0f};
    for(int n=0;n<r.ignite;++n)rounds::Step(m,p);
    const double d=m.drop[1],k=r.keepInh,a=r.ignite;
    const double want=d/(1.0-k)*(a-k*(1.0-std::pow(k,a))/(1.0-k))+a*d;
    Expect(std::fabs(p[1]-want)<1e-3,"motor: the drop by ignition (409, 90 frames, m)",p[1],want);
    Expect(std::fabs(p[2]-r.speed*a)<1e-3,"motor: ...coasting at AmmoSpeed (m along the rail)",p[2],r.speed*a);
    const int climb=static_cast<int>(std::ceil((r.top-r.speed)/r.accel));
    for(int n=0;n<climb;++n)rounds::Step(m,p);
    const float own=std::sqrt(m.own[0]*m.own[0]+m.own[1]*m.own[1]+m.own[2]*m.own[2]);
    Expect(std::fabs(own-r.top)<1e-4,"motor: at its top speed (CP[6]) once burnt (m/frame)",own,r.top);
    const float inh=m.inh[1],y=p[1];
    rounds::Step(m,p);
    Expect(std::fabs((p[1]-y)-(inh*rounds::kInhDecay+m.drop[1]))<1e-6,"motor: a powered frame's fall: 0.9 inh + drop (m)",p[1]-y,
           inh*rounds::kInhDecay+m.drop[1]);
}

// Each stock rocket fired level from 50 m up: how far under its rail's line it is 1000 m on, and the seconds it takes
// (the old sight's straight line: on it). After ignition the drop does not build up (rounds.h), so only a coast before
// the motor lights (the 409's 90 frames) takes it far off its line.
void StockRockets() {
    const float pos[3]={0.0f,50.0f,0.0f},dir[3]={0.0f,0.0f,1.0f};
    for(const Rocket& r:kRockets) {
        rounds::Motor m=MotorOf(r,dir);
        float p[3]={pos[0],pos[1],pos[2]};
        int n=0;
        for(;n<r.alive && p[2]<1000.0f;++n)rounds::Step(m,p);
        std::printf("      %-46s %.0f m on (1000, or its life's end): %.2f m under its line after %.1f s\n",r.name,p[2],pos[1]-p[1],
                    n/60.0f);
        if(r.ignite>0)Expect(pos[1]-p[1]>10.0f,"a rocket that coasts before ignition is off its line (m under)",pos[1]-p[1],10.0);
        else Expect(pos[1]-p[1]>0.0f && pos[1]-p[1]<10.0f,"one lit at once sinks a frame's drop a frame (m under)",pos[1]-p[1],0.0);
    }
    // Aimed 5 degrees down from 50 m: the line meets the ground at 571.5 m; the 409's rockets, coasting 1.5 s and falling
    // before the motor lights, land short of it.
    const float e=5.0f*0.0174532925f,down[3]={0.0f,-std::sin(e),std::cos(e)};
    float hit[3],end[3],took=0.0f;
    rounds::FirstHit(MotorOf(kRockets[0],down),pos,kRockets[0].alive,15,3000.0f,&Ground,hit,end,&took);
    const float line=50.0f/std::tan(e);
    std::printf("      409 rockets 5 deg down from 50 m: land %.0f m ahead, the straight line said %.0f m\n",hit[2],line);
    Expect(hit[2]<line-50.0f,"the 409's rockets land short of their line (m)",hit[2],line);
}
// --- The stock vehicles' gun sight against a target in the sky (src/roundaim.h GunSight; the user 2026-10-06, an E551
// with V_505TANK_DLC_CANNON04L aimed at a flying saucer, the HUD showing "CANNON 3132 m": "it never moves and does not
// match"). The turret camera (turretcam.cpp) keeps the bore on the point kAimFar out along the view when the view meets
// no map (the sky): bore = muzzle -> eye + look x 800. The eye and muzzle are the E551's rig as its log gives it
// (rig authored r=15.5 up=4.0). The player lays the sight's marks on the target by turning the view (the turret
// following it): the old sight's pipper on the target, the new sight's pipper on its lead mark. ---
struct Gun { const char* name; float speed,factor,ownerMove; int alive; };
const Gun kE551Gun={"V_505TANK_DLC_CANNON04L (E551)",11.0f,0.25f,0.0f,600};   // Root.cpk: AmmoSpeed, AmmoGravityFactor, AmmoOwnerMove, AmmoAlive
constexpr float kAimFar=800.0f,kReach=3000.0f;
const float kEye[3]={0.0f,8.0f,-14.0f},kMuzzle[3]={0.0f,3.5f,1.5f},kStill[3]={0.0f,0.0f,0.0f};

float NoGround(const float*,const float*,float*) { return -1.0f; }   // the sky: the map ray meets nothing

roundaim::Round RoundOf(const Gun& g) {
    roundaim::Round r{};
    r.speed=g.speed;r.ownerMove=g.ownerMove;r.alive=g.alive;
    r.drop[0]=0.0f;r.drop[1]=-kGravity*g.factor/3600.0f;r.drop[2]=0.0f;
    return r;
}
void Unit(float* v) { vec::Normalize(v); }
void Toward(const float* from,const float* to,float* dir) { for(int i=0;i<3;++i)dir[i]=to[i]-from[i];Unit(dir); }
// The bore the turret camera turns the gun onto for a view `look` meeting nothing.
void BoreOf(const float* look,float* bore) {
    const float far[3]={kEye[0]+look[0]*kAimFar,kEye[1]+look[1]*kAimFar,kEye[2]+look[2]*kAimFar};
    Toward(kMuzzle,far,bore);
}
// The old sight's pipper: where the round crosses kReach (rounds::FirstHit, no ground in the sky).
void OldPipper(const Gun& g,const float* bore,float* at) {
    rounds::Arc a{{bore[0]*g.speed,bore[1]*g.speed,bore[2]*g.speed},{0.0f,-kGravity*g.factor/3600.0f,0.0f}};
    float hit[3],took=0.0f;
    rounds::FirstHit(a,kMuzzle,g.alive,15,kReach,&NoGround,hit,at,&took);
}
// The new sight's marks for the target (`target`, `tvel`).
roundaim::GunMark NewMarks(const Gun& g,const float* bore,const float* target,const float* tvel) {
    const float none[3]={0.0f,0.0f,0.0f};
    return roundaim::GunSight(RoundOf(g),kMuzzle,bore,kStill,false,none,0.0f,target,tvel);
}
// The angle (deg) between the directions from the eye to `a` and to `b`: how far apart they are on the screen.
float Apart(const float* a,const float* b) {
    float da[3],db[3];Toward(kEye,a,da);Toward(kEye,b,db);
    return std::acos(vec::Clamp(vec::Dot(da,db),-1.0f,1.0f))*57.29578f;
}
// The view turned until the mark `mark(look, out[2][3])` gives out[0] over out[1] on the screen (the player laying it).
template<class Mark> void Lay(float* look,Mark mark) {
    for(int k=0;k<200;++k) {
        float at[2][3];mark(look,at);
        float have[3],want[3];Toward(kEye,at[0],have);Toward(kEye,at[1],want);
        for(int i=0;i<3;++i)look[i]+=want[i]-have[i];
        Unit(look);
    }
}

void SkySight() {
    const Gun& g=kE551Gun;
    const roundaim::Round round=RoundOf(g);
    // 1. "It never moves": the old pipper's place on the screen: within half a degree of the view's centre whatever the
    // view's elevation and whatever is flying there (it is the bore's point at 3 km, and the bore follows the view).
    float lo=1e9f,hi=-1e9f;
    for(float e=5.0f;e<=60.0f;e+=11.0f) {
        const float r=e*0.0174532925f,look[3]={0.0f,std::sin(r),std::cos(r)};
        float bore[3],at[3];BoreOf(look,bore);OldPipper(g,bore,at);
        const float centre[3]={kEye[0]+look[0]*1000.0f,kEye[1]+look[1]*1000.0f,kEye[2]+look[2]*1000.0f};
        const float off=Apart(at,centre);
        std::printf("      old sight, view %4.0f deg up: pipper %.2f deg off the centre, %.0f m out\n",e,off,vec::Dist(kMuzzle,at));
        lo=std::fmin(lo,off);hi=std::fmax(hi,off);
    }
    Expect(hi<0.6f,"old sight: its pipper stays on the screen's centre (deg off it, at most)",hi,0.6);
    // 2. "It does not match": a saucer 20 deg up at 600..2500 m, still or crossing at 40 m/s. The miss when each sight's
    // marks are laid on it (the round's nearest pass, roundaim::Nearest: the bullet core's per-frame step).
    const float elev=20.0f*0.0174532925f;
    const float ranges[]={600.0f,1200.0f,2000.0f,2500.0f};
    const float crossing[3]={40.0f,0.0f,0.0f};
    for(int moving=0;moving<2;++moving)
        for(float d:ranges) {
            const float* tvel=moving ? crossing : kStill;
            const float target[3]={kEye[0],kEye[1]+d*std::sin(elev),kEye[2]+d*std::cos(elev)};
            float oldLook[3]={0.0f,std::sin(elev),std::cos(elev)},newLook[3]={0.0f,std::sin(elev),std::cos(elev)};
            Lay(oldLook,[&](const float* look,float (*at)[3]) {
                float bore[3];BoreOf(look,bore);OldPipper(g,bore,at[0]);std::memcpy(at[1],target,12);
            });
            Lay(newLook,[&](const float* look,float (*at)[3]) {
                float bore[3];BoreOf(look,bore);
                const roundaim::GunMark m=NewMarks(g,bore,target,tvel);
                std::memcpy(at[0],m.pipper,12);std::memcpy(at[1],m.lead,12);
            });
            float oldBore[3],newBore[3];BoreOf(oldLook,oldBore);BoreOf(newLook,newBore);
            const roundaim::Pass was=roundaim::Fire(round,kMuzzle,oldBore,kStill,target,tvel);
            const roundaim::Pass now=roundaim::Fire(round,kMuzzle,newBore,kStill,target,tvel);
            const roundaim::GunMark m=NewMarks(g,newBore,target,tvel);
            std::printf("      saucer %4.0f m%s: old pipper on it misses by %5.1f m; new pipper on its lead mark misses by %.2f m"
                        " (label %4.0f m, %.1f s)\n",d,moving ? " crossing 40 m/s" : "                ",was.miss,now.miss,
                        vec::Dist(kMuzzle,m.lead),m.frames/60.0f);
            Expect(m.mark==roundaim::SightMark::ranged && m.inReach,"new sight: ranged on the saucer, within reach",m.inReach,1.0);
            Expect(now.miss<0.5f,"new sight: its pipper on the lead mark meets the saucer (miss, m)",now.miss,0.0);
            if(moving)Expect(was.miss>30.0f,"old sight: its pipper on a crossing saucer misses it (no lead; m)",was.miss,30.0);
        }
    // 3. Nothing there: no pipper (none), not a point at the reach. Beyond the round's life: ranged but out of reach.
    float bore[3];const float look[3]={0.0f,std::sin(elev),std::cos(elev)};BoreOf(look,bore);
    const roundaim::GunMark empty=NewMarks(g,bore,nullptr,kStill);
    Expect(empty.mark==roundaim::SightMark::none,"new sight: the sky with nothing in it: no pipper",static_cast<int>(empty.mark),0.0);
    const float far[3]={kEye[0],kEye[1]+9000.0f*std::sin(elev),kEye[2]+9000.0f*std::cos(elev)};
    const roundaim::GunMark gone=NewMarks(g,bore,far,kStill);
    Expect(gone.mark==roundaim::SightMark::ranged && !gone.inReach,"new sight: a saucer past its 6.6 km reach: dim",gone.inReach,0.0);
    // 4. The ground first: a hill 300 m out in front of a saucer at 1200 m: the ground's pipper.
    const float target[3]={kEye[0],kEye[1]+1200.0f*std::sin(elev),kEye[2]+1200.0f*std::cos(elev)};
    const float hill[3]={0.0f,100.0f,300.0f};
    const roundaim::GunMark ground=roundaim::GunSight(round,kMuzzle,bore,kStill,true,hill,30.0f,target,kStill);
    Expect(ground.mark==roundaim::SightMark::ground && vec::Dist(ground.pipper,hill)<1e-3f,"new sight: the map met first: its pipper",
           static_cast<int>(ground.mark),1.0);
}
}  // namespace

int main() {
    ArcChecks();
    MotorChecks();
    StockRockets();
    SkySight();
    std::printf("%s\n",failed ? "FAILED" : "all passed");
    return failed ? 1 : 0;
}
