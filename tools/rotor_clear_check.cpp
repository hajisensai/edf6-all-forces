// The NPC helis' rotor clearance (src/rotor_clear.h) offline, against boxes standing in for buildings: a heli whose box
// is clear but whose blades reach a wall is pushed off it, between two walls it is pushed to the middle, a street
// narrower than its disc makes it climb over the lower side (unless climbing is off), floors and roofs never push it,
// and a stand-in heli flown along a street canyon, drifting into a tower and hovering by one never has a wall inside its
// disc.
//   cmake --build build --target rotor_clear_check && build\rotor_clear_check.exe
#include "../src/rotor_clear.h"
#include <cmath>
#include <cstdio>
#include <vector>

using namespace crew::rotorclear;

namespace {
int fails=0,checks=0;
void Check(bool ok,const char* what) {
    ++checks;
    if(!ok){std::printf("FAIL %s\n",what);++fails;}
}

// An axis-aligned building: x in [x0, x1], z in [z0, z1], from the ground (0) to `top`.
struct Box { float x0,x1,z0,z1,top; };
using World=std::vector<Box>;

// Slab ray against the boxes: metres along a->b to the nearest face, its normal; < 0 with none. A start inside a
// box sees nothing of it (as Havok's ray from inside a convex).
float Ray(const World& w,const float* a,const float* b,float* hit,float* normal) {
    const float d[3]={b[0]-a[0],b[1]-a[1],b[2]-a[2]};
    const float len=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
    float best=2.0f;int bestAxis=-1;float bestSign=0.0f;
    for(const Box& x:w) {
        const float lo[3]={x.x0,0.0f,x.z0},hi[3]={x.x1,x.top,x.z1};
        bool inside=true;
        for(int i=0;i<3;++i)if(a[i]<lo[i] || a[i]>hi[i])inside=false;
        if(inside)continue;
        float t0=0.0f,t1=1.0f;int axis=-1;float sign=0.0f;bool miss=false;
        for(int i=0;i<3 && !miss;++i) {
            if(std::fabs(d[i])<1e-9f){if(a[i]<lo[i] || a[i]>hi[i])miss=true;continue;}
            float ta=(lo[i]-a[i])/d[i],tb=(hi[i]-a[i])/d[i];
            float s=-1.0f;
            if(ta>tb){const float t=ta;ta=tb;tb=t;s=1.0f;}
            if(ta>t0){t0=ta;axis=i;sign=s;}
            if(tb<t1)t1=tb;
            if(t0>t1)miss=true;
        }
        if(miss || axis<0 || t0>=best)continue;
        best=t0;bestAxis=axis;bestSign=sign;
    }
    if(bestAxis<0)return -1.0f;
    for(int i=0;i<3;++i){hit[i]=a[i]+d[i]*best;normal[i]=0.0f;}
    normal[bestAxis]=bestSign;
    return best*len;
}
// The top under `at` (the highest box over that point), or -1e9 with none.
float RoofAt(const World& w,const float* at) {
    float top=-1e9f;
    for(const Box& x:w)if(at[0]>=x.x0 && at[0]<=x.x1 && at[2]>=x.z0 && at[2]<=x.z1 && x.top>top)top=x.top;
    return top;
}
constexpr float kDecel=6.0f;   // the braking the stand-in plans with and has
const float kStill[3]={0.0f,0.0f,0.0f};
Result Run(const World& w,const float* pos,float radius,float* want,float* height,bool climb,const float* vel=kStill) {
    return Clear(pos,vel,radius,kDecel,want,height,climb,
        [&](const float* a,const float* b,float* hit,float* normal){ return Ray(w,a,b,hit,normal); },
        [&](const float* at){ return RoofAt(w,at); });
}
// Metres from the disc's centre (pos lifted kLift) to the nearest wall horizontally, the disc's height only.
float WallGap(const World& w,const float* pos) {
    const float y=pos[1]+kLift;
    float best=1e9f;
    for(const Box& x:w) {
        if(y>x.top)continue;
        const float dx=pos[0]<x.x0 ? x.x0-pos[0] : pos[0]>x.x1 ? pos[0]-x.x1 : 0.0f;
        const float dz=pos[2]<x.z0 ? x.z0-pos[2] : pos[2]>x.z1 ? pos[2]-x.z1 : 0.0f;
        const float g=std::sqrt(dx*dx+dz*dz);
        if(g<best)best=g;
    }
    return best;
}

// A stand-in heli: its velocity eases toward the wanted one at `accel` m/s2, its height toward the wanted one at
// up to 3 m/s (Steer's climb). The mode's want is `goal` (x, z) each step; avoidance on top of it.
struct Sim { float pos[3],vel[3]; };
float Fly(const World& w,Sim& s,const float* goalVel,float goalHeight,float radius,float seconds,float accel=kDecel) {
    constexpr float dt=1.0f/60.0f;
    float worst=1e9f;
    for(float t=0.0f;t<seconds;t+=dt) {
        float want[3]={goalVel[0],0.0f,goalVel[2]},height=goalHeight;
        Run(w,s.pos,radius,want,&height,true,s.vel);
        for(int i=0;i<3;i+=2) {
            const float dv=want[i]-s.vel[i],step=accel*dt;
            s.vel[i]+=dv>step ? step : dv<-step ? -step : dv;
            s.pos[i]+=s.vel[i]*dt;
        }
        float climb=(height-s.pos[1])*0.25f;
        climb=climb>3.0f ? 3.0f : climb<-3.0f ? -3.0f : climb;
        s.pos[1]+=climb*dt;
        const float g=WallGap(w,s.pos);
        if(g<worst)worst=g;
    }
    return worst;
}
// A lift-off (or a landing) beside a wall, as heli.cpp Steer gates it: the stick (here the commanded velocity) nothing
// horizontal on the ground and for kLiftOff ms after it, the mode's want (`goal`, x z) with the clearance on top at
// `share(sinceGround)` of its push; climbing (or sinking, `vy`) the whole time, on the ground under `groundUntil` s.
// The stand-in's velocity follows the command at kDecel. It reports the biggest step in the command from one frame to
// the next, how often the sideways velocity turned round, and where it ended.
constexpr unsigned long long kLiftOff=1500;
struct Lift { float step,x; int flips; };
template<class Share>
Lift Pad(const World& w,float x0,float y0,float vy,float groundUntil,bool land,float seconds,Share&& share) {
    constexpr float dt=1.0f/60.0f;
    Sim s{{x0,y0,0.0f},{0.0f,0.0f,0.0f}};
    Lift r{0.0f,0.0f,0};
    float last=0.0f;int sign=0;
    unsigned long long groundAt=0;bool seen=false;   // never on the ground: as long ago as heli.cpp's groundAt 0
    for(int i=0;i*dt<seconds;++i) {
        const unsigned long long ms=100000+static_cast<unsigned long long>(i)*16;
        const bool grounded=i*dt<groundUntil;
        if(grounded){groundAt=ms;seen=true;}
        const unsigned long long since=grounded ? 0 : seen ? ms-groundAt : ms;
        float want[3]={0.0f,0.0f,0.0f},height=s.pos[1];
        Clear(s.pos,s.vel,4.8f,kDecel,want,&height,!land,
              [&](const float* a,const float* b,float* hit,float* normal){ return Ray(w,a,b,hit,normal); },
              [&](const float* at){ return RoofAt(w,at); },share(land,since));
        const float cmd=since<kLiftOff ? 0.0f : want[0];
        if(i>0 && std::fabs(cmd-last)>r.step)r.step=std::fabs(cmd-last);
        last=cmd;
        const float dv=cmd-s.vel[0],a=kDecel*dt;
        s.vel[0]+=dv>a ? a : dv<-a ? -a : dv;
        s.pos[0]+=s.vel[0]*dt;
        if(!grounded)s.pos[1]+=vy*dt;
        const int now=s.vel[0]>0.05f ? 1 : s.vel[0]<-0.05f ? -1 : 0;
        if(now && sign && now!=sign)++r.flips;
        if(now)sign=now;
    }
    r.x=s.pos[0];
    return r;
}
}  // namespace

