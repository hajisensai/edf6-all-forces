// The Grape's barrel twitching left and right under the turret camera (the user, 2026-10-06: "the modified Grape's
// barrel twitches left and right while driving and cannot aim"), checked without the game on the Grape's own geometry
// and mechanics (docs/camera-re.md §7a):
//  - the gun (VEHICLE401_STRIKER.MRAB): the turret `Battery` turns about (0, 2.05, -1.23) on the hull, the barrel
//    `Barrel` about its trunnion 0.42 m ahead and 0.2 m up of that, the muzzle 2.2 m out along it; the yaw stops at
//    +-115 deg (car_base_constraint_data's hinge limit), the pitch at -40..+5 deg;
//  - the mechanics: turret and barrel are physics bodies. The yaw is read back from its joint before every aim step and a
//    velocity motor drives the joint onto the axis' angle in the physics step after it (0x6696A0 / 0x669630); the
//    barrel's position motor follows the pitch axis at 3.14 rad/s (+0.2 rad/s), a tank's at 31.4;
//  - the camera: the authored rig (game_object_camera_setting: 4 m over the hull's origin, 10.5 m back), held in the
//    world; the turret camera turns the gun onto the point under the screen's centre (a ground hit, else 800 m out);
//  - the controllers as the plugin runs them: the turret command (turretcam.h SteerAxes, AxisCommand) and the gun
//    stabilizer (stab.h Probe / Step / HeldIn), each frame in the game's order.
// Two ways of taking the want: 0.8.0's (from the muzzle, in the hull's frame now) and the fix's (from the bore's point at
// the turret's pivot, turretcam.h AimOrigin, in the frame the stabilizer's held axes are seen in, stab.h HeldIn). For
// each: how often the gun's heading turns back (reversals a second: the twitch) and how far its bore is off the point
// across, over a grid of turret params (the Grape's own are not known: brake, accel, top 0.5..3 rad/s), with the
// stabilizer on and off. Exit 1 when the fix's gun twitches or misses.
// cmake --build build --target grape_turret_check && build\grape_turret_check.exe
#include "../src/stab.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace {
using namespace crew;
constexpr float kDeg=stab::kPi/180.0f;
constexpr float kFixReversals=1.0f;   // a second, at most, for the fix's gun (a slalom's own turns back are under it)
int failures=0;

void Expect(bool ok,const char* what,double a=0.0,double b=0.0) {
    if(ok)return;
    ++failures;
    std::printf("FAIL %s (%.4f, %.4f)\n",what,a,b);
}

stab::Frame Hull(float yaw,float pitch,float roll) {
    const float cy=std::cos(yaw),sy=std::sin(yaw),cp=std::cos(pitch),sp=std::sin(pitch),cr=std::cos(roll),sr=std::sin(roll);
    const auto rot=[&](const float* v,float* out) {
        const float a[3]={v[0]*cr-v[1]*sr,v[0]*sr+v[1]*cr,v[2]};
        const float b[3]={a[0],a[1]*cp+a[2]*sp,-a[1]*sp+a[2]*cp};
        out[0]=b[0]*cy+b[2]*sy;out[1]=b[1];out[2]=-b[0]*sy+b[2]*cy;
    };
    const float ex[3]={1,0,0},ey[3]={0,1,0},ez[3]={0,0,1};
    stab::Frame f{};
    rot(ex,f.r);rot(ey,f.r+3);rot(ez,f.r+6);
    return f;
}

// The drive: heading swinging (a slalom: up to `turn` rad/s, its rate a sine every 2 `flip` s) at `speed` m/s, bumps in
// pitch, roll and height (`heave` m).
struct Drive { float turn,flip,speed,pitchAmp,pitchHz,rollAmp,rollHz,heave; };
float YawAt(const Drive& d,int f) { return d.turn*d.flip/stab::kPi*std::sin(stab::kPi*static_cast<float>(f)/60.0f/d.flip); }
stab::Frame HullAt(const Drive& d,int f) {
    const float t=static_cast<float>(f)/60.0f;
    return Hull(YawAt(d,f),d.pitchAmp*std::sin(2.0f*stab::kPi*d.pitchHz*t),d.rollAmp*std::sin(2.0f*stab::kPi*d.rollHz*t+0.7f));
}

