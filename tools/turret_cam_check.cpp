// The turret camera's math (src/turretcam.h) checked without the game:
//  - the turret command against the stock axis step (tcam::AxisStep, 0x5FBC00 as docs/camera-re.md §5 gives it) over a
//    grid of the axis params (brake 0.015..1.2, accel 0.015..1; top rate 0.3..3 rad/s) and errors from 1 to 170 deg: it
//    reaches the want without overshooting more than kOvershootDeg (or a quarter frame's turn at the top rate), within the time the stock top rate needs plus the lag's settling;
//    a full-circle axis takes the short way round; a want past the axis' end stops at the end; a camera sweeping at
//    40 deg/s is tracked (after the catch-up) within a degree;
//  - the rig: the eye `radius` behind O along the view, never under O + rise, the view's direction kept when raised;
//    an authored pair (game_object_camera_setting) placed at its own pitch puts the eye where it says; the high view at
//    its pitch puts the eye `height` up and `back` behind; headings grow turning right;
//  - who turns the turret (Foreign: EDF6AutoTurret's V2 answer decides, else V1's guess off the stick) and whether the
//    round's arc or the bore line goes through the view point (BallisticAim: the arc only for a real hit outside the
//    lead-circle mode);
//  - the high view's observed point (tcam::HighFocus, as turretcam.cpp ShotFocus feeds it: the round flown by
//    rounds::FirstHit over flat ground, the ground found under a point that is not the landing) over the gun's whole
//    elevation range, for the Katyusha, the howitzer and a flat gun: never in the air, never farther than the round's
//    reach or 3 km, continuous (the largest change between neighbouring elevations shrinks with the step: no jump), and
//    one-peaked (growing up to the farthest angle, falling after it). The flat gun's old observation (the shot's end
//    where it was) is checked to put the view over 100 m up, as the user's log had it (2409 m away at 2 deg up).
// Exit code 1 when one fails. Built on request only:
// cmake --build build --target turret_cam_check && build\turret_cam_check.exe
#include "../src/turretcam.h"
#include "../src/rounds.h"
#include <cmath>
#include <cstdio>
#include <initializer_list>

