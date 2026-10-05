// The Nix's torso hold (src/nix_twist.h, as src/nix.cpp runs it) against a stand-in of the stock update's order,
// without the game (docs/nix-re.md §2-3):
//  - the frame begins with the vehicle's matrix built from the heading h (0x645530): the frame is drawn at h;
//  - the plugin's hold moves the yaw axis back by the heading's change since the last update (nix.cpp Before);
//  - the stock aim step adds the mouse (stand-in: the input times a rate, no smoothing) and clamps at the axis' stops
//    when they span less than a turn, else wraps (0x5FBC00);
//  - the torso is drawn at h + twist; then the heading steps by the legs' turn (0x645190).
// Each scenario prints the torso's world yaw through it and whether it is what the controls mean: legs turning under a
// still mouse leave the torso where it looks (until the stop, then it goes round with the legs, the legs never pushed);
// the mouse alone turns it; the heading far from 0 and across +-pi changes nothing; an axis with no stops gets +-120 deg;
// NixTorsoTwist=0 is the stock (the torso turns with the legs). With the turret camera steering the torso (turretcam.cpp,
// decoupled: its command, tcam::AxisCommand on the stock axis step, takes the yaw axis onto a world point with the
// want's own drift fed forward) the hold must stand aside (nix.cpp TurretCamSteers): the camera alone keeps the torso
// on the point while the legs turn; with the hold too the turn is taken out twice and the torso swings off it.
// Exit code 1 when one is not.
// Built on request only: cmake --build build --target nix_twist_check && build\nix_twist_check.exe
#include "../src/nix_twist.h"
#include "../src/turretcam.h"
#include <cmath>
#include <cstdio>

