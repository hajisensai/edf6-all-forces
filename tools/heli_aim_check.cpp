// The mouse-aim flight of a stock helicopter (src/heliaim.h), checked without the game: the plugin's own law as heli.cpp
// AimFly runs it (aim::Move, the Instructor's attitude wanted, StockStick / CollectiveThrottle / PlayerYawInput, the
// pitch and the coordinated roll PlayerAttitudeHook hands the attitude function) flown against a stand-in of the stock
// heli's flight law as docs/aircraft-re.md and docs/heli-input-re.md §2a give it:
//  - heading angle a, nose (sin a, 0, cos a), heading rows: row 2 the nose, row 0 its right (-cos a, 0, sin a) (the
//    stock's -LX moves it right for LX > 0, docs/player-jet-re.md §2);
//  - horizontal: a frame v = d v + b (k (lateral row0 + forward row2) - d v), d 0.999, b and k as PlayerAssist makes them
//    for PlayerHeliStopSec 1 at the 506's stock top speed (18.5 m/s); nothing horizontal on the ground;
//  - attitude: pitch = lerp(pitch, maxTilt x forward input, tilt smoothing) (0x654E69; the input the hook hands it), the
//    tilt smoothing PlayerMouseTune's kPlayerTiltSmooth (the stock 0.005 for the "before" law);
//  - yaw: +0x1604 eases toward maxAngle*input, then the angular spring asks for angle*spring*60 rad/s.
//    The body blends its angular velocity separately (native audit: tests/heli_yaw_native_audit.py).
//    Positive input grows a; the "max yaw < 0" scenario verifies the native parameter's sign;
//  - vertical (aircraft-re.md "垂直", M): the rotor eases to the throttle at its up rate of the gap a frame (down rate
//    going down; 0x656744 / 0x656770): the stock 0.001 / 0.0007, the player's aim::kPlayerRotorRate (PlayerMouseTune),
//    never under the idle 0.13; a frame vy = lerp(1, 0.95, t) vy + rotor L - g / 60, t = (rotor - idle) / (hover - idle)
//    within 0..1, L such that the hover rotor 0.288 holds it; the ground at y 0 (contact: stopped, no horizontal input).
// Each sign scenario starts at a hover 50 m up, the nose along +Z, and prints where it went in its starting frame
// (forward, right, up), its compass turn (+ right) and speed, and whether that is the sign the keys and the mouse mean.
// Then the step responses (the user, 2026-10-09: 「这个直升机的飞控依旧怪怪的，参考战雷做吧」): the collective's climb and
// hold, the cyclic's speed, the nose following the mouse, the coordinated bank; each beside the law it replaces ("before":
// aim::Fly + StockThrottle on the stock rotor), whose porpoising is the root cause measured. Exit code 1 when one fails.
// Built on request (offline_checks): cmake --build build --target heli_aim_check && build\heli_aim_check.exe
#include "../src/heliaim.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
using namespace crew;
// heli.cpp's gains (kBrakeGain, the NPC's kClimbGain / kHoverLearn / kRotorGain for the "before" law), AimFly's constants
// and PlayerMouseTune's tilt smoothing.
constexpr float kBrakeGain=0.12f,kPlayerClimb=6.0f,kPi=3.14159265f,kTiltSmooth=0.05f,kStockTilt=0.005f,kMaxTilt=0.611f;
constexpr aim::RotorGains kGains{0.08f,0.03f,4.0f};
// The stand-in stock heli (see the top).
// Brute's slow native angular response; kYawMost is the requested player turn rate.
constexpr float kDamp=0.999f,kTop=18.5f,kStopFrames=60.0f,kYawMost=50.0f*3.14159265f/180.0f,kYawBlend=0.005f,kSpring=0.15f,kAngularBlend=0.025f,kDt=1.0f/60.0f;
// The screen: the camera rides behind the nose (docs/camera-re.md 3b: the seat's anchors on the heli), so the aim's
// mark stays on the screen while it is at most kScreenYaw off the nose (MoveOnScreen in the game).
constexpr float kScreenYaw=0.55f;
constexpr float kHover=0.288f,kIdle=0.13f,kFall=9.8f/60.0f,kLift=kFall/kHover,kHoverDamp=0.95f;

