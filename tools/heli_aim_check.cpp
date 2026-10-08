// The mouse-aim flight of a stock helicopter (src/heliaim.h), checked without the game: the plugin's own law (aim::Move,
// aim::Fly, and the NPC pilot's StockStick / StockThrottle / StockYaw that heli.cpp AimFly writes the input block with)
// flown against a stand-in of the stock heli's flight law as docs/aircraft-re.md and docs/heli-input-re.md §2a give it:
//  - heading angle a, nose (sin a, 0, cos a), heading rows: row 2 the nose, row 0 its right (-cos a, 0, sin a) (the
//    stock's -LX moves it right for LX > 0, docs/player-jet-re.md §2);
//  - horizontal: a frame v = d v + b (k (lateral row0 + forward row2) - d v), d 0.999, b and k as PlayerAssist makes them
//    for PlayerHeliStopSec 1 at the 506's stock top speed (18.5 m/s); nothing horizontal on the ground;
//  - yaw: +0x1604 eases toward maxAngle*input, then the angular spring asks for angle*spring*60 rad/s.
//    The body blends its angular velocity separately (native audit: tests/heli_yaw_native_audit.py).
//    Positive input grows a; the "max yaw < 0" scenario verifies the native parameter's sign;
//  - vertical (aircraft-re.md "垂直", M): the rotor eases to the throttle at 0.001 of the gap a frame up, 0.0007 down, never
//    under the idle 0.13; a frame vy = lerp(1, 0.95, t) vy + rotor L - g / 60, t = (rotor - idle) / (hover - idle) within
//    0..1, L such that the hover rotor 0.288 holds it (all of it climbs ~8 m/s, at idle it falls undamped); the ground at
//    y 0 (contact: stopped, no horizontal input).
// Each scenario starts at a hover 50 m up, the nose along +Z, and prints where it went in its starting frame (forward,
// right, up), its compass turn (+ right) and speed, and whether that is the sign the keys and the mouse mean. Exit code
// 1 when one is not. After them: one press of S stops at 0, and W held through the lift-off sets no speed (heli.cpp
// kLiftOffMs: no lurch when the lock ends). Built on request only: cmake --build build --target heli_aim_check && build\heli_aim_check.exe
#include "../src/heliaim.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
using namespace crew;
// heli.cpp's gains (kBrakeGain, kClimbGain / kHoverLearn / kRotorGain) and AimFly's constants.
constexpr float kBrakeGain=0.12f,kPlayerClimb=6.0f,kPi=3.14159265f;
constexpr aim::RotorGains kGains{0.08f,0.03f,4.0f};
// The stand-in stock heli (see the top).
// Brute's slow native angular response; kYawMost is the requested player turn rate.
constexpr float kDamp=0.999f,kTop=18.5f,kStopFrames=60.0f,kYawMost=50.0f*3.14159265f/180.0f,kYawBlend=0.005f,kSpring=0.15f,kAngularBlend=0.025f,kDt=1.0f/60.0f;
// The screen: the camera rides behind the nose (docs/camera-re.md 3b: the seat's anchors on the heli), so the aim's
// mark stays on the screen while it is at most kScreenYaw off the nose (MoveOnScreen in the game).
constexpr float kScreenYaw=0.55f;
constexpr float kHover=0.288f,kIdle=0.13f,kFall=9.8f/60.0f,kLift=kFall/kHover,kHoverDamp=0.95f;

struct Heli { float pos[3],vel[3],a,yawRate,rotor,maxYaw; float yawOffset=0.0f; };
float Wrap(float a) noexcept {
    while(a>kPi)a-=2.0f*kPi;
    while(a<-kPi)a+=2.0f*kPi;
    return a;
}
struct Inputs { float lateral,forward,throttle,yaw; };

