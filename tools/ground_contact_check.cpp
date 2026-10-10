// A player aircraft on the ground (src/pjet_handling.h GroundContact), checked without the game (the user, 2026-10-09:
// 「飞机没起飞的时候，会在地上一抖一抖的」).
//
// The stand-in: the airframe in its pitch plane (x along the nose, y up, the pitch about z) on two wheels 8 m apart
// (bomber501's gear: nose z +4, mains z -4) 2 m under its centre, over ground that slopes and undulates; the world's
// gravity 14.7 m/s^2 (the game's, docs/player-jet-re.md); a velocity-level contact solver as Havok's: each wheel's
// closing speed held at least at a push-out of kBaumgarte of its penetration a step (no restitution), friction up to
// kFriction of it, 8 sweeps, then the
// body moves. Each frame the controller writes the body's velocity and spin before the solver steps, as slot 57 and
// body506.cpp PhysicsHook do.
//
// Three controllers (`kNames`):
//  - "rolling before": what playerjet.cpp Ground wrote until 2026-10-09: the velocity level along the nose, its vertical
//    min(0, measured) (the measured 1.067 high: GetTickCount64's 15.6 ms steps against the game's 1/60 s), the spin the
//    body506 BodyAttitude onto a plane fitted to the ground +-3 m round the centre (kAttGain 6/s, its kind's roll cap);
//  - "parked before": the stock heli step a parked jet was handed to: any contact zeroes the attitude target (0x654E0F),
//    the spring 0x6CE8D0 (0.15 x 60 a rad, blended 0.125), the vertical (1 + (0.95 - 1) t) vy + rotor L at the idle rotor
//    (t 0), the horizontal passed through (0x651DF2);
//  - "now": GroundContact: the speed along the plane the body stands on (its up row), the solver's motion across it and
//    its pitch spin kept.
// For each: parked (speed 0) and rolling at 15 m/s, over 6 s after 2 s to settle: the spread about their trend of the
// wheels' mean gap over the ground and of their difference (a tilt against the contacts), the frames a wheel is off the ground (the body turned against its contacts: in the game the
// contact the solver keeps making and breaking, the shake), the most the solver pushed out. "now" must stay on both
// wheels and still (within kMostSpread / kMostPitch), and parked on the slope it must not slide; "before" is printed
// beside it, and must hold a wheel off somewhere (else the stand-in tells nothing apart).
// What this stand-in cannot show: Havok's own solver (its iterations, penetration recovery, sleeping) and the game's
// timing; the shake itself is not reproduced here, only the fight with the contacts that makes it.
// Built on request (offline_checks): cmake --build build --target ground_contact_check && build\ground_contact_check.exe
#include "../src/pjet_handling.h"
#include <cmath>
#include <cstdio>
#include <initializer_list>