struct Heli {
    float pos[3],vel[3],a,yawRate,rotor,maxYaw;
    float yawOffset=0.0f,pitch=0.0f,roll=0.0f;
    float up=aim::kPlayerRotorRate,down=aim::kPlayerRotorRate,tilt=kTiltSmooth;   // PlayerMouseTune's (Before: the stock's)
};
float Wrap(float a) noexcept {
    while(a>kPi)a-=2.0f*kPi;
    while(a<-kPi)a+=2.0f*kPi;
    return a;
}
// The input block, and what the attitude function is handed (heli.cpp PlayerAttitudeHook: pitch and roll).
struct Inputs { float lateral,forward,throttle,yaw,pitchIn,rollIn; };

void Step(Heli& h,const Inputs& in) noexcept {
    const bool ground=h.pos[1]<=0.0f;
    const float blend=1.0f-(1.0f-1.0f/kStopFrames)/kDamp,k=kTop/(kStopFrames*blend);   // PlayerAssist's params
    const float fwd[3]={std::sin(h.a),0.0f,std::cos(h.a)},right[3]={-std::cos(h.a),0.0f,std::sin(h.a)};
    if(!ground)for(int i=0;i<3;i+=2) {
        const float want=k*(in.lateral*right[i]+in.forward*fwd[i]);
        h.vel[i]=kDamp*h.vel[i]+blend*(want-kDamp*h.vel[i]);
    } else h.vel[0]=h.vel[2]=0.0f;
    // The attitude (0x654E69 / 0x654E84): + pitch nose down at + input, roll -maxTilt x the lateral; contact levels it.
    if(ground)h.pitch=h.roll=0.0f;
    else{h.pitch+=(kMaxTilt*in.pitchIn-h.pitch)*h.tilt;h.roll+=(-kMaxTilt*in.rollIn-h.roll)*h.tilt;}
    h.yawOffset+=(h.maxYaw*in.yaw-h.yawOffset)*kYawBlend;
    h.yawRate+=(h.yawOffset*kSpring*60.0f-h.yawRate)*kAngularBlend;
    if(!ground)h.a+=h.yawRate*kDt;
    h.rotor+=(in.throttle>h.rotor ? h.up : h.down)*(in.throttle-h.rotor);
    if(h.rotor<kIdle)h.rotor=kIdle;
    const float t=std::fmin(std::fmax((h.rotor-kIdle)/(kHover-kIdle),0.0f),1.0f);
    h.vel[1]=(1.0f+(kHoverDamp-1.0f)*t)*h.vel[1]+h.rotor*kLift-kFall;
    for(int i=0;i<3;++i)h.pos[i]+=h.vel[i]*kDt;
    if(h.pos[1]<0.0f){h.pos[1]=0.0f;if(h.vel[1]<0.0f)h.vel[1]=0.0f;}
}

// The player: what is held from `from` s to `to` s (keys: W / S `fore`, A / D `side`, Space / the brake key `vert`), and
// the mouse moved `mx`, `my` a frame for `frames` frames from `mouseAt` s.
struct Hand { float fore,side,vert; float from,to; float mx,my; float mouseAt; int frames; };

constexpr float kLiftOffSec=1.5f;   // heli.cpp kLiftOffMs
struct Pilot { float aim[3]; aim::Hold hold; float hover,yawPrev,yawRate,vel[3],groundAt,prevFwd[3],turn; };

void MoveAim(Pilot& p,const Heli& h,const Hand& hand,int frame) noexcept {
    const int mouseFrame=static_cast<int>(std::lround(hand.mouseAt/kDt));
    if(frame<mouseFrame || frame>=mouseFrame+hand.frames)return;
    // The stand-in's MoveOnScreen: a turn that takes the aim past the screen's edge is not taken, the elevation is.
    float a[3];std::memcpy(a,p.aim,12);
    aim::Move(a,hand.mx,0.0f,aim::kPerUnit);
    const float off=std::fabs(Wrap(std::atan2(a[0],a[2])-h.a)),was=std::fabs(Wrap(std::atan2(p.aim[0],p.aim[2])-h.a));
    if(off<=kScreenYaw || off<was)std::memcpy(p.aim,a,12);
    aim::Move(p.aim,0.0f,hand.my,aim::kPerUnit);
}

float YawIn(Pilot& p,const Heli& h,const aim::Want& w) noexcept {
    p.yawRate+=((h.a-p.yawPrev)/kDt-p.yawRate)*0.3f;p.yawPrev=h.a;
    return aim::PlayerYawInput(Wrap(std::atan2(w.face[0],w.face[2])-h.a),p.yawRate,kYawMost,h.maxYaw,h.yawOffset,kYawBlend,kSpring);
}

