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

// The high view's pitch (README 高视角; the user, 2026-10-06: "动几下就显示不对了"). The view's pitch is the normal rig's,
// the camera draws it `offset` lower (tcam::ViewOffset: the Katyusha's authored rig base -11.9 deg under HighCamPitch 40,
// -28.1 deg). Two ways it went wrong, both by the offset:
//  - a view started from the camera as drawn (a take-over: back in the seat, a seat change, the rider check flickering
//    while boarding, as at 14:56:23 in the user's log; the coupled free look) took the drawn pitch as the view's: the
//    camera then sank by the offset, again at every start, down to the clamp;
//  - the view's pitch was kept within +-80 deg of its own while the camera stops at -85: under the high view's offset
//    the stick turned a pitch the camera could not show (-57..-80 deg), and coming back up the view did not move
//    until it was undone.
void HighView() {
    const float offset=tcam::ViewOffset(true,40.0f,-11.9f*kDeg);
    Expect(std::fabs(offset/kDeg+28.1f)<0.01f,"the Katyusha's high view offset",offset/kDeg,-28.1);
    Expect(tcam::ViewOffset(false,40.0f,-11.9f*kDeg)==0.0f,"no offset off the high view");
    // Five starts in a row, each from the camera as the last one drew it: the camera stays where it was.
    float drawn=-40.0f*kDeg;
    for(int take=0;take<5;++take) {
        const float pitch=tcam::PitchFrom(drawn,offset);
        const float next=tcam::CameraPitch(pitch,offset);
        Expect(std::fabs(next-drawn)<1e-4f,"a start from the drawn camera keeps it",next/kDeg,drawn/kDeg);
        drawn=next;
    }
    std::printf("high view: after 5 starts the camera at %.2f deg (it was -40.00)\n",drawn/kDeg);
    // Mouse down for 2 s at 1.5 deg a frame (90 deg/s), then one frame up: the camera rises at once.
    const float step=1.5f*kDeg;
    float pitch=tcam::PitchFrom(-40.0f*kDeg,offset);
    for(int f=0;f<120;++f)pitch=tcam::ClampView(pitch-step,offset);
    const float low=tcam::CameraPitch(pitch,offset);
    int frames=0;
    for(;frames<60 && !(tcam::CameraPitch(pitch,offset)>low+0.5f*step);++frames)pitch=tcam::ClampView(pitch+step,offset);
    std::printf("high view: looked down to %.1f deg, the camera rises after %d frame(s) of stick up\n",low/kDeg,frames);
    Expect(std::fabs(low/kDeg+85.0f)<0.01f,"down as far as the camera goes",low/kDeg,-85.0);
    Expect(frames==1,"the camera rises on the first frame of stick up",frames,1.0);
    // Up as far as it goes: the camera's +85 or the view's own +80, whichever comes first; and the normal view keeps +-80.
    for(int f=0;f<200;++f)pitch=tcam::ClampView(pitch+step,offset);
    Expect(std::fabs(tcam::CameraPitch(pitch,offset)/kDeg-(80.0f-28.1f))<0.05f,"the high view's top",tcam::CameraPitch(pitch,offset)/kDeg,51.9);
    Expect(std::fabs(tcam::ClampView(2.0f,0.0f)-tcam::kPitchMost)<1e-6f && std::fabs(tcam::ClampView(-2.0f,0.0f)+tcam::kPitchMost)<1e-6f,
           "the normal view: +-80 deg as before");
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
