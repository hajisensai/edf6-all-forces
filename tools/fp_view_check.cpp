// The first person's arithmetic (src/fp_view.h), offline:
//  - the view transform with no hold and no shrink, the camera at the eye in the aim frame's own rotation: the identity
//    (the arms and weapon drawn where they are);
//  - a hold moves every pose by that much along the aim frame's own axes, whatever the frame's yaw and pitch;
//  - a shrink keeps the picture: a point and its shrunk copy lie on the same ray from the eye, at s times the distance;
//  - a collapse sends every point to its one place;
//  - the eye eased in the body frame: the first frame as it is, then a share of the way a frame; a turned body turns it;
//  - the weapon-anchored transform: the weapon's root lands at the grip in the camera's frame and points along the
//    camera's forward, whatever way the weapon was held (a tilted, scaled hand matrix as well);
//  - the free hand held on the other hand's weapon: pulled in to the box's body when off it (never pushed out, kept
//    within its length), slid round its axis only within the arc of straight below (a place outside brought to the
//    edge), and a roll about the axis keeps the hand's distance from it;
//  - the motion matrix: none is the identity, the pivot stays where it is under any turn, a pitch up tips ahead up;
//  - the hold is the weapon's own: facing any way, walking or jumping (the third-person sway in the hand), the weapon
//    and the support hand held to it sit at the same place in the view (the size along the weapon's axes);
//  - the procedural motion (src/fp_motion.h): nothing happening rests at 0; a shot kicks back and up and settles; a reload
//    turns it on its side (not down: the grip is below the view's edge); a switch dips out of view and comes back; a sprint lowers it; a turn of the view leaves it behind.
// Exit code 1 when one fails. cmake --build build --target fp_view_check && build\fp_view_check.exe
#include "../src/fp_view.h"
#include "../src/cockpit_instruments.h"
#include "../src/fp_motion.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

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