namespace {
using namespace crew;
constexpr float kDeg=tcam::kPi/180.0f,kOvershootDeg=0.5f,kSettleDeg=0.2f;   // or a quarter frame at the top rate
int failures=0;

void Expect(bool ok,const char* what,double a=0.0,double b=0.0) {
    if(ok)return;
    ++failures;
    std::printf("FAIL %s (%.4f, %.4f)\n",what,a,b);
}

// One axis from `start` to `want` (rad) under the command, `frames` at most; the worst overshoot past the want (rad)
// and the frame it first settled within kSettleDeg (-1 never).
struct Run { float overshoot; int settled; float end; };
Run Follow(float start,float want,const float* p,float lo,float hi,int frames) {
    tcam::Axis x{lo,hi,start,0.0f};
    const bool full=hi-lo>=2.0f*tcam::kPi-1e-3f;
    const float sign=(full ? tcam::Wrap(want-start) : want-start)>=0.0f ? 1.0f : -1.0f;
    const float target=full ? want : vec::Clamp(want,lo,hi);
    Run r{0.0f,-1,start};
    for(int f=0;f<frames;++f) {
        const float err=full ? tcam::Wrap(target-x.angle) : target-x.angle;
        tcam::AxisStep(x,tcam::AxisCommand(err,x.rate,0.0f,p),p);
        const float past=-(full ? tcam::Wrap(target-x.angle) : target-x.angle)*sign;
        if(past>r.overshoot)r.overshoot=past;
        const float left=std::fabs(full ? tcam::Wrap(target-x.angle) : target-x.angle);
        if(left<kSettleDeg*kDeg){if(r.settled<0)r.settled=f;}
        else r.settled=-1;
    }
    r.end=x.angle;
    return r;
}

void Steps() {
    const float brakes[]={0.015f,0.05f,0.2f,0.5f,1.2f},accels[]={0.015f,0.035f,0.1f,0.3f,1.0f},tops[]={0.3f,1.1f,3.0f};
    const float errors[]={1.0f,5.0f,20.0f,60.0f,120.0f,170.0f},signs[]={1.0f,-1.0f};
    int runs=0;float worst=0.0f;
    for(float br:brakes)for(float ac:accels)for(float top:tops)for(float e:errors)for(float s:signs) {
        const float p[3]={br,ac,top/60.0f};
        const float err=s*e*kDeg;
        const int budget=static_cast<int>(std::fabs(err)/p[2]+6.0f/ac+6.0f/br+60.0f);
        const Run r=Follow(0.0f,err,p,-tcam::kPi,tcam::kPi,budget+600);
        ++runs;
        if(r.overshoot>worst)worst=r.overshoot;
        char what[160];
        std::snprintf(what,sizeof(what),"step %.0f deg brake %.2f accel %.2f top %.1f rad/s: overshoot",s*e,br,ac,top);
        const float allowed=std::fmax(kOvershootDeg*kDeg,0.25f*p[2]);   // or a quarter frame's turn at the top rate
        Expect(r.overshoot<=allowed,what,r.overshoot/kDeg,allowed/kDeg);
        std::snprintf(what,sizeof(what),"step %.0f deg brake %.2f accel %.2f top %.1f rad/s: settled by frame",s*e,br,ac,top);
        Expect(r.settled>=0 && r.settled<=budget,what,r.settled,budget);
    }
    std::printf("steps: %d runs, worst overshoot %.3f deg\n",runs,worst/kDeg);
}

void Ends() {
    const float p[3]={0.1f,0.1f,1.1f/60.0f};
    // A full circle: from 170 deg to -170 deg is 20 deg the short way (through 180).
    tcam::Axis x{-tcam::kPi,tcam::kPi,170.0f*kDeg,0.0f};
    float travelled=0.0f,last=x.angle;
    for(int f=0;f<600;++f) {
        tcam::AxisStep(x,tcam::AxisCommand(tcam::Wrap(-170.0f*kDeg-x.angle),x.rate,0.0f,p),p);
        travelled+=std::fabs(tcam::Wrap(x.angle-last));last=x.angle;
    }
    Expect(std::fabs(tcam::Wrap(x.angle+170.0f*kDeg))<kSettleDeg*kDeg && travelled<25.0f*kDeg,"full circle: the short way",travelled/kDeg,25.0);
    // A pitch axis with stops -60..+5 deg (negative up): a want of -80 deg stops at -60 deg.
    const Run r=Follow(0.0f,-80.0f*kDeg,p,-60.0f*kDeg,5.0f*kDeg,900);
    Expect(std::fabs(r.end+60.0f*kDeg)<1e-4f,"pitch stop: rests at the stop",r.end/kDeg,-60.0);
}

void Sweep() {
    // The camera sweeps right at 40 deg/s; a 1.1 rad/s (63 deg/s) turret catches up, then holds within a degree of it.
    const float params[][3]={{0.1f,0.1f,1.1f/60.0f},{0.015f,0.035f,1.1f/60.0f},{1.2f,0.035f,1.1f/60.0f},{0.5f,1.0f,1.1f/60.0f}};
    for(const auto& p:params) {
        tcam::Axis x{-tcam::kPi,tcam::kPi,0.0f,0.0f};
        float want=0.0f,worst=0.0f;
        const float rate=40.0f*kDeg/60.0f;
        for(int f=0;f<900;++f) {
            want=tcam::Wrap(want+rate);
            tcam::AxisStep(x,tcam::AxisCommand(tcam::Wrap(want-x.angle),x.rate,rate,p),p);
            if(f>480){const float lag=std::fabs(tcam::Wrap(want-x.angle));if(lag>worst)worst=lag;}
        }
        char what[96];
        std::snprintf(what,sizeof(what),"sweep brake %.3f accel %.3f: steady lag",p[0],p[1]);
        Expect(worst<=1.0f*kDeg,what,worst/kDeg,1.0);
        std::printf("sweep 40 deg/s, brake %.3f accel %.3f: steady lag %.2f deg\n",p[0],p[1],worst/kDeg);
    }
}

void Rigs() {
    const float origin[3]={100.0f,10.0f,-50.0f};
    // Headings: turning right (yaw growing) moves the view toward the right of its heading.
    float d0[3],d1[3],fwd[3],right[3];
    tcam::Dir(0.3f,0.0f,d0);tcam::Dir(0.31f,0.0f,d1);tcam::Flat(0.3f,fwd,right);
    const float moved[3]={d1[0]-d0[0],d1[1]-d0[1],d1[2]-d0[2]};
    Expect(vec::Dot(moved,right)>0.0f,"yaw grows to the right",vec::Dot(moved,right));
    Expect(std::fabs(tcam::YawOf(d0)-0.3f)<1e-5f && std::fabs(tcam::PitchOf(d0))<1e-5f,"Dir / YawOf / PitchOf agree",tcam::YawOf(d0));
    // The Blacker's authored pair (0,4,0)/(0,4,-15.5): level at its own pitch, the eye 15.5 m behind, 4 m up.
    tcam::Rig rig{};
    const float look[3]={0.0f,4.0f,0.0f},eye[3]={0.0f,4.0f,-15.5f};
    Expect(tcam::Authored(look,eye,&rig),"Blacker pair is a rig");
    float d[3],e[3],l[3];
    tcam::Dir(0.0f,0.0f,d);tcam::Place(rig,origin,0.0f,d,e,l);
    Expect(std::fabs(e[1]-14.0f)<1e-4f && std::fabs(e[2]-(origin[2]-15.5f))<1e-4f,"Blacker eye where authored",e[1],e[2]);
    // Looking 30 deg up: the eye stays at its authored height (not under the turret), the view still 30 deg up.
    tcam::Dir(0.0f,30.0f*kDeg,d);tcam::Place(rig,origin,0.0f,d,e,l);
    float v[3]={l[0]-e[0],l[1]-e[1],l[2]-e[2]};vec::Normalize(v);
    Expect(e[1]>=origin[1]+4.0f-1e-4f,"looking up: the eye not lower than authored",e[1]);
    Expect(vec::Dot(v,d)>0.99999f,"looking up: the view keeps its direction",vec::Dot(v,d));
    // Looking 30 deg down: the eye orbits up over the authored height.
    tcam::Dir(0.0f,-30.0f*kDeg,d);tcam::Place(rig,origin,0.0f,d,e,l);
    Expect(std::fabs(e[1]-(origin[1]+4.0f+15.5f*0.5f))<1e-3f,"looking down: the eye orbits up",e[1]);
    // The Katyusha's (0,4.5,4)/(0,8.5,-15) at its own pitch: the eye where authored.
    const float kl[3]={0.0f,4.5f,4.0f},ke[3]={0.0f,8.5f,-15.0f};
    Expect(tcam::Authored(kl,ke,&rig),"Katyusha pair is a rig");
    const float kd[3]={0.0f,kl[1]-ke[1],kl[2]-ke[2]};
    float kv[3]={kd[0],kd[1],kd[2]};vec::Normalize(kv);
    tcam::Place(rig,origin,0.0f,kv,e,l);
    Expect(std::fabs(e[1]-(origin[1]+8.5f))<1e-3f && std::fabs(e[2]-(origin[2]-15.0f))<1e-3f,"Katyusha eye where authored",e[1],e[2]);
    // A heading turned 90 deg right: the authored point 4 m ahead swings with it.
    tcam::Place(rig,origin,tcam::kPi*0.5f,kv,e,l);
    tcam::Flat(tcam::kPi*0.5f,fwd,right);
    Expect(std::fabs(l[0]-(origin[0]+4.0f*fwd[0]))<1e-3f && std::fabs(l[2]-(origin[2]+4.0f*fwd[2]))<1e-3f,"rig turns with the heading",l[0],l[2]);
    // The high view (45 up, 35 back, 40 down): at its pitch the eye is 45 m up and 35 m behind the origin.
    const tcam::Rig high=tcam::High(45.0f,35.0f,40.0f);
    tcam::Dir(0.0f,-40.0f*kDeg,d);tcam::Place(high,origin,0.0f,d,e,l);
    Expect(std::fabs(e[1]-(origin[1]+45.0f))<1e-3f && std::fabs(e[2]-(origin[2]-35.0f))<1e-3f,"high view: 45 up, 35 back",e[1],e[2]);
    // Level, the high eye stays 45 m up.
    tcam::Dir(0.0f,0.0f,d);tcam::Place(high,origin,0.0f,d,e,l);
    Expect(e[1]>=origin[1]+45.0f-1e-3f,"high view level: still 45 m up",e[1]);
    // A pair that is no camera behind its point.
    const float bad[3]={0.0f,4.0f,1.0f};
    Expect(!tcam::Authored(look,bad,&rig),"an eye ahead of its point is no rig");
}
}  // namespace