// The gun's geometry on the hull (hull local): the turret's pivot, the trunnion off it (turned by the yaw), the muzzle's
// reach along the barrel.
constexpr float kPivot[3]={0.0f,2.05f,-1.23f},kTrunnion[3]={0.0f,0.2f,0.42f},kBarrel=2.2f;
// The drawn gun in the world: trunnion (the Grape's look-at camera bone, `Barrel`: its turret pivot as turretcam.cpp
// TurretPivot reads it), muzzle and bore for the hull `h` at `at` and the joint's angles `a` (the aim's senses).
void Gun(const stab::Frame& h,const float* at,const float* a,float* trunnion,float* muzzle,float* dir) {
    const float cy=std::cos(a[0]),sy=std::sin(a[0]);
    const float t[3]={kPivot[0]+kTrunnion[0]*cy+kTrunnion[2]*sy,kPivot[1]+kTrunnion[1],kPivot[2]-kTrunnion[0]*sy+kTrunnion[2]*cy};
    float w[3];stab::World(h,t,w);
    for(int i=0;i<3;++i)trunnion[i]=at[i]+w[i];
    stab::Dir(h,a,dir);
    for(int i=0;i<3;++i)muzzle[i]=trunnion[i]+dir[i]*kBarrel;
}

// The view: a world heading off the start's and a pitch (down negative); the camera's point: the authored rig's look-at
// 4 m over the hull's origin, the ray through it onto the ground (y 0), else 800 m out (turretcam.cpp AimPoint).
void AimPoint(const float* at,float yaw,float pitch,float* p) {
    const float d[3]={std::sin(yaw)*std::cos(pitch),std::sin(pitch),std::cos(yaw)*std::cos(pitch)};
    const float o[3]={at[0],at[1]+4.0f,at[2]};
    const float t=d[1]<-1e-3f ? -o[1]/d[1] : 1e9f;
    const float s=t<3000.0f ? t : 800.0f;
    for(int i=0;i<3;++i)p[i]=o[i]+d[i]*s;
}

enum class Way { v080, fix };
struct Result { float reversals,worst,mean; };