namespace {
using crew::handling::GroundContact;
constexpr float kDt=1.0f/60.0f,kGravity=14.7f,kPi=3.14159265f;
constexpr float kAxle=4.0f,kWheelDrop=2.0f,kMass=1.0f,kInertia=kMass*(kAxle*kAxle+kWheelDrop*kWheelDrop)/3.0f;
constexpr float kBaumgarte=0.2f,kSlop=0.005f,kFriction=0.5f;
constexpr int kSweeps=8;
constexpr float kMostSpread=0.002f,kMostPitch=0.05f*kPi/180.0f;   // 2 mm, 0.05 deg

struct Ground { float slope,bump,wave,drop=0.0f; };   // drop: m the whole ground has fallen (a crater under it)
float Height(const Ground& g,float x) noexcept { return g.slope*x+g.bump*std::sin(x*g.wave)-g.drop; }
float Rise(const Ground& g,float x) noexcept { return g.slope+g.bump*g.wave*std::cos(x*g.wave); }

struct Body { float x,y,th,vx,vy,w; };   // centre, pitch (+ nose up), velocity, pitch rate
void Wheel(const Body& b,float lx,float* p,float* r) noexcept {
    const float ly=-kWheelDrop,c=std::cos(b.th),s=std::sin(b.th);
    r[0]=lx*c-ly*s;r[1]=lx*s+ly*c;
    p[0]=b.x+r[0];p[1]=b.y+r[1];
}

// The contact solver and the move (see the top); `push` the most it pushed out (m/s) this step.
void Solve(Body& b,const Ground& g,float* push) noexcept {
    b.vy-=kGravity*kDt;
    float acc[2]={0.0f,0.0f},fric[2]={0.0f,0.0f};
    for(int sweep=0;sweep<kSweeps;++sweep)for(int k=0;k<2;++k) {
        float p[2],r[2];Wheel(b,k ? -kAxle : kAxle,p,r);
        const float depth=Height(g,p[0])-p[1];
        if(depth<-0.05f)continue;   // off the ground (speculative margin)
        float n[2]={-Rise(g,p[0]),1.0f};
        const float l=std::sqrt(n[0]*n[0]+n[1]*n[1]);n[0]/=l;n[1]/=l;
        const float vn=(b.vx-b.w*r[1])*n[0]+(b.vy+b.w*r[0])*n[1];
        const float rn=r[0]*n[1]-r[1]*n[0];
        const float want=depth>kSlop ? kBaumgarte*(depth-kSlop)/kDt : depth/kDt;   // ahead of the gap: may close it, no more
        float lambda=(want-vn)/(1.0f/kMass+rn*rn/kInertia);
        const float was=acc[k];
        acc[k]=std::fmax(0.0f,was+lambda);lambda=acc[k]-was;
        b.vx+=lambda*n[0]/kMass;b.vy+=lambda*n[1]/kMass;b.w+=lambda*rn/kInertia;
        // Friction along the ground, at most kFriction of the normal impulse so far.
        const float t[2]={n[1],-n[0]},vt=(b.vx-b.w*r[1])*t[0]+(b.vy+b.w*r[0])*t[1],rt=r[0]*t[1]-r[1]*t[0];
        float tau=-vt/(1.0f/kMass+rt*rt/kInertia);
        const float wasT=fric[k];
        fric[k]=std::fmax(-kFriction*acc[k],std::fmin(kFriction*acc[k],wasT+tau));tau=fric[k]-wasT;
        b.vx+=tau*t[0]/kMass;b.vy+=tau*t[1]/kMass;b.w+=tau*rt/kInertia;
    }
    *push=std::fmax(*push,std::fmax(acc[0],acc[1])/kMass);
    b.x+=b.vx*kDt;b.y+=b.vy*kDt;b.th+=b.w*kDt;
}

enum Law { rollingBefore, parkedBefore, now };
const char* const kNames[]={"rolling before","parked before ","now           "};

struct Spread { float y,pitch,push,slide; int airborne; };
Spread Run(Law law,const Ground& g,float speed) noexcept {
    Body b{};
    b.y=Height(g,0.0f)+kWheelDrop+0.3f;b.th=std::atan(g.slope);
    float prevY=b.y,measured=0.0f,rotorRate=0.0f;
    const int settle=static_cast<int>(2.0f/kDt),frames=settle+static_cast<int>(6.0f/kDt);
    double sy=0,syy=0,st=0,stt=0,sx=0,sxy=0,sxt=0,sxx=0;
    Spread out{};
    float startX=0.0f;
    for(int f=0;f<frames;++f) {
        // The controller's write.
        if(law==rollingBefore) {
            const float vy=measured<0.0f ? std::fmax(measured,-30.0f) : 0.0f;
            b.vx=speed;b.vy=vy;
            const float h=(Height(g,b.x+3.0f)-Height(g,b.x-3.0f))/6.0f,target=std::atan(h);
            b.w=std::fmax(-1.2f,std::fmin(1.2f,6.0f*(target-b.th)));
        } else if(law==parkedBefore) {
            rotorRate+=(-b.th*0.15f*60.0f-rotorRate)*0.125f;b.w=rotorRate;
            b.vy=b.vy+0.13f*(34.0f/60.0f);   // the idle rotor's lift a frame (t 0: undamped)
            if(speed>0.0f)b.vx=speed;
        } else {
            const float up[3]={-std::sin(b.th),std::cos(b.th),0.0f},fwd[3]={std::cos(b.th),std::sin(b.th),0.0f};
            const float vel[3]={fwd[0]*speed,fwd[1]*speed,0.0f},omega[3]={0.0f,0.0f,0.0f};
            float lin[3]={b.vx,b.vy,0.0f},ang[3]={0.0f,0.0f,b.w};
            GroundContact(up,vel,omega,lin,ang);
            b.vx=lin[0];b.vy=lin[1];b.w=ang[2];
        }
        float push=0.0f;
        Solve(b,g,&push);
        measured=(b.y-prevY)/kDt*(16.67f/15.6f);prevY=b.y;
        if(f<settle){startX=b.x;continue;}
        if(f==settle)startX=b.x;
        out.push=std::fmax(out.push,push);
        // About the trend (a straight line in time): rolling over a slope the height rises steadily.
        // The wheels' gaps over the ground (<0: in it): their mean and their difference (a tilt against the contacts).
        float gap[2];
        for(int k=0;k<2;++k){float q[2],r[2];Wheel(b,k ? -kAxle : kAxle,q,r);gap[k]=q[1]-Height(g,q[0]);}
        const double t=f-settle,y=0.5*(gap[0]+gap[1]),p=(gap[0]-gap[1])/(2.0f*kAxle);
        sx+=t;sxx+=t*t;sy+=y;syy+=y*y;st+=p;stt+=p*p;sxy+=t*y;sxt+=t*p;
        for(int k=0;k<2;++k){float q[2],r[2];Wheel(b,k ? -kAxle : kAxle,q,r);if(Height(g,q[0])-q[1]<-0.02f){++out.airborne;break;}}
    }
    const double n=frames-settle;
    const auto resid=[&](double s,double ss,double sxs){
        const double slope=(n*sxs-sx*s)/(n*sxx-sx*sx),icpt=(s-slope*sx)/n;
        const double v=ss/n-2*icpt*s/n-2*slope*sxs/n+icpt*icpt+2*icpt*slope*sx/n+slope*slope*sxx/n;
        return static_cast<float>(std::sqrt(std::fmax(v,0.0)));
    };
    out.y=resid(sy,syy,sxy);out.pitch=resid(st,stt,sxt);
    out.slide=b.x-startX-speed*6.0f;
    return out;
}

int fought=0;   // the cases a law before held a wheel off the ground against its contacts
bool Case(const char* name,const Ground& g,float speed) noexcept {
    bool ok=true;
    for(Law law:{rollingBefore,parkedBefore,now}) {
        if(law==parkedBefore && speed>0.0f)continue;   // the stock step held only a parked jet
        const Spread s=Run(law,g,speed);
        // Parked it must not slide; rolling, the friction of the hull's contacts slows it as before (the same for both).
        const bool good=s.airborne==0 && s.y<kMostSpread && s.pitch<kMostPitch && (speed>0.0f || std::fabs(s.slide)<0.05f);
        if(law!=now && s.airborne>0)++fought;
        std::printf("%-30s %s gap spread %7.4f m  tilt spread %6.3f deg  off the ground %3d frames  push-out %5.2f m/s  "
                    "slide %+6.2f m%s\n",name,kNames[law],s.y,s.pitch*180.0f/kPi,s.airborne,s.push,s.slide,
                    law==now ? (good ? "  ok" : "  WRONG") : "");
        if(law==now)ok=ok && good;
    }
    return ok;
}
}  // namespace