void AuthoredActions() {
    const float origin[3]={10,2,-30},grip[3]={.2f,-.3f,.6f},rest[3]={.3f,1.1f,.5f};
    const auto aim=Frame(.4f,1.3f,origin),camera=Frame(.4f,1.3f,origin);
    bool follows=true,connected=true;
    for(int frame=0;frame<120;++frame) {
        const float phase=std::sin(frame*.08f);
        // Native spear anticipation/thrust and shield translation, sampled before muzzle events.
        const float action[3]={-.15f*phase,.12f*phase,.35f*phase};
        M4 weapon=aim;float local[3];for(int i=0;i<3;++i)local[i]=rest[i]+action[i];
        Point(aim,local,weapon.m+12);M4 transform,inverse;
        follows &= NativeGripTransform(weapon,aim,camera,grip,.6f,&transform,Identity(),rest);
        InverseAffine(camera,&inverse);float world[3],placed[3];Point(transform,weapon.m+12,world);Point(inverse,world,placed);
        for(int i=0;i<3;++i)follows &= Near(placed[i],.6f*(grip[i]+action[i]));
        const float shoulder[3]={-.3f,0,0},elbow[3]={-.2f,-.3f,.1f},wrist[3]={0,-.4f,.4f};
        const float target[3]={-.1f+phase*.1f,-.3f,.5f},want[3]={-.3f,-.2f,0},pole[3]={-1,-1.5f,0};
        M4 upper,fore;connected &= ArmChainTo(shoulder,elbow,wrist,target,want,pole,&upper,&fore);
        float e1[3],e2[3],tip[3],s1[3];Point(upper,elbow,e1);Point(fore,elbow,e2);Point(fore,wrist,tip);Point(upper,shoulder,s1);
        for(int i=0;i<3;++i)connected &= Near(e1[i],e2[i]) && Near(tip[i],target[i]);
        float original[3],moved[3];Sub(elbow,shoulder,original);Sub(e1,s1,moved);connected &= Near(Length(original),Length(moved));
        Sub(wrist,elbow,original);Sub(tip,e2,moved);connected &= Near(Length(original),Length(moved));
    }
    Expect(follows,"authored anticipation, thrust and guard translations survive viewmodel placement");
    Expect(connected,"moving support wrist retains a connected elbow and both limb lengths throughout strafing");
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
    Expect(Near(local[0],0.0f) && Near(local[1],1.65f) && Near(local[2],0.1f),
           "the first frame: on the body's axis at the head's height pushed up, ahead of the axis (not of the head)",local[1],local[2]);
    const float lower[3]={100.3f,1.2f,50.0f};
    EaseEye(body,lower,0.05f,0.1f,0.25f,false,local);
    Expect(Near(local[1],1.55f),"then a quarter of the way to a lower head",local[1]);
    const float swayed[3]={100.7f,1.2f,50.4f};   // the head thrown forward and to the side (a dash, a stomp)
    float before[3]={local[0],local[1],local[2]};
    EaseEye(body,swayed,0.05f,0.1f,0.25f,false,local);
    Expect(Near(local[0],before[0]) && Near(local[2],before[2]),"the head's sway forward and sideways does not move the eye",local[0],local[2]);
    float w[3];
    EyeWorld(body,local,w);
    Expect(Near(w[0],100.1f) && Near(w[2],50.0f),"the eye back in the world, ahead along the turned body",w[0],w[2]);
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

void Held() {
    M4 box=Identity();   // a Mag Blaster's: 0.03 x 0.12 x 0.50 m half sizes, centred at the origin, pointing +z
    box.m[0]=0.03f;box.m[5]=0.12f;box.m[10]=0.50f;
    const float off[3]={-0.2f,-0.2f,0.2f};
    float move[3],to[3];
    const bool ok=SnapToBox(box,off,0.04f,move);
    for(int i=0;i<3;++i)to[i]=off[i]+move[i];
    const float q=std::sqrt((0.2f/0.03f)*(0.2f/0.03f)+(0.2f/0.12f)*(0.2f/0.12f));
    const float want=std::sqrt(0.08f)/q+0.04f,got=std::sqrt(to[0]*to[0]+to[1]*to[1]);
    Expect(ok && Near(got,want),"a hand off the gun is pulled in to its body and the palm",got,want);
    Expect(Near(to[0]/to[1],1.0f) && Near(to[2],0.2f),"...square to its axis, the same way round",to[0]/to[1],to[2]);
    const float in[3]={0.0f,-0.05f,0.1f};
    SnapToBox(box,in,0.04f,move);
    Expect(Near(move[0],0) && Near(move[1],0) && Near(move[2],0),"a hand on it stays (never pushed out)",move[1]);
    const float past[3]={0.0f,-0.1f,0.9f};
    SnapToBox(box,past,0.04f,move);
    Expect(Near(past[2]+move[2],0.5f) && Near(move[1],0),"...and is kept within its length",past[2]+move[2]);
    M4 flat=box;flat.m[5]=0.0f;
    Expect(!SnapToBox(flat,off,0.04f,move),"a degenerate box: no hold");
    const float centre[3]={0,0,0},axis[3]={0,0,1},down[3]={0,-1,0},below[3]={0,-0.1f,0};
    Expect(Near(ClampedRoll(centre,axis,below,down,2.0f,1.0f),1.0f),"the slide round the gun stops at the arc",
           ClampedRoll(centre,axis,below,down,2.0f,1.0f));
    Expect(Near(ClampedRoll(centre,axis,below,down,-0.3f,1.0f),-0.3f),"...and is as asked within it",
           ClampedRoll(centre,axis,below,down,-0.3f,1.0f));
    const float side[3]={0.1f*std::sin(1.5f),-0.1f*std::cos(1.5f),0.0f};
    Expect(Near(ClampedRoll(centre,axis,side,down,0.0f,1.0f),-0.5f),"a hand already round the side is brought to the edge",
           ClampedRoll(centre,axis,side,down,0.0f,1.0f));
    float r1[3];
    Point(TurnAbout(centre,axis,0.8f),to,r1);
    Expect(Near(std::sqrt(r1[0]*r1[0]+r1[1]*r1[1]),got) && Near(r1[2],to[2]),"a roll about the axis keeps the hold",
           std::sqrt(r1[0]*r1[0]+r1[1]*r1[1]),got);
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
    Expect(p.z<-.06f && p.pitch>.09f && q.z>p.z && q.pitch<p.pitch,
           "a shot kicks immediately, then returns without waiting for the movement spring",p.z,p.pitch);
    State automatic{};
    for(int i=0;i<120;++i) {
        const Pose burst=Advance(automatic,shot);
        Expect(burst.z>=-kRecoilMostBack && burst.pitch<=kRecoilMostUp,"automatic recoil stays bounded",burst.z,burst.pitch);
    }
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
    Cue dash=Quiet();dash.move=Move::dash;dash.speed=0.3f;
    float most=0.0f;
    for(int i=0;i<60;++i) {   // dash after dash (the Fencer's: 0.2 s each, toggling)
        dash.move=(i/12)%2 ? Move::none : Move::dash;
        p=Advance(s,dash);
        most=std::fmax(most,std::fmax(std::fabs(p.pitch),std::fmax(std::fabs(p.yaw),std::fabs(p.roll))));
    }
    Expect(most<1e-6f,"a dash keeps the weapon aimed: no turn, however often it toggles",most);
    Expect(p.z<=0.0f && p.z>-0.06f,"...only dragged back a little",p.z);
    s=State{};
    Cue look=Quiet();look.lookYaw=0.05f;
    p=Advance(s,look);
    Expect(p.yaw<0,"turning right leaves it behind (to the left)",p.yaw);
    s=State{};look.lookYaw=2.0f;look.lookPitch=-2.0f;
    float maximum=0;
    for(int i=0;i<600;++i) {
        if(i%15==0){look.lookYaw=-look.lookYaw;look.lookPitch=-look.lookPitch;}
        p=Advance(s,look);maximum=std::fmax(maximum,std::fmax(std::fabs(p.yaw),std::fabs(p.pitch)));
    }
    Expect(maximum<0.02f,"rapid alternating turns stay below 1.15 degrees at default sway",maximum);
    s=State{};look.sway=0;p=Run(s,look,60);
    Expect(p.yaw==0 && p.pitch==0,"zero sway only disables turn motion",p.yaw,p.pitch);
    look.shots=1;look.recoil=1;p=Advance(s,look);
    Expect(p.pitch>.09f,"turn sway disabled still preserves recoil",p.pitch);

    s=State{};
    Cue air=Quiet();air.grounded=false;air.speed=0.3f;
    p=Run(s,air,30);
    Expect(std::fabs(p.x)<1e-6f && std::fabs(p.y)<1e-6f,"no bob in the air",p.x,p.y);
}
}  // namespace

