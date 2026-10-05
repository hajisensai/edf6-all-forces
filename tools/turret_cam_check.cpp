// The turret camera's math (src/turretcam.h) checked without the game:
//  - the turret command against the stock axis step (tcam::AxisStep, 0x5FBC00 as docs/camera-re.md §5 gives it) over a
//    grid of the axis params (brake 0.015..1.2, accel 0.015..1; top rate 0.3..3 rad/s) and errors from 1 to 170 deg: it
//    reaches the want without overshooting more than kOvershootDeg (or a quarter frame's turn at the top rate), within the time the stock top rate needs plus the lag's settling;
//    a full-circle axis takes the short way round; a want past the axis' end stops at the end; a camera sweeping at
//    40 deg/s is tracked (after the catch-up) within a degree;
//  - the rig: the eye `radius` behind O along the view, never under O + rise, the view's direction kept when raised;
//    an authored pair (game_object_camera_setting) placed at its own pitch puts the eye where it says; the high view at
//    its pitch puts the eye `height` up and `back` behind; headings grow turning right.
// Exit code 1 when one fails. Built on request only:
// cmake --build build --target turret_cam_check && build\turret_cam_check.exe
#include "../src/turretcam.h"
#include <cmath>
#include <cstdio>

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

int main() {
    Steps();
    Ends();
    Sweep();
    Rigs();
    std::printf(failures ? "%d FAILED\n" : "all passed\n",failures);
    return failures ? 1 : 0;
}