// The degraded path (the solver's velocity unreadable, pjet_handling.h Drives / GroundFallback): the ground under the
// craft falls 1 m away after it settled. Rolling on the fallback (the measured fall along the normal) and on the
// contract it comes down onto it; the whole velocity written (what the degraded path did: a parked craft's 0 every
// frame) cancels the gravity but for one step's (14.7/60 m/s): it only creeps down, still 0.27 m up 3 s on. A parked
// craft is not driven there at all (the stock step's).
bool Degraded() noexcept {
    const auto run=[](int law,float speed){   // 0 the contract, 1 the fallback, 2 the whole velocity
        const Ground g{0.0f,0.0f,0.0f};
        Ground now=g;
        Body b{};b.y=kWheelDrop+0.05f;
        float prevY=b.y;
        for(int f=0;f<static_cast<int>(4.0f/kDt);++f) {
            if(f==static_cast<int>(1.0f/kDt))now.drop=1.0f;
            const float up[3]={-std::sin(b.th),std::cos(b.th),0.0f},fwd[3]={std::cos(b.th),std::sin(b.th),0.0f};
            const float vel[3]={fwd[0]*speed,fwd[1]*speed,0.0f},omega[3]={0.0f,0.0f,0.0f};
            const float measured[3]={b.vx,(b.y-prevY)/kDt,0.0f};
            float lin[3]={b.vx,b.vy,0.0f},ang[3]={0.0f,0.0f,b.w};
            if(law==0)GroundContact(up,vel,omega,lin,ang);
            else if(law==1)crew::handling::GroundFallback(up,vel,omega,measured,lin,ang);
            else{for(int i=0;i<3;++i)lin[i]=vel[i];ang[2]=0.0f;}
            b.vx=lin[0];b.vy=lin[1];b.w=ang[2];
            prevY=b.y;
            float push=0.0f;
            Solve(b,now,&push);
        }
        float gap=0.0f;
        for(int k=0;k<2;++k){float q[2],r[2];Wheel(b,k ? -kAxle : kAxle,q,r);gap=std::fmax(gap,q[1]-Height(now,q[0]));}
        return gap;
    };
    const float contract=run(0,0.0f),fallback=run(1,5.0f),whole=run(2,0.0f);
    const bool drives=crew::handling::Drives(false,true) && crew::handling::Drives(false,false) && crew::handling::Drives(true,true) &&
                      !crew::handling::Drives(true,false);
    const bool ok=contract<0.05f && fallback<0.05f && whole>0.2f && drives;
    std::printf("%-30s wheel gap after the ground fell 1 m: contract %.3f m, fallback (rolling) %.3f m, whole velocity %.3f m "
                "(hangs); parked without the solver goes to the stock step %s  %s\n","degraded path",contract,fallback,whole,
                drives ? "yes" : "NO",ok ? "ok" : "WRONG");
    return ok;
}