// `stabilize`: the gun stabilizer on (the Grape's: lag 3 ms). Measured from frame `settle`: the gun's heading turning
// back (its frame's turn changing sign by more than 0.02 deg), and its bore's miss across: the angle in the hull's plane
// between the bore and the line from the bore's point at the pivot to the camera's point.
Result Run(const Drive& d,float viewYaw,float viewPitch,const float* p,bool stabilize,Way way,int frames=900,int settle=180) {
    tcam::Axis x[2]={{-115.0f*kDeg,115.0f*kDeg,0.0f,0.0f},{-40.0f*kDeg,5.0f*kDeg,0.0f,0.0f}};
    const stab::Stops stops[2]={stab::StopsOf(x[0].lo,x[0].hi),stab::StopsOf(x[1].lo,x[1].hi)};
    const stab::Perf perf{0.003f,0.35f};
    stab::Hold hold{};stab::Probe probe{};stab::Choice choice{};
    tcam::SteerState steer{};
    bool active=false;
    float joint=0.0f,barrel=0.0f;   // the physics: the turret's yaw on the hull, the barrel's drawn pitch
    float at[3]={0.0f,0.0f,0.0f};
    float posed[2]={0.0f,0.0f},posedWas[2]={0.0f,0.0f};
    float trunnion[3],muzzle[3],dir[3];
    {const float a[2]={joint,barrel};Gun(HullAt(d,0),at,a,trunnion,muzzle,dir);}
    Result r{0,0,0};double sum=0.0;int n=0;
    float lastYaw=0.0f,lastTurn=0.0f;bool hasYaw=false,hasTurn=false;
    for(int f=0;f<frames;++f) {
        const stab::Frame h=HullAt(d,f),prev=f>0 ? HullAt(d,f-1) : h;
        x[0].angle=joint;   // 0x6696A0: the yaw axis := its joint
        const float before[2]={x[0].angle,x[1].angle};
        float point[3];AimPoint(at,viewYaw,viewPitch,point);
        // turretcam.cpp Aim: what it steers against (StabHeld), the want (Wants), the command (Steer).
        const stab::Frame seen=active && choice.next && f>0 ? stab::Ahead(h,prev) : h;
        float held[2]={before[0],before[1]},hull[2]={0.0f,0.0f};
        stab::Frame frame=h;
        if(active)stab::HeldIn(hold,stops,seen,h,before,held,hull,&frame);
        if(way==Way::v080)frame=h;
        float origin[3];
        tcam::AimOrigin(muzzle,dir,way==Way::fix ? trunnion : nullptr,origin);
        float l[3],across=0.0f;
        tcam::LocalTo(frame.r,origin,point,l,&across);
        const float want[2]={std::atan2(l[0],l[2]),-std::atan2(l[1],across)};
        float in[2];
        tcam::SteerAxes(steer,want,held,hull,x,p,0.0087f,in);
        for(int i=0;i<2;++i)tcam::AxisStep(x[i],in[i],p);
        if(stabilize) {   // stab.cpp Hold: the probe fed with the drawn gun, then the step
            if(f>0)stab::Feed(probe,h,prev,posed,0.0f,0.0f,posedWas,dir,false);
            choice=stab::Decide(probe,false,0.0f);
            active=choice.known && choice.fits;
            if(active) {
                const float after[2]={x[0].angle,x[1].angle};
                float out[2];
                stab::Step(hold,stops,before,after,p[2],h,choice.next && f>0 ? stab::Ahead(h,prev) : h,perf,out);
                x[0].angle=out[0];x[1].angle=out[1];
            } else hold.live=false;
        }
        posedWas[0]=posed[0];posedWas[1]=posed[1];
        // The physics step: the hull moves on; the joint reaches the axis' angle (the motor's 60 x error rad/s for a
        // frame), within the hinge's limit; the barrel's position motor eases toward the pitch axis.
        joint=vec::Clamp(x[0].angle,x[0].lo,x[0].hi);
        const float e=x[1].angle-barrel,v=(3.14f*e+(e>0.0f ? 0.2f : -0.2f))/60.0f;
        barrel+=std::fabs(v)>std::fabs(e) ? e : v;
        const stab::Frame next=HullAt(d,f+1);
        const float heading=YawAt(d,f);
        at[0]+=std::sin(heading)*d.speed/60.0f;at[2]+=std::cos(heading)*d.speed/60.0f;
        at[1]=d.heave*std::sin(2.0f*stab::kPi*1.3f*static_cast<float>(f+1)/60.0f);
        posed[0]=joint;posed[1]=barrel;
        {const float a[2]={joint,barrel};Gun(next,at,a,trunnion,muzzle,dir);}
        if(f<settle)continue;
        // The gun's heading in the world and its miss across.
        const float yaw=std::atan2(dir[0],dir[2]);
        if(hasYaw) {
            const float turn=tcam::Wrap(yaw-lastYaw);
            if(hasTurn && turn*lastTurn<0.0f && std::fabs(turn-lastTurn)>0.02f*kDeg)r.reversals+=1.0f;
            lastTurn=turn;hasTurn=true;
        }
        lastYaw=yaw;hasYaw=true;
        // The ideal: the bore line through the point from the bore's point at the pivot (worked out here, not by AimOrigin).
        float o2[3];{float k=0.0f;for(int i=0;i<3;++i)k+=(trunnion[i]-muzzle[i])*dir[i];for(int i=0;i<3;++i)o2[i]=muzzle[i]+dir[i]*k;}
        float lp[3],ld[3],acr=0.0f;
        tcam::LocalTo(next.r,o2,point,lp,&acr);
        stab::Local(next,dir,ld);
        const float miss=std::fabs(tcam::Wrap(std::atan2(lp[0],lp[2])-std::atan2(ld[0],ld[2])));
        if(miss>r.worst)r.worst=miss;
        sum+=miss;++n;
    }
    r.reversals/=static_cast<float>(frames-settle)/60.0f;
    r.mean=n ? static_cast<float>(sum/n) : 0.0f;
    return r;
}

struct Case { const char* name; Drive drive; float viewYaw,viewPitch; float fixMissDeg; };