int main() {
    constexpr float r506=4.8f,r410=10.0f;

    // A wall 4 m to the east (x = 4): the 506's 2.8 m half-wide box clear of it, its 4.8 m disc not.
    {
        const World w{{4.0f,40.0f,-50.0f,50.0f,30.0f}};
        const float pos[3]={0.0f,10.0f,0.0f};
        float want[3]={5.0f,0.0f,0.0f},height=10.0f;
        const Result r=Run(w,pos,r506,want,&height,false);
        Check(r.walls>=1 && std::fabs(r.nearest-4.0f)<0.01f,"a wall 4 m east is seen");
        Check(want[0]<-1.0f,"its wanted velocity into the wall becomes a push out of it");
        Check(std::fabs(want[2])<1e-3f,"the push is straight out of a wall square to it");
        Check(height==10.0f,"climbing off: the height stays");
    }
    // The same wall with climbing on: the disc reaches it, so it goes kOverRoof over its top.
    {
        const World w{{4.0f,40.0f,-50.0f,50.0f,30.0f}};
        const float pos[3]={0.0f,10.0f,0.0f};
        float want[3]={0.0f,0.0f,0.0f},height=10.0f;
        const Result r=Run(w,pos,r506,want,&height,true);
        Check(std::fabs(r.roof-30.0f)<0.01f && std::fabs(height-(30.0f+kOverRoof))<0.01f,"in the disc: climbs over the wall's top");
    }
    // A wall past the disc but within the margin pushes without the climb.
    {
        const World w{{6.5f,40.0f,-50.0f,50.0f,30.0f}};
        const float pos[3]={0.0f,10.0f,0.0f};
        float want[3]={0.0f,0.0f,0.0f},height=10.0f;
        const Result r=Run(w,pos,r506,want,&height,true);
        Check(r.walls>=1 && want[0]<0.0f,"within the margin: pushed off");
        Check(height==10.0f,"within the margin: no climb");
    }
    // Nothing within the clearance: the want is left alone.
    {
        const World w{{20.0f,40.0f,-50.0f,50.0f,30.0f}};
        const float pos[3]={0.0f,10.0f,0.0f};
        float want[3]={3.0f,0.0f,-2.0f},height=10.0f;
        const Result r=Run(w,pos,r506,want,&height,true);
        Check(r.nearest<0.0f && r.walls==0 && want[0]==3.0f && want[2]==-2.0f && height==10.0f,"a far wall changes nothing");
    }
    // Flying away from a near wall is left as it is (nothing into it to stop), plus the push out.
    {
        const World w{{4.0f,40.0f,-50.0f,50.0f,30.0f}};
        const float pos[3]={0.0f,10.0f,0.0f};
        float want[3]={-5.0f,0.0f,0.0f},height=10.0f;
        Run(w,pos,r506,want,&height,false);
        Check(want[0]<=-5.0f,"flying off the wall is never slowed");
    }
    // A roof below and the ground: floors are not walls.
    {
        const World w{{-50.0f,50.0f,-50.0f,50.0f,8.0f}};
        const float pos[3]={0.0f,7.0f,0.0f};   // body centre under the roof's top: the rays start 1 m over it
        float want[3]={2.0f,0.0f,0.0f},height=7.0f;
        const Result r=Run(w,pos,r506,want,&height,true);
        Check(r.walls==0 && want[0]==2.0f,"a roof under the disc does not push");
    }
    // Two walls 14 m apart (x = -7 and +7): the 506 2 m off the middle is pushed back toward it; in the middle (7 m to
    // either, more than its 4.8 m disc plus kOverlap) the pushes cancel and it does not climb.
    {
        const World w{{-40.0f,-7.0f,-50.0f,50.0f,30.0f},{7.0f,40.0f,-50.0f,50.0f,30.0f}};
        const float pos[3]={2.0f,10.0f,0.0f};
        float want[3]={0.0f,0.0f,0.0f},height=10.0f;
        Run(w,pos,r506,want,&height,false);
        Check(want[0]<-0.5f,"between two walls: pushed toward the middle");
        const float mid[3]={0.0f,10.0f,0.0f};
        float wantMid[3]={0.0f,0.0f,0.0f},hMid=10.0f;
        Run(w,mid,r506,wantMid,&hMid,true);
        Check(std::fabs(wantMid[0])<1e-3f,"in the middle the two pushes cancel");
        Check(hMid==10.0f,"a street wider than the disc plus overlap: no climb");
    }
    // A street 12 m wide is narrower than the 410's 20 m disc: it climbs over the lower side, and the push is toward
    // the middle.
    {
        const World w{{-40.0f,-6.0f,-50.0f,50.0f,25.0f},{6.0f,40.0f,-50.0f,50.0f,40.0f}};
        const float pos[3]={0.0f,10.0f,0.0f};
        float want[3]={0.0f,0.0f,8.0f},height=10.0f;
        const Result r=Run(w,pos,r410,want,&height,true);
        Check(r.walls>=2,"the 410 sees both sides");
        Check(height>=40.0f+kOverRoof-0.01f,"a street narrower than its disc: it climbs over the higher side its disc reaches");
        float wantLand[3]={0.0f,0.0f,0.0f},hLand=10.0f;
        Run(w,pos,r410,wantLand,&hLand,false);
        Check(hLand==10.0f,"landing: never climbs");
    }
    // The pushes are capped.
    {
        const World w{{0.5f,40.0f,-50.0f,50.0f,30.0f}};
        const float pos[3]={0.0f,10.0f,0.0f};
        float want[3]={0.0f,0.0f,0.0f},height=10.0f;
        Run(w,pos,r410,want,&height,false);
        Check(std::sqrt(want[0]*want[0]+want[2]*want[2])<=kPushMax+1e-3f,"the push is at most kPushMax");
    }

    // Flown: a street canyon 16 m wide running north (walls x = -8 / +8, 60 m tall), the 506 3 m off its middle,
    // wanting 15 m/s north and a little into the east wall: its disc stays out of the walls.
    {
        const World w{{-60.0f,-8.0f,-500.0f,500.0f,60.0f},{8.0f,60.0f,-500.0f,500.0f,60.0f}};
        Sim s{{3.0f,15.0f,-200.0f},{0.0f,0.0f,10.0f}};
        const float goal[3]={1.5f,0.0f,15.0f};
        const float worst=Fly(w,s,goal,15.0f,r506,20.0f);
        std::printf("canyon: nearest wall %.2f m (disc %.1f m), ended x=%.2f y=%.1f z=%.0f\n",worst,r506,s.pos[0],s.pos[1],s.pos[2]);
        Check(worst>r506,"canyon: the 506's disc never reaches a wall");
        Check(s.pos[2]>0.0f,"canyon: it still makes its way along the street");
    }
    // Flown: drifting sideways at 6 m/s into a tower 20 m off: stops short with the disc clear.
    {
        const World w{{20.0f,60.0f,-60.0f,60.0f,80.0f}};
        Sim s{{0.0f,20.0f,0.0f},{6.0f,0.0f,0.0f}};
        const float goal[3]={6.0f,0.0f,0.0f};
        const float worst=Fly(w,s,goal,20.0f,r506,15.0f);
        std::printf("tower drift: nearest wall %.2f m (disc %.1f m)\n",worst,r506);
        Check(worst>r506,"tower drift: the disc stays off the tower");
    }
    // Flown: sliding sideways at 15 m/s at a tower 40 m off (further than the clearance alone looks): the rays reach
    // out by its stopping distance, so it stops in time.
    {
        const World w{{40.0f,80.0f,-60.0f,60.0f,80.0f}};
        Sim s{{0.0f,20.0f,0.0f},{15.0f,0.0f,0.0f}};
        const float goal[3]={15.0f,0.0f,0.0f};
        const float worst=Fly(w,s,goal,20.0f,r506,15.0f);
        std::printf("fast drift: nearest wall %.2f m (disc %.1f m)\n",worst,r506);
        Check(worst>r506,"fast drift: the disc stays off the tower");
    }
    // Flown: along a wall at a slant (15 deg into it) at 20 m/s: slides along it without the disc touching.
    {
        const World w{{6.0f,60.0f,-500.0f,500.0f,60.0f}};
        Sim s{{0.0f,15.0f,-300.0f},{0.0f,0.0f,20.0f}};
        const float goal[3]={20.0f*0.2588f,0.0f,20.0f*0.9659f};
        const float worst=Fly(w,s,goal,15.0f,r506,20.0f);
        std::printf("slant: nearest wall %.2f m (disc %.1f m), z=%.0f\n",worst,r506,s.pos[2]);
        Check(worst>r506,"slant: the disc stays off the wall it slides along");
    }
    // Flown: the 410 hovering 8 m off a tower (its disc 10 m: already in it), told to hold there: it backs off.
    {
        const World w{{8.0f,60.0f,-60.0f,60.0f,30.0f}};
        Sim s{{0.0f,20.0f,0.0f},{0.0f,0.0f,0.0f}};
        const float goal[3]={0.0f,0.0f,0.0f};
        Fly(w,s,goal,20.0f,r410,10.0f);
        const float gap=WallGap(w,s.pos);
        std::printf("410 hover: wall %.2f m (disc %.1f m) at y=%.1f\n",gap,r410,s.pos[1]);
        Check(gap>r410 || s.pos[1]+kLift>30.0f,"410 hover: off the tower or over it");
    }

    // How much of the push it takes: none landing, on the ground or in the lift-off, then eased in over kPushEaseMs.
    Check(PushShare(true,1000000,kLiftOff)==0.0f,"push share: none landing");
    Check(PushShare(false,0,kLiftOff)==0.0f && PushShare(false,kLiftOff,kLiftOff)==0.0f,"push share: none on the ground or lifting off");
    Check(std::fabs(PushShare(false,kLiftOff+kPushEaseMs/2,kLiftOff)-0.5f)<1e-3f,"push share: half way through the ease, half");
    Check(PushShare(false,kLiftOff+kPushEaseMs,kLiftOff)==1.0f && PushShare(false,~0ull,kLiftOff)==1.0f,"push share: all after it");
    // Without the push the cut still stops it short of the wall: the blades' safety is not eased.
    {
        const World w{{4.0f,40.0f,-50.0f,50.0f,30.0f}};
        const float pos[3]={0.0f,10.0f,0.0f};
        float want[3]={5.0f,0.0f,0.0f},height=10.0f;
        Clear(pos,kStill,r506,kDecel,want,&height,false,
              [&](const float* a,const float* b,float* hit,float* normal){ return Ray(w,a,b,hit,normal); },
              [&](const float* at){ return RoofAt(w,at); },0.0f);
        Check(std::fabs(want[0])<1e-3f,"no push: the speed into a wall inside the disc is still cut to nothing, and no push out");
    }
    // Lifting off 4 m from a tower (the 506's box clear, its disc not), straight up at 3 m/s: before, the whole push
    // (3.8 m short at 1.2 m/s a metre: 4.6 m/s) came in as one step when the lift-off's 1.5 s ended, a few metres up.
    {
        const World w{{4.0f,40.0f,-50.0f,50.0f,80.0f}};
        const auto whole=[](bool,unsigned long long){ return 1.0f; };
        const auto eased=[](bool landing,unsigned long long since){ return PushShare(landing,since,kLiftOff); };
        const Lift was=Pad(w,0.0f,0.0f,3.0f,1.0f,false,8.0f,whole),now=Pad(w,0.0f,0.0f,3.0f,1.0f,false,8.0f,eased);
        std::printf("lift-off by a tower: the command's biggest step %.2f m/s before, %.2f now; %d / %d turns; off it %.1f / %.1f m\n",
                    was.step,now.step,was.flips,now.flips,was.x,now.x);
        Check(was.step>3.0f,"lift-off (before): the push came in as a step of metres a second");
        Check(now.step<0.2f,"lift-off: the push eases in, no step in the command");
        Check(now.flips==0,"lift-off: the sideways velocity never turns round");
        Check(-now.x+4.0f>r506-0.5f,"lift-off: it still backs off the tower once eased in");
    }
    // Landing beside a wall (the land mode wants to sit where it is, 4.5 m off it): before, the push held it off its
    // spot as it came down; now it comes straight down onto it, the cut keeping it from going any closer.
    {
        const World w{{4.5f,40.0f,-50.0f,50.0f,80.0f}};
        const auto whole=[](bool,unsigned long long){ return 1.0f; };
        const auto eased=[](bool landing,unsigned long long since){ return PushShare(landing,since,kLiftOff); };
        const Lift was=Pad(w,0.0f,12.0f,-2.0f,-1.0f,true,5.0f,whole),now=Pad(w,0.0f,12.0f,-2.0f,-1.0f,true,5.0f,eased);
        std::printf("landing by a wall: off its spot %.2f m before, %.2f m now\n",was.x,now.x);
        Check(was.x<-1.0f,"landing (before): pushed off its spot, against the landing's wanted velocity");
        Check(std::fabs(now.x)<0.01f,"landing: straight down onto its spot");
    }

    std::printf("rotor_clear_check: %d checks, %d failed\n",checks,fails);
    return fails ? 1 : 0;
}