// The hold is the weapon's, not the world's (docs/fp-render-re.md §17; the user, 2026-10-18: centred facing east or
// west, off to the right with the arms showing facing north or south; a twitch walking and jumping). A rifle of two
// bone boxes (body and magazine, the native records' half sizes and centres) held by a soldier facing `heading`, its
// third-person animation turning it by `sway` (pitch, yaw) in the hand; the camera faces `heading` too. Returns the
// weapon root's place in the camera's own frame after the first person's transform, the size taken along the weapon's
// own axes (`own`) or, as before, from the game's world-axis box; `wrist` the support hand held to that box.
struct ViewHold { float root[3],wrist[3]; };
ViewHold HoldAt(float heading,float swayPitch,float swayYaw,bool own) {
    const float feet[3]={37.0f,4.0f,-12.0f},eye[3]={0.0f,1.62f,0.10f},zero[3]={0,0,0};
    const M4 body=Frame(0.0f,heading,feet),camera=CameraAt(body,eye);
    const float hand[3]={0.18f,1.30f,0.30f};
    const M4 inHand=Frame(swayPitch,swayYaw,hand);      // the weapon root in the body frame (the animation)
    const M4 root=Mul(inHand,body);
    BoneBox boxes[2];
    const float bodyAt[3]={0.0f,0.03f,0.05f},magAt[3]={0.0f,-0.06f,0.12f};
    boxes[0].world=Mul(Frame(0,0,bodyAt),root);
    boxes[0].half[0]=0.035f;boxes[0].half[1]=0.09f;boxes[0].half[2]=0.42f;
    boxes[0].centre[0]=0.0f;boxes[0].centre[1]=0.02f;boxes[0].centre[2]=0.22f;
    boxes[1].world=Mul(Frame(0.2f,0,magAt),root);
    boxes[1].half[0]=0.02f;boxes[1].half[1]=0.08f;boxes[1].half[2]=0.04f;
    for(float& c:boxes[1].centre)c=0.0f;
    M4 box=WorldBounds(boxes,2);                         // the game's model+0x110 (world axes)
    if(own) {FrameBounds(root,boxes,2,&box);box=Mul(box,root);}   // along the weapon's own axes, placed by its root
    float half[3];
    if(own){M4 local;FrameBounds(root,boxes,2,&local);HalfSizes(local,half);}
    else HalfSizes(box,half);
    float grip[3]={-0.10f,-0.20f,0.40f};                 // right is -x in the camera's frame
    FitGrip(half,false,false,-1.0f,grip);
    M4 t,inv;
    ViewHold out{};
    if(!AnchoredTransform(root,camera,grip,0.5f,&t) || !InverseAffine(camera,&inv))return out;
    float world[3];Point(t,root.m+12,world);Point(inv,world,out.root);
    // The support wrist where the animation has it (on the guard, a little off it), held to the box through T.
    const float guard[3]={0.07f,-0.05f,0.38f};
    float wristWorld[3],moved[3],snap[3];
    Point(root,guard,wristWorld);Point(t,wristWorld,moved);
    if(SnapToBox(Mul(box,t),moved,0.035f*0.5f,snap))for(int i=0;i<3;++i)moved[i]+=snap[i];
    Point(inv,moved,out.wrist);
    (void)zero;
    return out;
}

const float zeroPoint[3]={0,0,0};
float Apart(const float* a,const float* b) { float d[3];Sub(a,b,d);return Length(d); }