void Step(Heli& h,const Inputs& in) noexcept {
    const bool ground=h.pos[1]<=0.0f;
    const float blend=1.0f-(1.0f-1.0f/kStopFrames)/kDamp,k=kTop/(kStopFrames*blend);   // PlayerAssist's params
    const float fwd[3]={std::sin(h.a),0.0f,std::cos(h.a)},right[3]={-std::cos(h.a),0.0f,std::sin(h.a)};
    if(!ground)for(int i=0;i<3;i+=2) {
        const float want=k*(in.lateral*right[i]+in.forward*fwd[i]);
        h.vel[i]=kDamp*h.vel[i]+blend*(want-kDamp*h.vel[i]);
    } else h.vel[0]=h.vel[2]=0.0f;
    h.yawOffset+=(h.maxYaw*in.yaw-h.yawOffset)*kYawBlend;
    h.yawRate+=(h.yawOffset*kSpring*60.0f-h.yawRate)*kAngularBlend;
    if(!ground)h.a+=h.yawRate*kDt;
    h.rotor+=(in.throttle>h.rotor ? 0.001f : 0.0007f)*(in.throttle-h.rotor);
    if(h.rotor<kIdle)h.rotor=kIdle;
    const float t=std::fmin(std::fmax((h.rotor-kIdle)/(kHover-kIdle),0.0f),1.0f);
    h.vel[1]=(1.0f+(kHoverDamp-1.0f)*t)*h.vel[1]+h.rotor*kLift-kFall;
    for(int i=0;i<3;++i)h.pos[i]+=h.vel[i]*kDt;
    if(h.pos[1]<0.0f){h.pos[1]=0.0f;if(h.vel[1]<0.0f)h.vel[1]=0.0f;}
}

// The player: what is held from `from` s to `to` s (keys), and the mouse moved `mx`, `my` a frame for `frames` frames
// from `mouseAt` s.
struct Hand { float fore,side,vert; float from,to; float mx,my; float mouseAt; int frames; };

// AimFly's frame on the stand-in (heli.cpp AimFly, the same calls in the same order).
constexpr float kLiftOffSec=1.5f;   // heli.cpp kLiftOffMs
struct Pilot { float aim[3]; aim::Hold hold; float hover,yawPrev,yawRate,vel[3],groundAt; };
Inputs Fly(Pilot& p,const Heli& h,const Hand& hand,float t,int frame) noexcept {
    const bool held=t>=hand.from && t<hand.to;
    const aim::Keys keys{held ? hand.fore : 0.0f,held ? hand.side : 0.0f,held ? hand.vert : 0.0f};
    const int mouseFrame=static_cast<int>(std::lround(hand.mouseAt/kDt));
    if(frame>=mouseFrame && frame<mouseFrame+hand.frames) {
        // The stand-in's MoveOnScreen: a turn that takes the aim past the screen's edge is not taken, the climb is.
        float a[3];std::memcpy(a,p.aim,12);
        aim::Move(a,hand.mx,0.0f,aim::kPerUnit);
        const float off=std::fabs(Wrap(std::atan2(a[0],a[2])-h.a)),was=std::fabs(Wrap(std::atan2(p.aim[0],p.aim[2])-h.a));
        if(off<=kScreenYaw || off<was)std::memcpy(p.aim,a,12);
        aim::Move(p.aim,0.0f,hand.my,aim::kPerUnit);
    }
    const float fwd[3]={std::sin(h.a),0.0f,std::cos(h.a)},right[3]={-std::cos(h.a),0.0f,std::sin(h.a)};
    const bool grounded=h.pos[1]<=0.0f;
    if(grounded)p.groundAt=t;
    const bool lifting=t-p.groundAt<kLiftOffSec;
    const aim::Want w=aim::Fly(p.hold,p.aim,fwd,h.pos,h.vel,keys,kTop,kPlayerClimb,grounded,lifting,h.pos[1],kDt);
    Inputs in{};
    aim::StockStick(w.vel,h.vel,kTop,kBrakeGain,fwd,right,&in.forward,&in.lateral);
    if(lifting)in.forward=in.lateral=0.0f;
    float off=std::atan2(w.face[0],w.face[2])-h.a;
    while(off>kPi)off-=2*kPi;
    while(off<-kPi)off+=2*kPi;
    p.yawRate+=((h.a-p.yawPrev)/kDt-p.yawRate)*0.3f;p.yawPrev=h.a;
    in.yaw=aim::PlayerYawInput(off,p.yawRate,kYawMost,h.maxYaw,h.yawOffset,kYawBlend,kSpring);
    const bool learn=p.hold.holding && std::fabs(p.hold.y-h.pos[1])<6.0f;
    in.throttle=aim::StockThrottle(w.climb,h.vel[1],h.rotor,&p.hover,learn,kDt,kGains);
    return in;
}

