// The gun stabilizer's math (src/stab.h) checked without the game: a hull driven over bumps (pitch and roll a few
// degrees at 1-2 Hz) and through turns, its gun stepped by the stock axis step (src/turretcam.h AxisStep, 0x5FBC00),
// drawn by a pose that takes either the aim step's own hull or the next one, its muzzle a little off its axes' line:
//  - a gun left alone (input 0): its world pointing error without the stabilizer and with it, both pose timings; the
//    probe (stab::Probe) finds the timing, and the error with it is within kHoldDeg;
//  - the turret camera steering the gun onto a far point (turretcam.h AxisCommand with the stabilizer's shift taken out
//    of its drift, as turretcam.cpp Steer does) holds it better than the camera alone and does not ring;
//  - a hull turning faster than the turret's drive: the gun lags at most the slip, then catches up once it stops;
//  - a gunner's gun on the main turret (seat 0's yaw axis turns its mount): the probe finds the mount, and the gun holds
//    while seat 0's own stabilizer turns the turret under it; one on the hull is told apart the same way;
//  - a gun whose pitch moves against its axis (a seat the frame does not describe): the probe says it does not fit;
//  - the stops: an axis is never stepped past its end, the reference rides it there;
//  - the drive's top is against the hull (the user, 2026-10-09: "炮塔旋转速度应该叠加底座旋转速度"): the turret camera
//    slewing onto a far world point, the hull standing still / turning the same way at w / against it faster than the
//    drive / slaloming: the gun's world rate reaches the drive's top + w, is carried off, settles on the point.
// Exit code 1 when one fails. Built on request only:
// cmake --build build --target stab_check && build\stab_check.exe
#include "../src/stab.h"
#include <cmath>
#include <cstdio>