// AimFly's frame on the stand-in (heli.cpp AimFly, the same calls in the same order).
Inputs Fly(Pilot& p,const Heli& h,const Hand& hand,float t,int frame) noexcept {
    const bool held=t>=hand.from && t<hand.to;
    const float vert=held ? std::fmax(-1.0f,std::fmin(1.0f,hand.fore+hand.vert)) : 0.0f,side=held ? hand.side : 0.0f;
    MoveAim(p,h,hand,frame);
    const float fwd[3]={std::sin(h.a),0.0f,std::cos(h.a)},right[3]={-std::cos(h.a),0.0f,std::sin(h.a)};
    const bool grounded=h.pos[1]<=0.0f;
    if(grounded)p.groundAt=t;
    const bool lifting=t-p.groundAt<kLiftOffSec;
    float pitch=0.0f;
    const aim::Want w=aim::Instructor(p.hold,p.aim,fwd,h.pos,h.vel,vert,side,kTop,kPlayerClimb,kMaxTilt,grounded,lifting,h.pos[1],&pitch);
    Inputs in{};
    aim::StockStick(w.vel,h.vel,kTop,kBrakeGain,fwd,right,&in.forward,&in.lateral);
    if(lifting)in.forward=in.lateral=0.0f;
    in.yaw=YawIn(p,h,w);
    const float turn=((fwd[0]-p.prevFwd[0])*right[0]+(fwd[2]-p.prevFwd[2])*right[2])/kDt;
    std::memcpy(p.prevFwd,fwd,12);
    p.turn+=(turn-p.turn)*0.3f;
    in.pitchIn=-pitch/kMaxTilt;
    in.rollIn=grounded ? in.lateral : std::fmax(-1.0f,std::fmin(1.0f,in.lateral+aim::BankInput(h.vel[0]*fwd[0]+h.vel[2]*fwd[2],p.turn,kMaxTilt)));
    if(!grounded) {
        const bool learn=p.hold.holding && std::fabs(p.hold.y-h.pos[1])<6.0f;
        const aim::Rotor r{kLift,kHoverDamp,kIdle,h.up,h.down};
        in.throttle=aim::CollectiveThrottle(w.climb,h.vel[1],h.rotor,&p.hover,learn,kDt,r);
    } else in.throttle=vert>0.0f ? 1.0f : 0.0f;
    return in;
}

// The law the instructor replaces (2026-10-06 .. 10-08): aim::Fly (the aim's elevation a climb, W / S a speed setpoint),
// the nose the aim's pitch, the NPC's StockThrottle on the stock rotor.
Inputs Before(Pilot& p,const Heli& h,const Hand& hand,float t,int frame) noexcept {
    const bool held=t>=hand.from && t<hand.to;
    const aim::Keys keys{held ? hand.fore : 0.0f,held ? hand.side : 0.0f,held ? hand.vert : 0.0f};
    MoveAim(p,h,hand,frame);
    const float fwd[3]={std::sin(h.a),0.0f,std::cos(h.a)},right[3]={-std::cos(h.a),0.0f,std::sin(h.a)};
    const bool grounded=h.pos[1]<=0.0f;
    if(grounded)p.groundAt=t;
    const bool lifting=t-p.groundAt<kLiftOffSec;
    const aim::Want w=aim::Fly(p.hold,p.aim,fwd,h.pos,h.vel,keys,kTop,kPlayerClimb,grounded,lifting,h.pos[1],kDt);
    Inputs in{};
    aim::StockStick(w.vel,h.vel,kTop,kBrakeGain,fwd,right,&in.forward,&in.lateral);
    if(lifting)in.forward=in.lateral=0.0f;
    in.yaw=YawIn(p,h,w);
    in.pitchIn=-std::asin(p.aim[1])/kMaxTilt;in.rollIn=in.lateral;
    const bool learn=p.hold.holding && std::fabs(p.hold.y-h.pos[1])<6.0f;
    in.throttle=aim::StockThrottle(w.climb,h.vel[1],h.rotor,&p.hover,learn,kDt,kGains);
    return in;
}
using Law=Inputs(*)(Pilot&,const Heli&,const Hand&,float,int);