struct Expect { float fwd,right,up,turn; };   // the sign each must have: +1, -1, 0 (about still: |x| under the slack), 2 (any)
struct Scenario { const char* name; const char* means; Hand hand; float seconds; Expect expect; float maxYaw; };

bool Matches(float x,float want,float slack) noexcept {
    if(want==2.0f)return true;
    if(want==0.0f)return std::fabs(x)<slack;
    return x*want>slack;
}

bool Run(const Scenario& sc) noexcept {
    Heli h{{0.0f,50.0f,0.0f},{0.0f,0.0f,0.0f},0.0f,0.0f,kHover,sc.maxYaw};
    Pilot p{};
    p.aim[0]=0.0f;p.aim[1]=0.0f;p.aim[2]=1.0f;p.hover=kHover;p.groundAt=-10.0f;
    const int frames=static_cast<int>(sc.seconds/kDt);
    for(int f=0;f<frames;++f)Step(h,Fly(p,h,sc.hand,static_cast<float>(f)*kDt,f));
    // In the starting frame: forward +Z, right -X (heliaim.h RightOf), up +Y; the compass turn + right (a falling).
    const float fwd=h.pos[2],right=-h.pos[0],up=h.pos[1]-50.0f,turn=-h.a*180.0f/kPi;
    const float speed=std::sqrt(h.vel[0]*h.vel[0]+h.vel[2]*h.vel[2]);
    const bool ok=Matches(fwd,sc.expect.fwd,3.0f) && Matches(right,sc.expect.right,3.0f) && Matches(up,sc.expect.up,2.0f) &&
                  Matches(turn,sc.expect.turn,5.0f);
    std::printf("%-26s %-34s fwd %+7.1f m  right %+7.1f m  up %+6.1f m  turn %+6.1f deg  %5.1f m/s  set %+5.1f  %s\n",sc.name,sc.means,fwd,
                right,up,turn,speed,p.hold.speed,ok ? "ok" : "WRONG");
    return ok;
}
}  // namespace

// The mouse swept right and kept going (1 unit a frame for 4 s, aim::kPerUnit 0.05 rad a unit), held at the screen's
// edge as the game does, the heli at a hover: the turn rate it keeps from 2 s to 4 s. The user (2026-10-06): "the mouse
// moves and nothing changes, it does not follow the mouse"; their log had the 506 turning 5-17 deg/s. Pushed on, the
// heading must turn at kFollowShare of the requested player turn rate or more.
constexpr float kFollowShare=0.8f;
float SweepRate() noexcept {
    Heli h{{0.0f,50.0f,0.0f},{0.0f,0.0f,0.0f},0.0f,0.0f,kHover,kYawMost};
    Pilot p{};
    p.aim[2]=1.0f;p.hover=kHover;p.groundAt=-10.0f;
    const Hand sweep{0,0,0,0,0,1.0f,0,0,240};
    float at2=0.0f;
    const int frames=static_cast<int>(4/kDt);
    for(int f=0;f<frames;++f) {
        if(f==frames/2)at2=h.a;
        Step(h,Fly(p,h,sweep,static_cast<float>(f)*kDt,f));
    }
    return -(h.a-at2)/2.0f;   // rad/s, + to the right
}
bool FollowsTheMouse() noexcept {
    const float now=SweepRate(),want=kFollowShare*kYawMost;
    const bool ok=now>=want;
    std::printf("%-26s %-34s turns %.1f deg/s (want >= %.0f, its most %.0f); native lag/spring  %s\n","mouse swept right 4 s",
                "the heading follows the mouse",now*180.0f/kPi,want*180.0f/kPi,kYawMost*180.0f/kPi,ok ? "ok" : "WRONG");
    return ok;
}