namespace {
constexpr float kPi=3.14159265358979f,kDeg=kPi/180.0f,kDt=1.0f/60.0f;

struct Axis { float min,max,angle; };

// The stock step (0x5FBC00) as far as the hold cares: the angle moved by the input, clamped or wrapped.
void StockStep(Axis& a,float input) noexcept {
    a.angle+=input;
    const float span=a.max-a.min;
    if(span<nixtwist::kTwoPi && std::fabs(span-nixtwist::kTwoPi)>1.1920929e-7f) {
        if(a.angle<a.min)a.angle=a.min;
        if(a.angle>a.max)a.angle=a.max;
    } else a.angle-=std::trunc(a.angle/nixtwist::kTwoPi)*nixtwist::kTwoPi;
}

struct Rig {
    Axis axis;
    float heading,last;   // the legs' heading; the heading the hold saw at the last update
    bool held,started;
};

// One frame: the legs turn at `legs` rad/s, the mouse at `mouse` rad/s. The torso's world yaw as drawn.
float Frame(Rig& r,float legs,float mouse) noexcept {
    const float drawn=r.heading;
    if(r.held && r.started)r.axis.angle=nixtwist::Hold(r.axis.angle,nixtwist::Wrap(r.heading-r.last),nixtwist::TwistStops(r.axis.min,r.axis.max));
    r.last=r.heading;r.started=true;
    StockStep(r.axis,mouse*kDt);
    const float torso=drawn+r.axis.angle;
    r.heading+=legs*kDt;
    return torso;
}

int failures=0;
void Expect(bool ok,const char* what,float got,float want) noexcept {
    std::printf("  %-62s %8.3f deg (want %8.3f) %s\n",what,got/kDeg,want/kDeg,ok ? "ok" : "WRONG");
    if(!ok)++failures;
}
bool Near(float a,float b,float tol=0.05f*kDeg) noexcept { return std::fabs(nixtwist::Wrap(a-b))<=tol; }

Rig Nix(float heading,bool held,float min=-70.0f*kDeg,float max=70.0f*kDeg) noexcept {
    return Rig{Axis{min,max,0.0f},heading,heading,held,false};
}

void LegsUnderStillMouse() {
    std::puts("legs turn 45 deg right (A/D) under a still mouse, then 60 deg more:");
    Rig r=Nix(0.0f,true);
    const float start=Frame(r,0.0f,0.0f);
    float worst=0.0f,t=0.0f;
    for(int i=0;i<30;++i){t=Frame(r,90.0f*kDeg,0.0f);const float d=std::fabs(nixtwist::Wrap(t-start));if(d>worst)worst=d;}
    Expect(worst<0.01f*kDeg,"the torso's world yaw, worst drift while the legs turned 45",worst,0.0f);
    Frame(r,0.0f,0.0f);   // the last frame's turn is given back at the next update
    Expect(Near(r.axis.angle,-45.0f*kDeg),"the twist once the legs stopped",r.axis.angle,-45.0f*kDeg);
    for(int i=0;i<40;++i)t=Frame(r,90.0f*kDeg,0.0f);
    Frame(r,0.0f,0.0f);t=Frame(r,0.0f,0.0f);
    Expect(Near(r.axis.angle,-70.0f*kDeg),"past the stop: the twist held at the stop",r.axis.angle,-70.0f*kDeg);
    Expect(Near(t,r.heading-70.0f*kDeg),"...and the torso goes round with the legs",t,r.heading-70.0f*kDeg);
    Expect(Near(r.heading,105.0f*kDeg),"the legs turned all the way (never pushed or held)",r.heading,105.0f*kDeg);
}

void MouseAlone() {
    std::puts("the mouse turns the torso 30 deg left, the legs still:");
    Rig r=Nix(0.3f,true);
    const float start=Frame(r,0.0f,0.0f);
    float t=start;
    for(int i=0;i<60;++i)t=Frame(r,0.0f,-30.0f*kDeg);
    Expect(Near(t,start-30.0f*kDeg,0.6f*kDeg),"the torso's world yaw",t,start-30.0f*kDeg);
}

void MouseWhileTurning() {
    std::puts("the legs turn right 40 deg while the mouse turns the torso left 20 deg:");
    Rig r=Nix(0.0f,true);
    const float start=Frame(r,0.0f,0.0f);
    float t=start;
    for(int i=0;i<60;++i)t=Frame(r,40.0f*kDeg,-20.0f*kDeg);
    Expect(Near(t,start-20.0f*kDeg,0.8f*kDeg),"the torso turned by the mouse alone",t,start-20.0f*kDeg);
}

void FarHeadings() {
    std::puts("the heading far from 0 (wound up 1000 rad) and across +-pi:");
    Rig r=Nix(1000.0f,true);
    const float start=Frame(r,0.0f,0.0f);
    float worst=0.0f;
    for(int i=0;i<45;++i){const float t=Frame(r,-80.0f*kDeg,0.0f);const float d=std::fabs(nixtwist::Wrap(t-start));if(d>worst)worst=d;}
    Expect(worst<0.05f*kDeg,"worst drift at 1000 rad, legs turning 60 deg left",worst,0.0f);
    Rig w=Nix(kPi-0.2f,true);
    const float s2=Frame(w,0.0f,0.0f);
    worst=0.0f;
    for(int i=0;i<30;++i){const float t=Frame(w,50.0f*kDeg,0.0f);const float d=std::fabs(nixtwist::Wrap(t-s2));if(d>worst)worst=d;}
    Expect(worst<0.01f*kDeg,"worst drift with the heading crossing +pi",worst,0.0f);
}

void NoStops() {
    std::puts("an aim axis with no stops (a full turn, the stock wraps it):");
    Rig r=Nix(0.0f,true,-kPi,kPi);
    Frame(r,0.0f,0.0f);
    for(int i=0;i<150;++i)Frame(r,90.0f*kDeg,0.0f);
    Frame(r,0.0f,0.0f);
    const nixtwist::Stops s=nixtwist::TwistStops(-kPi,kPi);
    Expect(Near(s.hi,120.0f*kDeg) && Near(s.lo,-120.0f*kDeg),"the stops given",s.hi,120.0f*kDeg);
    Expect(Near(r.axis.angle,-120.0f*kDeg),"the twist after the legs turned 225",r.axis.angle,-120.0f*kDeg);
}

void Stock() {
    std::puts("NixTorsoTwist=0 (stock): the torso turns with the legs:");
    Rig r=Nix(0.0f,false);
    const float start=Frame(r,0.0f,0.0f);
    float t=start;
    for(int i=0;i<30;++i)t=Frame(r,90.0f*kDeg,0.0f);
    t=Frame(r,0.0f,0.0f);
    Expect(Near(t,start+45.0f*kDeg),"the torso's world yaw",t,start+45.0f*kDeg);
}

// The turret camera steering the torso onto a world heading `target` while the legs turn at `legs` rad/s, the hold
// `held` or not: the worst miss of the torso's drawn world yaw over the last second (after the first two to settle).
float CameraSteered(bool held,float legs,float target) noexcept {
    const float p[3]={0.2f,0.15f,1.6f*kDt};   // brake, accel, top (rad/frame): a Nix-like torso, faster than the legs
    crew::tcam::Axis a{-70.0f*kDeg,70.0f*kDeg,0.0f,0.0f};
    float heading=0.0f,last=0.0f,lastWant=0.0f,drift=0.0f,worst=0.0f;
    bool started=false;
    for(int f=0;f<180;++f) {
        const float drawn=heading;
        if(held && started)a.angle=nixtwist::Hold(a.angle,nixtwist::Wrap(heading-last),nixtwist::TwistStops(a.lo,a.hi));
        last=heading;
        // turretcam.cpp Steer: the want in the hull's frame of this frame's matrix, its drift a frame fed forward.
        const float want=nixtwist::Wrap(target-heading);
        if(started)drift+=(std::fmax(-0.2f,std::fmin(0.2f,want-lastWant))-drift)*0.5f;
        lastWant=want;started=true;
        crew::tcam::AxisStep(a,crew::tcam::AxisCommand(want-a.angle,a.rate,drift,p),p);
        const float miss=std::fabs(nixtwist::Wrap(drawn+a.angle-target));
        if(f>=120 && miss>worst)worst=miss;
        heading+=legs*kDt;
    }
    return worst;
}

void CameraAndHold() {
    std::puts("the turret camera steers the torso onto a point 10 deg off while the legs turn 12 deg/s the other way:");
    const float alone=CameraSteered(false,-12.0f*kDeg,10.0f*kDeg),both=CameraSteered(true,-12.0f*kDeg,10.0f*kDeg);
    Expect(alone<0.5f*kDeg,"the hold standing aside: the worst miss",alone,0.0f);
    Expect(both>2.0f*alone && both>0.25f*kDeg,"the hold on too (the turn taken out twice): the worst miss is larger",both,alone);
}

void AimRows() {
    std::puts("the torso's aim in the vehicle's frame:");
    float d[3];
    nixtwist::AimLocal(0.0f,0.0f,d);
    Expect(std::fabs(d[2]-1.0f)<1e-6f && std::fabs(d[0])<1e-6f,"straight: forward (0, 0, 1), its z",d[2],1.0f);
    nixtwist::AimLocal(30.0f*kDeg,0.0f,d);
    Expect(Near(std::atan2(d[0],d[2]),30.0f*kDeg),"twist 30: its yaw atan2(x, z)",std::atan2(d[0],d[2]),30.0f*kDeg);
    nixtwist::AimLocal(0.0f,-20.0f*kDeg,d);
    Expect(Near(std::asin(d[1]),20.0f*kDeg),"pitch axis -20 (negative up): its elevation",std::asin(d[1]),20.0f*kDeg);
}
}  // namespace

int main() {
    LegsUnderStillMouse();
    MouseAlone();
    MouseWhileTurning();
    FarHeadings();
    NoStops();
    Stock();
    CameraAndHold();
    AimRows();
    std::printf("%s (%d wrong)\n",failures ? "FAILED" : "all as the controls mean",failures);
    return failures ? 1 : 0;
}