// Rolling with the throttle (pjet_handling.h AlongPlane / RollSpeed; the user, 2026-10-09: 「这个飞机起飞的时候撞到东西了，然后
// 弹来弹去的」): the ground step's speed integrated as playerjet.cpp Ground does it, the throttle full (kThrust m/s^2 up to
// kTop), on the stand-in's contacts. "before" read last frame's velocity along the level nose; "now" along the plane the
// wheels stand on. Started at kRollFrom m/s, after kRollFor s "now" must have gained the throttle's speed (within 2%: over
// bumps each frame's change of pitch still turns a little of it away) whatever the
// slope (the plugin owns the speed along the ground; the stand-in's slope adds no gravity along it, as GroundContact
// does not), and moved as far as that speed says; "before" is printed beside it and must lose speed on the pitched
// ground (else the case tells nothing apart).
constexpr float kThrust=10.0f,kTop=100.0f,kRollFrom=15.0f,kRollFor=4.0f;
struct Roll { float speed,moved; };
Roll RollThrottle(const Ground& g,bool level) noexcept {
    Body b{};
    b.y=Height(g,0.0f)+kWheelDrop;b.th=std::atan(g.slope);
    float vel[3]={kRollFrom*std::cos(b.th),kRollFrom*std::sin(b.th),0.0f};
    const float startX=b.x;
    float speed=0.0f;
    for(int f=0;f<static_cast<int>(kRollFor/kDt);++f) {
        const float up[3]={-std::sin(b.th),std::cos(b.th),0.0f},nose[3]={1.0f,0.0f,0.0f};
        float ahead[3];
        if(!crew::handling::AlongPlane(nose,up,ahead))return Roll{};
        speed=level ? vel[0] : crew::handling::RollSpeed(vel,ahead);
        speed+=std::fmin(kThrust*kDt,kTop-speed);
        for(int i=0;i<3;++i)vel[i]=ahead[i]*speed;
        const float omega[3]={0.0f,0.0f,0.0f};
        float lin[3]={b.vx,b.vy,0.0f},ang[3]={0.0f,0.0f,b.w};
        GroundContact(up,vel,omega,lin,ang);
        b.vx=lin[0];b.vy=lin[1];b.w=ang[2];
        float push=0.0f;
        Solve(b,g,&push);
    }
    return Roll{speed,b.x-startX};
}
bool Throttle() noexcept {
    const struct { const char* name; Ground g; } cases[]={
        {"throttle, flat",                 {0.0f,0.0f,0.0f}},
        {"throttle, 4 deg slope",          {0.07f,0.0f,0.0f}},
        {"throttle, 10 deg (on rubble)",   {0.18f,0.0f,0.0f}},
        {"throttle, field (0.2 m / 9 m)",  {0.02f,0.2f,0.7f}},
    };
    const float want=kRollFrom+kThrust*kRollFor,wantMoved=(kRollFrom+want)*0.5f*kRollFor;
    bool ok=true;
    int told=0;
    for(const auto& c:cases) {
        const Roll before=RollThrottle(c.g,true),now=RollThrottle(c.g,false);
        // Along the ground (x is level): the slope's run is cos of the way rolled.
        const float run=std::cos(std::atan(c.g.slope));
        const bool good=std::fabs(now.speed-want)<0.02f*want && (c.g.bump>0.0f || std::fabs(now.moved-wantMoved*run)<0.03f*wantMoved);
        if(before.speed<want-5.0f)++told;
        std::printf("%-30s rolling with the throttle %.0f s from %.0f m/s (wants %.0f): before %5.1f m/s, %5.1f m; now %5.1f m/s, %5.1f m  %s\n",
                    c.name,kRollFor,kRollFrom,want,before.speed,before.moved,now.speed,now.moved,good ? "ok" : "WRONG");
        ok=ok && good;
    }
    std::printf("%-30s %d cases where the level read lost speed  %s\n","the throttle cases discriminate",told,told>0 ? "ok" : "WRONG");
    return ok && told>0;
}

