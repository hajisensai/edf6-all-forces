// The first person's arithmetic (src/fp_view.h), offline:
//  - the view transform with no hold and no shrink, the camera at the eye in the aim frame's own rotation: the identity
//    (the arms and weapon drawn where they are);
//  - a hold moves every pose by that much along the aim frame's own axes, whatever the frame's yaw and pitch;
//  - a shrink keeps the picture: a point and its shrunk copy lie on the same ray from the eye, at s times the distance;
//  - a collapse sends every point to its one place;
//  - the eye eased in the body frame: the first frame as it is, then a share of the way a frame; a turned body turns it;
//  - the weapon-anchored transform: the weapon's root lands at the grip in the camera's frame and points along the
//    camera's forward, whatever way the weapon was held (a tilted, scaled hand matrix as well);
//  - the motion matrix: none is the identity, the pivot stays where it is under any turn, a pitch up tips ahead up;
//  - the procedural motion (src/fp_motion.h): nothing happening rests at 0; a shot kicks back and up and settles; a reload
//    turns it on its side (not down: the grip is below the view's edge); a switch dips out of view and comes back; a sprint lowers it; a turn of the view leaves it behind.
// Exit code 1 when one fails. cmake --build build --target fp_view_check && build\fp_view_check.exe
#include "../src/fp_view.h"
#include "../src/fp_motion.h"
#include <cmath>
#include <cstdio>

