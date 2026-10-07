// The Sazabi's poses (src/sazabi_pose.h, as src/sazabi.cpp runs it) offline, on the joints of its model.
//   sazabi_pose_check                         the checks below on the built-in rig (exit 1 on a failure)
//   sazabi_pose_check --joints F --dump OUT   the same scenarios on the joints in F ("name x y z" lines, sz_root's
//                                             frame: tools/sazabi_pose_view.py writes it from the model folder), every
//                                             frame's bone transforms into OUT for tools/sazabi_pose_view.py to draw
// Checks: every scenario's every bone finite; the rifle (+z of sz_rifle) along the aim within 2 deg whenever the arm is
// fully raised; the drawn tomahawk's grip within 1.5 m of the right hand and its blade lit, the stowed one dark and
// within its bind offset of the shield; a flying funnel where it flies, nose along its course, a docked one in its pack; the soles never more than kSink under the floor walking.
// Built on request only: cmake --build build --target sazabi_pose_check && build\sazabi_pose_check.exe
#define _CRT_SECURE_NO_WARNINGS
#include "../src/sazabi_pose.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace {
using namespace sazabi;
// The model folder's joints when this was written (pylib/sazabi_model.py skeleton(), sz_ bones in kBones order).
constexpr float kBuiltIn[kBoneCount][3]={
    {0.000f,0.000f,0.000f},{0.000f,12.920f,-0.430f},{0.000f,14.638f,-0.430f},{0.000f,16.571f,-0.430f},
    {0.000f,20.436f,0.644f},{0.000f,16.900f,3.300f},{0.000f,19.363f,-2.148f},
    {2.362f,22.369f,-3.221f},{2.948f,24.892f,-4.975f},{2.539f,24.152f,-4.059f},{2.134f,23.419f,-3.151f},
    {2.577f,19.363f,-3.007f},
    {-2.362f,22.369f,-3.221f},{-4.319f,24.681f,-4.185f},{-3.681f,23.971f,-3.383f},{-3.049f,23.269f,-2.588f},
    {-2.577f,19.363f,-3.007f},
    {3.651f,20.651f,-0.430f},{3.651f,20.436f,-0.430f},{4.725f,18.289f,0.000f},{6.443f,16.785f,2.148f},
    {5.584f,17.537f,1.074f},{6.248f,19.885f,-2.280f},
    {-3.651f,20.651f,-0.430f},{-3.651f,20.436f,-0.430f},{-4.617f,18.718f,0.430f},{-6.228f,14.638f,1.718f},
    {-6.228f,14.638f,1.718f},{-6.228f,14.888f,12.318f},
    {2.577f,12.061f,-0.430f},{5.584f,9.913f,0.859f},{7.087f,4.329f,-1.289f},
    {-2.577f,12.061f,-0.430f},{-5.584f,9.913f,0.859f},{-7.087f,4.329f,-1.289f},
    {7.897f,16.350f,2.770f},{4.609f,19.228f,-1.341f},
};
constexpr float kAnkleMiss=0.15f;   // m: the IK's miss at most
constexpr float kSink=1.5f;   // m: a sole this far under the floor at most (the gait's bob and the crouch's drop)
constexpr int kFrames=24;

struct Scenario { const char* name; PoseInput (*at)(float u); };   // u 0..1 through the scenario