Heli Hovering(float y,float maxYaw,bool stock) noexcept {
    Heli h{{0.0f,y,0.0f},{0.0f,0.0f,0.0f},0.0f,0.0f,kHover,maxYaw};
    if(stock){h.up=0.001f;h.down=0.0007f;h.tilt=kStockTilt;}
    return h;
}
Pilot Fresh(float hover) noexcept {
    Pilot p{};
    p.aim[2]=1.0f;p.hover=hover;p.groundAt=-10.0f;p.prevFwd[2]=1.0f;
    return p;
}

struct Expect { float fwd,right,up,turn; };   // the sign each must have: +1, -1, 0 (about still: |x| under the slack), 2 (any)
struct Scenario { const char* name; const char* means; Hand hand; float seconds; Expect expect; float maxYaw; };

bool Matches(float x,float want,float slack) noexcept {
    if(want==2.0f)return true;
    if(want==0.0f)return std::fabs(x)<slack;
    return x*want>slack;
}

bool Run(const Scenario& sc) noexcept {
    Heli h=Hovering(50.0f,sc.maxYaw,false);
    Pilot p=Fresh(kHover);
    const int frames=static_cast<int>(sc.seconds/kDt);
    for(int f=0;f<frames;++f)Step(h,Fly(p,h,sc.hand,static_cast<float>(f)*kDt,f));
    // In the starting frame: forward +Z, right -X (heliaim.h RightOf), up +Y; the compass turn + right (a falling).
    const float fwd=h.pos[2],right=-h.pos[0],up=h.pos[1]-50.0f,turn=-h.a*180.0f/kPi;
    const float speed=std::sqrt(h.vel[0]*h.vel[0]+h.vel[2]*h.vel[2]);
    const bool ok=Matches(fwd,sc.expect.fwd,3.0f) && Matches(right,sc.expect.right,3.0f) && Matches(up,sc.expect.up,2.0f) &&
                  Matches(turn,sc.expect.turn,5.0f);
    std::printf("%-28s %-36s fwd %+7.1f m  right %+7.1f m  up %+6.1f m  turn %+6.1f deg  %5.1f m/s  %s\n",sc.name,sc.means,fwd,
                right,up,turn,speed,ok ? "ok" : "WRONG");
    return ok;
}

// --- The step responses ---
// The collective: W held kClimbSec from a hover, let go; the height it comes to, its overshoot past that, the time it
// settles within kSettleBand of it, and how often the climb changes its sign after the release (porpoising).
constexpr float kClimbSec=3.0f,kSettleBand=0.3f;
struct Climbed { float rate1,peak,end,overshoot,settle; int reversals; };
Climbed Collective(Law law,bool stock,float hoverGuess,float sign) noexcept {
    Heli h=Hovering(50.0f,kYawMost,stock);
    Pilot p=Fresh(hoverGuess);
    // The instructor's collective is W / S; the law before climbed on Space / the brake key (its W / S a speed setpoint).
    const Hand hand=law==&Fly ? Hand{sign,0,0,0,kClimbSec,0,0,0,0} : Hand{0,0,sign,0,kClimbSec,0,0,0,0};
    Climbed c{};
    const int frames=static_cast<int>(40.0f/kDt),release=static_cast<int>(kClimbSec/kDt);
    float lastVy=0.0f,trace[2400];
    for(int f=0;f<frames;++f) {
        Step(h,law(p,h,hand,static_cast<float>(f)*kDt,f));
        trace[f]=h.pos[1];
        if(f==static_cast<int>(1.0f/kDt))c.rate1=h.vel[1];
        if(f>release+30 && std::fabs(h.vel[1])>0.05f) {
            if(lastVy!=0.0f && (h.vel[1]>0.0f)!=(lastVy>0.0f))++c.reversals;
            lastVy=h.vel[1];
        }
    }
    c.end=trace[frames-1];
    c.peak=trace[release];
    for(int f=release;f<frames;++f)c.peak=sign>0.0f ? std::fmax(c.peak,trace[f]) : std::fmin(c.peak,trace[f]);
    c.overshoot=std::fabs(c.peak-c.end);
    c.settle=0.0f;
    for(int f=frames-1;f>=release;--f)if(std::fabs(trace[f]-c.end)>kSettleBand){c.settle=static_cast<float>(f-release)*kDt;break;}
    return c;
}
bool CollectiveSteps() noexcept {
    bool ok=true;
    // The hover the pilot starts from is aim::HoverRotor's (the NPC's learned 0.424 for the 506, within 2 % of the measured
    // gravity's 14.7 m/s^2); the last case starts it 47 % off (the stand-in's 9.8 m/s^2) and must still settle unporpoised.
    const struct { const char* name; float sign,guess,settle; } cases[]={{"W 3 s, let go",1.0f,kHover,5.0f},
        {"S 3 s, let go",-1.0f,kHover,5.0f},{"W 3 s, hover guess +47%",1.0f,0.424f,8.0f}};
    for(const auto& k:cases) {
        const Climbed now=Collective(&Fly,false,k.guess,k.sign),was=Collective(&Before,true,k.guess,k.sign);
        // Climbs at once (half the most within 1 s), stops where it is let go (overshoot under 1.5 m), settles within 5 s, no
        // porpoise (at most one reversal of the climb after the release).
        const bool pass=k.sign*now.rate1>0.5f*kPlayerClimb && now.overshoot<1.5f && now.settle<k.settle && now.reversals<=1 &&
                        std::fabs(now.end-50.0f)>8.0f;
        std::printf("%-28s now:    climb at 1 s %+5.2f m/s, held %+6.1f m, overshoot %4.2f m, settles %4.1f s, reversals %d  %s\n",
                    k.name,now.rate1,now.end-50.0f,now.overshoot,now.settle,now.reversals,pass ? "ok" : "WRONG");
        std::printf("%-28s before: climb at 1 s %+5.2f m/s, held %+6.1f m, overshoot %4.2f m, settles %4.1f s, reversals %d\n","",
                    was.rate1,was.end-50.0f,was.overshoot,was.settle,was.reversals);
        ok=ok && pass;
    }
    return ok;
}