void Owners() {
    const float stick[2]={0.3f,-0.1f},close[2]={0.31f,-0.1f},off[2]={0.0f,0.0f};
    // EDF6AutoTurret answers: its word is taken whatever the inputs look like (a small correction near its lock is
    // still its; an input that happens to equal the stick while it steers too).
    Expect(tcam::Foreign(1,close,stick,0.02f),"V2: it steers, its small correction near the target is its");
    Expect(tcam::Foreign(1,stick,stick,0.02f),"V2: it steers, an input equal to the stick is still its");
    Expect(!tcam::Foreign(0,off,stick,0.02f),"V2: it does not steer, an input off the stick is not taken as its");
    // No answer (an older EDF6AutoTurret, or none): V1's guess.
    Expect(!tcam::Foreign(-1,close,stick,0.02f) && tcam::Foreign(-1,off,stick,0.02f),"V1: the input off the stick by more than the tolerance");
    Expect(tcam::BallisticAim(true,false),"a real hit, AUTO / no auto-turret: the round's arc");
    Expect(!tcam::BallisticAim(true,true),"lead circle: the bore line (the circle solved the arc)");
    Expect(!tcam::BallisticAim(false,false) && !tcam::BallisticAim(false,true),"no hit (the made-up far point): the bore line");
}

