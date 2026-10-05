// The mouse-aim flight of a stock helicopter (src/heliaim.h), checked without the game: the plugin's own law (aim::Move,
// aim::Fly, and the NPC pilot's StockStick / StockThrottle / StockYaw that heli.cpp AimFly writes the input block with)
// flown against a stand-in of the stock heli's flight law as docs/aircraft-re.md and docs/heli-input-re.md §2a give it:
//  - heading angle a, nose (sin a, 0, cos a), heading rows: row 2 the nose, row 0 its right (-cos a, 0, sin a) (the
//    stock's -LX moves it right for LX > 0, docs/player-jet-re.md §2);
//  - horizontal: a frame v = d v + b (k (lateral row0 + forward row2) - d v), d 0.999, b and k as PlayerAssist makes them
//    for PlayerHeliStopSec 1 at the 506's stock top speed (18.5 m/s); nothing horizontal on the ground;
//  - yaw: + grows a (the stock writes -RX: the mouse to the right turns it right, a falling), the turn rate easing to
//    kYawMost x the input over kYawLag s (the logs: 45-70 deg/s at full, ~0.8 s behind the input);
//  - vertical (aircraft-re.md "垂直", M): the rotor eases to the throttle at 0.001 of the gap a frame up, 0.0007 down, never
//    under the idle 0.13; a frame vy = lerp(1, 0.95, t) vy + rotor L - g / 60, t = (rotor - idle) / (hover - idle) within
//    0..1, L such that the hover rotor 0.288 holds it (all of it climbs ~8 m/s, at idle it falls undamped); the ground at
//    y 0 (contact: stopped, no horizontal input).
// Each scenario starts at a hover 50 m up, the nose along +Z, and prints where it went in its starting frame (forward,
// right, up), its compass turn (+ right) and speed, and whether that is the sign the keys and the mouse mean. Exit code
// 1 when one is not. Built on request only: cmake --build build --target heli_aim_check && build\heli_aim_check.exe
#include "../src/heliaim.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
using namespace crew;
// heli.cpp's gains (kBrakeGain, kClimbGain / kHoverLearn / kRotorGain, kYawDamp / kYawFeed) and AimFly's constants.
constexpr float kBrakeGain=0.12f,kYawDamp=1.2f,kYawFeed=1.1f,kPlayerClimb=6.0f,kPi=3.14159265f;
constexpr aim::RotorGains kGains{0.08f,0.03f,4.0f};
// The stand-in stock heli (see the top).
constexpr float kDamp=0.999f,kTop=18.5f,kStopFrames=60.0f,kYawMost=1.0f,kYawLag=0.8f,kDt=1.0f/60.0f;
constexpr float kHover=0.288f,kIdle=0.13f,kFall=9.8f/60.0f,kLift=kFall/kHover,kHoverDamp=0.95f;

struct Heli { float pos[3],vel[3],a,yawRate,rotor; };
struct Inputs { float lateral,forward,throttle,yaw; };