// The cyclic: the mouse pitches the aim down 20 deg (in 0.5 s) and holds it 12 s, then levels it (in 0.5 s): the speed it
// flies at (the CyclicSpeed of that pitch), its overshoot, the time to 90 %, the nose's pitch 1 s after the mouse
// stopped (of the aim's), the speed left 6 s after levelling, the height it lost.
bool CyclicStep() noexcept {
    Heli h=Hovering(50.0f,kYawMost,false);
    Pilot p=Fresh(kHover);
    const float el=-20.0f*kPi/180.0f;
    const int tilt=static_cast<int>(std::lround(-el/aim::kPerUnit/1.0f));   // frames at a unit of mouse a frame
    const Hand down{0,0,0,0,0,0,-1.0f,0,tilt},up{0,0,0,0,0,0,1.0f,12.0f,tilt};
    const float want=aim::CyclicSpeed(el,kMaxTilt,kTop);
    float peak=0.0f,to90=-1.0f,nose1=0.0f,lowest=50.0f;
    const int frames=static_cast<int>(12.0f/kDt);
    for(int f=0;f<frames;++f) {
        Step(h,Fly(p,h,down,static_cast<float>(f)*kDt,f));
        const float speed=h.vel[2];
        peak=std::fmax(peak,speed);
        if(to90<0.0f && speed>=0.9f*want)to90=static_cast<float>(f)*kDt;
        if(f==tilt+60)nose1=h.pitch;
        lowest=std::fmin(lowest,h.pos[1]);
    }
    float back=0.0f;
    for(int f=frames;f<frames+static_cast<int>(6.0f/kDt);++f) {
        Step(h,Fly(p,h,up,static_cast<float>(f)*kDt,f));
        back=std::fmin(back,h.vel[2]);
        lowest=std::fmin(lowest,h.pos[1]);
    }
    const float left=h.vel[2],noseShare=nose1/(-el);
    const bool ok=want>0.4f*kTop && peak<1.05f*want && to90>0.0f && to90<4.0f && noseShare>0.9f && std::fabs(left)<0.5f &&
                  back>-0.5f && 50.0f-lowest<1.0f;
    std::printf("%-28s %-36s want %.1f m/s, peak %.1f, 90%% at %.1f s, nose %.0f%% of the aim 1 s on, after levelling %.2f m/s "
                "(back at most %.2f), height lost %.2f m  %s\n","mouse down 20 deg, level","flies forward, then stops",want,peak,to90,
                noseShare*100.0f,left,back,50.0f-lowest,ok ? "ok" : "WRONG");
    // The nose the old law gave the same mouse: the stock tilt smoothing 0.005 a frame (3.3 s).
    Heli b=Hovering(50.0f,kYawMost,true);
    Pilot q=Fresh(kHover);
    for(int f=0;f<tilt+60;++f)Step(b,Before(q,b,down,static_cast<float>(f)*kDt,f));
    std::printf("%-28s %-36s before: nose %.0f%% of the aim 1 s on, flying %.1f m/s, sinking %.1f m/s\n","","",b.pitch/(-el)*100.0f,
                b.vel[2],-b.vel[1]);
    return ok;
}

