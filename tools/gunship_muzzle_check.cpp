// The gunship's muzzle (src/gunmuzzle.h) checked without the game, on the circles it flies (the user, 2026-10-06:
// 「炮舰机的机炮会打到自己身上」「炮舰机的轰炸炮弹，感觉在飞机后面出现的」):
//  - a frame-by-frame orbit: the NPC gunship (jet_internal.h kKinds gunship: 120 m/s, 2 g, 350 m over a 600 m circle, so
//    banked 60 degrees on the 848 m circle 2 g holds) and the player's pylon turn (playerjet_crew.inc / README: 400-900 m,
//    74-145 m/s, banked as the turn needs up to playerjet.cpp kTurnBank 1.2 rad), the target at the circle's centre;
//  - each shot as before (from the vehicle's origin, the shell's stock 60-frame wait: docs/carrier-laser-re.md §3 #15) and
//    as now (from the muzzle, no wait: jet_bay.cpp kIfcWait), a frame of lag either way (the round's object may take its
//    first step a frame after it is made: not verified in the game);
//  - for each: how far the round's start is from where the gunship's muzzle is when the round leaves, and how much of its
//    line to the target runs inside the airframe's box grown by the round's hit radius.
// Now: no line inside, the start at most a frame's flight from the muzzle. Before: printed (the check also fails if the
// old numbers stop showing the bug: the scenario would no longer test anything). Exit code 1 on a failure. Built on request:
// cmake --build build --target gunship_muzzle_check && build\gunship_muzzle_check.exe
#include "../src/gunmuzzle.h"
#include <cmath>
#include <cstdio>

