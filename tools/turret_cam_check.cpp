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
//    lead-circle mode).
// Exit code 1 when one fails. Built on request only:
// cmake --build build --target turret_cam_check && build\turret_cam_check.exe
#include "../src/turretcam.h"
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

// The high view (README 高视角; the user, 2026-10-06: "动几下就显示不对了"): its aim is a ground point (tcam::HighAim).
//  - Switching keeps the screen's centre on its point both ways: the normal view's centre point taken as the high
//    view's, and the high view's point handed back through the normal rig (tcam::AimAt), over ranges 20..1200 m, any
//    heading, the point level with the hull or on a hill.
//  - Far off it is not touchy: a frame of full stick at 600 m against the old high view (a pitch: tcam::High at
//    HighCam 45 / 35 / 40, the eye at 45 m looking -3.7 deg), and it never looks past the ground (the old one was over
//    the horizon after a few frames up).
// The Katyusha's authored rig (game_object_camera_setting: look (0, 4.5, 4), eye (0, 8.5, -15); the user's log: r 19.4).
float GroundAlong(const float* eye,const float* look,float y,float* hit) {
    const float d[3]={look[0]-eye[0],look[1]-eye[1],look[2]-eye[2]};
    if(!(std::fabs(d[1])>1e-6f))return -1.0f;
    const float t=(y-eye[1])/d[1];
    for(int i=0;i<3;++i)hit[i]=eye[i]+d[i]*t;
    return t;
}
float OffLine(const float* eye,const float* look,const float* p) {   // p's distance from the line eye -> look
    float d[3]={look[0]-eye[0],look[1]-eye[1],look[2]-eye[2]};
    vec::Normalize(d);
    const float w[3]={p[0]-eye[0],p[1]-eye[1],p[2]-eye[2]};
    const float t=w[0]*d[0]+w[1]*d[1]+w[2]*d[2];
    const float q[3]={w[0]-d[0]*t,w[1]-d[1]*t,w[2]-d[2]*t};
    return std::sqrt(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]);
}
// The old high view's ground point `range` m out (flat ground at the hull's height): its pitch found by bisection, then
// how far a frame of full stick down (1.5 deg, TurretCamRate 90) moves it, and how many frames up lose it.
void OldHighView(float range,float* step,int* lost) {
    const tcam::Rig high=tcam::High(45.0f,35.0f,40.0f);
    const float origin[3]={0.0f,0.0f,0.0f};
    auto hitAt=[&](float pitch) {
        float d[3],eye[3],look[3],hit[3];
        tcam::Dir(0.0f,pitch,d);
        tcam::Place(high,origin,0.0f,d,eye,look);
        return GroundAlong(eye,look,0.0f,hit)>0.0f ? std::sqrt(hit[0]*hit[0]+hit[2]*hit[2]) : 1e9f;
    };
    float lo=-80.0f*kDeg,hi=-0.01f*kDeg;
    for(int i=0;i<60;++i){const float mid=0.5f*(lo+hi);(hitAt(mid)<range ? lo : hi)=mid;}
    const float rate=1.5f*kDeg;
    *step=range-hitAt(lo-rate);
    *lost=0;
    for(float pitch=lo;hitAt(pitch)<1e8f && *lost<100;pitch+=rate)++*lost;
}
void HighView() {
    float look[3],eye[3];
    tcam::Rig rig{};
    const float alook[3]={0.0f,4.5f,4.0f},aeye[3]={0.0f,8.5f,-15.0f};
    Expect(tcam::Authored(alook,aeye,&rig),"the Katyusha's rig");
    const float origin[3]={120.0f,6.0f,-40.0f};
    float worstOn=0.0f,worstBack=0.0f;
    for(float range:{20.0f,60.0f,150.0f,300.0f,600.0f,1200.0f})
        for(float yaw:{-2.5f,-0.7f,0.0f,1.1f,3.0f})
            for(float hill:{0.0f,30.0f}) {
                // Normal view on the point: then the high view taken on it.
                float fwd[3],right[3];
                tcam::Flat(yaw,fwd,right);
                const float p[3]={origin[0]+fwd[0]*range,origin[1]+hill,origin[2]+fwd[2]*range};
                float vy=yaw,vp=0.0f,d[3];
                tcam::AimAt(rig,origin,p,&vy,&vp);
                tcam::Dir(vy,vp,d);
                tcam::Place(rig,origin,vy,d,eye,look);
                float g[3];
                if(!(GroundAlong(eye,look,p[1],g)>0.0f)){Expect(false,"the normal view's centre meets the point's height ahead",range,yaw);continue;}
                worstBack=std::fmax(worstBack,std::sqrt((g[0]-p[0])*(g[0]-p[0])+(g[2]-p[2])*(g[2]-p[2])));
                const tcam::HighAim a=tcam::HighAimAt(origin,g,vy);
                float he[3],hl[3],hg[3];
                tcam::HighPlace(origin,a,45.0f,35.0f,he,hl);
                GroundAlong(he,hl,g[1],hg);
                worstOn=std::fmax(worstOn,std::sqrt((hg[0]-g[0])*(hg[0]-g[0])+(hg[2]-g[2])*(hg[2]-g[2])));
                // The high view handed back: the normal view's centre through the high one's point.
                float hp[3];tcam::HighPoint(origin,a,hp);
                float by=a.yaw,bp=0.0f;
                tcam::AimAt(rig,origin,hp,&by,&bp);
                tcam::Dir(by,bp,d);
                tcam::Place(rig,origin,by,d,eye,look);
                worstBack=std::fmax(worstBack,OffLine(eye,look,hp));
                Expect(tcam::Wrap(a.yaw-yaw)<1e-3f && tcam::Wrap(a.yaw-yaw)>-1e-3f,"the high view's bearing is the point's",a.yaw,yaw);
            }
    std::printf("high view switch: worst miss on 0.000 m by design, measured %.4f m on, %.4f m back\n",worstOn,worstBack);
    Expect(worstOn<0.01f,"switching on keeps the screen's centre on its point",worstOn,0.01);
    Expect(worstBack<0.05f,"switching back keeps the screen's centre on its point",worstBack,0.05);
    // Far off: a frame of full stick at 600 m, old against new; the old one's horizon.
    float oldStep=0.0f;int oldLost=0;
    OldHighView(600.0f,&oldStep,&oldLost);
    tcam::HighAim a{0.0f,600.0f,0.0f};
    const tcam::HighAim before=a;
    tcam::HighTurn(a,0.0f,1.0f,1.5f*kDeg);
    const float newStep=before.range-a.range;
    tcam::HighAim side=before;
    tcam::HighTurn(side,1.0f,0.0f,1.5f*kDeg);
    float p0[3],p1[3];
    const float o0[3]={0.0f,0.0f,0.0f};
    tcam::HighPoint(o0,before,p0);tcam::HighPoint(o0,side,p1);
    const float newSide=std::sqrt((p1[0]-p0[0])*(p1[0]-p0[0])+(p1[2]-p0[2])*(p1[2]-p0[2]));
    std::printf("high view at 600 m, a frame of full stick: old %.1f m along (over the horizon after %d frames up), new %.1f m along, %.1f m aside\n",
                oldStep,oldLost,newStep,newSide);
    Expect(newStep*10.0f<oldStep,"far off the range moves a tenth of the old view's step or less",newStep,oldStep);
    // Up as far as it goes: the range stops, the sight never shallower than kHighMinSight (the point stays on the ground).
    float shallow=1.0f;
    for(int f=0;f<2000;++f) {
        tcam::HighTurn(a,0.0f,-1.0f,1.5f*kDeg);
        tcam::HighPlace(o0,a,45.0f,35.0f,eye,look);
        const float d[3]={look[0]-eye[0],look[1]-eye[1],look[2]-eye[2]};
        shallow=std::fmin(shallow,-tcam::PitchOf(d));
    }
    std::printf("high view: stick up for 2000 frames: range %.0f m, the sight never shallower than %.2f deg\n",a.range,shallow/kDeg);
    Expect(std::fabs(a.range-tcam::kHighMaxRange)<1e-3f,"the range stops at its most",a.range,tcam::kHighMaxRange);
    Expect(shallow>=tcam::kHighMinSight-1e-4f,"the sight never past the ground's least angle",shallow/kDeg,tcam::kHighMinSight/kDeg);
    // Near: the eye as HighCamHeight / Back put it (45 m up, 35 m back), the sight steeper than the least.
    a=tcam::HighAim{0.0f,20.0f,0.0f};
    tcam::HighPlace(o0,a,45.0f,35.0f,eye,look);
    Expect(std::fabs(eye[1]-45.0f)<1e-3f && std::fabs(eye[2]+35.0f)<1e-3f,"near: the eye where HighCam puts it",eye[1],eye[2]);
}

int main() {
    Owners();
    Steps();
    Ends();
    Sweep();
    Rigs();
    HighView();
    std::printf(failures ? "%d FAILED\n" : "all passed\n",failures);
    return failures ? 1 : 0;
}