// The rotor that holds the height (aim::HoverRotor) from the SGOs' lift as it is in memory (heli_movement[0][1] / 60):
// the 506 / 409 / 410 (34) at the NPC's learned 0.424, the 602 (70) near what the NPC learned flying it (0.23-0.26
// climbing), never pinned at the clamp's 1.0 (the old guess: the 602 climbed on full throttle).
bool HoverRotors() noexcept {
    const float h506=aim::HoverRotor(34.0f/60.0f,1.0f),h602=aim::HoverRotor(70.0f/60.0f,1.0f);
    const bool ok=std::fabs(h506-0.424f)<0.005f && h602>0.18f && h602<0.26f;
    std::printf("%-26s %-34s 506 %.3f  602 %.3f  %s\n","hover rotor","from the lift as it is in memory",h506,h602,ok ? "ok" : "WRONG");
    return ok;
}

// MoveOnScreen: the aim at the screen's right edge, the mouse up and right: the turn is not taken, the climb is.
bool MovesPerAxis() noexcept {
    // A plain projection: the camera at the origin looking +z, row vectors, clip (x, y, 0, z): screen x / z, y / z.
    const float vp[16]={1,0,0,0, 0,1,0,0, 0,0,0,1, 0,0,0,0};
    const float pos[3]={0.0f,0.0f,0.0f},dir[3]={0.0f,0.0f,1.0f};
    const float edge=std::atan(0.84f);
    float a[3]={-std::sin(edge),0.0f,std::cos(edge)};   // at the edge's side (x = -0.84 on the screen)
    const float before=a[1];
    const float mx=-a[0]>0.0f ? 1.0f : -1.0f;   // the turn that takes it on past the edge (heliaim.h RightOf: right lowers x)
    aim::MoveOnScreen(vp,pos,a,mx,1.0f,aim::kPerUnit,dir,800.0f,0.85f);
    const bool on=aim::OnScreen(vp,pos,a,800.0f,0.85f),ok=a[1]>before+0.03f && on;
    std::printf("%-26s %-34s elevation %+.3f -> %+.3f, on screen %d  %s\n","mouse up and out at the edge","still climbs (axis by axis)",
                before,a[1],on ? 1 : 0,ok ? "ok" : "WRONG");
    return ok;
}