void Step(Heli& h,const Inputs& in) noexcept {
    const bool ground=h.pos[1]<=0.0f;
    const float blend=1.0f-(1.0f-1.0f/kStopFrames)/kDamp,k=kTop/(kStopFrames*blend);   // PlayerAssist's params
    const float fwd[3]={std::sin(h.a),0.0f,std::cos(h.a)},right[3]={-std::cos(h.a),0.0f,std::sin(h.a)};
    if(!ground)for(int i=0;i<3;i+=2) {
        const float want=k*(in.lateral*right[i]+in.forward*fwd[i]);
        h.vel[i]=kDamp*h.vel[i]+blend*(want-kDamp*h.vel[i]);
    } else h.vel[0]=h.vel[2]=0.0f;
    h.yawRate+=(kYawMost*in.yaw-h.yawRate)*(kDt/kYawLag);
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
struct Pilot { float aim[3]; aim::Hold hold; float hover,yawPrev,yawRate,vel[3]; };
Inputs Fly(Pilot& p,const Heli& h,const Hand& hand,float t,int frame) noexcept {
    const bool held=t>=hand.from && t<hand.to;
    const aim::Keys keys{held ? hand.fore : 0.0f,held ? hand.side : 0.0f,held ? hand.vert : 0.0f};
    const int mouseFrame=static_cast<int>(std::lround(hand.mouseAt/kDt));
    if(frame>=mouseFrame && frame<mouseFrame+hand.frames)aim::Move(p.aim,hand.mx,hand.my,aim::kPerUnit);
    const float fwd[3]={std::sin(h.a),0.0f,std::cos(h.a)},right[3]={-std::cos(h.a),0.0f,std::sin(h.a)};
    const bool grounded=h.pos[1]<=0.0f;
    const aim::Want w=aim::Fly(p.hold,p.aim,fwd,h.pos,h.vel,keys,kTop,kPlayerClimb,grounded,h.pos[1],kDt);
    Inputs in{};
    aim::StockStick(w.vel,h.vel,kTop,kBrakeGain,fwd,right,&in.forward,&in.lateral);
    float off=std::atan2(w.face[0],w.face[2])-h.a;
    while(off>kPi)off-=2*kPi;
    while(off<-kPi)off+=2*kPi;
    p.yawRate+=((h.a-p.yawPrev)/kDt-p.yawRate)*0.3f;p.yawPrev=h.a;
    in.yaw=aim::StockYaw(off,p.yawRate,0.0f,kYawDamp,kYawFeed);
    const bool learn=p.hold.holding && std::fabs(p.hold.y-h.pos[1])<6.0f;
    in.throttle=aim::StockThrottle(w.climb,h.vel[1],h.rotor,&p.hover,learn,kDt,kGains);
    return in;
}

struct Expect { float fwd,right,up,turn; };   // the sign each must have: +1, -1, 0 (about still: |x| under the slack), 2 (any)
struct Scenario { const char* name; const char* means; Hand hand; float seconds; Expect expect; };

bool Matches(float x,float want,float slack) noexcept {
    if(want==2.0f)return true;
    if(want==0.0f)return std::fabs(x)<slack;
    return x*want>slack;
}

bool Run(const Scenario& sc) noexcept {
    Heli h{{0.0f,50.0f,0.0f},{0.0f,0.0f,0.0f},0.0f,0.0f,kHover};
    Pilot p{};
    p.aim[0]=0.0f;p.aim[1]=0.0f;p.aim[2]=1.0f;p.hover=kHover;
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

int main() {
    //                                              fore side vert from to   mx    my   mouseAt frames  s     fwd right up turn
    const Scenario scenarios[]={
        {"nothing",                "hovers where it is",              {0, 0, 0, 0,0,     0, 0, 0,0},   10, {0, 0, 0, 0}},
        {"W 1.5 s, let go",        "flies on forward at the set speed",{1, 0, 0, 0,1.5f,  0, 0, 0,0},   15, {1, 0, 0, 0}},
        {"S 1.5 s from a hover",   "backs off",                       {-1,0, 0, 0,1.5f,  0, 0, 0,0},   10, {-1,0, 0, 0}},
        {"W 1.5 s, mouse up 30 deg","climbs along the aim",           {1, 0, 0, 0,1.5f,  0, 1, 2,10},  15, {1, 0, 1, 0}},
        {"W 1.5 s, mouse down 30 deg","descends along the aim",       {1, 0, 0, 0,1.5f,  0,-1, 2,10},  10, {1, 0,-1, 0}},
        {"mouse up 30 deg, hover", "climbs gently",                   {0, 0, 0, 0,0,     0, 1, 0,10},  10, {0, 0, 1, 0}},
        {"mouse right 57 deg",     "turns right onto the aim",        {0, 0, 0, 0,0,     1, 0, 0,20},  10, {2, 2, 0, 1}},
        {"mouse left 57 deg",      "turns left onto the aim",         {0, 0, 0, 0,0,    -1, 0, 0,20},  10, {2, 2, 0,-1}},
        {"D 3 s",                  "sidesteps right",                 {0, 1, 0, 0,3,     0, 0, 0,0},   6,  {0, 1, 0, 0}},
        {"A 3 s",                  "sidesteps left",                  {0,-1, 0, 0,3,     0, 0, 0,0},   6,  {0,-1, 0, 0}},
        {"Space 4 s",              "climbs, then holds",              {0, 0, 1, 0,4,     0, 0, 0,0},   12, {0, 0, 1, 0}},
        {"brake key 10 s",         "descends (the rotor spins down slow)",{0, 0,-1, 0,10,  0, 0, 0,0},   14, {0, 0,-1, 0}},
    };
    int bad=0;
    for(const auto& sc:scenarios)bad+=Run(sc) ? 0 : 1;
    // One press of S from a forward speed stops at 0 (the detent), it does not back off.
    Heli h{{0.0f,50.0f,0.0f},{0.0f,0.0f,0.0f},0.0f,0.0f,kHover};
    Pilot p{};
    p.aim[2]=1.0f;p.hover=kHover;
    const Hand w{1,0,0,0,1.5f,0,0,0,0},s{-1,0,0,0,6,0,0,0,0};
    for(int f=0;f<static_cast<int>(10/kDt);++f)Step(h,Fly(p,h,w,static_cast<float>(f)*kDt,f));
    const float set=p.hold.speed;
    for(int f=0;f<static_cast<int>(10/kDt);++f)Step(h,Fly(p,h,s,static_cast<float>(f)*kDt,f));
    const float speed=std::sqrt(h.vel[0]*h.vel[0]+h.vel[2]*h.vel[2]);
    const bool stop=set>5.0f && p.hold.speed==0.0f && speed<0.5f;
    std::printf("%-26s %-34s set %.1f -> %.1f m/s, flying %.2f m/s  %s\n","W, then S held 6 s","stops at 0 (no back speed)",set,
                p.hold.speed,speed,stop ? "ok" : "WRONG");
    bad+=stop ? 0 : 1;
    std::printf("%s\n",bad ? "SOME SIGNS WRONG" : "all signs as meant");
    return bad ? 1 : 0;
}