namespace {
int failures=0;

void Expect(bool ok,const char* what,double a=0.0,double b=0.0) {
    if(ok)return;
    ++failures;
    std::printf("FAIL %s (%.4f, %.4f)\n",what,a,b);
}

constexpr float kG=9.8f,kPi=3.14159265f;
constexpr int kShellWait=60;   // DEMOGUNSHIPFIREE25 #15

struct Orbit {
    const char* name;
    float speed,radius,height,maxBank;   // m/s, m, m over the target, rad
};

// The gunship `frame` frames into the orbit: matrix `m` (veh+0x60 layout: right, up, forward, position), counterclockwise
// seen from above with the target (the origin) on its left, banked as the turn needs (at most maxBank; past it the
// circle widens to what maxBank holds).
float Bank(const Orbit& o,float* radius) {
    float bank=std::atan(o.speed*o.speed/(kG*o.radius));
    *radius=o.radius;
    if(bank>o.maxBank){bank=o.maxBank;*radius=o.speed*o.speed/(kG*std::tan(bank));}
    return bank;
}

void PoseT(const Orbit& o,float frame,float* m) {
    float r;
    const float bank=Bank(o,&r);
    const float a=o.speed/r*frame/60.0f;   // rad round the circle
    const float pos[3]={r*std::cos(a),o.height,r*std::sin(a)};
    const float fwd[3]={-std::sin(a),0.0f,std::cos(a)};          // counterclockwise (x to z)
    const float in[3]={-std::cos(a),0.0f,-std::sin(a)};          // to the centre
    const float up[3]={in[0]*std::sin(bank),std::cos(bank),in[2]*std::sin(bank)};
    // right = up x forward (x right, y up, z forward)
    const float right[3]={up[1]*fwd[2]-up[2]*fwd[1],up[2]*fwd[0]-up[0]*fwd[2],up[0]*fwd[1]-up[1]*fwd[0]};
    for(int c=0;c<3;++c){m[c]=right[c];m[4+c]=up[c];m[8+c]=fwd[c];m[12+c]=pos[c];}
    m[3]=m[7]=m[11]=0.0f;m[15]=1.0f;
}
void Pose(const Orbit& o,int frame,float* m) { PoseT(o,static_cast<float>(frame),m); }

struct Worst { float startOff=0.0f,inside=0.0f,bare=0.0f; };   // bare: inside the box itself, not grown

// A shot fired at frame `f` at the target, leaving `wait + lag` frames later from `muzzle ? the muzzle : the origin` as it
// was at `f` (the IFC's start point is written once, when the round is made).
void Shot(const Orbit& o,int f,int wait,int lag,bool muzzle,float hit,Worst* w) {
    const float target[3]={0.0f,0.0f,0.0f};
    float m[16],at[16];
    Pose(o,f,m);
    float start[3]={m[12],m[13],m[14]};
    if(muzzle)gunmuzzle::Muzzle(m,gunmuzzle::kGunship,target,hit+gunmuzzle::kMargin,start);
    Pose(o,f+wait+lag,at);
    float now[3];
    gunmuzzle::Muzzle(at,gunmuzzle::kGunship,target,hit+gunmuzzle::kMargin,now);   // where the muzzle is when it leaves
    const float d[3]={start[0]-now[0],start[1]-now[1],start[2]-now[2]};
    const float off=std::sqrt(gunmuzzle::Dot(d,d));
    const float in=gunmuzzle::InsideLength(at,gunmuzzle::kGunship,start,target,hit);
    if(off>w->startOff)w->startOff=off;
    const float bare=gunmuzzle::InsideLength(at,gunmuzzle::kGunship,start,target,0.0f);
    if(in>w->inside)w->inside=in;
    if(bare>w->bare)w->bare=bare;
}

void Scenario(const Orbit& o,const char* gun,int stockWait,float hit) {
    float r;
    const float bank=Bank(o,&r);
    const float look=std::atan(o.height/r);
    const float frame=o.speed/60.0f;
    {   // where the line to the target leaves the plane, in its own axes (x aside, y up, z forward): before and now
        float m[16],muzzle[3],local[3],target[3]={0.0f,0.0f,0.0f},dir[3];
        Pose(o,0,m);
        gunmuzzle::Muzzle(m,gunmuzzle::kGunship,target,hit+gunmuzzle::kMargin,muzzle);
        gunmuzzle::ToLocal(m,muzzle,local);
        float t[3];
        gunmuzzle::ToLocal(m,target,t);
        const float l=std::sqrt(gunmuzzle::Dot(t,t));
        for(int i=0;i<3;++i)dir[i]=t[i]/l;
        std::printf("%-22s %-6s line to the target from the origin, plane axes (%.3f,%.3f,%.3f); airframe x +-%.2f y %.2f..%.2f z +-%.2f; "
                    "muzzle now (%.2f,%.2f,%.2f)\n",o.name,gun,dir[0],dir[1],dir[2],gunmuzzle::kGunship.half[0],
                    gunmuzzle::kGunship.centre[1]-gunmuzzle::kGunship.half[1],gunmuzzle::kGunship.centre[1]+gunmuzzle::kGunship.half[1],
                    gunmuzzle::kGunship.half[2],local[0],local[1],local[2]);
    }
    for(int lag=0;lag<=1;++lag) {
        Worst before,now;
        for(int f=0;f<600;f+=7) {   // ten seconds of the orbit, a shot every 7 frames
            Shot(o,f,stockWait,lag,false,hit,&before);
            Shot(o,f,0,lag,true,hit,&now);
        }
        std::printf("%-22s %-6s bank %4.1f look-down %4.1f deg, r %4.0f m, %5.1f m/frame, lag %d: before start %6.1f m off the muzzle, "
                    "%5.1f m of line in the airframe (%5.1f m in its bare box) | now %4.2f m off, %4.2f m in\n",o.name,gun,bank*180.0f/kPi,look*180.0f/kPi,r,frame,lag,
                    before.startOff,before.inside,before.bare,now.startOff,now.inside);
        Expect(now.inside==0.0f,"now: the line to the target crosses the airframe",now.inside);
        Expect(now.startOff<=frame*static_cast<float>(lag)+0.05f,"now: the round leaves more than its lag's flight from the muzzle",
               now.startOff,frame*static_cast<float>(lag));
        if(lag==0 && stockWait==0)Expect(before.bare>1.0f,"before: the scenario no longer shows the line through the airframe",before.bare);
        if(lag==0 && stockWait>0)Expect(before.startOff>frame*static_cast<float>(stockWait)*0.9f,
                                        "before: the scenario no longer shows the shell's stale start",before.startOff);
    }
}

// The muzzle's own geometry: where the line from the box's centre leaves the box grown by `clear`.
void GeometryChecks() {
    const float m[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 10.0f,350.0f,-20.0f,1};   // level, facing +z
    const float below[3]={m[12],m[13]-500.0f,m[14]};
    float out[3];
    Expect(gunmuzzle::Muzzle(m,gunmuzzle::kGunship,below,2.0f,out),"straight down: a muzzle");
    const float bottom=m[13]+gunmuzzle::kGunship.centre[1]-gunmuzzle::kGunship.half[1];
    Expect(std::fabs(out[1]-(bottom-2.0f))<1e-3f,"straight down: 2 m under the belly",out[1],bottom-2.0f);
    Expect(std::fabs(out[0]-m[12])<1e-3f && std::fabs(out[2]-m[14])<1e-3f,"straight down: under the centre");
    const float near[3]={m[12],m[13]+2.0f,m[14]+3.0f};
    Expect(!gunmuzzle::Muzzle(m,gunmuzzle::kGunship,near,2.0f,out),"an aim inside the box: no muzzle");
    // A player can aim at nearby terrain during low flight: no minimum range is imposed by CameraRay.
    // A target outside the bare model can still be inside the shell's 10 m collision clearance. The firing
    // callers must preserve false here and decline the shot, rather than spawning at the returned centre.
    float low[16];
    for(int i=0;i<16;++i)low[i]=m[i];
    low[13]=5.0f;
    const float ground[3]={low[12],0.0f,low[14]};
    Expect(!gunmuzzle::Muzzle(low,gunmuzzle::kGunship,ground,gunmuzzle::kShellHit+gunmuzzle::kMargin,out),
           "low flight: do not fire a shell at terrain inside its collision clearance");
    Expect(gunmuzzle::Muzzle(low,gunmuzzle::kGunship,ground,gunmuzzle::kCannonHit+gunmuzzle::kMargin,out),
           "low flight: cannon can still fire when its smaller hit sphere clears the belly");
    low[13]=1.0f;
    Expect(!gunmuzzle::Muzzle(low,gunmuzzle::kGunship,ground,gunmuzzle::kCannonHit+gunmuzzle::kMargin,out),
           "near terrain: do not fire cannon from the airframe centre");
    const float side[3]={m[12]-1000.0f,m[13]+2.136f,m[14]};   // level out of the left wingtip
    Expect(gunmuzzle::Muzzle(m,gunmuzzle::kGunship,side,1.0f,out) && std::fabs(out[0]-(m[12]-25.938f-1.0f))<1e-2f,
           "level to the side: off the wingtip",out[0],m[12]-26.938f);
    const float a[3]={m[12]-30.0f,m[13]+1.0f,m[14]},b[3]={m[12]+30.0f,m[13]+1.0f,m[14]};
    Expect(std::fabs(gunmuzzle::InsideLength(m,gunmuzzle::kGunship,a,b,0.0f)-51.876f)<1e-2f,"a line across the wings: its span inside");
}
}  // namespace

