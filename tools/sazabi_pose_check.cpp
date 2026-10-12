// The Sazabi's animation (src/sazabi_anim.h, as src/sazabi.cpp runs it) offline, on the joints of its model: each
// scenario a few seconds of input, the animator stepped at 60 Hz, kFrames frames of it checked (and dumped).
//   sazabi_pose_check                         the checks below on the built-in rig (exit 1 on a failure)
//   sazabi_pose_check --joints F --dump OUT   the same scenarios on the joints in F ("name x y z" lines, sz_root's
//                                             frame: tools/sazabi_pose_view.py writes it from the model folder), every
//                                             frame's bone transforms into OUT for tools/sazabi_pose_view.py to draw
// Checks: every scenario's every bone finite; the rifle (+z of sz_rifle) along the aim within 2 deg (and its kick)
// whenever the animator lets it fire (RifleReady); the rifle rigid in the right hand or on its rack; the tomahawk lit in
// the fist or dark on the shield at its bind offset, and in the fist through every swing's strike; the shield's face onto
// the aim when the animator lets its missiles go (ShieldReady); a flying funnel where it flies, nose along its course, a
// docked one in its pack; each ankle where its step puts it; the soles never more than kSink under the floor.
// Built on request only: cmake --build build --target sazabi_pose_check && build\sazabi_pose_check.exe
#define _CRT_SECURE_NO_WARNINGS
#include "../src/sazabi_anim.h"
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
constexpr float kHz=60.0f,kRun=26.0f;