PoseInput Stand(float u) { PoseInput i; i.t=u*4.0f; i.aim=0.0f; return i; }
PoseInput Walk(float u) { PoseInput i; i.t=u; i.gait=u*2.0f*kPi; i.stride=0.55f; i.aim=0.0f; return i; }
PoseInput Run(float u) { PoseInput i; i.t=u; i.gait=u*2.0f*kPi; i.stride=1.0f; i.aim=0.0f; i.lean=4.0f*kDeg; return i; }
PoseInput Fly(float u) { PoseInput i; i.t=u; i.air=1.0f; i.lean=(10.0f+20.0f*u)*kDeg; i.aim=0.0f; return i; }
PoseInput Land(float u) { PoseInput i; i.t=u; i.crouch=u<0.3f ? u/0.3f : 1.0f-(u-0.3f)/0.7f; i.aim=0.0f; return i; }
PoseInput Aim(float u) {
    PoseInput i; i.t=u; i.aim=1.0f;
    i.aimYaw=(-50.0f+100.0f*u)*kDeg; i.aimPitch=(30.0f-60.0f*u)*kDeg; return i;
}
PoseInput AimWalk(float u) { PoseInput i=Aim(0.5f); i.gait=u*2.0f*kPi; i.stride=0.6f; return i; }
PoseInput Guard(float u) { PoseInput i; i.t=u; i.guard=u<0.5f ? u*2.0f : 1.0f; i.aim=1.0f; return i; }
PoseInput Swing(float u) { PoseInput i; i.t=u; i.swing=u; i.aim=0.0f; return i; }
PoseInput Slash(float u) { PoseInput i=Swing(u); i.combo=1; return i; }
PoseInput Rise(float u) { PoseInput i=Swing(u); i.combo=2; return i; }
PoseInput Boost(float u) { PoseInput i; i.t=u; i.air=1.0f; i.boost=u<0.3f ? u/0.3f : 1.0f; i.lean=20.0f*kDeg; i.aim=1.0f; return i; }
PoseInput Recoil(float u) { PoseInput i=Aim(0.5f); const float s=u*0.6f; i.recoil=s<0.06f ? s/0.06f : s<0.41f ? Smooth(1.0f-(s-0.06f)/0.35f) : 0.0f; return i; }
// the funnels launched one by one, each flying a ring 30 m ahead at its chest's height, nose to the ring's centre
PoseInput Funnels(float u) {
    PoseInput i; i.t=u;
    for(int k=0;k<6;++k) {
        i.funnelOut[k]=u*6.0f>static_cast<float>(k);
        const float a=u*2.0f*kPi+static_cast<float>(k)*kPi/3.0f;
        i.funnelAt[k][0]=8.0f*std::cos(a);i.funnelAt[k][1]=20.0f+8.0f*std::sin(a);i.funnelAt[k][2]=30.0f;
        i.funnelDir[k][0]=-std::cos(a);i.funnelDir[k][1]=-std::sin(a);i.funnelDir[k][2]=0.0f;
    }
    return i;
}
PoseInput Cannon(float u) { PoseInput i; i.t=u; i.cannon=u; i.crouch=0.3f*u; i.aim=0.0f; return i; }
constexpr Scenario kScenarios[]={
    {"stand",Stand},{"walk",Walk},{"run",Run},{"fly",Fly},{"land",Land},{"aim",Aim},{"aimwalk",AimWalk},
    {"guard",Guard},{"swing",Swing},{"slash",Slash},{"rise",Rise},{"funnels",Funnels},{"cannon",Cannon},
    {"boost",Boost},{"recoil",Recoil},
};

int failures=0;
float standAnkle[2]{};   // the ankles' height standing still (the stance: the soles on the floor)
void Fail(const char* scenario,int frame,const char* what) {
    std::printf("FAIL %s frame %d: %s\n",scenario,frame,what);
    ++failures;
}

float Dist(const float* a,const float* b) {
    return std::sqrt((a[0]-b[0])*(a[0]-b[0])+(a[1]-b[1])*(a[1]-b[1])+(a[2]-b[2])*(a[2]-b[2]));
}