// The gatling's spread (gunmuzzle.h Scatter): every round within the cone (its radius at most half the width at the aim's
// distance) and off the aim (but for the pattern's first point, near the centre), the pattern's mean on the aim, the same
// shot count landing the same, a spread of 0 or no line leaving the aim as it is.
void ScatterChecks() {
    const float from[3]={300.0f,350.0f,-200.0f},at[3]={-500.0f,0.0f,400.0f};
    const float d[3]={at[0]-from[0],at[1]-from[1],at[2]-from[2]};
    const float len=std::sqrt(gunmuzzle::Dot(d,d)),spread=0.004f,most=len*spread*0.5f;
    float sum[3]={},out[3],again[3];
    for(int n=0;n<gunmuzzle::kScatterRing;++n) {
        gunmuzzle::Scatter(from,at,spread,n,out);
        const float off[3]={out[0]-at[0],out[1]-at[1],out[2]-at[2]};
        const float r=std::sqrt(gunmuzzle::Dot(off,off));
        Expect(r<=most*1.0001f,"scatter: within the cone",r,most);
        Expect(r>=most*0.17f,"scatter: off the aim",r,most);   // the innermost point: sqrt(0.5 / 16) of the radius
        Expect(std::fabs(gunmuzzle::Dot(off,d))<=1e-3f*len*most,"scatter: square to the line",gunmuzzle::Dot(off,d),0.0);
        for(int c=0;c<3;++c)sum[c]+=off[c];
        gunmuzzle::Scatter(from,at,spread,n+gunmuzzle::kScatterRing,again);
        Expect(std::fabs(again[0]-out[0])+std::fabs(again[1]-out[1])+std::fabs(again[2]-out[2])<1e-3f,"scatter: the pattern repeats");
    }
    const float mean=std::sqrt(gunmuzzle::Dot(sum,sum))/static_cast<float>(gunmuzzle::kScatterRing);
    Expect(mean<=most*0.15f,"scatter: the pattern's mean on the aim",mean,most);
    gunmuzzle::Scatter(from,at,0.0f,5,out);
    Expect(out[0]==at[0] && out[1]==at[1] && out[2]==at[2],"scatter: none with no spread");
    gunmuzzle::Scatter(at,at,spread,5,out);
    Expect(out[0]==at[0] && out[1]==at[1] && out[2]==at[2],"scatter: none with no line");
    const float down[3]={at[0],at[1]+800.0f,at[2]};   // straight down: the other side axis
    gunmuzzle::Scatter(down,at,spread,3,out);
    Expect(std::isfinite(out[0]+out[1]+out[2]) && out[1]==at[1],"scatter: straight down stays on the ground plane",out[1],at[1]);
}

