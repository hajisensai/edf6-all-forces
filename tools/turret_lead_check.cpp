// The player's turret aim math (autoturret/src/aimmath.h), checked without the game:
//  - LeadSolve: a round fired along the direction it gives (the lead circle's), stepped as the game steps it (a frame:
//    v += drop along the vehicle's down, p += v; autoturret/docs/re-notes.md "Rounds in flight"), against a target
//    moving at a constant velocity, passes within kHit of the target. The KG6 flak (8 m/frame, gravity factor 1) at an
//    air target crossing, closing and climbing, the Titan's side cannon (4 m/frame, gravity factor 2) at a ground target
//    driving, from a level vehicle and one tilted on a slope (the drop is along its own down, as the aim models it).
//    Out of reach: false.
//  - NextPick: the nearest first, then each next one out, round to the nearest after the last; none: -1.
//  - OffView: the angle off the view and the distance.
// Exit code 1 on a failure. Built on request only:
// cmake --build build --target turret_lead_check && build\turret_lead_check.exe
#include "../autoturret/src/aimmath.h"
#include <cmath>
#include <cstdio>

namespace {
using namespace autoturret;
constexpr float kGravity=14.7f/3600.0f;   // m/frame^2 per unit AmmoGravityFactor (re-notes "Gravity and the ballistic solve")
constexpr float kHit=0.1f;                // m: the solve's own error (the flak's proximity fuse is 5 m)
constexpr float kDeg=0.0174532925f;
int failures=0;

void Check(bool ok,const char* what) {
    std::printf("%s  %s\n",ok ? "ok  " : "FAIL",what);
    if(!ok)++failures;
}

// The vehicle's rows (right, up, forward, position) for a heading, a pitch (nose up +) and a roll.
void Rows(float yaw,float pitch,float roll,float* m) {
    const float cy=std::cos(yaw),sy=std::sin(yaw),cp=std::cos(pitch),sp=std::sin(pitch),cr=std::cos(roll),sr=std::sin(roll);
    const float f[3]={sy*cp,sp,cy*cp};                       // forward
    const float r0[3]={cy,0.0f,-sy};                          // level right
    const float u0[3]={-sy*sp,cp,-cy*sp};                     // up, pitched
    for(int i=0;i<3;++i){m[i]=r0[i]*cr-u0[i]*sr;m[4+i]=u0[i]*cr+r0[i]*sr;m[8+i]=f[i];}
    m[3]=m[7]=m[11]=0.0f;m[12]=m[13]=m[14]=0.0f;m[15]=1.0f;
}

// The closest the round comes to the target over `frames` + 10 frames.
float Miss(const float* m,const float* muzzle,const float* dir,float speed,float drop,const float* target,const float* vel,float frames) {
    float p[3]={muzzle[0],muzzle[1],muzzle[2]},v[3],t[3]={target[0],target[1],target[2]};
    for(int i=0;i<3;++i)v[i]=dir[i]*speed;
    float best=1e9f;
    const int n=static_cast<int>(frames)+10;
    for(int k=1;k<=n;++k) {
        for(int i=0;i<3;++i){v[i]-=m[4+i]*drop;p[i]+=v[i];t[i]+=vel[i];}
        // Both move in a straight line through the frame (the game sweeps the round's segment): the closest the two
        // come within it, from their separation at its start `a` and its change over the frame `b`.
        float a[3],b[3];
        for(int i=0;i<3;++i){b[i]=v[i]-vel[i];a[i]=(p[i]-t[i])-b[i];}
        const float bb=aim::Dot3(b,b),f=bb>1e-9f ? std::fmin(1.0f,std::fmax(0.0f,-aim::Dot3(a,b)/bb)) : 0.0f;
        const float c[3]={a[0]+b[0]*f,a[1]+b[1]*f,a[2]+b[2]*f};
        best=std::fmin(best,std::sqrt(aim::Dot3(c,c)));
    }
    return best;
}

void Lead(const char* name,float yaw,float pitch,float roll,float speed,float gravityFactor,const float* target,const float* vel) {
    float m[16];
    Rows(yaw,pitch,roll,m);
    const float muzzle[3]={m[4]*2.5f,m[5]*2.5f,m[6]*2.5f};
    const float drop=gravityFactor*kGravity;
    float aimAt[3],dir[3],frames;
    const bool ok=aim::LeadSolve(m,muzzle,target,vel,speed,drop,aimAt,dir,&frames);
    const float miss=ok ? Miss(m,muzzle,dir,speed,drop,target,vel,frames) : 1e9f;
    char what[256];
    std::snprintf(what,sizeof(what),"%s: flight %.0f frames, miss %.2f m",name,frames,miss);
    Check(ok && miss<kHit,what);
}
}  // namespace

int main() {
    // The flak (KG6: AmmoSpeed 8 m/frame, gravity factor 1) at air targets 300..450 m out.
    const float crossing[3]={250.0f,120.0f,200.0f},crossVel[3]={-1.2f,0.0f,0.4f};       // ~75 m/s across
    Lead("flak, crossing air target, level",0.0f,0.0f,0.0f,8.0f,1.0f,crossing,crossVel);
    const float closing[3]={0.0f,150.0f,420.0f},closeVel[3]={0.1f,-0.3f,-1.5f};
    Lead("flak, closing diving air target, level",0.3f,0.0f,0.0f,8.0f,1.0f,closing,closeVel);
    const float climbing[3]={-200.0f,60.0f,180.0f},climbVel[3]={0.6f,0.9f,0.0f};
    Lead("flak, climbing target, on a slope",0.7f,8.0f*kDeg,-6.0f*kDeg,8.0f,1.0f,climbing,climbVel);
    // The Titan's side cannon (subCannon: 4 m/frame, gravity factor 2) at a ground target driving 10 m/s.
    const float ground[3]={60.0f,-2.0f,240.0f},groundVel[3]={0.17f,0.0f,0.0f};
    Lead("titan side cannon, ground target, level",0.0f,0.0f,0.0f,4.0f,2.0f,ground,groundVel);
    Lead("titan side cannon, ground target, tilted",-0.4f,-5.0f*kDeg,4.0f*kDeg,4.0f,2.0f,ground,groundVel);
    // A still target: the direction is the plain arc's.
    const float still[3]={0.0f,0.0f,300.0f},none[3]={0.0f,0.0f,0.0f};
    Lead("flak, still target",0.0f,0.0f,0.0f,8.0f,1.0f,still,none);
    {
        float m[16];Rows(0.0f,0.0f,0.0f,m);
        const float muzzle[3]={0.0f,0.0f,0.0f},distant[3]={0.0f,0.0f,5000.0f};
        float a[3],d[3],n;
        Check(!aim::LeadSolve(m,muzzle,distant,none,4.0f,2.0f*kGravity,a,d,&n),"out of reach: no solution");
    }
    Check(aim::NextPick(3,-1)==0 && aim::NextPick(3,0)==1 && aim::NextPick(3,1)==2 && aim::NextPick(3,2)==0,
          "NextPick: the nearest, then each next one out, round");
    Check(aim::NextPick(0,-1)==-1 && aim::NextPick(1,0)==0,"NextPick: none in sight, the only one");
    {
        const float eye[3]={0.0f,0.0f,0.0f},dir[3]={0.0f,0.0f,1.0f},p[3]={100.0f,0.0f,100.0f};
        float distance;
        const float a=aim::OffView(eye,dir,p,&distance);
        Check(std::fabs(a-45.0f*kDeg)<1e-4f && std::fabs(distance-141.421356f)<1e-3f,"OffView: 45 deg, 141.4 m");
    }
    std::printf("%s\n",failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