void OrientationAndSteadiness() {
    // Facing: the same hold any way the player faces.
    const ViewHold ref=HoldAt(0.0f,0.0f,0.0f,true),old0=HoldAt(0.0f,0.0f,0.0f,false);
    float worstRoot=0,worstWrist=0,oldRoot=0,oldWrist=0;
    for(int k=0;k<72;++k) {
        const float heading=-3.14159265f+k*0.0872664626f;   // every 5 degrees
        const ViewHold h=HoldAt(heading,0.0f,0.0f,true),o=HoldAt(heading,0.0f,0.0f,false);
        worstRoot=std::fmax(worstRoot,Apart(h.root,ref.root));worstWrist=std::fmax(worstWrist,Apart(h.wrist,ref.wrist));
        oldRoot=std::fmax(oldRoot,Apart(o.root,old0.root));oldWrist=std::fmax(oldWrist,Apart(o.wrist,old0.wrist));
    }
    Expect(worstRoot<1e-4f,"facing any way: the weapon sits at the same place in the view",worstRoot);
    Expect(worstWrist<1e-4f,"...and the support hand is held to it at the same place",worstWrist);
    Expect(oldRoot>0.02f,"(the world-axis box moved the weapon with the facing: the reported symptom, m)",oldRoot);
    Expect(oldWrist>0.01f,"(...and the support hand with it, m)",oldWrist);
    // Walking and jumping: the third-person animation turns the weapon in the hand every frame; the first person shows
    // it straight ahead at its grip, so nothing of the sway may reach its place (the twitch).
    float stepRoot=0,stepWrist=0,stepOld=0;
    ViewHold last=HoldAt(0.7f,0,0,true),lastOld=HoldAt(0.7f,0,0,false);
    for(int f=1;f<=240;++f) {
        const float walk=std::sin(f*0.31f),jump=f>120 ? std::fmin(1.0f,(f-120)/20.0f) : 0.0f;
        const float pitch=0.07f*walk+0.45f*jump,yaw=0.10f*std::sin(f*0.155f);
        const ViewHold h=HoldAt(0.7f,pitch,yaw,true),o=HoldAt(0.7f,pitch,yaw,false);
        stepRoot=std::fmax(stepRoot,Apart(h.root,last.root));stepOld=std::fmax(stepOld,Apart(o.root,lastOld.root));
        last=h;lastOld=o;
        (void)stepWrist;
    }
    Expect(stepRoot<1e-4f,"walking and jumping: the weapon does not move in the view frame to frame",stepRoot);
    Expect(stepOld>0.002f,"(the world-axis box twitched it, m a frame)",stepOld);
    // The draw runs on another thread, 0..3 update steps after the camera step (measured, §17): the soldier has walked
    // on by then. T composed at the draw from the pose it draws (T = anchor_now^-1 * L * C, L = anchor * T_step * C^-1)
    // keeps the weapon where the camera step put it in the view; the camera step's own T carries it off by the walk.
    {
        const float feet[3]={5,0,9},eye[3]={0,1.62f,0.1f},hand[3]={0.18f,1.3f,0.3f},grip[3]={-0.1f,-0.2f,0.4f};
        const M4 body=Frame(0,0.8f,feet),camera=CameraAt(body,eye),root=Mul(Frame(0.05f,-0.04f,hand),body);
        M4 t,inv,camInv;
        AnchoredTransform(root,camera,grip,0.5f,&t);InverseAffine(camera,&camInv);
        const M4 place=Mul(Mul(root,t),camInv);
        float worstNew=0,worstOld=0;
        for(int steps=0;steps<=3;++steps) {
            const M4 now=Mul(root,Translate(0.12f*steps*std::sin(0.8f),0.03f*steps,0.12f*steps*std::cos(0.8f)));
            InverseAffine(now,&inv);
            const M4 drawT=Mul(Mul(inv,place),camera);
            float a[3],b[3],at[3],w[3];
            Point(Mul(now,drawT),zeroPoint,w);Point(camInv,w,a);
            Point(Mul(now,t),zeroPoint,w);Point(camInv,w,b);
            Point(place,zeroPoint,at);
            worstNew=std::fmax(worstNew,Apart(a,at));worstOld=std::fmax(worstOld,Apart(b,at));
        }
        Expect(worstNew<1e-4f,"walked on 0..3 steps before the draw: the weapon stays at its place in the view",worstNew);
        Expect(worstOld>0.1f,"(the camera step's T carried it off by the walk, m)",worstOld);
    }
    // The size itself: the same box along the weapon's axes whatever the world turn, and the game's own box along the
    // world's axes reproduced (its several-meshes path: centre and half sizes of the bones' world boxes).
    BoneBox one{};one.world=Frame(0.0f,1.5707963f,ref.root);one.half[0]=0.03f;one.half[1]=0.1f;one.half[2]=0.5f;
    const M4 world=WorldBounds(&one,1);
    M4 local;FrameBounds(one.world,&one,1,&local);
    float wh[3],lh[3];HalfSizes(world,wh);HalfSizes(local,lh);
    Expect(Near(wh[0],0.5f) && Near(wh[2],0.03f),"a weapon facing along the world's x: the game's box is its length wide",wh[0],wh[2]);
    Expect(Near(lh[0],0.03f) && Near(lh[1],0.1f) && Near(lh[2],0.5f),"...its size along its own axes is its width wide",lh[0],lh[2]);
}