// The round's path against the airframe as both fly (gunmuzzle.h Launch / Clears; the user, 2026-10-10: 「炮舰机的机炮有可能
// 会打在自己身上，导致没打出去」). The checks above hold the airframe still while the round flies; here the gunship flies on
// (its true path: the orbit itself, or a straight run turning at a constant rate) while the round leaves at once or a frame
// late, and the round's hit sphere is followed against the airframe's box 16 steps a frame. For every shot:
//  - now (Launch, then Clears holds it or it is fired): a fired round never touches its airframe; a round held is counted
//    (the gun did not fire: it must stay rare, or the gunship stops shooting);
//  - before (the 2026-10-06 muzzle: the line from a still airframe): how much of its path the airframe met;
//  - the gate (Clears) against the truth, on both starts: it must never call clear a path the truth finds inside.
namespace {
using gunmuzzle::Round;
using gunmuzzle::Motion;

// The gunship's true flight: an orbit, or a straight run from `m0` at `mo` (a constant turn: a pull-up, a roll).
struct Path {
    const Orbit* orbit;
    float m0[16];
    Motion mo;
    void At(float t,float* m) const {
        if(orbit)PoseT(*orbit,t,m);
        else gunmuzzle::PoseAt(m0,mo,t,m);
    }
    // Its motion at frame `t` as the plugin hands it to Launch / Clears (jet_bay.cpp MotionOf: the entry's velocity and spin).
    Motion MotionAt(float t) const {
        if(!orbit)return mo;
        float a[16],b[16];
        PoseT(*orbit,t-0.5f,a);PoseT(*orbit,t+0.5f,b);
        float r;
        Bank(*orbit,&r);
        Motion m{{b[12]-a[12],b[13]-a[13],b[14]-a[14]},{0.0f,-orbit->speed/r/60.0f,0.0f}};   // counterclockwise: x to z
        return m;
    }
};

// How much of the round's path (from `start` at `aim`, leaving `lag` frames after frame `f`) runs inside the airframe grown
// by `grow`, the airframe on its true path.
float TrueInside(const Path& p,float f,int lag,const Round& r,const float* start,const float* aim,float grow) {
    float v[3];
    gunmuzzle::LaunchVelocity(start,aim,r,v);
    const float d[3]={aim[0]-start[0],aim[1]-start[1],aim[2]-start[2]};
    const float flight=gunmuzzle::Len(d)/r.speed;
    const float until=flight<40.0f ? flight : 40.0f;
    float m[16],prev[3],now[3];
    p.At(f+static_cast<float>(lag),m);
    gunmuzzle::ToLocal(m,start,prev);
    float inside=0.0f;
    for(int i=1;;++i) {
        float t=static_cast<float>(i)/16.0f;
        if(t>until)t=until;
        const float at[3]={start[0]+v[0]*t,start[1]+v[1]*t-0.5f*r.fall*t*t,start[2]+v[2]*t};
        p.At(f+static_cast<float>(lag)+t,m);
        gunmuzzle::ToLocal(m,at,now);
        inside+=gunmuzzle::SegmentInside(gunmuzzle::kGunship,prev,now,grow);
        for(int c=0;c<3;++c)prev[c]=now[c];
        if(!(t<until))break;
    }
    return inside;
}

struct Tally { int shots=0,held=0,noMuzzle=0,touched=0,gateMissed=0,oldTouched=0; float oldWorst=0.0f,newWorst=0.0f; };

// A shot at frame `f` of path `p` at `aim` with round `r`, now and before.
void Shoot(const Path& p,float f,const Round& r,const float* aim,Tally* t) {
    float m[16];
    p.At(f,m);
    const Motion mo=p.MotionAt(f);
    ++t->shots;
    float start[3];
    if(!gunmuzzle::Launch(m,mo,gunmuzzle::kGunship,r,aim,r.hit+gunmuzzle::kMargin,start))++t->noMuzzle;
    else {
        const bool clear=gunmuzzle::Clears(m,mo,gunmuzzle::kGunship,r,start,aim);
        float worst=0.0f;
        for(int lag=0;lag<=gunmuzzle::kLeaveLag;++lag) {
            const float in=TrueInside(p,f,lag,r,start,aim,r.hit);
            if(in>worst)worst=in;
        }
        if(!clear)++t->held;
        else if(worst>0.0f){++t->touched;if(worst>t->newWorst)t->newWorst=worst;}
    }
    float old[3];
    if(gunmuzzle::Muzzle(m,gunmuzzle::kGunship,aim,r.hit+gunmuzzle::kMargin,old)) {
        float worst=0.0f;
        for(int lag=0;lag<=gunmuzzle::kLeaveLag;++lag) {
            const float in=TrueInside(p,f,lag,r,old,aim,r.hit);
            if(in>worst)worst=in;
        }
        if(worst>0.0f){++t->oldTouched;if(worst>t->oldWorst)t->oldWorst=worst;}
        // The gate on the old start: a path the truth finds inside by more than the linearised motion can miss (the orbit's
        // curve over the few frames the round is near) must be held.
        if(worst>0.05f && gunmuzzle::Clears(m,mo,gunmuzzle::kGunship,r,old,aim))++t->gateMissed;
    }
}

void Report(const char* name,const char* gun,const Tally& t) {
    std::printf("%-34s %-7s %4d shots: now %3d held, %d no muzzle, %d fired into the airframe (worst %.2f m) | before %3d into "
                "the airframe (worst %5.2f m) | gate missed %d\n",name,gun,t.shots,t.held,t.noMuzzle,t.touched,t.newWorst,t.oldTouched,
                t.oldWorst,t.gateMissed);
    Expect(t.touched==0,"now: a fired round touches its own airframe",t.touched,t.newWorst);
    Expect(t.gateMissed==0,"the gate calls clear a path that meets the airframe",t.gateMissed);
}

struct Gun { const char* name; Round round; };
const Gun kGuns[]={{"cannon",gunmuzzle::kCannon},{"gatling",gunmuzzle::kGatling},{"shell",gunmuzzle::kShell}};

// The orbits, the target at the circle's centre (the NPC's) and on the far side, outside the turn (`outside`: the other
// side of the airframe from the one the 2026-10-06 checks fire through).
Tally Orbiting(const Orbit& o,const Gun& g,bool outside) {
    Path p{&o,{},{}};
    Tally t;
    for(int f=0;f<600;f+=7) {
        float m[16];
        PoseT(o,static_cast<float>(f),m);
        const float k=outside ? 1.6f : 0.0f;   // 0.6 of the radius past the circle, under the high wing
        const float aim[3]={m[12]*k,0.0f,m[14]*k};
        Shoot(p,static_cast<float>(f),g.round,aim,&t);
        if(g.round.speed==gunmuzzle::kGatling.speed)   // the gatling's scattered rounds about it too
            for(int n=0;n<gunmuzzle::kScatterRing;n+=5){float s[3];gunmuzzle::Scatter(m+12,aim,0.004f,n,s);Shoot(p,static_cast<float>(f),g.round,s,&t);}
    }
    return t;
}

// A straight run at `speed` m/s, level along +z at 350 m, turning at `spin` (rad/s, world) while it fires at targets ahead
// and below (`ahead`: +1 ahead, -1 behind) from 300 to 1500 m out and 3 to 60 degrees down.
Tally Running(float speed,const float* spin,float ahead,const Gun& g) {
    Path p{nullptr,{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,350.0f,0,1},{{0.0f,0.0f,speed/60.0f},{spin[0]/60.0f,spin[1]/60.0f,spin[2]/60.0f}}};
    Tally t;
    const float dists[]={300.0f,600.0f,1000.0f,1500.0f},downs[]={3.0f,8.0f,15.0f,25.0f,40.0f,60.0f};
    for(float d:dists)for(float down:downs)for(int f=0;f<30;f+=3) {
        const float a=down*kPi/180.0f;
        const float aim[3]={d*0.02f,350.0f-d*std::sin(a),ahead*d*std::cos(a)};
        Shoot(p,static_cast<float>(f),g.round,aim,&t);
    }
    return t;
}

// The ballistic solve (LaunchVelocity, 0x2312A0) lands an arcing round on its aim, and Launch / Clears hold for it: none of
// the gunship's rounds falls today (all three #6 0), this keeps the path model right for one that would.
void ArcChecks() {
    const Round arc{6.0f,0.05f,2.0f};
    const float from[3]={10.0f,350.0f,-20.0f},aim[3]={-400.0f,0.0f,300.0f};
    float v[3];
    gunmuzzle::LaunchVelocity(from,aim,arc,v);
    const float d[3]={aim[0]-from[0],aim[1]-from[1],aim[2]-from[2]};
    const float tt=gunmuzzle::Len(d)/arc.speed;
    const float at[3]={from[0]+v[0]*tt,from[1]+v[1]*tt-0.5f*arc.fall*tt*tt,from[2]+v[2]*tt};
    const float miss[3]={at[0]-aim[0],at[1]-aim[1],at[2]-aim[2]};
    Expect(gunmuzzle::Len(miss)<0.05f,"arc: the solve lands on the aim",gunmuzzle::Len(miss));
    const float straight[3]={d[0]/gunmuzzle::Len(d)*arc.speed,d[1]/gunmuzzle::Len(d)*arc.speed,d[2]/gunmuzzle::Len(d)*arc.speed};
    Expect(v[1]>straight[1]+1.0f,"arc: it leaves above the line",v[1],straight[1]);
    const Orbit o{"arc NPC orbit",120.0f,600.0f,350.0f,std::acos(1.0f/2.0f)};
    Path p{&o,{},{}};
    Tally t;
    for(int f=0;f<600;f+=7){const float c[3]={0.0f,0.0f,0.0f};Shoot(p,static_cast<float>(f),arc,c,&t);}
    Report("arcing round, NPC orbit",">arc",t);
    float m[16];
    p.At(0.0f,m);
    const float up[3]={m[12],m[13]+600.0f,m[14]};
    float start[3];
    Expect(gunmuzzle::Launch(m,p.MotionAt(0.0f),gunmuzzle::kGunship,arc,up,arc.hit+gunmuzzle::kMargin,start),"arc: a muzzle for an aim overhead");
}

// The motion handed to Launch is the orbit's own: a frame on, PoseAt from it lands where the orbit is.
void MotionChecks() {
    const Orbit o{"NPC gunship",120.0f,600.0f,350.0f,std::acos(1.0f/2.0f)};
    Path p{&o,{},{}};
    float m[16],next[16],want[16];
    p.At(100.0f,m);
    gunmuzzle::PoseAt(m,p.MotionAt(100.0f),1.0f,next);
    p.At(101.0f,want);
    float worst=0.0f;
    for(int i=0;i<15;++i)if(std::fabs(next[i]-want[i])>worst)worst=std::fabs(next[i]-want[i]);
    // A straight frame against the circle's arc: the chord's sag, (2 m)^2 / (2 x 848 m) = 2.4 mm at the NPC's speed.
    Expect(worst<5e-3f,"motion: a frame on along the orbit",worst);
}

// A still airframe and a path straight away from it: Launch is the plain muzzle, Clears passes; a start inside the box
// grown by the hit radius: Clears holds it.
void GateChecks() {
    const float m[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,350.0f,0,1};
    const Motion still{};
    const float below[3]={0.0f,0.0f,0.0f};
    float a[3],b[3];
    Expect(gunmuzzle::Launch(m,still,gunmuzzle::kGunship,gunmuzzle::kCannon,below,2.1f,a) &&
           gunmuzzle::Muzzle(m,gunmuzzle::kGunship,below,2.1f,b) && std::fabs(a[0]-b[0])+std::fabs(a[1]-b[1])+std::fabs(a[2]-b[2])<1e-3f,
           "gate: still, Launch is the muzzle");
    Expect(gunmuzzle::Clears(m,still,gunmuzzle::kGunship,gunmuzzle::kCannon,a,below),"gate: still, straight down clears");
    const float inside[3]={0.0f,350.0f+0.5f,0.0f};
    Expect(!gunmuzzle::Clears(m,still,gunmuzzle::kGunship,gunmuzzle::kCannon,inside,below),"gate: a start inside is held");
    // Flying on at 145 m/s with a round leaving the nose for a target ahead: from the still muzzle a late round starts
    // inside (the airframe has moved 2.4 m on), from Launch it does not.
    const Motion run{{0.0f,0.0f,145.0f/60.0f},{0.0f,0.0f,0.0f}};
    const float ahead[3]={0.0f,350.0f-30.0f,800.0f};
    Expect(gunmuzzle::Muzzle(m,gunmuzzle::kGunship,ahead,gunmuzzle::kCannonHit+gunmuzzle::kMargin,b) &&
           !gunmuzzle::Clears(m,run,gunmuzzle::kGunship,gunmuzzle::kCannon,b,ahead),"gate: the still muzzle, flying on: held");
    Expect(gunmuzzle::Launch(m,run,gunmuzzle::kGunship,gunmuzzle::kCannon,ahead,gunmuzzle::kCannonHit+gunmuzzle::kMargin,a) &&
           gunmuzzle::Clears(m,run,gunmuzzle::kGunship,gunmuzzle::kCannon,a,ahead),"gate: Launch, flying on: clear");
}

void FlightChecks(const Orbit* orbits,int count) {
    MotionChecks();
    GateChecks();
    ArcChecks();
    for(int i=0;i<count;++i)for(const Gun& g:kGuns) {
        const Tally in=Orbiting(orbits[i],g,false),out=Orbiting(orbits[i],g,true);
        char name[64];
        std::snprintf(name,sizeof name,"%s, target centre",orbits[i].name);
        Report(name,g.name,in);
        std::snprintf(name,sizeof name,"%s, target outside",orbits[i].name);
        Report(name,g.name,out);
        Expect(in.held==0,"orbit: a round at the circle's centre held",in.held);
    }
    struct Run { const char* name; float speed,spin[3],ahead; };
    const Run runs[]={
        {"run 145 m/s, ahead",145.0f,{0.0f,0.0f,0.0f},1.0f},
        {"run 74 m/s, ahead",74.0f,{0.0f,0.0f,0.0f},1.0f},
        {"run 145 m/s, pulling up 0.6 rad/s",145.0f,{-0.6f,0.0f,0.0f},1.0f},
        {"run 145 m/s, rolling 1.2 rad/s",145.0f,{0.0f,0.0f,1.2f},1.0f},
        {"run 145 m/s, turning 0.3 rad/s",145.0f,{0.0f,0.3f,0.0f},1.0f},
        {"run 145 m/s, behind",145.0f,{0.0f,0.0f,0.0f},-1.0f},
    };
    for(const Run& r:runs)for(const Gun& g:kGuns) {
        const Tally t=Running(r.speed,r.spin,r.ahead,g);
        Report(r.name,g.name,t);
        if(r.ahead>0.0f && r.spin[0]==0.0f && r.spin[1]==0.0f && r.spin[2]==0.0f) {
            // The bug the 2026-10-06 muzzle left: firing ahead, a late round starts where the nose was, inside the airframe.
            Expect(t.oldTouched>0,"before: the run no longer shows the late round in the nose",t.oldTouched);
            Expect(t.held*10<=t.shots,"run ahead: more than a tenth of the rounds held",t.held,t.shots);
        }
    }
}
}  // namespace

int main() {
    GeometryChecks();
    ScatterChecks();
    const float playerBank=1.2f;   // playerjet.cpp kTurnBank
    const Orbit orbits[]={
        {"NPC gunship",120.0f,600.0f,350.0f,std::acos(1.0f/2.0f)},   // kKinds gunship: 2 g -> 60 degrees
        {"player 74 m/s r400",74.0f,400.0f,350.0f,playerBank},
        {"player 120 m/s r600",120.0f,600.0f,300.0f,playerBank},
        {"player 145 m/s r900",145.0f,900.0f,500.0f,playerBank},
    };
    for(const Orbit& o:orbits) {
        Scenario(o,"cannon",0,gunmuzzle::kCannonHit);
        Scenario(o,"gatling",0,gunmuzzle::kGatlingHit);
        Scenario(o,"shell",kShellWait,gunmuzzle::kShellHit);
    }
    FlightChecks(orbits,static_cast<int>(sizeof(orbits)/sizeof(orbits[0])));
    std::printf(failures ? "gunship_muzzle_check: %d FAILED\n" : "gunship_muzzle_check: all passed\n",failures);
    return failures ? 1 : 0;
}