// Rolled off what it stood on (pjet_handling.h RollsIntoAir; the log of 2026-10-09 19:21): flying only from the air's
// least speed (playerjet.cpp kStallFloor 25 m/s) over kOffGround 6 m; slower it stays on its contacts and falls.
bool OffTheGround() noexcept {
    constexpr float kNone=-1e9f,kOff=6.0f,kFloor=25.0f;
    const struct { float clear,speed; bool air; const char* what; } rows[]={
        {6.2f,1.0f,false,"lifted 6 m at 1 m/s (19:21:25, then a 20 m/s dive)"},
        {6.5f,22.0f,false,"lifted 6.5 m at 22 m/s (19:21:08, then STALL)"},
        {6.5f,25.0f,true,"6.5 m at the air's least speed"},
        {40.0f,80.0f,true,"off a cliff at 80 m/s"},
        {2.0f,80.0f,false,"2 m over it at 80 m/s (on its wheels)"},
        {kNone,80.0f,false,"no ground seen"},
    };
    bool ok=true;
    for(const auto& r:rows) {
        const bool air=crew::handling::RollsIntoAir(r.clear,kNone,kOff,r.speed,kFloor);
        std::printf("%-30s %-52s %s  %s\n","off the ground",r.what,air ? "air   " : "ground",air==r.air ? "ok" : "WRONG");
        ok=ok && air==r.air;
    }
    return ok;
}

int main() {
    // `slope` the rise a metre, `bump` m and `wave` rad/m of the undulation (a runway's few cm, a field's 10-30 cm).
    const struct { const char* name; Ground g; float speed; } cases[]={
        {"parked, flat",                {0.0f,0.0f,0.0f},0.0f},
        {"parked, 4 deg slope",         {0.07f,0.0f,0.0f},0.0f},
        {"parked, field (0.2 m / 9 m)", {0.02f,0.2f,0.7f},0.0f},
        {"rolling 15 m/s, flat",        {0.0f,0.0f,0.0f},15.0f},
        {"rolling 15 m/s, runway",      {0.01f,0.03f,0.15f},15.0f},
    };
    int bad=0;
    for(const auto& c:cases)bad+=Case(c.name,c.g,c.speed) ? 0 : 1;
    bad+=Degraded() ? 0 : 1;
    bad+=Throttle() ? 0 : 1;
    bad+=OffTheGround() ? 0 : 1;
    // The stand-in tells the laws apart: a law before held a wheel off the ground (turned against its contacts) somewhere.
    std::printf("%-30s %d cases where a law before held a wheel off the ground  %s\n","the stand-in discriminates",fought,fought>0 ? "ok" : "WRONG");
    bad+=fought>0 ? 0 : 1;
    // The contract itself: the plugin's velocity across the normal, the solver's along it; the solver's spin across the
    // normal, the plugin's about it.
    const float up[3]={0.0f,0.8f,0.6f},vel[3]={3.0f,1.0f,2.0f},omega[3]={0.0f,0.4f,0.3f};
    float lin[3]={-1.0f,-2.0f,5.0f},ang[3]={0.7f,-0.2f,0.1f};
    const float sn=lin[1]*0.8f+lin[2]*0.6f,an=ang[1]*0.8f+ang[2]*0.6f;
    GroundContact(up,vel,omega,lin,ang);
    const float ln=lin[1]*0.8f+lin[2]*0.6f,wn=ang[1]*0.8f+ang[2]*0.6f;
    const float across=lin[0]-vel[0],acrossSpin=ang[0]-0.7f;
    const bool split=std::fabs(ln-sn)<1e-5f && std::fabs(across)<1e-5f && std::fabs(wn-0.5f)<1e-5f && std::fabs(acrossSpin)<1e-5f &&
                     std::fabs(an-wn)>0.1f;
    std::printf("%-30s normal %.3f (solver %.3f), spin about it %.3f (plugin 0.500)  %s\n","the contract",ln,sn,wn,split ? "ok" : "WRONG");
    bad+=split ? 0 : 1;
    std::printf("%s\n",bad ? "SOME CHECKS FAILED" : "ground_contact_check: all passed");
    return bad ? 1 : 0;
}