namespace {
using namespace crew;
constexpr float kDeg=stab::kPi/180.0f;
constexpr float kHoldDeg=0.25f;          // the most a held gun may be off its world line on the bumps (deg)
int failures=0;

void Expect(bool ok,const char* what,double a=0.0,double b=0.0) {
    if(ok)return;
    ++failures;
    std::printf("FAIL %s (%.4f, %.4f)\n",what,a,b);
}

// The hull at heading `yaw`, nose `pitch` up, rolled `roll` (rad): rows are the world images of its local axes.
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
// The gun's offset `o` turned by the axes `a` (the inverse of stab::Unturn).
void Turn(const float* a,const float* o,float* v) {
    const float cy=std::cos(a[0]),sy=std::sin(a[0]),e=-a[1],ce=std::cos(e),se=std::sin(e);
    const float y=o[1]*ce+o[2]*se,z=-o[1]*se+o[2]*ce,x=o[0];
    v[0]=x*cy+z*sy;v[1]=y;v[2]=-x*sy+z*cy;
}
float Between(const float* a,const float* b) {   // atan2 of the cross and the dot: exact near 0 (acos is not)
    const float x[3]={a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
    return std::atan2(std::sqrt(stab::Dot3(x,x)),stab::Dot3(a,b));
}

// The hull's motion: a heading turning at up to `turn` rad/s (a slalom, its rate swinging as a sine every 2 `flip` s;
// 0: steadily, until `stopAt` s), bumps in pitch and roll.
struct Drive { float turn,flip,pitchAmp,pitchHz,rollAmp,rollHz,stopAt; };
stab::Frame HullAt(const Drive& d,int frame) {
    const float t=static_cast<float>(frame)/60.0f;
    const float moving=t<d.stopAt ? t : d.stopAt;
    const float yaw=d.flip>0.0f ? d.turn*d.flip/stab::kPi*std::sin(stab::kPi*moving/d.flip) : d.turn*moving;
    return Hull(yaw,d.pitchAmp*std::sin(2.0f*stab::kPi*d.pitchHz*t),d.rollAmp*std::sin(2.0f*stab::kPi*d.rollHz*t+0.7f));
}

struct Gun { tcam::Axis x[2]; stab::Hold hold; stab::Probe probe; stab::Choice choice; float drift[2],lastWant[2]; bool hasWant; };
struct Result { float worst,mean; stab::Choice choice; float lateWorst,lag; };

// One gun on the hull, input 0 (or the turret camera onto the starting line, `camera`), `frames` frames; the error is
// measured from `settle` on. `stabilize`: the stabilizer on. `next`: the pose takes the next step's hull.
// `lateFrom`: from that frame on, the most the gun's line moves in the world in a frame (lateWorst); `lag` the most the
// gun lagged its reference. `blind`: the probe's timing ignored (the step's own hull taken), to show what it buys.
Result Run(const Drive& d,const float* p,const stab::Perf& perf,bool stabilize,bool next,bool camera,int frames,int settle,int lateFrom=-1,bool blind=false) {
    Gun g{};
    g.x[0]=tcam::Axis{-stab::kPi,stab::kPi,0.0f,0.0f};
    g.x[1]=tcam::Axis{-60.0f*kDeg,10.0f*kDeg,-5.0f*kDeg,0.0f};
    const float o[3]={0.02f,-0.015f,0.9997f};   // the muzzle a little off the axes' line (an offset barrel)
    const stab::Stops stops[2]={stab::StopsOf(g.x[0].lo,g.x[0].hi),stab::StopsOf(g.x[1].lo,g.x[1].hi)};
    float line[3],drawn[3];
    {   const float a[2]={g.x[0].angle,g.x[1].angle};float l[3];Turn(a,o,l);stab::World(HullAt(d,0),l,line);std::memcpy(drawn,line,12);   }
    float target[3];
    {   const float a[2]={g.x[0].angle,g.x[1].angle};stab::Dir(HullAt(d,0),a,target);   }   // the camera's far point's line
    float posed[2]={g.x[0].angle,g.x[1].angle},posedWas[2]={posed[0],posed[1]};
    Result r{0.0f,0.0f,{},0.0f,0.0f};
    float lastDrawn[3]={drawn[0],drawn[1],drawn[2]};
    double sum=0.0;int n=0;
    for(int f=0;f<frames;++f) {
        const stab::Frame h=HullAt(d,f),prev=HullAt(d,f>0 ? f-1 : 0);
        const float before[2]={g.x[0].angle,g.x[1].angle};
        float in[2]={0.0f,0.0f};
        if(camera) {   // turretcam.cpp Steer: the want in the hull's frame, from the held axes, its drift less the hull's part
            float want[2];stab::Angles(h,target,want);
            const stab::Frame seenNow=stabilize && g.choice.next && f>0 ? stab::Ahead(h,prev) : h;
            float held[2],hull[2];
            if(!stabilize || !stab::Held(g.hold,stops,seenNow,before,held,hull)){held[0]=before[0];held[1]=before[1];hull[0]=hull[1]=0.0f;}
            for(int i=0;i<2;++i) {
                const bool full=i==0;   // (a full circle: the yaw axis)
                const float tgt=full ? want[i] : vec::Clamp(want[i],g.x[i].lo,g.x[i].hi);
                const float err=full ? tcam::Wrap(tgt-held[i]) : tgt-held[i];
                if(g.hasWant) {
                    const float moved=(full ? tcam::Wrap(tgt-g.lastWant[i]) : tgt-g.lastWant[i])-hull[i];
                    g.drift[i]+=(vec::Clamp(moved,-0.2f,0.2f)-g.drift[i])*0.5f;
                }
                g.lastWant[i]=tgt;
                in[i]=tcam::AxisCommand(err,g.x[i].rate,g.drift[i],p);
            }
            g.hasWant=true;
        }
        for(int i=0;i<2;++i){tcam::AxisStep(g.x[i],in[i],p);g.hold.full[i]=in[i]>=0.999f ? 1.0f : in[i]<=-0.999f ? -1.0f : 0.0f;}
        if(stabilize) {
            if(f>0)stab::Feed(g.probe,h,prev,posed,0.0f,0.0f,posedWas,drawn,false);
            g.choice=stab::Decide(g.probe,false,0.0f);
            const float after[2]={g.x[0].angle,g.x[1].angle};
            const stab::Frame seen=g.choice.next && !blind && f>0 ? stab::Ahead(h,prev) : h;
            float out[2];
            stab::Step(g.hold,stops,before,after,p[2],h,seen,perf,out);
            g.x[0].angle=out[0];g.x[1].angle=out[1];
        }
        posedWas[0]=posed[0];posedWas[1]=posed[1];
        posed[0]=g.x[0].angle;posed[1]=g.x[1].angle;
        const stab::Frame pose=next ? HullAt(d,f+1) : h;
        float l[3];Turn(posed,o,l);stab::World(pose,l,drawn);
        // The gun left alone holds its starting line; the camera's holds the bore on its point.
        float bore[3];stab::Dir(pose,posed,bore);
        const float e=camera ? Between(bore,target) : Between(drawn,line);
        if(f>=settle){if(e>r.worst)r.worst=e;sum+=e;++n;}
        if(g.hold.error>r.lag)r.lag=g.hold.error;
        const float moved=Between(drawn,lastDrawn);
        std::memcpy(lastDrawn,drawn,12);
        if(lateFrom>=0 && f>=lateFrom && moved>r.lateWorst)r.lateWorst=moved;
    }
    r.mean=n ? static_cast<float>(sum/n) : 0.0f;
    r.choice=g.choice;
    return r;
}

const stab::Perf kMbt{0.0015f,0.35f},kTitan{0.003f,0.35f};

void Bumps() {
    const Drive d{20.0f*kDeg,3.0f,3.0f*kDeg,1.5f,2.0f*kDeg,1.1f,1e9f};   // turning 20 deg/s, flipping every 3 s, on bumps
    const float p[3]={0.1f,0.1f,1.1f/60.0f};
    for(int next=0;next<2;++next) {
        const Result off=Run(d,p,kMbt,false,next!=0,false,600,60);
        const Result on=Run(d,p,kMbt,true,next!=0,false,600,60);
        const Result titan=Run(d,p,kTitan,true,next!=0,false,600,60);
        const Result blind=Run(d,p,kMbt,true,next!=0,false,600,60,-1,true);
        std::printf("bumps + 20 deg/s turns, pose %s: off worst %.2f mean %.2f deg; MBT worst %.3f mean %.3f deg; Titan worst %.3f mean %.3f deg;"
                    " probe: %s, %s\n",next ? "a step ahead" : "the step's own",off.worst/kDeg,off.mean/kDeg,on.worst/kDeg,on.mean/kDeg,
                    titan.worst/kDeg,titan.mean/kDeg,on.choice.next ? "a step ahead" : "the step's own",on.choice.fits ? "fits" : "does not fit");
        Expect(off.worst>5.0f*kDeg,"bumps: a gun left alone is thrown off its line",off.worst/kDeg);
        Expect(on.worst<kHoldDeg*kDeg,"bumps: the MBT's stabilizer holds the line",on.worst/kDeg,kHoldDeg);
        Expect(titan.worst<2.0f*kHoldDeg*kDeg && titan.mean>=on.mean,"bumps: the Titan's looser one holds it, less well",titan.worst/kDeg,on.worst/kDeg);
        Expect(on.choice.fits && on.choice.next==(next!=0),"bumps: the probe finds the pose's hull",on.choice.next,next);
        if(next) {
            std::printf("  the same without the probe's timing (the step's own hull): MBT worst %.3f mean %.3f deg\n",blind.worst/kDeg,blind.mean/kDeg);
            Expect(on.mean<blind.mean*0.5f,"bumps: aiming a step ahead pays where the pose is",on.mean/kDeg,blind.mean/kDeg);
        }
    }
}

void Camera() {
    const Drive d{25.0f*kDeg,2.5f,3.0f*kDeg,1.7f,2.0f*kDeg,1.2f,1e9f};
    const float params[][3]={{0.1f,0.1f,1.1f/60.0f},{0.015f,0.035f,1.1f/60.0f},{0.15f,0.035f,0.6f/60.0f}};
    for(const auto& p:params) {
        const Result alone=Run(d,p,kMbt,false,false,true,720,120);
        const Result held=Run(d,p,kMbt,true,false,true,720,120);
        std::printf("turret camera on a far point, bumps + 25 deg/s turns, brake %.3f accel %.3f top %.2f rad/s: camera alone worst %.2f mean %.2f deg,"
                    " with the stabilizer worst %.3f mean %.3f deg\n",p[0],p[1],p[2]*60.0f,alone.worst/kDeg,alone.mean/kDeg,held.worst/kDeg,held.mean/kDeg);
        Expect(held.worst<kHoldDeg*kDeg,"camera + stabilizer: on the point",held.worst/kDeg,kHoldDeg);
        Expect(held.mean<alone.mean*0.5f,"camera + stabilizer: better than the camera alone",held.mean/kDeg,alone.mean/kDeg);
    }
}

void Outrun() {
    // A pivot turn at 60 deg/s for 2 s with a 17 deg/s turret: the drive is outrun, the gun lags its reference at most
    // the slip (the rest of the hull's turn drags it, as an overloaded stabilizer's gun goes with the hull); once the
    // hull stops, the gun catches up and stands still in the world.
    const Drive d{60.0f*kDeg,0.0f,0.0f,1.0f,0.0f,1.0f,2.0f};
    const float p[3]={0.1f,0.1f,0.3f/60.0f};
    const Result r=Run(d,p,kMbt,true,false,false,600,0,360);
    std::printf("pivot 60 deg/s against a 17 deg/s turret: the gun %.1f deg off its starting line at worst, lagging its reference at most %.1f deg"
                " (slip %.1f); from 4 s (2 s after the turn) it moves at most %.4f deg a frame\n",r.worst/kDeg,r.lag/kDeg,kMbt.slip/kDeg,r.lateWorst/kDeg);
    Expect(r.lag<=kMbt.slip+1e-4f,"outrun: lags at most the slip",r.lag/kDeg,kMbt.slip/kDeg);
    Expect(r.worst>kMbt.slip,"outrun: the excess drags the gun",r.worst/kDeg);
    Expect(r.lateWorst<0.001f*kDeg,"outrun: caught up and still after the turn",r.lateWorst/kDeg);
}

// A gunner's gun: on the main turret (`onTurret`) or the hull; seat 0's yaw stabilized against the hull's turn (so it
// moves); the gunner wiggles its pitch for the first 3 s (an AI tracking), then holds; the error after that.
void Gunner(bool onTurret) {
    const Drive d{30.0f*kDeg,2.0f,3.0f*kDeg,1.5f,2.0f*kDeg,1.1f,1e9f};
    const float p[3]={0.1f,0.1f,1.1f/60.0f};
    tcam::Axis s0{-stab::kPi,stab::kPi,0.4f,0.0f};
    stab::Hold h0{};
    const stab::Stops s0Stops[2]={stab::StopsOf(-stab::kPi,stab::kPi),stab::StopsOf(-1.0f,1.0f)};
    tcam::Axis g[2]={{-stab::kPi,stab::kPi,0.2f,0.0f},{-0.6f,0.3f,-0.05f,0.0f}};
    const stab::Stops gStops[2]={stab::StopsOf(g[0].lo,g[0].hi),stab::StopsOf(g[1].lo,g[1].hi)};
    stab::Hold hold{};stab::Probe probe{};stab::Choice choice{};
    const float o[3]={0.0f,0.0f,1.0f};
    float posed[2]={g[0].angle,g[1].angle},posedWas[2]={posed[0],posed[1]},s0Posed=s0.angle,s0Was=s0.angle,drawn[3]={0,0,1};
    float line[3]={0,0,0};
    float worst=0.0f;
    const auto mountOf=[&](const stab::Frame& hull,float yaw0) { return onTurret ? stab::Turned(hull,yaw0) : hull; };
    for(int f=0;f<600;++f) {
        const stab::Frame h=HullAt(d,f),prev=HullAt(d,f>0 ? f-1 : 0);
        // Seat 0 first (as the 403's slot 4 steps them): input 0, its own stabilizer (it knows its mount: the hull).
        const float b0[2]={s0.angle,0.0f};
        tcam::AxisStep(s0,0.0f,p);
        float out0[2];const float a0[2]={s0.angle,0.0f};
        stab::Step(h0,s0Stops,b0,a0,p[2],h,h,kMbt,out0);
        s0.angle=out0[0];
        // The gunner: its probe reads seat 0's yaw as the last pose drew it (b0) and now.
        const float before[2]={g[0].angle,g[1].angle};
        const float in[2]={0.0f,f<180 ? 0.6f*std::sin(static_cast<float>(f)*0.08f) : 0.0f};
        for(int i=0;i<2;++i)tcam::AxisStep(g[i],in[i],p);
        if(f>0)stab::Feed(probe,h,prev,posed,s0Posed,s0Was,posedWas,drawn,true);
        choice=stab::Decide(probe,true,s0.angle);
        const float after[2]={g[0].angle,g[1].angle};
        if(choice.known && choice.fits) {
            const stab::Frame m=choice.mount ? stab::Turned(h,s0.angle) : h;
            float out[2];
            stab::Step(hold,gStops,before,after,p[2],m,m,kMbt,out);
            g[0].angle=out[0];g[1].angle=out[1];
        } else hold.live=false;
        posedWas[0]=posed[0];posedWas[1]=posed[1];s0Was=s0Posed;
        posed[0]=g[0].angle;posed[1]=g[1].angle;s0Posed=s0.angle;
        float l[3];Turn(posed,o,l);stab::World(mountOf(h,s0.angle),l,drawn);
        if(f==240)std::memcpy(line,drawn,12);
        if(f>240){const float e=Between(drawn,line);if(e>worst)worst=e;}
    }
    std::printf("gunner on %s: probe %s, mount %s; held from 4 s on: worst %.3f deg\n",onTurret ? "the main turret" : "the hull",
                choice.known ? (choice.fits ? "fits" : "does not fit") : "undecided",choice.mount ? "the main turret" : "the hull",worst/kDeg);
    Expect(choice.known && choice.fits && choice.mount==(onTurret ? 1 : 0),"gunner: the probe finds its mount",choice.mount,onTurret);
    Expect(worst<kHoldDeg*kDeg,"gunner: holds its line",worst/kDeg,kHoldDeg);
}

// A gun whose pitch turns against its axis: the probe must not take it.
void WrongSign() {
    const Drive d{20.0f*kDeg,3.0f,3.0f*kDeg,1.5f,2.0f*kDeg,1.1f,1e9f};
    const float p[3]={0.1f,0.1f,1.1f/60.0f};
    tcam::Axis g[2]={{-stab::kPi,stab::kPi,0.0f,0.0f},{-0.6f,0.3f,-0.1f,0.0f}};
    stab::Probe probe{};
    float posed[2]={0,0},posedWas[2]={0,0},drawn[3]={0,0,1};
    stab::Choice c{};
    for(int f=0;f<600;++f) {
        const stab::Frame h=HullAt(d,f),prev=HullAt(d,f>0 ? f-1 : 0);
        const float in[2]={0.3f*std::sin(static_cast<float>(f)*0.05f),0.6f*std::sin(static_cast<float>(f)*0.07f)};
        for(int i=0;i<2;++i)tcam::AxisStep(g[i],in[i],p);
        if(f>0)stab::Feed(probe,h,prev,posed,0.0f,0.0f,posedWas,drawn,false);
        c=stab::Decide(probe,false,0.0f);
        posedWas[0]=posed[0];posedWas[1]=posed[1];
        posed[0]=g[0].angle;posed[1]=g[1].angle;
        const float real[2]={posed[0],-posed[1]};   // the bone turns the other way
        stab::Dir(h,real,drawn);
    }
    std::printf("a gun whose pitch turns against its axis: the probe says it %s\n",c.fits ? "fits (wrong)" : "does not fit");
    Expect(!c.fits,"wrong sign: left stock");
}

void Stops() {
    // Nose pitching 12 deg down and up with a pitch axis stopping at -10..+5 deg: the axis never past its stops.
    const Drive d{0.0f,0.0f,12.0f*kDeg,0.5f,0.0f,1.0f,1e9f};
    const float p[3]={0.1f,0.1f,1.1f/60.0f};
    tcam::Axis g[2]={{-0.5f,0.5f,0.0f,0.0f},{-10.0f*kDeg,5.0f*kDeg,0.0f,0.0f}};
    const stab::Stops s[2]={stab::StopsOf(g[0].lo,g[0].hi),stab::StopsOf(g[1].lo,g[1].hi)};
    stab::Hold hold{};
    float most=-1.0f,least=1.0f;
    for(int f=0;f<600;++f) {
        const stab::Frame h=HullAt(d,f);
        const float before[2]={g[0].angle,g[1].angle};
        for(int i=0;i<2;++i)tcam::AxisStep(g[i],0.0f,p);
        const float after[2]={g[0].angle,g[1].angle};
        float out[2];
        stab::Step(hold,s,before,after,p[2],h,h,kMbt,out);
        g[0].angle=out[0];g[1].angle=out[1];
        if(out[1]>most)most=out[1];
        if(out[1]<least)least=out[1];
    }
    Expect(most<=g[1].hi+1e-6f && least>=g[1].lo-1e-6f,"stops: the pitch axis stays within them",least/kDeg,most/kDeg);
    Expect(most>g[1].hi-0.5f*kDeg && least<g[1].lo+0.5f*kDeg,"stops: it rides them (the reference not wound up past them)",least/kDeg,most/kDeg);
    std::printf("stops -10..+5 deg under a 12 deg pitching hull: the axis went %.2f..%.2f deg\n",least/kDeg,most/kDeg);
}

void NativeReadbackContract() {
    const stab::Frame previous=HullAt(Drive{},0);
    const stab::Frame current=stab::Turned(previous,0.2f*kDeg);
    const stab::Stops limits[2]={stab::StopsOf(-stab::kPi,stab::kPi),stab::StopsOf(-1.0f,1.0f)};
    const float start[2]={0.3f,0.0f};
    stab::Hold h{};float out[2];
    stab::Step(h,limits,start,start,0.5f*kDeg,previous,previous,kMbt,out);
    const float measured=start[0]-0.3f*kDeg;
    const float before[2]={measured,0},after[2]={measured+0.02f*kDeg,0.01f*kDeg};
    stab::Readback(h,limits[0],0,start[0],measured);
    Expect(std::memcmp(h.last.r,previous.r,sizeof(previous.r))==0 && std::memcmp(h.seen.r,previous.r,sizeof(previous.r))==0,
           "native readback reconciles local actuator state without replacing the previous parent basis");
    stab::Step(h,limits,before,after,0.5f*kDeg,current,current,kMbt,out);
    Expect(std::fabs(h.shift[0]+0.2f*kDeg*stab::Gain(kMbt))<1e-6f,
           "physical tracking lag, hull turn and player input coexist without double correction",h.shift[0]/kDeg);
    Expect(std::fabs(out[1]-after[1])<1e-6f,"a yaw readback does not rewrite the pitch motor command");
}
// The turret camera slewing the gun onto a world point `away` rad to its left (the yaw axis free, the hull's heading
// turning at `w` rad/s, + to the left: toward the point; a slalom of amplitude `w` rad/s every 2 s with `slalom`),
// stabilized as the plugin does it (turretcam.cpp Steer from stab::Held, stab.h Step with the command's ends) or with
// `full` left off (the drive's top a world rate: the old model). Its world heading's fastest turn (rad/s), the frame it
// is first within 0.5 deg, its error at 2 s and the most it is off over the last second.
struct Slew { float peak; int on; float at2,late; };
Slew SlewOnto(float away,float w,bool slalom,bool full) {
    const float p[3]={0.1f,0.1f,0.5f/60.0f};   // a 0.5 rad/s (29 deg/s) drive
    tcam::Axis x[2]={{-stab::kPi,stab::kPi,0.0f,0.0f},{-1.0f,0.3f,0.0f,0.0f}};
    const stab::Stops stops[2]={stab::StopsOf(x[0].lo,x[0].hi),stab::StopsOf(x[1].lo,x[1].hi)};
    stab::Hold hold{};stab::Probe probe{};
    const auto heading=[&](int f) {
        const float t=static_cast<float>(f)/60.0f;
        return slalom ? w*2.0f/stab::kPi*std::sin(stab::kPi*t/2.0f) : w*t;
    };
    float target[3];{const float a[2]={away,0.0f};stab::Dir(Hull(0.0f,0.0f,0.0f),a,target);}
    tcam::SteerState steer{};
    Slew r{0.0f,-1,0.0f,0.0f};
    float lastYaw=0.0f;
    for(int f=0;f<360;++f) {
        const stab::Frame h=Hull(heading(f),0.0f,0.0f);
        const float before[2]={x[0].angle,x[1].angle};
        float held[2],hull[2],want[2],in[2];
        if(!stab::Held(hold,stops,h,before,held,hull)){held[0]=before[0];held[1]=before[1];hull[0]=hull[1]=0.0f;}
        stab::Angles(h,target,want);
        tcam::SteerAxes(steer,want,held,hull,x,p,0.0087f,in);
        for(int i=0;i<2;++i){tcam::AxisStep(x[i],in[i],p);hold.full[i]=full && std::fabs(in[i])>=0.999f ? (in[i]>0.0f ? 1.0f : -1.0f) : 0.0f;}
        const float after[2]={x[0].angle,x[1].angle};
        float out[2];
        stab::Step(hold,stops,before,after,p[2],h,h,kMbt,out);
        x[0].angle=out[0];x[1].angle=out[1];
        float bore[3];stab::Dir(h,out,bore);
        float world[2];stab::Angles(Hull(0.0f,0.0f,0.0f),bore,world);
        if(f>0)r.peak=std::fmax(r.peak,std::fabs(tcam::Wrap(world[0]-lastYaw))*60.0f);
        lastYaw=world[0];
        const float err=Between(bore,target);
        if(r.on<0 && err<0.5f*kDeg)r.on=f;
        if(f==120)r.at2=err;
        if(f>=300)r.late=std::fmax(r.late,err);
    }
    (void)probe;
    return r;
}

void HullAddsToDrive() {
    const float top=0.5f,away=120.0f*kDeg;
    const Slew still=SlewOnto(away,0.0f,false,true),stillOld=SlewOnto(away,0.0f,false,false);
    const Slew with=SlewOnto(away,0.3f,false,true),withOld=SlewOnto(away,0.3f,false,false);
    const Slew against=SlewOnto(away,-0.8f,false,true);
    const Slew snake=SlewOnto(away,0.3f,true,true);   // within the drive (faster carries it off, as above)
    std::printf("drive 0.5 rad/s onto a point 120 deg left: hull still peak %.3f rad/s, on at frame %d (full off: %.3f, %d); "
                "hull turning toward it 0.3 rad/s: peak %.3f rad/s, on at %d (full off: %.3f, %d); against at 0.8 rad/s: error %.1f deg at 2 s; "
                "slalom +-0.3 rad/s: last second off by at most %.3f deg\n",still.peak,still.on,stillOld.peak,stillOld.on,with.peak,with.on,
                withOld.peak,withOld.on,against.at2/kDeg,snake.late/kDeg);
    Expect(still.on==stillOld.on && std::fabs(still.peak-stillOld.peak)<1e-4f,"hull still: the slew is the same as before",still.on,stillOld.on);
    Expect(still.peak<=top*1.001f,"hull still: never faster than the drive",still.peak,top);
    Expect(with.peak>=(top+0.3f)*0.97f,"hull turning the same way: the gun's world rate is the drive's top plus the hull's",with.peak,top+0.3f);
    Expect(with.on>=0 && with.on<still.on*0.8f && with.on<withOld.on,"hull turning the same way: on the point sooner",with.on,still.on);
    Expect(withOld.peak<=top*1.02f,"(the old model capped the world rate at the drive's top)",withOld.peak,top);
    Expect(against.at2>away,"hull turning against it faster than the drive: the gun is carried off",against.at2/kDeg,away/kDeg);
    Expect(snake.on>=0 && snake.late<0.5f*kDeg,"world point, slaloming hull: the gun settles on it",snake.late/kDeg);
    // The full slew let go: the hull turning the same way at 0.3 rad/s under a full command for a second, then the hull
    // stopped and the command 0 (the stock step turns nothing). The gun stays where the slew left it, no pull back.
    // The hull turning to + heading helps a + command here (the gun keeps no error: the counter-turn was not made).
    const stab::Stops st[2]={stab::StopsOf(-stab::kPi,stab::kPi),stab::StopsOf(-1.0f,0.3f)};
    stab::Hold hold{};
    float axes[2]={0.0f,0.0f},out[2];
    const float step=top/60.0f;
    stab::Step(hold,st,axes,axes,step,Hull(0.0f,0.0f,0.0f),Hull(0.0f,0.0f,0.0f),kMbt,out);
    bool helped=true;
    float heading=0.0f;
    for(int f=1;f<=60;++f) {
        heading+=0.3f/60.0f;
        const stab::Frame h=Hull(heading,0.0f,0.0f);
        const float before[2]={axes[0],axes[1]},after[2]={axes[0]+step,axes[1]};
        hold.full[0]=1.0f;hold.full[1]=0.0f;
        stab::Step(hold,st,before,after,step,h,h,kMbt,out);
        helped=helped && out[0]==after[0] && hold.shift[0]==0.0f && !hold.slipping && hold.error<1e-6f;
        axes[0]=out[0];axes[1]=out[1];
    }
    hold.full[0]=0.0f;
    const stab::Frame h=Hull(heading,0.0f,0.0f);
    stab::Step(hold,st,axes,axes,step,h,h,kMbt,out);
    float held[2],part[2];
    const bool seen=stab::Held(hold,st,h,axes,held,part);
    std::printf("full slew let go: the step after moves the axis %.6f deg; the look ahead shows it %.6f deg off\n",
                (out[0]-axes[0])/kDeg,(held[0]-axes[0])/kDeg);
    Expect(helped,"(the hull's turn did help the full slew: no counter-turn)");
    Expect(std::fabs(out[0]-axes[0])<1e-6f,"full slew let go: no pull back toward a trailing reference",(out[0]-axes[0])/kDeg);
    Expect(seen && std::fabs(held[0]-axes[0])<1e-6f,"full slew let go: the look ahead holds the gun as it is",(held[0]-axes[0])/kDeg);
}
}  // namespace

int main() {
    Bumps();
    Camera();
    Outrun();
    Gunner(true);
    Gunner(false);
    WrongSign();
    Stops();
    NativeReadbackContract();
    HullAddsToDrive();
    std::printf(failures ? "%d FAILED\n" : "all passed\n",failures);
    return failures ? 1 : 0;
}