// The support hand's arm (§17.4): the forearm goes with the hand (its angle to the palm the animation's, < 20 degrees
// of change: exactly none), both bones keep their length (no squeezed arm), the elbow is shared, and the open shoulder
// end stays out of the view (no cut sleeve on screen) whichever way the animation had the forearm.
void SupportArms() {
    Frustum view{};
    const float right[3]={1,0,0},up[3]={0,1,0},ahead[3]={0,0,1};
    for(int i=0;i<3;++i){view.right[i]=right[i];view.up[i]=up[i];view.ahead[i]=ahead[i];view.eye[i]=0;}
    view.xs=1.0f;view.ys=1.7f;view.nearClip=0.03f;
    const float sleeve=0.035f,shoulder[3]={-0.1f,-0.103f,-0.085f};   // the Ranger's left shoulder, shrunk by 0.5
    bool bend=true,lengths=true,elbow=true,hidden=true,reached=true,solved=true;
    float worstBend=0;
    for(int k=0;k<48;++k) {
        // The animation's arm after T: a wrist on the guard, its forearm coming from below and behind (a support
        // hand under the gun), turned round 30..150 degrees about the view's axis.
        const float phase=0.5236f+k*0.0436f,tilt=0.4f+0.6f*std::fabs(std::sin(k*0.7f));
        const float h0[3]={0.01f+0.03f*std::sin(k*1.3f),-0.11f,0.30f+0.08f*std::cos(k*0.9f)};
        const float fdir[3]={-std::cos(phase)*0.5f,-std::fabs(std::sin(phase))*0.8f,-tilt*0.5f};
        float f[3]={fdir[0],fdir[1],fdir[2]};Unit(f);
        float e0[3],s0[3];for(int i=0;i<3;++i)e0[i]=h0[i]+f[i]*0.115f;
        const float udir[3]={-0.4f,-0.5f,-0.6f};float u[3]={udir[0],udir[1],udir[2]};Unit(u);
        for(int i=0;i<3;++i)s0[i]=e0[i]+u[i]*0.1035f;
        const float wrist[3]={h0[0]+0.01f,h0[1]+0.02f,h0[2]-0.01f};   // held to the gun's box
        M4 upper,fore;
        if(!SupportArm(s0,e0,h0,wrist,shoulder,view,sleeve,0.30f,&upper,&fore)){solved=false;continue;}
        float s1[3],e1[3],e2[3],w1[3],a[3],b[3];
        Point(upper,s0,s1);Point(upper,e0,e1);Point(fore,e0,e2);Point(fore,h0,w1);
        elbow=elbow && Apart(e1,e2)<1e-5f;reached=reached && Apart(w1,wrist)<1e-5f;
        lengths=lengths && Near(Apart(s1,e1),0.1035f) && Near(Apart(e2,w1),0.115f);
        Sub(h0,e0,a);Sub(w1,e2,b);Unit(a);Unit(b);
        const float angle=std::acos(crew::fpview::Clamp(Dot3(a,b),-1.0f,1.0f))*57.29578f;   // the forearm turned against the palm
        worstBend=std::fmax(worstBend,angle);bend=bend && angle<20.0f;
        hidden=hidden && !Seen(view,s1,sleeve);
    }
    Expect(solved,"a support arm is found for every animated forearm");
    Expect(bend,"the support hand's forearm keeps its angle to the palm (change < 20 degrees)",worstBend);
    Expect(lengths,"upper arm and forearm keep their measured length (never squeezed)");
    Expect(elbow && reached,"one shared elbow, the wrist on its place on the gun");
    Expect(hidden,"the upper arm's open shoulder end is out of the view (no cut sleeve on screen)");
    const float inView[3]={0,0,0.5f},below[3]={0,-0.6f,0.3f},behind[3]={0,-0.02f,-0.1f};
    Expect(Seen(view,inView,0.01f) && !Seen(view,below,0.01f) && !Seen(view,behind,0.01f),"the frustum test: ahead seen, below and behind not");
}

void CameraOwnership() {
    const float eye[3]={10,20,30};
    const auto base=crew::fpview::HeadingFrame(.3f,-.2f,eye);
    auto observed=crew::fpview::HeadingFrame(.9f,.4f,eye);
    const auto edit=crew::fpview::CameraWriteOf(base,observed);
    observed.m[12]=200;crew::fpview::ReturnCameraWrite(&observed,edit);
    Expect(std::memcmp(observed.m,base.m,48)==0 && observed.m[12]==200,"camera ownership returns axes independently of a newer eye");
    auto moved=base;moved.m[13]+=5;const auto eyeEdit=crew::fpview::CameraWriteOf(base,moved);
    const auto foreign=crew::fpview::HeadingFrame(-1.1f,.6f,eye);std::memcpy(moved.m,foreign.m,48);
    crew::fpview::ReturnCameraWrite(&moved,eyeEdit);
    Expect(std::memcmp(moved.m,foreign.m,48)==0 && moved.m[13]==20,"camera ownership returns eye independently of a newer direction");
    for(float yaw:{-3.1f,-1.0f,0.0f,1.0f,3.1f})for(float pitch:{-.9f,0.0f,.9f}) {
        const auto f=crew::fpview::HeadingFrame(yaw,pitch,eye);float to[3];
        for(int i=0;i<3;++i)to[i]=eye[i]+f.m[8+i]*100;
        M4 fromRay;const bool ok=crew::fpview::LookFrame(eye,to,&fromRay);
        float error=0;for(int i=0;i<16;++i)error+=std::fabs(f.m[i]-fromRay.m[i]);
        Expect(ok && error<1e-5f,"controller heading/pitch and its actual eye/ray form the same native camera frame");
    }
}

