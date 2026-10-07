// The Sazabi's aim assist (src/sazabi_assist.h) offline. Checks: Pick takes the enemy nearest the reticle within the
// cone and range, none outside; the held one is kept within the wider keep cone (no flicking between two side by side),
// of its several lock points the nearest the centre; Solve puts a point on the centre's ray of the riding camera's rig
// (sazabi_camera.inc ViewRay: the eye orbits behind the mech); Pull eases toward it without overshooting, capped, the
// short way across +-pi; flown at 60 Hz the pull settles a still and a crossing enemy on the reticle, and the player's
// full stick turn away still breaks off it.
//   sazabi_assist_check            the checks (exit 1 on a failure)
#include "../src/sazabi_assist.h"
#include <cmath>
#include <cstdio>
#include <limits>

namespace {
using namespace sazabi::assist;
constexpr float kPi=3.14159265f,kDeg=kPi/180.0f,kDt=1.0f/60.0f;
// the rig (sazabi_camera.inc): pivot 14.8 m over the soles, the eye 45 m back along the aim, 11 m right, 1 m up
constexpr float kUp=14.8f,kBack=45.0f,kSide=11.0f,kRise=1.0f;
constexpr float kCone=8.0f*kDeg,kKeep=kCone*1.6f,kRange=600.0f,kGain=4.0f,kMost=1.5f,kTurn=110.0f*kDeg;
int failures=0;

void Check(bool ok,const char* what,float got) {
    if(ok)return;
    std::printf("FAIL %s (%.4f)\n",what,got);
    ++failures;
}
void Ray(const float* origin,float yaw,float pitch,float* eye,float* dir) {
    const float cp=std::cos(pitch);
    dir[0]=std::sin(yaw)*cp;dir[1]=std::sin(pitch);dir[2]=std::cos(yaw)*cp;
    const float right[3]={-std::cos(yaw),0.0f,std::sin(yaw)};
    for(int k=0;k<3;++k)eye[k]=origin[k]+(k==1 ? kUp+kRise : 0.0f)-dir[k]*kBack+right[k]*kSide;
}
// How far off the centre's ray `at` is, seen from the eye (rad).
float OffCentre(const float* origin,float yaw,float pitch,const float* at) {
    float eye[3],dir[3],dist;
    Ray(origin,yaw,pitch,eye,dir);
    return Angle(dir,eye,at,&dist);
}
// A point `deg` right of the centre's ray (yaw), `dist` m from the eye, of a camera at (yaw 0, pitch 0) over the origin.
void Beside(const float* origin,float deg,float up,float dist,float* out) {
    float eye[3],dir[3];
    Ray(origin,0.0f,0.0f,eye,dir);
    const float a=-deg*kDeg;   // the camera's right is -x heading +z
    out[0]=eye[0]+std::sin(a)*dist;out[1]=eye[1]+up;out[2]=eye[2]+std::cos(a)*dist;
}

void Picks() {
    const float o[3]={0.0f,0.0f,0.0f};
    float eye[3],dir[3];
    Ray(o,0.0f,0.0f,eye,dir);
    int a=0,b=0,c=0;
    Point p[4];
    p[0].obj=&a;Beside(o,5.0f,0.0f,300.0f,p[0].at);    // 5 deg off
    p[1].obj=&b;Beside(o,2.0f,0.0f,400.0f,p[1].at);    // 2 deg off: the nearest the centre
    p[2].obj=&c;Beside(o,20.0f,0.0f,100.0f,p[2].at);   // outside the cone
    p[3].obj=&c;Beside(o,1.0f,0.0f,900.0f,p[3].at);    // past the range
    Check(Pick(eye,dir,p,4,kCone,kKeep,kRange,nullptr)==1,"picks the one nearest the reticle within the cone",0.0f);
    Check(Pick(eye,dir,p+2,2,kCone,kKeep,kRange,nullptr)==-1,"none outside the cone or past the range",0.0f);
    Check(Pick(eye,dir,p,4,kCone,kKeep,kRange,&a)==0,"keeps the held one though another is nearer the centre",0.0f);
    Point q[2];
    q[0].obj=&a;Beside(o,11.0f,0.0f,300.0f,q[0].at);   // the held one past the cone, within the keep cone
    q[1].obj=&b;Beside(o,6.0f,0.0f,300.0f,q[1].at);
    Check(Pick(eye,dir,q,2,kCone,kKeep,kRange,&a)==0,"keeps the held one within the keep cone",0.0f);
    Beside(o,14.0f,0.0f,300.0f,q[0].at);                // past the keep cone
    Check(Pick(eye,dir,q,2,kCone,kKeep,kRange,&a)==1,"lets the held one go past the keep cone",0.0f);
    Point g[3];   // a giant's three weak points: of the held one's, the nearest the centre
    g[0].obj=&a;Beside(o,7.0f,0.0f,200.0f,g[0].at);
    g[1].obj=&a;Beside(o,3.0f,0.0f,200.0f,g[1].at);
    g[2].obj=&b;Beside(o,0.5f,0.0f,200.0f,g[2].at);
    Check(Pick(eye,dir,g,3,kCone,kKeep,kRange,&a)==1,"of the held one's points the nearest the centre",0.0f);
}

void CrowdsAndCover() {
    const float o[3]={0,0,0};
    float eye[3],dir[3];Ray(o,0,0,eye,dir);
    int objects[40]{};
    Candidates<32> crowd;
    auto add=[&](int id,float angle,const void* held) {
        Point p{};p.obj=&objects[id];Beside(o,angle,0,300,p.at);
        crowd.Add(p,eye,dir,kCone,kKeep,kRange,held);
    };
    for(int i=0;i<32;++i)add(i,7.0f,nullptr);
    add(32,0.1f,nullptr);
    int pick=Pick(eye,dir,crowd.pts,crowd.n,kCone,kKeep,kRange,nullptr);
    Check(crowd.n==32 && pick>=0 && crowd.pts[pick].obj==&objects[32],
          "the reticle's nearest enemy survives a full registry buffer",0);
    crowd={};
    for(int i=0;i<32;++i)add(i,1.0f,&objects[33]);
    add(33,11.0f,&objects[33]);
    pick=Pick(eye,dir,crowd.pts,crowd.n,kCone,kKeep,kRange,&objects[33]);
    Check(pick>=0 && crowd.pts[pick].obj==&objects[33],"a late held enemy survives nearer unheld enemies",0);
    crowd={};
    for(int i=0;i<32;++i)add(i,11.0f,nullptr);
    Check(crowd.n==0,"unheld enemies in only the keep cone cannot fill the candidate buffer",0);
    add(34,2.0f,nullptr);
    const Point bad{&objects[35],{std::numeric_limits<float>::quiet_NaN(),0,0}};
    crowd.Add(bad,eye,dir,kCone,kKeep,kRange,nullptr);
    Check(crowd.n==1,"nonfinite registry points are rejected",0);
    Check(Visible(300,-1),"an unobstructed ray is visible",0);
    Check(Visible(300,300),"a map hit at the target itself is visible",0);
    Check(!Visible(300,299),"a target one metre behind a wall is hidden",0);
    Check(!Visible(300,297.1f),"a target within the former three metre grace is hidden",0);
    Check(!Visible(300,100),"a distant wall hides the target",0);
}

void Solves() {
    const float o[3]={10.0f,0.0f,-30.0f};
    const float targets[][3]={{60.0f,40.0f,200.0f},{-150.0f,5.0f,80.0f},{0.0f,120.0f,400.0f},{30.0f,-10.0f,60.0f}};
    for(const auto& t:targets) {
        float yaw,pitch;
        Solve(t,0.0f,0.0f,[&](float y,float p,float* eye){float d[3];Ray(o,y,p,eye,d);},&yaw,&pitch);
        const float off=OffCentre(o,yaw,pitch,t);
        Check(off<0.2f*kDeg,"Solve puts the point on the centre's ray",off/kDeg);
    }
}

void Pulls() {
    float now=0.0f;
    for(int i=0;i<600;++i)now+=Pull(now,0.5f,kGain,kMost,kDt);
    Check(std::fabs(now-0.5f)<1e-3f,"Pull settles on its want",now);
    float prev=0.0f;
    bool over=false,fast=false;
    now=0.0f;
    for(int i=0;i<120;++i){const float d=Pull(now,1.0f,50.0f,kMost,kDt);now+=d;over=over || now>1.0f+1e-5f;fast=fast || d>kMost*kDt+1e-6f;prev=d;}
    Check(!over,"Pull never overshoots",now);
    Check(!fast,"Pull keeps under its fastest",prev);
    const float d=Pull(3.0f,-3.0f,kGain,kMost,kDt);
    Check(d>0.0f,"Pull goes the short way across pi",d);
}

// Flown at 60 Hz: the mech at the origin, its camera eased onto an enemy by the pull alone (the stick at rest), or with
// the stick turning it away at full speed.
float Fly(const float* start,const float* vel,float secs,float stick,bool* held) {
    const float o[3]={0.0f,0.0f,0.0f};
    float yaw=0.0f,pitch=0.0f,at[3]={start[0],start[1],start[2]};
    int obj=0;
    const void* h=nullptr;
    *held=true;
    for(int i=0;i<static_cast<int>(secs/kDt);++i) {
        for(int k=0;k<3;++k)at[k]+=vel[k]*kDt;
        yaw+=stick*kDt;
        float eye[3],dir[3];
        Ray(o,yaw,pitch,eye,dir);
        Point p{&obj,{at[0],at[1],at[2]}};
        const int pick=Pick(eye,dir,&p,1,kCone,kKeep,kRange,h);
        h=pick>=0 ? &obj : nullptr;
        if(pick<0){*held=false;continue;}
        float wy,wp;
        Solve(at,yaw,pitch,[&](float y,float pp,float* e){float d[3];Ray(o,y,pp,e,d);},&wy,&wp);
        yaw=WrapPi(yaw+Pull(yaw,wy,kGain,kMost,kDt));
        pitch+=Pull(pitch,wp,kGain,kMost,kDt);
    }
    return OffCentre(o,yaw,pitch,at);
}

void Flights() {
    float start[3];
    const float o[3]={0.0f,0.0f,0.0f},still[3]={0.0f,0.0f,0.0f},cross[3]={-20.0f,0.0f,0.0f};
    bool held;
    Beside(o,6.0f,10.0f,250.0f,start);
    float off=Fly(start,still,1.5f,0.0f,&held);
    Check(held && off<0.3f*kDeg,"a still enemy 6 deg off is settled on the reticle in 1.5 s",off/kDeg);
    Beside(o,2.0f,0.0f,250.0f,start);
    off=Fly(start,cross,3.0f,0.0f,&held);   // crossing at 20 m/s, 250 m out (about 4.6 deg/s)
    Check(held && off<1.5f*kDeg,"a crossing enemy is kept on the reticle",off/kDeg);
    Beside(o,0.0f,0.0f,250.0f,start);
    Fly(start,still,1.0f,-kTurn,&held);      // the stick full the other way
    Check(!held,"the full stick turn breaks off the enemy within 1 s",0.0f);
}
}  // namespace

int main() {
    Picks();
    CrowdsAndCover();
    Solves();
    Pulls();
    Flights();
    if(failures){std::printf("sazabi_assist_check: %d failed\n",failures);return 1;}
    std::printf("sazabi_assist_check: all passed\n");
    return 0;
}