// The coordinated bank: flying forward (aim 20 deg down), the mouse swept right: the roll the attitude is asked is the
// way a slide to the right rolls it (the lateral's sign), at about atan(V w / g) of the max tilt; level flight none.
bool Banks() noexcept {
    Heli h=Hovering(50.0f,kYawMost,false);
    Pilot p=Fresh(kHover);
    const int tilt=static_cast<int>(std::lround(20.0f*kPi/180.0f/aim::kPerUnit));
    const Hand fly{0,0,0,0,0,0,-1.0f,0,tilt};
    float level=0.0f;
    for(int f=0;f<static_cast<int>(8.0f/kDt);++f){const Inputs in=Fly(p,h,fly,static_cast<float>(f)*kDt,f);level=in.rollIn;Step(h,in);}
    const Hand sweep{0,0,0,0,0,1.0f,0,8.0f,120};
    float roll=0.0f,expect=0.0f;
    for(int f=static_cast<int>(8.0f/kDt);f<static_cast<int>(10.0f/kDt);++f) {
        const Inputs in=Fly(p,h,sweep,static_cast<float>(f)*kDt,f);
        roll=in.rollIn;expect=std::atan(h.vel[2]*std::fabs(h.yawRate)/aim::kGravity)/kMaxTilt;
        Step(h,in);
    }
    const bool ok=std::fabs(level)<0.05f && roll>0.3f*expect && roll>0.05f;
    std::printf("%-28s %-36s roll input %.2f (a right slide's sign +; atan(V w/g) %.2f), level flight %.2f  %s\n",
                "forward, mouse swept right","banks into the turn",roll,expect,level,ok ? "ok" : "WRONG");
    return ok;
}

// The mouse swept right and kept going (1 unit a frame for 4 s, aim::kPerUnit 0.05 rad a unit), held at the screen's
// edge as the game does, the heli at a hover: the turn rate it keeps from 2 s to 4 s. The user (2026-10-06): "the mouse
// moves and nothing changes, it does not follow the mouse"; their log had the 506 turning 5-17 deg/s. Pushed on, the
// heading must turn at kFollowShare of the requested player turn rate or more.
constexpr float kFollowShare=0.8f;
float SweepRate() noexcept {
    Heli h=Hovering(50.0f,kYawMost,false);
    Pilot p=Fresh(kHover);
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
    std::printf("%-28s %-36s turns %.1f deg/s (want >= %.0f, its most %.0f); native lag/spring  %s\n","mouse swept right 4 s",
                "the heading follows the mouse",now*180.0f/kPi,want*180.0f/kPi,kYawMost*180.0f/kPi,ok ? "ok" : "WRONG");
    return ok;
}

// The rotor that holds the height (aim::HoverRotor) from the SGOs' lift as it is in memory (heli_movement[0][1] / 60):
// the 506 / 409 / 410 (34) at the NPC's learned 0.424, the 602 (70) near what the NPC learned flying it (0.23-0.26
// climbing), never pinned at the clamp's 1.0 (the old guess: the 602 climbed on full throttle).
bool HoverRotors() noexcept {
    const float h506=aim::HoverRotor(34.0f/60.0f,1.0f),h602=aim::HoverRotor(70.0f/60.0f,1.0f);
    const bool ok=std::fabs(h506-0.424f)<0.005f && h602>0.18f && h602<0.26f;
    std::printf("%-28s %-36s 506 %.3f  602 %.3f  %s\n","hover rotor","from the lift as it is in memory",h506,h602,ok ? "ok" : "WRONG");
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
    std::printf("%-28s %-36s elevation %+.3f -> %+.3f, on screen %d  %s\n","mouse up and out at the edge","still pitches (axis by axis)",
                before,a[1],on ? 1 : 0,ok ? "ok" : "WRONG");
    return ok;
}
}  // namespace