void Grid(const Case& c) {
    const float brakes[]={0.015f,0.1f,0.5f,1.2f},accels[]={0.035f,0.1f,0.3f,1.0f},tops[]={0.5f,1.57f,3.0f};
    for(int st=0;st<2;++st) {
        Result worst[2]={{0,0,0},{0,0,0}};float meanSum[2]={0,0};int runs=0,twitching[2]={0,0};
        for(float br:brakes)for(float ac:accels)for(float tp:tops) {
            const float p[3]={br,ac,tp/60.0f};
            for(int w=0;w<2;++w) {
                const Result r=Run(c.drive,c.viewYaw,c.viewPitch,p,st!=0,w ? Way::fix : Way::v080);
                if(r.reversals>worst[w].reversals)worst[w].reversals=r.reversals;
                if(r.worst>worst[w].worst)worst[w].worst=r.worst;
                if(r.mean>worst[w].mean)worst[w].mean=r.mean;
                meanSum[w]+=r.mean;
                if(r.reversals>5.0f)++twitching[w];
            }
            ++runs;
        }
        std::printf("%-48s %-15s 0.8.0: %2d/%d param sets twitch, reversals up to %4.1f/s, miss worst %4.2f deg (mean %.2f); fix: %d twitch,"
                    " reversals up to %3.1f/s, miss worst %4.2f deg (mean %.2f)\n",c.name,st ? "stabilizer on:" : "stabilizer off:",twitching[0],runs,
                    worst[0].reversals,worst[0].worst/kDeg,meanSum[0]/static_cast<float>(runs)/kDeg,twitching[1],worst[1].reversals,
                    worst[1].worst/kDeg,meanSum[1]/static_cast<float>(runs)/kDeg);
        Expect(worst[1].reversals<=kFixReversals,"fix: the gun does not twitch",worst[1].reversals,kFixReversals);
        Expect(worst[1].mean<=c.fixMissDeg*kDeg,"fix: the gun stays on the point",worst[1].mean/kDeg,c.fixMissDeg);
    }
}

// A far point while steering hard, the stabilizer on: 0.8.0 saw the want in the hull's frame now, the held axes in the
// frame a step ahead, one frame of the hull's turn apart; the gun was kept that far off the point, the offset swinging
// with the steering. Only turrets that outrun the hull's turn (a slower one is dragged: the stabilizer's slip).
void Far() {
    const Drive d{40.0f*kDeg,1.5f,12.0f,2.0f*kDeg,1.5f,1.5f*kDeg,1.1f,0.05f};
    const float brakes[]={0.1f,0.5f,1.2f},accels[]={0.1f,0.3f,1.0f},tops[]={1.57f,3.0f};
    float worst[2]={0,0},meanSum[2]={0,0},rev[2]={0,0};int runs=0;
    for(float br:brakes)for(float ac:accels)for(float tp:tops) {
        const float p[3]={br,ac,tp/60.0f};
        for(int w=0;w<2;++w) {
            const Result r=Run(d,0.0f,0.0f,p,true,w ? Way::fix : Way::v080);
            if(r.mean>worst[w])worst[w]=r.mean;
            if(r.reversals>rev[w])rev[w]=r.reversals;
            meanSum[w]+=r.mean;
        }
        ++runs;
    }
    std::printf("far point (800 m), steering 40 deg/s, stabilizer on: miss mean over the params 0.8.0 %.3f deg (worst run %.3f), fix %.3f"
                " deg (worst run %.3f); reversals up to %.1f / %.1f a second\n",meanSum[0]/static_cast<float>(runs)/kDeg,worst[0]/kDeg,
                meanSum[1]/static_cast<float>(runs)/kDeg,worst[1]/kDeg,rev[0],rev[1]);
    Expect(worst[1]<0.5f*worst[0],"far point: the want seen where the held axes are halves the miss",worst[1]/kDeg,worst[0]/kDeg);
}
}  // namespace

int main() {
    // Looking at the ground ahead while driving: the point 3.4 m ahead of the hull's origin, 4.6 m from the turret's pivot
    // (the muzzle 2.6 m out from it).
    Grid({"ground ahead (view 50 deg down), slalom 20 deg/s",{20.0f*kDeg,2.5f,8.0f,2.0f*kDeg,1.5f,1.5f*kDeg,1.1f,0.05f},0.0f,-50.0f*kDeg,0.6f});
    Grid({"ground ahead (view 35 deg down), slalom 20 deg/s",{20.0f*kDeg,2.5f,8.0f,2.0f*kDeg,1.5f,1.5f*kDeg,1.1f,0.05f},0.0f,-35.0f*kDeg,0.6f});
    Grid({"ground aside (40 deg down, 30 deg right)",{20.0f*kDeg,2.5f,8.0f,2.0f*kDeg,1.5f,1.5f*kDeg,1.1f,0.05f},-30.0f*kDeg,-40.0f*kDeg,0.6f});
    Far();
    std::printf(failures ? "%d FAILED\n" : "all passed\n",failures);
    return failures ? 1 : 0;
}