int main() {
    //                                              fore side vert from to   mx    my   mouseAt frames  s     fwd right up turn
    const Scenario scenarios[]={
        {"nothing",                "hovers where it is",              {0, 0, 0, 0,0,     0, 0, 0,0},   10, {0, 0, 0, 0}, kYawMost},
        {"W 1.5 s, let go",        "flies on forward at the set speed",{1, 0, 0, 0,1.5f,  0, 0, 0,0},   15, {1, 0, 0, 0}, kYawMost},
        {"S 1.5 s from a hover",   "backs off",                       {-1,0, 0, 0,1.5f,  0, 0, 0,0},   10, {-1,0, 0, 0}, kYawMost},
        {"W 1.5 s, mouse up 30 deg","climbs along the aim",           {1, 0, 0, 0,1.5f,  0, 1, 2,10},  15, {1, 0, 1, 0}, kYawMost},
        {"W 1.5 s, mouse down 30 deg","descends along the aim",       {1, 0, 0, 0,1.5f,  0,-1, 2,10},  10, {1, 0,-1, 0}, kYawMost},
        {"mouse up 30 deg, hover", "climbs gently",                   {0, 0, 0, 0,0,     0, 1, 0,10},  10, {0, 0, 1, 0}, kYawMost},
        {"mouse right 57 deg",     "turns right onto the aim",        {0, 0, 0, 0,0,     1, 0, 0,20},  10, {2, 2, 0, 1}, kYawMost},
        {"mouse left 57 deg",      "turns left onto the aim",         {0, 0, 0, 0,0,    -1, 0, 0,20},  10, {2, 2, 0,-1}, kYawMost},
        {"D 3 s",                  "sidesteps right",                 {0, 1, 0, 0,3,     0, 0, 0,0},   6,  {0, 1, 0, 0}, kYawMost},
        {"A 3 s",                  "sidesteps left",                  {0,-1, 0, 0,3,     0, 0, 0,0},   6,  {0,-1, 0, 0}, kYawMost},
        {"Space 4 s",              "climbs, then holds",              {0, 0, 1, 0,4,     0, 0, 0,0},   12, {0, 0, 1, 0}, kYawMost},
        {"mouse right, max yaw < 0","turns right onto the aim (its sign)",{0, 0, 0, 0,0,   1, 0, 0,20},  10, {2, 2, 0, 1},-kYawMost},
        {"mouse left, max yaw < 0", "turns left onto the aim (its sign)", {0, 0, 0, 0,0,  -1, 0, 0,20},  10, {2, 2, 0,-1},-kYawMost},
        {"brake key 10 s",         "descends (the rotor spins down slow)",{0, 0,-1, 0,10,  0, 0, 0,0},   14, {0, 0,-1, 0}, kYawMost},
    };
    int bad=0;
    for(const auto& sc:scenarios)bad+=Run(sc) ? 0 : 1;
    // One press of S from a forward speed stops at 0 (the detent), it does not back off.
    Heli h{{0.0f,50.0f,0.0f},{0.0f,0.0f,0.0f},0.0f,0.0f,kHover,kYawMost};
    Pilot p{};
    p.aim[2]=1.0f;p.hover=kHover;p.groundAt=-10.0f;
    const Hand w{1,0,0,0,1.5f,0,0,0,0},s{-1,0,0,0,6,0,0,0,0};
    for(int f=0;f<static_cast<int>(10/kDt);++f)Step(h,Fly(p,h,w,static_cast<float>(f)*kDt,f));
    const float set=p.hold.speed;
    for(int f=0;f<static_cast<int>(10/kDt);++f)Step(h,Fly(p,h,s,static_cast<float>(f)*kDt,f));
    const float speed=std::sqrt(h.vel[0]*h.vel[0]+h.vel[2]*h.vel[2]);
    const bool stop=set>5.0f && p.hold.speed==0.0f && speed<0.5f;
    std::printf("%-26s %-34s set %.1f -> %.1f m/s, flying %.2f m/s  %s\n","W, then S held 6 s","stops at 0 (no back speed)",set,
                p.hold.speed,speed,stop ? "ok" : "WRONG");
    bad+=stop ? 0 : 1;
    // On the ground, Space and W held 1.2 s: it lifts off straight up, W in the lift-off's kLiftOffSec sets nothing (the
    // setpoint stays 0), so when the lock ends it does not lurch forward to a speed built up meanwhile.
    Heli g{{0.0f,0.0f,0.0f},{0.0f,0.0f,0.0f},0.0f,0.0f,kHover,kYawMost};
    Pilot q{};
    q.aim[2]=1.0f;q.hover=kHover;
    const Hand lift{1,0,1,0,1.2f,0,0,0,0};
    float setMost=0.0f;
    for(int f=0;f<static_cast<int>(5/kDt);++f) {
        Step(g,Fly(q,g,lift,static_cast<float>(f)*kDt,f));
        setMost=std::fmax(setMost,q.hold.speed);
    }
    const bool still=g.pos[1]>0.5f && setMost==0.0f && std::fabs(g.pos[2])<1.0f;
    std::printf("%-26s %-34s up %+.1f m, fwd %+.1f m, most set %.1f m/s  %s\n","Space + W 1.2 s from ground","lifts straight, no speed set",
                g.pos[1],g.pos[2],setMost,still ? "ok" : "WRONG");
    bad+=still ? 0 : 1;
    bad+=FollowsTheMouse() ? 0 : 1;
    bad+=HoverRotors() ? 0 : 1;
    bad+=MovesPerAxis() ? 0 : 1;
    std::printf("%s\n",bad ? "SOME SIGNS WRONG" : "all signs as meant");
    return bad ? 1 : 0;
}