// The tomahawk's gameplay timer (sazabi_arms.inc Tomahawk) for a combo of `swings` swings begun at `start` s: the
// swing's u and which swing at `t`; -1 before and after.
void Combo(float t,float start,int swings,PoseInput* i) {
    float at=start;
    for(int c=0;c<swings;++c) {
        const float len=SwingSec(c);
        if(t>=at && t<at+len){i->swing=(t-at)/len;i->combo=c;return;}
        at+=len;
    }
    i->swing=-1.0f;i->combo=t<start ? 0 : swings-1;
}
struct Scenario { const char* name; float secs; PoseInput (*at)(float t); };
PoseInput Driven(float t) { PoseInput i; i.t=t; i.aim=1.0f; return i; }
PoseInput Stand(float t) { PoseInput i; i.t=t; i.aim=0.0f; return i; }
PoseInput Idle(float t) { return Driven(t); }
PoseInput Walk(float t) { PoseInput i=Driven(t); i.move[1]=12.0f; return i; }
PoseInput Run(float t) { PoseInput i=Driven(t); i.move[1]=26.0f; return i; }
PoseInput Strafe(float t) { PoseInput i=Driven(t); i.move[0]=-12.0f; return i; }   // to its right
PoseInput Back(float t) { PoseInput i=Driven(t); i.move[1]=-10.0f; return i; }
PoseInput Diagonal(float t) { PoseInput i=Driven(t); i.move[0]=8.0f; i.move[1]=14.0f; return i; }
PoseInput Turn(float t) { PoseInput i=Driven(t); i.yawRate=t<2.0f ? 1.6f : 0.0f; return i; }
PoseInput Stop(float t) { PoseInput i=Driven(t); i.move[1]=t<1.2f ? 20.0f : 0.0f; return i; }
PoseInput Fly(float t) { PoseInput i=Driven(t); i.air=1.0f; i.lean=Clamp(t*15.0f,0.0f,28.0f)*kDeg; i.move[1]=40.0f; return i; }
PoseInput Land(float t) { PoseInput i=Driven(t); i.crouch=t<0.2f ? 0.0f : std::fmax(0.0f,1.0f-(t-0.2f)*2.2f); return i; }
PoseInput Aim(float t) {
    PoseInput i=Driven(t);
    const float u=Clamp((t-0.5f)/2.5f,0.0f,1.0f);
    i.aimYaw=(-50.0f+100.0f*u)*kDeg; i.aimPitch=(30.0f-60.0f*u)*kDeg; return i;
}
PoseInput AimWalk(float t) { PoseInput i=Walk(t); i.aimYaw=40.0f*kDeg; i.aimPitch=10.0f*kDeg; return i; }
PoseInput Guard(float t) { PoseInput i=Driven(t); i.guard=t>0.4f && t<2.4f ? 1.0f : 0.0f; return i; }
PoseInput Present(float t) { PoseInput i=Driven(t); i.present=t>0.4f && t<1.8f; i.aimYaw=20.0f*kDeg; i.aimPitch=15.0f*kDeg; return i; }
PoseInput Swing(float t) { PoseInput i=Driven(t); Combo(t,0.3f,1,&i); return i; }
PoseInput Melee(float t) { PoseInput i=Driven(t); Combo(t,0.3f,3,&i); return i; }
PoseInput MeleeFire(float t) { PoseInput i=Melee(t); i.fire=t>2.2f; return i; }
PoseInput MeleeWalk(float t) { PoseInput i=Melee(t); i.move[1]=10.0f; return i; }
// two combos, the tomahawk put away between them (its last swing left the gameplay's combo at 2): the second draws again
constexpr float kRechain=4.6f;
PoseInput Rechain(float t) { PoseInput i=Driven(t); Combo(t,0.3f,3,&i); if(t>=kRechain)Combo(t,kRechain,2,&i); return i; }
PoseInput Slow(float t) { PoseInput i=Driven(t); i.move[1]=3.0f; return i; }
// the shield up, three hits stopped on it (each a jolt back through the arm)
PoseInput Block(float t) { PoseInput i=Guard(t); i.blocks=(t>1.0f)+(t>1.15f)+(t>1.6f); return i; }
// the animator (re)started on a mech whose shield had already stopped hits (a count carried in PoseInput): no jolt
PoseInput BlockedBefore(float t) { PoseInput i=Guard(t); i.blocks=4; return i; }
PoseInput Switch(float t) { PoseInput i=Driven(t); i.special=t<0.3f ? 0 : t<1.3f ? 1 : t<2.3f ? 2 : 0; return i; }
PoseInput Boost(float t) { PoseInput i=Driven(t); i.air=1.0f; i.boost=t>0.3f ? 1.0f : 0.0f; i.lean=20.0f*kDeg; i.move[1]=60.0f; return i; }
PoseInput Recoil(float t) {
    PoseInput i=Driven(t); i.fire=true;
    const float s=std::fmod(t,0.5f);
    i.recoil=s<0.06f ? s/0.06f : s<0.41f ? Smooth(1.0f-(s-0.06f)/0.35f) : 0.0f; return i;
}
// the funnels launched one by one, each flying a ring 30 m ahead at its chest's height, nose to the ring's centre
PoseInput Funnels(float t) {
    PoseInput i=Driven(t);
    const float u=t/3.0f;
    for(int k=0;k<6;++k) {
        i.funnelOut[k]=u*6.0f>static_cast<float>(k);
        const float a=u*2.0f*kPi+static_cast<float>(k)*kPi/3.0f;
        i.funnelAt[k][0]=8.0f*std::cos(a);i.funnelAt[k][1]=20.0f+8.0f*std::sin(a);i.funnelAt[k][2]=30.0f;
        i.funnelDir[k][0]=-std::cos(a);i.funnelDir[k][1]=-std::sin(a);i.funnelDir[k][2]=0.0f;
    }
    return i;
}
PoseInput Cannon(float t) { PoseInput i=Driven(t); i.cannon=Clamp(t-0.3f,0.0f,1.0f); i.crouch=0.3f*i.cannon; return i; }
constexpr Scenario kScenarios[]={
    {"stand",3.0f,Stand},{"idle",6.0f,Idle},{"walk",3.0f,Walk},{"run",3.0f,Run},{"strafe",3.0f,Strafe},{"back",3.0f,Back},
    {"diagonal",3.0f,Diagonal},{"turn",3.0f,Turn},{"stop",3.0f,Stop},{"fly",2.0f,Fly},{"land",1.5f,Land},{"aim",3.0f,Aim},
    {"aimwalk",3.0f,AimWalk},{"guard",3.0f,Guard},{"present",2.4f,Present},{"swing",3.0f,Swing},{"melee",4.0f,Melee},
    {"meleefire",3.2f,MeleeFire},{"meleewalk",4.0f,MeleeWalk},{"switch",3.2f,Switch},{"funnels",3.0f,Funnels},
    {"cannon",2.0f,Cannon},{"boost",2.0f,Boost},{"recoil",2.0f,Recoil},{"rechain",6.0f,Rechain},{"slow",3.0f,Slow},{"block",3.0f,Block},
    {"blockedbefore",1.0f,BlockedBefore},
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

void CheckArms(const Scenario& s,int f,const PoseInput& in,const Rig& rig,const Anim& a,const Pose& p) {
    if(RifleReady(a) && a.raise>0.995f && a.ready>0.995f && a.brace<0.005f) {   // what fires, along the aim
        const V3 want=AimDir(in,0.0f),got=Row(p.modelRot[kRifle],2);
        if(VDot(want,got)<std::cos((2.0f+kRecoilMuzzle*in.recoil)*kDeg))Fail(s.name,f,"the rifle is off the aim (its kick aside)");
    }
    if(p.rifleInHand) {
        if(Dist(p.modelPos[kRifle],p.modelPos[kHandR])>0.01f)Fail(s.name,f,"the rifle in hand is not at the hand");
    } else if(Dist(p.modelPos[kRifle],p.modelPos[kPelvis])>8.0f)Fail(s.name,f,"the racked rifle is not on the hip");
    if(p.axeInHand) {
        if(p.scale[kAxeBlade]!=1.0f)Fail(s.name,f,"the tomahawk in hand is dark");
        const V3 fist=Of(p.modelPos[kHandR])+Times(Of(kFist),p.modelRot[kHandR]);
        const V3 up=Times(AxeUp(rig),p.modelRot[kAxe]);
        if(VLen(Of(p.modelPos[kAxe])+up*kAxeGrip-fist)>0.05f)Fail(s.name,f,"the tomahawk is not in the fist");
    } else {
        if(p.scale[kAxeBlade]!=0.0f)Fail(s.name,f,"the stowed tomahawk's blade is lit");
        const float bindOff=Dist(rig.joint[kAxe],rig.joint[kShield]);
        if(std::fabs(Dist(p.modelPos[kAxe],p.modelPos[kShield])-bindOff)>0.01f)Fail(s.name,f,"the stowed tomahawk left the shield");
    }
    if(in.swing>=WindEnd(in.combo) && in.swing<=StrikeEnd(in.combo) && !p.axeInHand)
        Fail(s.name,f,"striking without the tomahawk in hand");
    if(ShieldReady(a) && a.present>0.995f) {   // the shield's face (its missiles') onto the aim
        const V3 face=Times(ShieldNormal(rig),p.modelRot[kForearmL]);
        if(VDot(face,AimDir(in,0.0f))<std::cos(25.0f*kDeg))Fail(s.name,f,"the shield is not turned onto the aim");
    }
}

void Check(const Scenario& s,int f,const PoseInput& in,const Rig& rig,const Anim& a,const Pose& p) {
    for(int b=0;b<kBoneCount;++b)
        for(int k=0;k<9;++k)
            if(!std::isfinite(p.modelRot[b].m[k]) || (k<3 && !std::isfinite(p.modelPos[b][k]))){Fail(s.name,f,"not finite");return;}
    CheckArms(s,f,in,rig,a,p);
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
            const float* at=p.modelPos[foot[k]];
            const float ex=at[0]-(p.ankleStand[k][0]+p.footDx[k]),ez=at[2]-(p.ankleStand[k][2]+p.footDz[k]);
            dy[k]=at[1]-(p.ankleStand[k][1]+p.footLift[k]);
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

// Every step: a planted foot stays where it is on the ground (in sz_root's frame it goes back as fast as the mech goes
// on: kSlip of that, at most), walking steadily; a combo after the tomahawk was put away draws it (the rifle in hand at
// its start).
constexpr float kSlip=0.2f;
void CheckStep(const Scenario& s,float t,float dt,const PoseInput& in,const Anim& a,const Pose& p,const float was[2][3],
               const bool wasPlanted[2]) {
    const float speed=std::sqrt(in.move[0]*in.move[0]+in.move[1]*in.move[1]);
    if(in.air==0.0f && speed>1.0f && t>1.5f && dt>0.0f && in.swing<0.0f)
        for(int k=0;k<2;++k) {
            if(!a.planted[k] || !wasPlanted[k])continue;
            const float* at=p.modelPos[k==0 ? kFootL : kFootR];
            const float vx=(at[0]-was[k][0])/dt+in.move[0],vz=(at[2]-was[k][2])/dt+in.move[1];
            if(std::sqrt(vx*vx+vz*vz)>kSlip*speed+0.5f){
                std::printf("  t %.2f foot %d slides %.1f m/s at %.1f m/s\n",t,k,std::sqrt(vx*vx+vz*vz),speed);
                Fail(s.name,static_cast<int>(t*60.0f),"a planted foot slides");
            }
        }
    // a block jolts the shield (its kick sprung out), hits counted before the animator started do not
    if(std::strcmp(s.name,"block")==0 && t>1.03f && t<1.08f && !(a.kick>0.02f))
        Fail(s.name,static_cast<int>(t*60.0f),"a stopped hit did not jolt the shield");
    if(std::strcmp(s.name,"blockedbefore")==0 && a.kick!=0.0f)
        Fail(s.name,static_cast<int>(t*60.0f),"hits counted before the animator started jolted the shield");
    if(std::strcmp(s.name,"rechain")==0 && t>=kRechain && t<kRechain+0.04f && !p.rifleInHand)
        Fail(s.name,static_cast<int>(t*60.0f),"a combo after the put-away did not draw again (the rifle not in hand)");
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
    {
        Anim a;
        for(int k=0;k<120;++k)Animate(Stand(0.0f),rig,kRun,1.0f/kHz,a,&p);
        standAnkle[0]=p.modelPos[kFootL][1];standAnkle[1]=p.modelPos[kFootR][1];
    }
    for(const Scenario& s:kScenarios) {
        Anim a;
        const int steps=static_cast<int>(s.secs*kHz+0.5f);
        int f=0;
        float was[2][3]{};
        bool wasPlanted[2]{};
        for(int k=0;k<=steps;++k) {
            const PoseInput in=s.at(static_cast<float>(k)/kHz);
            const float dt=k==0 ? 0.0f : 1.0f/kHz;
            Animate(in,rig,kRun,dt,a,&p);
            CheckStep(s,static_cast<float>(k)/kHz,dt,in,a,p,was,wasPlanted);
            for(int side=0;side<2;++side) {
                std::memcpy(was[side],p.modelPos[side==0 ? kFootL : kFootR],sizeof was[side]);
                wasPlanted[side]=a.planted[side];
            }
            if(k*(kFrames-1)<f*steps)continue;   // frame f at step f*steps/(kFrames-1)
            Check(s,f,in,rig,a,p);
            if(out) {
                std::fprintf(out,"frame %s %d\n",s.name,f);
                for(int b=0;b<kBoneCount;++b) {
                    std::fprintf(out,"%ls",kBones[b].name);
                    for(float v:p.modelRot[b].m)std::fprintf(out," %.5f",v);
                    std::fprintf(out," %.4f %.4f %.4f %.2f\n",p.modelPos[b][0],p.modelPos[b][1],p.modelPos[b][2],p.scale[b]);
                }
            }
            ++f;
        }
    }
    if(out)std::fclose(out);
    std::printf(failures ? "sazabi_pose_check: %d failures\n" : "sazabi_pose_check: all scenarios pass\n",failures);
    return failures ? 1 : 0;
}
