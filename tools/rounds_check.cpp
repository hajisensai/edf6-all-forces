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
}  // namespace

int main() {
    ArcChecks();
    MotorChecks();
    StockRockets();
    std::printf("%s\n",failed ? "FAILED" : "all passed");
    return failed ? 1 : 0;
}