int main() {
    // A unit of mouse a frame is aim::kPerUnit (0.05 rad): 7 frames take the aim ~20 deg.
    //                                              fore side vert from to   mx    my   mouseAt frames  s     fwd right up turn
    const Scenario scenarios[]={
        {"nothing",                "hovers where it is",              {0, 0, 0, 0,0,     0, 0, 0,0},   10, {0, 0, 0, 0}, kYawMost},
        {"mouse down 20 deg",      "noses down, flies forward level", {0, 0, 0, 0,0,     0,-1, 0,7},   10, {1, 0, 0, 0}, kYawMost},
        {"mouse up 20 deg",        "noses up, backs off level",       {0, 0, 0, 0,0,     0, 1, 0,7},   10, {-1,0, 0, 0}, kYawMost},
        {"mouse down 2 deg",       "within the dead zone: hovers",    {0, 0, 0, 0,0,     0,-1, 0,1},   10, {0, 0, 0, 0}, kYawMost},
        {"W 3 s",                  "climbs, then holds",              {1, 0, 0, 0,3,     0, 0, 0,0},   10, {0, 0, 1, 0}, kYawMost},
        {"S 3 s",                  "descends, then holds",            {-1,0, 0, 0,3,     0, 0, 0,0},   10, {0, 0,-1, 0}, kYawMost},
        {"mouse right 57 deg",     "turns right onto the aim",        {0, 0, 0, 0,0,     1, 0, 0,20},  10, {2, 2, 0, 1}, kYawMost},
        {"mouse left 57 deg",      "turns left onto the aim",         {0, 0, 0, 0,0,    -1, 0, 0,20},  10, {2, 2, 0,-1}, kYawMost},
        {"D 3 s",                  "sidesteps right",                 {0, 1, 0, 0,3,     0, 0, 0,0},   6,  {0, 1, 0, 0}, kYawMost},
        {"A 3 s",                  "sidesteps left",                  {0,-1, 0, 0,3,     0, 0, 0,0},   6,  {0,-1, 0, 0}, kYawMost},
        {"Space 3 s",              "climbs, then holds",              {0, 0, 1, 0,3,     0, 0, 0,0},   10, {0, 0, 1, 0}, kYawMost},
        {"brake key 3 s",          "descends, then holds",            {0, 0,-1, 0,3,     0, 0, 0,0},   10, {0, 0,-1, 0}, kYawMost},
        {"mouse right, max yaw < 0","turns right onto the aim (its sign)",{0, 0, 0, 0,0,   1, 0, 0,20},  10, {2, 2, 0, 1},-kYawMost},
        {"mouse left, max yaw < 0", "turns left onto the aim (its sign)", {0, 0, 0, 0,0,  -1, 0, 0,20},  10, {2, 2, 0,-1},-kYawMost},
    };
    int bad=0;
    for(const auto& sc:scenarios)bad+=Run(sc) ? 0 : 1;
    // On the ground, W held 1.2 s with the aim pitched down: it lifts off straight up (the collective spins the rotor up,
    // no horizontal stick within kLiftOffSec), not sliding along the ground into whatever is ahead.
    Heli g=Hovering(0.0f,kYawMost,false);
    g.rotor=kIdle;
    Pilot q=Fresh(kHover);q.groundAt=0.0f;
    const Hand lift{1,0,0,0,1.2f,0,-1,0,7};
    float setMost=0.0f;
    for(int f=0;f<static_cast<int>(1.5f/kDt);++f) {
        Step(g,Fly(q,g,lift,static_cast<float>(f)*kDt,f));
        setMost=std::fmax(setMost,q.hold.speed);
    }
    const bool still=g.pos[1]>0.5f && setMost==0.0f && std::fabs(g.pos[2])<0.5f;
    std::printf("%-28s %-36s up %+.1f m, fwd %+.1f m, most speed set %.1f m/s  %s\n","W 1.2 s from the ground","lifts straight, no slide",
                g.pos[1],g.pos[2],setMost,still ? "ok" : "WRONG");
    bad+=still ? 0 : 1;
    bad+=CollectiveSteps() ? 0 : 1;
    bad+=CyclicStep() ? 0 : 1;
    bad+=Banks() ? 0 : 1;
    bad+=FollowsTheMouse() ? 0 : 1;
    bad+=HoverRotors() ? 0 : 1;
    bad+=MovesPerAxis() ? 0 : 1;
    std::printf("%s\n",bad ? "SOME CHECKS FAILED" : "heli_aim_check: all passed");
    return bad ? 1 : 0;
}