// Observation stays on the actual shot, independently of distance, terrain height and free look.
void HighView() {
    const float origin[3]={120.0f,6.0f,-40.0f};
    int cases=0;
    for(float range:{0.0f,2.0f,20.0f,600.0f,2400.0f})
        for(float yaw:{-2.5f,0.0f,1.1f})
            for(float hill:{-80.0f,0.0f,300.0f})
                for(float orbit:{-1.0f,0.0f,1.0f}) {
                    float forward[3],right[3],eye[3],look[3];
                    tcam::Flat(yaw,forward,right);
                    const float point[3]={origin[0]+forward[0]*range,origin[1]+hill,origin[2]+forward[2]*range};
                    tcam::ImpactPlace(origin,point,yaw,45.0f,35.0f,orbit,orbit,eye,look);
                    Expect(vec::Dist(look,point)<1e-4f,"shot endpoint never clamped or flattened");
                    Expect(std::fabs(vec::Dist(eye,point)-std::sqrt(45.0f*45.0f+35.0f*35.0f))<0.001f,
                           "observation distance does not grow with shot range");
                    Expect(eye[1]>point[1],"free look remains above the real shot endpoint");
                    ++cases;
                }
    std::printf("high view: %d real endpoint/free-look placements\n",cases);
}

// --- the high view's observed point (tcam::HighFocus) ---
// A gun: AmmoSpeed (m/frame), AmmoGravityFactor, AmmoAlive (frames); its muzzle `up` m over flat ground (y = 0).
struct Gun { const char* name; float speed,factor; int alive; float up; };
constexpr float kGravity=14.7f,kSightFar=3000.0f,kSegment=15;   // turretcam.cpp (its search: twice kSightFar) / rounds.cpp's

float FlatRay(const float* a,const float* b,float* hit) {
    if(!(a[1]>=0.0f) || !(b[1]<0.0f))return -1.0f;
    const float t=a[1]/(a[1]-b[1]);
    for(int i=0;i<3;++i)hit[i]=a[i]+(b[i]-a[i])*t;
    float d[3]={hit[0]-a[0],hit[1]-a[1],hit[2]-a[2]};
    return std::sqrt(vec::Dot(d,d));
}