namespace {
using namespace crew::fpview;
int failures=0;

void Expect(bool ok,const char* what,float a=0.0f,float b=0.0f) {
    std::printf("%s  %s (%.4f, %.4f)\n",ok ? "ok  " : "FAIL",what,a,b);
    if(!ok)++failures;
}

bool Near(float a,float b,float eps=1e-4f) { return std::fabs(a-b)<=eps; }

// The aim frame: pitch about x, then yaw about y, at `at` (the game's +0x60 layout: rows right, up, forward, position).
M4 Frame(float pitch,float yaw,const float* at) {
    const float cp=std::cos(pitch),sp=std::sin(pitch),cy=std::cos(yaw),sy=std::sin(yaw);
    M4 m=Identity();
    const float right[3]={cy,0.0f,-sy},up[3]={sy*sp,cp,cy*sp},fwd[3]={sy*cp,-sp,cy*cp};
    for(int i=0;i<3;++i){m.m[i]=right[i];m.m[4+i]=up[i];m.m[8+i]=fwd[i];m.m[12+i]=at[i];}
    return m;
}

// The camera: the aim frame's rotation at the point `eyeLocal` of it.
M4 CameraAt(const M4& aim,const float* eyeLocal) {
    M4 c=aim;
    Point(aim,eyeLocal,c.m+12);
    return c;
}

void Identity_() {
    const float at[3]={10.0f,2.0f,-30.0f},eye[3]={0.0f,1.6f,0.1f},hold[3]={0.0f,0.0f,0.0f};
    const M4 aim=Frame(0.3f,1.1f,at);
    M4 t;
    const bool ok=ViewTransform(aim,CameraAt(aim,eye),hold,1.0f,&t);
    float worst=0.0f;
    const M4 id=Identity();
    for(int i=0;i<16;++i)worst=std::fmax(worst,std::fabs(t.m[i]-id.m[i]));
    Expect(ok && worst<1e-4f,"no hold, no shrink: the identity",worst);
}

void Hold() {
    const float at[3]={-5.0f,0.0f,7.0f},eye[3]={0.0f,1.6f,0.1f},hold[3]={0.2f,-0.1f,0.3f};
    const M4 aim=Frame(-0.4f,2.5f,at);
    M4 t;
    ViewTransform(aim,CameraAt(aim,eye),hold,1.0f,&t);
    const float p[3]={1.0f,2.0f,3.0f};
    float q[3];
    Point(t,p,q);
    float want[3];
    for(int j=0;j<3;++j)want[j]=p[j]+hold[0]*aim.m[j]+hold[1]*aim.m[4+j]+hold[2]*aim.m[8+j];
    const float err=std::fabs(q[0]-want[0])+std::fabs(q[1]-want[1])+std::fabs(q[2]-want[2]);
    Expect(err<1e-4f,"a hold moves along the aim frame's axes",err);
}

void Shrink() {
    const float at[3]={0.0f,0.0f,0.0f},eye[3]={0.0f,1.6f,0.1f},hold[3]={0.0f,0.0f,0.0f};
    const M4 aim=Frame(0.2f,-0.7f,at);
    const M4 cam=CameraAt(aim,eye);
    M4 t;
    ViewTransform(aim,cam,hold,0.25f,&t);
    const float p[3]={0.3f,1.2f,0.6f};
    float q[3];
    Point(t,p,q);
    float dp[3],dq[3];
    for(int j=0;j<3;++j){dp[j]=p[j]-cam.m[12+j];dq[j]=q[j]-cam.m[12+j];}
    const float lp=std::sqrt(dp[0]*dp[0]+dp[1]*dp[1]+dp[2]*dp[2]),lq=std::sqrt(dq[0]*dq[0]+dq[1]*dq[1]+dq[2]*dq[2]);
    const float cosine=(dp[0]*dq[0]+dp[1]*dq[1]+dp[2]*dq[2])/(lp*lq);
    Expect(Near(cosine,1.0f,1e-5f),"a shrunk point is on the same ray from the eye",cosine);
    Expect(Near(lq,lp*0.25f),"...at a quarter of the distance",lq,lp*0.25f);
}

void Singular() {
    M4 zero{},t;
    const float hold[3]={0.0f,0.0f,0.0f};
    Expect(!ViewTransform(zero,Identity(),hold,1.0f,&t),"a singular aim frame: nothing moved");
}

void Collapse_() {
    const float at[3]={4.0f,5.0f,6.0f},p[3]={-9.0f,8.0f,0.5f};
    float q[3];
    Point(Collapse(at),p,q);
    Expect(Near(q[0],4.0f) && Near(q[1],5.0f) && Near(q[2],6.0f),"a collapse sends every point to its place",q[0],q[2]);
}

void Eye() {
    const float at[3]={100.0f,0.0f,50.0f};
    const M4 body=Frame(0.0f,1.5707963f,at);   // turned 90 degrees: its forward is world +x
    const float head[3]={100.3f,1.6f,50.0f};   // 0.3 m ahead of the body, 1.6 m up
    float local[3]={0.0f,0.0f,0.0f};
    EaseEye(body,head,0.05f,0.1f,0.25f,true,local);
    Expect(Near(local[1],1.65f) && Near(local[2],0.4f),"the first frame: the head's place as it is, pushed up and ahead",local[1],local[2]);
    const float lower[3]={100.3f,1.2f,50.0f};
    EaseEye(body,lower,0.05f,0.1f,0.25f,false,local);
    Expect(Near(local[1],1.55f),"then a quarter of the way to a lower head",local[1]);
    float w[3];
    EyeWorld(body,local,w);
    Expect(Near(w[0],100.4f) && Near(w[2],50.0f),"the eye back in the world, ahead along the turned body",w[0],w[2]);
}

void Anchored() {
    const float at[3]={3.0f,1.1f,-2.0f};
    M4 weapon=Frame(0.6f,-1.2f,at);   // held tilted, turned away
    for(int i=0;i<3;++i){weapon.m[i]*=1.3f;weapon.m[4+i]*=1.3f;weapon.m[8+i]*=1.3f;}   // and scaled
    const float camAt[3]={10.0f,5.0f,7.0f},eye[3]={0.0f,0.0f,0.0f},grip[3]={0.15f,-0.16f,0.28f};
    const M4 cam=CameraAt(Frame(-0.2f,0.4f,camAt),eye);
    M4 t;
    const bool ok=AnchoredTransform(weapon,cam,grip,1.0f,&t);
    float q[3],want[3];
    Point(t,at,q);
    for(int j=0;j<3;++j)want[j]=cam.m[12+j]+grip[0]*cam.m[j]+grip[1]*cam.m[4+j]+grip[2]*cam.m[8+j];
    const float err=std::fabs(q[0]-want[0])+std::fabs(q[1]-want[1])+std::fabs(q[2]-want[2]);
    Expect(ok && err<1e-4f,"anchored: the weapon's root at the grip",err);
    float tip[3],tq[3];
    for(int j=0;j<3;++j)tip[j]=at[j]+weapon.m[8+j];
    Point(t,tip,tq);
    float d[3];
    for(int j=0;j<3;++j)d[j]=tq[j]-q[j];
    const float l=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
    const float cosine=(d[0]*cam.m[8]+d[1]*cam.m[9]+d[2]*cam.m[10])/l;
    Expect(Near(cosine,1.0f,1e-5f),"...pointing along the camera's forward",cosine);
}

void Arms() {
    const float elbow[3]={-0.1f,-0.1f,0.3f},toward[3]={-0.2f,-0.3f,-0.1f};
    float shoulder[3],se[3],st[3];
    const bool ok=ReachShoulder(elbow,toward,0.30f,shoulder);
    Sub(shoulder,elbow,se);Sub(toward,elbow,st);
    Expect(ok && Near(Length(se),0.30f),"the shoulder at the upper arm's length from the elbow",Length(se));
    Unit(se);Unit(st);
    Expect(Near(Dot3(se,st),1.0f),"...toward the place asked",Dot3(se,st));
    Expect(!ReachShoulder(elbow,elbow,0.30f,shoulder),"no direction: no shoulder");
    // The forearm stays: the upper arm's map keeps the elbow where it is.
    const float s0[3]={0.2f,0.3f,0.1f};   // the stock shoulder: the upper arm is as long as it was
    float was[3];
    Sub(elbow,s0,was);
    M4 up;
    ReachShoulder(elbow,toward,Length(was),shoulder);
    float pe[3],ps[3];
    const bool uok=SegmentMap(s0,elbow,shoulder,elbow,&up);
    Point(up,elbow,pe);Point(up,s0,ps);
    Expect(uok && Near(pe[0],elbow[0]) && Near(pe[1],elbow[1]) && Near(pe[2],elbow[2]),"the elbow stays put",pe[0],pe[2]);
    Expect(Near(ps[0],shoulder[0]) && Near(ps[1],shoulder[1]) && Near(ps[2],shoulder[2]),"...the shoulder lands",ps[0],ps[1]);
    // The forearm's roll about the weapon's axis through the wrist: the elbow goes the way asked, the wrist stays.
    const float wrist[3]={0.1f,-0.2f,0.4f},axis[3]={0.0f,0.0f,1.0f},raised[3]={0.0f,0.0f,0.15f};   // elbow up, behind
    const float up0[3]={wrist[0]+raised[0],wrist[1]+0.2f,wrist[2]-0.15f},want[3]={-1.0f,-1.5f,0.0f};
    float way[3];
    Sub(up0,wrist,way);
    const M4 roll=TurnAbout(wrist,axis,RollAngle(way,want,axis));
    float e1[3],w1[3],got[3];
    Point(roll,up0,e1);Point(roll,wrist,w1);
    Sub(e1,wrist,got);
    float gotFlat[3]={got[0],got[1],0.0f},wantFlat[3]={want[0],want[1],0.0f};
    Unit(gotFlat);Unit(wantFlat);
    Expect(Near(Dot3(gotFlat,wantFlat),1.0f),"a roll takes the elbow out and down",Dot3(gotFlat,wantFlat));
    Expect(Near(e1[2],up0[2]) && Near(Length(got),Length(way)),"...about the weapon's axis, rigidly",e1[2],Length(got));
    Expect(Near(w1[0],wrist[0]) && Near(w1[1],wrist[1]) && Near(w1[2],wrist[2]),"...the wrist stays put",w1[0],w1[1]);
    Expect(RollAngle(want,want,axis)==0.0f,"already that way: no roll",RollAngle(want,want,axis));
    const float a0[3]={1,2,3},b0[3]={1,2,4},a1[3]={-5,0,1},b1[3]={-5,1,1};
    M4 m;
    const bool mok=SegmentMap(a0,b0,a1,b1,&m);
    float pa[3],pb[3];
    Point(m,a0,pa);Point(m,b0,pb);
    Expect(mok && Near(pa[0],-5) && Near(pa[1],0) && Near(pa[2],1) && Near(pb[0],-5) && Near(pb[1],1) && Near(pb[2],1),
           "a segment map takes a0 to a1 and b0 to b1",pb[1],pb[2]);
    const float c0[3]={1,3,3};   // a point beside the segment keeps its distance from it
    float pc[3],d[3];
    Point(m,c0,pc);Sub(pc,pa,d);
    Expect(Near(Length(d),1.0f),"...rigidly (no scale)",Length(d));
    const float b2[3]={1,2,2};   // turned half round
    Expect(SegmentMap(a0,b0,a0,b2,&m),"a half turn is a map too");
    Point(m,b0,pb);
    Expect(Near(pb[2],2.0f),"...and lands right",pb[2]);
}

void Motion() {
    const float pivot[3]={0.15f,-0.2f,0.35f};
    const M4 none=MotionMatrix(0,0,0,0,0,0,pivot);
    float worst=0.0f;
    const M4 id=Identity();
    for(int i=0;i<16;++i)worst=std::fmax(worst,std::fabs(none.m[i]-id.m[i]));
    Expect(worst<1e-6f,"no motion: the identity",worst);
    float q[3];
    Point(MotionMatrix(0,0,0,0.4f,-0.3f,0.7f,pivot),pivot,q);
    Expect(Near(q[0],pivot[0]) && Near(q[1],pivot[1]) && Near(q[2],pivot[2]),"the pivot stays put under a turn",q[0],q[2]);
    const float zero[3]={0,0,0},ahead[3]={0,0,1};
    Point(MotionMatrix(0,0,0,0.5f,0,0,zero),ahead,q);
    Expect(q[1]>0.4f,"a pitch up tips ahead up",q[1]);
}

using namespace crew::fpmotion;
Cue Quiet() { return Cue{0,0,0,true,0,0,0,false,false,Move::none}; }

Pose Run(State& s,const Cue& c,int frames) { Pose p{};for(int i=0;i<frames;++i)p=Advance(s,c);return p; }

void Springs() {
    State s{};
    Pose p=Run(s,Quiet(),60);
    Expect(p.x==0 && p.y==0 && p.z==0 && p.pitch==0 && p.yaw==0 && p.roll==0,"nothing happening: at rest",p.y,p.pitch);
    Cue shot=Quiet();shot.shots=1;shot.recoil=1.0f;
    p=Advance(s,shot);
    Pose q=Advance(s,Quiet());
    Expect(p.z<0 && p.pitch>0 && q.z<p.z,"a shot kicks back and up",p.z,p.pitch);
    p=Run(s,Quiet(),90);
    Expect(std::fabs(p.z)<1e-3f && std::fabs(p.pitch)<1e-3f,"...and settles",p.z,p.pitch);
    Cue reload=Quiet();reload.reloading=true;
    p=Run(s,reload,60);
    Expect(p.y>=0.0f && p.pitch>0.0f && p.roll>0.4f,"a reload turns it on its side without dropping it out of view",p.y,p.roll);
    s=State{};
    Cue sw=Quiet();sw.switched=true;
    Advance(s,sw);
    p=Run(s,Quiet(),10);
    Expect(p.y<-0.1f,"a switch dips it out of view",p.y);
    p=Run(s,Quiet(),90);
    Expect(std::fabs(p.y)<1e-3f,"...and brings it back",p.y);
    s=State{};
    Cue sprint=Quiet();sprint.move=Move::sprint;sprint.speed=0.3f;
    p=Run(s,sprint,60);
    Expect(p.pitch<-0.2f && p.yaw<-0.3f,"a sprint lowers it and turns it in",p.pitch,p.yaw);
    s=State{};
    Cue look=Quiet();look.lookYaw=0.05f;
    p=Advance(s,look);
    Expect(p.yaw<0,"turning right leaves it behind (to the left)",p.yaw);
    s=State{};
    Cue air=Quiet();air.grounded=false;air.speed=0.3f;
    p=Run(s,air,30);
    Expect(std::fabs(p.x)<1e-6f && std::fabs(p.y)<1e-6f,"no bob in the air",p.x,p.y);
}
}  // namespace

int main() {
    Identity_();
    Hold();
    Shrink();
    Singular();
    Collapse_();
    Eye();
    Anchored();
    Arms();
    Motion();
    Springs();
    std::printf(failures ? "%d FAILED\n" : "all passed\n",failures);
    return failures ? 1 : 0;
}
