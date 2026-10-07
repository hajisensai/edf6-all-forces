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

void Pose(const Orbit& o,int frame,float* m) {
    float r;
    const float bank=Bank(o,&r);
    const float a=o.speed/r*static_cast<float>(frame)/60.0f;   // rad round the circle
    const float pos[3]={r*std::cos(a),o.height,r*std::sin(a)};
    const float fwd[3]={-std::sin(a),0.0f,std::cos(a)};          // counterclockwise (x to z)
    const float in[3]={-std::cos(a),0.0f,-std::sin(a)};          // to the centre
    const float up[3]={in[0]*std::sin(bank),std::cos(bank),in[2]*std::sin(bank)};
    // right = up x forward (x right, y up, z forward)
    const float right[3]={up[1]*fwd[2]-up[2]*fwd[1],up[2]*fwd[0]-up[0]*fwd[2],up[0]*fwd[1]-up[1]*fwd[0]};
    for(int c=0;c<3;++c){m[c]=right[c];m[4+c]=up[c];m[8+c]=fwd[c];m[12+c]=pos[c];}
    m[3]=m[7]=m[11]=0.0f;m[15]=1.0f;
}

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
    std::printf(failures ? "gunship_muzzle_check: %d FAILED\n" : "gunship_muzzle_check: all passed\n",failures);
    return failures ? 1 : 0;
}