void Check(const Scenario& s,int f,const PoseInput& in,const Rig& rig,const Pose& p) {
    for(int b=0;b<kBoneCount;++b)
        for(int k=0;k<9;++k)
            if(!std::isfinite(p.modelRot[b].m[k]) || (k<3 && !std::isfinite(p.modelPos[b][k]))){Fail(s.name,f,"not finite");return;}
    if(in.aim>=1.0f && in.swing<0.0f) {
        const float yaw=Clamp(in.aimYaw,-kMostAimYaw*kDeg,kMostAimYaw*kDeg),pitch=Clamp(in.aimPitch,-kMostAimPitch*kDeg,kMostAimPitch*kDeg);
        const float want[3]={std::sin(yaw)*std::cos(pitch),std::sin(pitch),std::cos(yaw)*std::cos(pitch)};
        const float* got=&p.modelRot[kRifle].m[6];
        const float dot=got[0]*want[0]+got[1]*want[1]+got[2]*want[2];
        if(dot<std::cos((2.0f+kRecoilMuzzle*in.recoil)*kDeg))Fail(s.name,f,"the rifle is off the aim (its kick aside)");
    }
    if(in.swing>=0.0f) {
        if(Dist(p.modelPos[kAxe],p.modelPos[kHandR])>1.5f)Fail(s.name,f,"the drawn tomahawk is not in the right hand");
        if(p.scale[kAxeBlade]!=1.0f || p.scale[kRifle]!=0.0f)Fail(s.name,f,"blade dark or rifle shown while swinging");
    } else {
        const float bindOff=Dist(rig.joint[kAxe],rig.joint[kShield]);
        if(std::fabs(Dist(p.modelPos[kAxe],p.modelPos[kShield])-bindOff)>0.01f)Fail(s.name,f,"the stowed tomahawk left the shield");
        if(p.scale[kAxeBlade]!=0.0f)Fail(s.name,f,"the stowed tomahawk's blade is lit");
    }
    for(int k=0;k<6;++k) {   // a flying funnel where it flies, its nose along its direction; a docked one in its pack
        const int b=kFunnels[k];
        if(p.scale[b]!=1.0f)Fail(s.name,f,"a funnel not drawn");
        if(!in.funnelOut[k]) {
            const int pack=kBones[b].parent;
            if(std::fabs(Dist(p.modelPos[b],p.modelPos[pack])-Dist(rig.joint[b],rig.joint[pack]))>0.01f)Fail(s.name,f,"a docked funnel left its pack");
            continue;
        }
        if(Dist(p.modelPos[b],in.funnelAt[k])>0.01f)Fail(s.name,f,"a flying funnel not where it flies");
        float nose[3];
        const float l=std::sqrt(kFunnelNose[0]*kFunnelNose[0]+kFunnelNose[1]*kFunnelNose[1]+kFunnelNose[2]*kFunnelNose[2]);
        const float bind[3]={kFunnelNose[0]/l,kFunnelNose[1]/l,kFunnelNose[2]/l};
        Apply(bind,p.modelRot[b],nose);
        const float* d=in.funnelDir[k];
        if(nose[0]*d[0]+nose[1]*d[1]+nose[2]*d[2]<std::cos(1.0f*kDeg))Fail(s.name,f,"a flying funnel's nose off its direction");
    }
    if(in.air==0.0f) {   // each ankle where its step puts it (Gait's IK); Plant moves both by the same height after
        const int foot[2]={kFootL,kFootR};
        float dy[2];
        for(int k=0;k<2;++k) {
            const float* a=p.modelPos[foot[k]];
            const float ex=a[0]-p.ankleStand[k][0],ez=a[2]-(p.ankleStand[k][2]+p.footDz[k]);
            dy[k]=a[1]-(p.ankleStand[k][1]+p.footLift[k]);
            if(std::sqrt(ex*ex+ez*ez)>kAnkleMiss){std::printf("  ankle %d off by (%.2f, %.2f)\n",k,ex,ez);Fail(s.name,f,"an ankle not where its step puts it");}
        }
        if(std::fabs(dy[0]-dy[1])>kAnkleMiss)Fail(s.name,f,"an ankle not at its step's height");
    }
    constexpr int kFeet[2]={kFootL,kFootR};
    for(int k=0;k<2;++k) {   // the soles stand on the floor in the stance: an ankle this far under its stance height sinks
        const float drop=standAnkle[k]-p.modelPos[kFeet[k]][1];
        if(in.air==0.0f && drop>kSink)Fail(s.name,f,"a sole sinks through the floor");
    }
}

bool LoadJoints(const char* path,Rig* rig) {
    std::FILE* h=std::fopen(path,"r");
    if(!h)return false;
    char name[64];
    float x,y,z;
    int seen=0;
    while(std::fscanf(h,"%63s %f %f %f",name,&x,&y,&z)==4) {
        wchar_t w[64];
        std::mbstowcs(w,name,64);
        for(int b=0;b<kBoneCount;++b)
            if(std::wcscmp(w,kBones[b].name)==0){rig->joint[b][0]=x;rig->joint[b][1]=y;rig->joint[b][2]=z;++seen;}
    }
    std::fclose(h);
    return seen==kBoneCount;
}
}  // namespace

int main(int argc,char** argv) {
    Rig rig{};
    std::memcpy(rig.joint,kBuiltIn,sizeof rig.joint);
    const char* dump=nullptr;
    for(int a=1;a+1<argc;++a) {
        if(std::strcmp(argv[a],"--joints")==0 && !LoadJoints(argv[a+1],&rig)){std::printf("cannot read every joint from %s\n",argv[a+1]);return 2;}
        if(std::strcmp(argv[a],"--dump")==0)dump=argv[a+1];
    }
    std::FILE* out=dump ? std::fopen(dump,"w") : nullptr;
    if(dump && !out){std::printf("cannot write %s\n",dump);return 2;}
    static Pose p;
    PoseInput still;
    still.aim=0.0f;
    Animate(still,rig,&p);
    standAnkle[0]=p.modelPos[kFootL][1];standAnkle[1]=p.modelPos[kFootR][1];
    for(const Scenario& s:kScenarios) {
        for(int f=0;f<kFrames;++f) {
            const PoseInput in=s.at(static_cast<float>(f)/(kFrames-1));
            Animate(in,rig,&p);
            Check(s,f,in,rig,p);
            if(!out)continue;
            std::fprintf(out,"frame %s %d\n",s.name,f);
            for(int b=0;b<kBoneCount;++b) {
                std::fprintf(out,"%ls",kBones[b].name);
                for(float v:p.modelRot[b].m)std::fprintf(out," %.5f",v);
                std::fprintf(out," %.4f %.4f %.4f %.2f\n",p.modelPos[b][0],p.modelPos[b][1],p.modelPos[b][2],p.scale[b]);
            }
        }
    }
    if(out)std::fclose(out);
    std::printf(failures ? "sazabi_pose_check: %d failures\n" : "sazabi_pose_check: all scenarios pass\n",failures);
    return failures ? 1 : 0;
}