// What ShotFocus observes for `g` fired at `elev` rad (heading +Z), and the old observation (the shot's end as it was).
struct Seen { float across,y,oldY; };
Seen Observe(const Gun& g,float elev) {
    const float muzzle[3]={0.0f,g.up,0.0f},dir[3]={0.0f,std::sin(elev),std::cos(elev)};
    rounds::Arc arc{};
    for(int i=0;i<3;++i){arc.vel[i]=dir[i]*g.speed;arc.drop[i]=0.0f;}
    arc.drop[1]=-kGravity*g.factor/3600.0f;
    float hit[3],end[3],took=0.0f;
    const bool landed=rounds::FirstHit(arc,muzzle,g.alive,static_cast<int>(kSegment),2.0f*kSightFar,&FlatRay,hit,end,&took);
    const float* at=landed ? hit : end;
    float out[3];
    tcam::HighFocus(muzzle,dir,landed,at,kSightFar,0.0f,out);
    if(!(landed && tcam::AcrossDistance(muzzle,at)<=kSightFar)) {   // ShotFocus: the ground straight under it
        const float from[3]={out[0],g.up+1000.0f,out[2]},to[3]={out[0],-1000.0f,out[2]};
        float down[3];
        if(FlatRay(from,to,down)>=0.0f)out[1]=down[1];
    }
    return Seen{tcam::AcrossDistance(muzzle,out),out[1],at[1]};
}

// The largest change of the observed distance between neighbouring elevations `step` deg apart over [lo, hi] deg.
float WorstStep(const Gun& g,float lo,float hi,float step) {
    float worst=0.0f,last=Observe(g,lo*kDeg).across;
    for(float e=lo+step;e<=hi+1e-4f;e+=step) {
        const float d=Observe(g,e*kDeg).across;
        worst=std::fmax(worst,std::fabs(d-last));
        last=d;
    }
    return worst;
}

void HighFocusSweep() {
    const Gun guns[]={{"Katyusha",2.0f,1.0f,1500,3.0f},{"howitzer",4.0f,1.0f,1200,3.5f},{"flat gun",20.0f,0.5f,120,4.0f}};
    constexpr float kLo=-30.0f,kHi=85.0f,kStep=0.01f,kSlack=1.0f;   // deg; m (the 15-frame chord's own error)
    for(const Gun& g:guns) {
        const float reach=std::fmin(kSightFar,g.speed*static_cast<float>(g.alive));
        float most=0.0f,peakAt=kLo,highestOld=-1e9f;
        bool aired=false,over=false;
        for(float e=kLo;e<=kHi+1e-4f;e+=kStep) {
            const Seen s=Observe(g,e*kDeg);
            aired=aired || std::fabs(s.y)>1e-3f;
            over=over || s.across>reach+kSlack;
            if(s.across>most){most=s.across;peakAt=e;}
            highestOld=std::fmax(highestOld,s.oldY);
        }
        Expect(!aired,"high view: the observed point is on the ground at every elevation");
        Expect(!over,"high view: never farther than the round's reach or 3 km",most,reach);
        // One peak: growing up to the farthest angle, falling after it.
        bool rising=true,falling=true;
        float last=Observe(g,kLo*kDeg).across;
        for(float e=kLo+kStep;e<=kHi+1e-4f;e+=kStep) {
            const float d=Observe(g,e*kDeg).across;
            if(e<=peakAt && d<last-kSlack)rising=false;
            if(e>peakAt && d>last+kSlack)falling=false;
            last=d;
        }
        Expect(rising,"high view: the distance grows with the elevation up to the farthest angle",peakAt);
        Expect(falling,"high view: past the farthest angle it only falls",peakAt);
        // No jump: a quarter of the step gives (about) a quarter of the largest change; a jump would stay as large.
        const float coarse=WorstStep(g,kLo,kHi,0.04f),fine=WorstStep(g,kLo,kHi,0.01f);
        Expect(fine<=0.5f*coarse+kSlack,"high view: continuous in the elevation (no jump)",coarse,fine);
        std::printf("high view %s: farthest %.0f m at %.1f deg (reach %.0f m), largest change %.1f m / 0.04 deg, %.1f m / 0.01 deg;"
                    " the old observation up to %.0f m high\n",g.name,most,peakAt,reach,coarse,fine,highestOld);
        if(g.speed>10.0f)Expect(highestOld>100.0f,"the flat gun's old observation was in the air (the user's log)",highestOld);
    }
}

int main() {
    HighFocusSweep();
    Owners();
    Steps();
    Ends();
    Sweep();
    Rigs();
    HighView();
    std::printf(failures ? "%d FAILED\n" : "all passed\n",failures);
    return failures ? 1 : 0;
}