int main() {
    CameraOwnership();
    OrientationAndSteadiness();
    SupportArms();
    Expect(Near(CenteredGrip(.17f,.07f),.10f) && Near(CenteredGrip(.34f,.07f),.27f),
           "existing default grip values upgrade toward the centre",CenteredGrip(.17f,.07f));
    Expect(CenteredGrip(.02f,.07f)==0 && Near(CenteredGrip(-.17f,.07f),-.10f),
           "centering never crosses the midline and preserves handedness",CenteredGrip(-.17f,.07f));

    // A melee blade's authored rotation and swing must survive, while its grip stays at the requested placement.
    for(float swing:{-.8f,0.0f,.9f}) {
        const float origin[3]={4,2,-7},eye[3]={0,1.7f,.1f},wrist[3]={.3f,1.2f,.5f},grip[3]={.3f,-.25f,.4f};
        const M4 aim=Frame(-.2f,1.1f,origin),camera=CameraAt(aim,eye),local=Frame(.5f+swing,-.7f,wrist);
        const M4 weapon=Mul(local,aim);
        M4 transform,invCamera;
        const bool ok=NativeGripTransform(weapon,aim,camera,grip,.5f,&transform) && InverseAffine(camera,&invCamera);
        const M4 shown=Mul(Mul(weapon,transform),invCamera);
        float error=0;
        for(int row=0;row<3;++row)for(int col=0;col<3;++col)
            error=std::fmax(error,std::fabs(shown.m[row*4+col]-local.m[row*4+col]*.5f));
        Expect(ok && error<1e-5f,"a melee weapon keeps its native holding angle and animated swing",error);
        Expect(Near(shown.m[12],grip[0]*.5f) && Near(shown.m[13],grip[1]*.5f) && Near(shown.m[14],grip[2]*.5f),
               "...while the grip stays placed in the view");
    }
    // Across a full recoil/reload-sized wrist sweep, both ends of the solved elbow must agree and each measured
    // arm length must stay unchanged. The hand itself never receives a corrective transform.
    for(int i=0;i<120;++i) {
        const float phase=i*.0523598776f;
        const float shoulder[3]={.3f,-.25f,-.05f},elbow[3]={.4f,-.45f,.2f};
        const float wrist[3]={.2f+.05f*std::sin(phase),-.2f,.5f};
        const float want[3]={.35f,-.3f,-.1f},pole[3]={1,-1.5f,0};
        M4 upper,fore;
        const bool ok=ArmChain(shoulder,elbow,wrist,want,pole,&upper,&fore);
        float a[3],b[3],h[3],s[3],d[3],before[3];
        Point(upper,elbow,a);Point(fore,elbow,b);Point(fore,wrist,h);Point(upper,shoulder,s);
        Sub(a,b,d);Sub(h,wrist,before);
        Expect(ok && Length(d)<1e-5f && Length(before)<1e-5f,"arm chain has a shared elbow and an unchanged wrist");
        Sub(a,s,d);Sub(elbow,shoulder,before);
        Expect(Near(Length(d),Length(before)),"upper arm never stretches");
        Sub(a,wrist,d);Sub(elbow,wrist,before);
        Expect(Near(Length(d),Length(before)),"forearm never stretches");
    }
    using namespace crew::instruments;
    const M4 shell=Translate(1,2,3);
    const M4 hiddenShell=ShellPose(shell,(1u<<0)|(1u<<12),0);
    Expect(hiddenShell.m[0]==0 && hiddenShell.m[5]==0 && hiddenShell.m[10]==0 && hiddenShell.m[12]==0,
           "the local cabin shell collapses to one point before any live readings");
    Expect(ShellPose(shell,1u<<12,12).m[0]==0,"the last transport passenger uses its own visibility bit");
    for(int seat:{-1,1,31,32})
        Expect(std::memcmp(ShellPose(shell,1,seat).m,shell.m,64)==0,
               "other seats and invalid shifts preserve the exact native shell pose");
    Expect(!Segment(5,0,1) && Segment(5,0,2),"physical digit 5 has the correct segment pattern");
    Expect(!Segment(5,1,6) && Segment(-1,1,6),"leading zero is blank; an unavailable value is a dash");
    Readings reading{};reading.share[hp]=.25f;
      reading.flight=true;reading.value[climb]=-12.5f;
      auto needle=crew::instruments::Pose(Identity(),L'a',climb,50,0,reading,true);
      Expect(needle.m[0]!=0 && needle.m[5]!=0,"negative live climb is a valid signed needle reading");
      reading.flight=false;
      Expect(crew::instruments::Pose(Identity(),L'a',climb,50,0,reading,true).m[0]==0,
             "ground vehicles do not invent flight-only readings");
      Expect(crew::instruments::Pose(Identity(),L'u',climb,0,0,reading,true).m[0]!=0,
             "unknown signed readings show the unavailable marker");
      reading.value[speed]=80;
      needle=crew::instruments::Pose(Identity(),L'r',speed,160,0,reading,true);
      Expect(Near(needle.m[0],1) && Near(needle.m[5],1),"truck speed gauge has its own 160 km/h scale");
      reading.value[speed]=-1;
      Expect(crew::instruments::Pose(Identity(),L'r',speed,160,0,reading,true).m[0]==0,
             "unavailable speed does not park a fake needle at zero");
      reading.value[speed]=0;
      Expect(crew::instruments::Pose(Identity(),L'u',speed,0,0,reading,true).m[0]==0,
             "a measured zero speed is different from an unknown reading");
      Expect(crew::instruments::Pose(Identity(),L'D',ammo,0,6,reading,false).m[0]!=0
             && crew::instruments::Pose(Identity(),L'D',ammo,0,0,reading,false).m[0]==0,
             "new numeric widgets show unknown dashes outside their owning seat");
      Expect(crew::instruments::Pose(Identity(),L'B',fuel,0,0,reading,false).m[0]==0
             && crew::instruments::Pose(Identity(),L'T',0,0,0,reading,false).m[0]==0,
             "new bars and contacts cannot show invented neighbouring-seat values");
    for(int language=0;language<4;++language) {
        reading.language=language;
        int visible=0;
        for(int variant=0;variant<4;++variant) {
            const auto pose=crew::instruments::Pose(Identity(),L'l',variant,0,0,reading,true);
            visible+=pose.m[0]!=0;
            Expect((pose.m[0]!=0)==(variant==language),"physical label selects current HUD language");
        }
        Expect(visible==1,"exactly one language variant is visible");
    }
    reading.value[fuel]=-1;
    Expect(crew::instruments::Pose(Identity(),L'l',reading.language,fuel+1,0,reading,true).m[0]==0,
           "unavailable fuel also hides its translated caption");
    reading.value[hp]=200;
    for(float health:{1.f,.4f,.1f}) {
        reading.share[hp]=health;int visible=0;
        for(int colour=0;colour<3;++colour)visible+=crew::instruments::Pose(Identity(),L'c',hp,colour,0,reading,true).m[0]!=0;
        Expect(visible==1,"exactly one whole-vehicle damage colour is visible");
    }
    reading.value[hp]=-1;
    Expect(crew::instruments::Pose(Identity(),L'c',hp,2,0,reading,true).m[0]==0,"unknown health stays a neutral outline");
    reading.value[hp]=300000;reading.share[hp]=.5f;
    Expect(crew::instruments::Pose(Identity(),L'p',hp,1,0,reading,true).m[0]!=0,
           "large native durability still displays a valid percent");
    reading.share[hp]=.25f;
    M4 bar=crew::instruments::Pose(Identity(),L'b',hp,0,0,reading,true);
    Expect(Near(bar.m[0],.25f) && Near(bar.m[5],1),"a physical bar scales only along its own horizontal axis");
    {   // The weapons screen (cockpit_screens.inc PublishInstruments -> ArmReadings -> e / B / v / D controls).
        using crew::instruments::Pose;
        Readings w{};
        for(float& v:w.value)v=-1.0f;
        ArmRow rows[4]{};
        rows[0]=ArmRow{2,0,0.0f,-1.0f,false};      // AP, spent from the ready rack
        rows[1]=ArmRow{4,1,1.0f,0.42f,false};      // HEAT, its next round going in
        rows[2]=ArmRow{0,286,0.57f,-1.0f,false};   // the coaxial MG
        rows[3]=ArmRow{7,0,0.0f,-1.0f,true};       // a launcher spent for good
        ArmReadings(w,rows,3,1);
        auto shown=[&](wchar_t mode,int field,int place){return Pose(Identity(),mode,field,place,0,w,true).m[0]!=0;};
        int pictures=0,frames=0,cells=0;
        for(int k=0;k<12;++k)pictures+=shown(L'e',arm,k);
        for(int i=0;i<kRows;++i)frames+=shown(L'e',pick,i);
        for(int i=0;i<kRows;++i)for(int k=0;k<12;++k)cells+=shown(L'e',rowRound+i,k);
        Expect(pictures==1 && shown(L'e',arm,4),"the weapons screen shows the picked round's one tag and picture (HEAT)");
        Expect(cells==3 && shown(L'e',rowRound,2) && shown(L'e',rowRound+1,4) && shown(L'e',rowRound+2,0),
               "each cell names its weapon by its round class (AP, HEAT, MG), one each");
        Expect(frames==1 && shown(L'e',pick,1),"one cell framed: the picked one");
        Expect(shown(L'l',w.language,load+1) && !shown(L'l',w.language,empty+1),"LOADING while the next round goes in");
        Expect(Near(Pose(Identity(),L'b',load,0,0,w,true).m[0],.42f) && Pose(Identity(),L'b',mag,0,0,w,true).m[0]==0,
               "loading fills the bar in place of the magazine");
        Expect(Near(Pose(Identity(),L'b',row+2,0,0,w,true).m[0],.57f),"each listed weapon's cell has its fill");
        {
            Readings two{};
            for(float& v:two.value)v=-1.0f;
            ArmReadings(two,rows,2,0);
            Expect(Pose(Identity(),L'v',row+1,0,0,two,true).m[0]!=0 && Pose(Identity(),L'v',row+2,0,0,two,true).m[0]==0,
                   "no cell past the seat's weapons");
        }
        Expect(Near(Pose(Identity(),L'b',row+1,0,0,w,true).m[0],.42f),"a loading weapon's cell shows its loading");
        ArmReadings(w,rows,4,3);
        Expect(shown(L'l',w.language,empty+1) && !shown(L'l',w.language,load+1),"a spent launcher: EMPTY, no loading");
        Expect(w.value[pick]==2 && shown(L'e',rowRound,4) && shown(L'e',rowRound+2,7),
               "four weapons: the cells a window holding the picked one (HEAT, MG, RKT; RKT framed)");
        Readings one{};
        for(float& v:one.value)v=-1.0f;
        ArmReadings(one,rows,1,0);
        Expect(one.value[pick]<0 && one.value[row]<0,"a single weapon lists no cells");
        Expect(Pose(Identity(),L'e',arm,2,0,one,false).m[0]==0,"another seat's picture is hidden, not all twelve at once");
    }
    {   // The flight instruments: a compass card, an altimeter's hands, an attitude card, zero-padded digits.
        using crew::instruments::Pose;
        Readings f{};
        for(float& v:f.value)v=-1.0f;
        f.flight=true;f.value[heading]=90;f.value[altitude]=2500;f.value[pitch]=10;f.value[roll]=30;
        const float tip[3]={0,1,0};
        auto at=[&](const M4& m,float* out){for(int c=0;c<3;++c)out[c]=tip[0]*m.m[c]+tip[1]*m.m[4+c]+tip[2]*m.m[8+c]+m.m[12+c];};
        float p[3];
        at(Pose(Identity(),L'h',heading,0,0,f,true),p);
        Expect(Near(p[0],-1) && Near(p[1],0),"heading 090: the card's N turned to the left, E under the lubber line",p[0],p[1]);
        at(Pose(Identity(),L'n',speed,0,0,f,true),p);
        f.value[speed]=0;at(Pose(Identity(),L'n',speed,0,0,f,true),p);
        Expect(p[0]<0 && p[1]<0,"the needles' sense: zero at the scale's lower left",p[0],p[1]);
        at(Pose(Identity(),L'w',altitude,1000,0,f,true),p);
        Expect(Near(p[0],0) && Near(p[1],-1),"2500 m: the long hand at 5 (half way round)",p[0],p[1]);
        at(Pose(Identity(),L'w',altitude,10000,0,f,true),p);
        Expect(Near(p[0],1) && Near(p[1],0),"2500 m: the short hand at 2.5 (a quarter round)",p[0],p[1]);
        Expect(Pose(Identity(),L'h',heading,1,0,f,true).m[0]==0 && Pose(Identity(),L'h',heading,0,0,f,true).m[0]!=0,
               "flying: the ground's compass card gives way to the attitude card");
        // The attitude card: its centre dropped 10 degrees x 0.0045 below the window's, the card turned against the
        // 30 degree right bank (anticlockwise: its right side up).
        const M4 g=Pose(Identity(),L'g',pitch,45,1400,f,true);
        const float centre[3]={g.m[12],g.m[13],g.m[14]};
        float right[3];
        const float side[3]={1,0,0};
        for(int c=0;c<3;++c)right[c]=side[0]*g.m[c]+side[1]*g.m[4+c]+side[2]*g.m[8+c]+g.m[12+c]-centre[c];
        Expect(Near(std::sqrt(centre[0]*centre[0]+centre[1]*centre[1]),.045f) && right[1]>0.4f && right[0]>0.8f,
               "nose 10 up, right bank 30: the horizon 0.045 below and its right side raised",right[0],right[1]);
        f.value[pitch]=80;
        const M4 most=Pose(Identity(),L'g',pitch,45,1400,f,true);
        Expect(Near(std::sqrt(most.m[12]*most.m[12]+most.m[13]*most.m[13]),.14f),"the card's travel stops at its window's mask");
        f.flight=false;
        Expect(Pose(Identity(),L'g',pitch,45,1400,f,true).m[0]==0 && Pose(Identity(),L'h',heading,1,0,f,true).m[0]!=0,
               "on the ground: no attitude card, the compass card in its window");
        f.value[heading]=74;
        Expect(Segment(74,2,0,true) && !Segment(74,2,6,true) && !Segment(74,2,0),"a heading's digits are zero-padded: 074");
        Expect(Pose(Identity(),L'Z',heading,2,0,f,true).m[0]!=0,"Z digits light the leading zero");
    }
    {   // A neighbour seat's panel (firstperson.cpp InstrumentPose): the vehicle's own readings are every seat's.
        using crew::instruments::SeatPose;
        Expect(SeatPoseOf(false,L'h',heading,1,4)==SeatPose::live && SeatPoseOf(false,L'w',altitude,1000,4)==SeatPose::live
               && SeatPoseOf(false,L'g',pitch,45,6)==SeatPose::live && SeatPoseOf(false,L'Z',heading,2,6)==SeatPose::live,
               "a neighbour's compass card, altimeter, attitude card and heading digits read the vehicle live");
        Expect(SeatPoseOf(false,L'e',arm,2,4)==SeatPose::neutral && SeatPoseOf(false,L'v',row,0,4)==SeatPose::neutral
               && SeatPoseOf(false,L'd',ammo,0,6)==SeatPose::skip && SeatPoseOf(false,L'l',0,ammo+1,4)==SeatPose::neutral,
               "a neighbour's weapons screen does not show this seat's weapons");
        Expect(SeatPoseOf(false,L'l',0,0,4)==SeatPose::live && SeatPoseOf(false,L't',heading,0,4)==SeatPose::skip,
               "l's field is a language, t's a contact: not vehicle readings");
        Expect(SeatPoseOf(false,L'h',hp,0,4)==SeatPose::neutral,"a card on a seat's own field stays hidden next door");
        Readings none{};
        for(float& v:none.value)v=-1.0f;
        none.flight=true;none.value[heading]=90;
        Expect(crew::instruments::Pose(Identity(),L'h',heading,0,0,none,false).m[0]==0 && crew::instruments::Pose(Identity(),L'g',pitch,45,1400,none,false).m[0]==0
               && crew::instruments::Pose(Identity(),L'w',altitude,1000,0,none,false).m[0]==0,
               "no readings yet: the cards and hands are put away, not left overlapping at rest");
    }
    AuthoredActions();
    Identity_();
    Hold();
    Shrink();
    Singular();
    Collapse_();
    Eye();
    Anchored();
    Arms();
    Held();
    Motion();
    Springs();
    std::printf(failures ? "%d FAILED\n" : "all passed\n",failures);
    return failures ? 1 : 0;
}
