// The first person's arithmetic (src/fp_view.h), offline:
//  - the view transform with no hold and no shrink, the camera at the eye in the aim frame's own rotation: the identity
//    (the arms and weapon drawn where they are);
//  - a hold moves every pose by that much along the aim frame's own axes, whatever the frame's yaw and pitch;
//  - a shrink keeps the picture: a point and its shrunk copy lie on the same ray from the eye, at s times the distance;
//  - a collapse sends every point to its one place;
//  - the eye eased in the body frame: the first frame as it is, then a share of the way a frame; a turned body turns it.
// Exit code 1 when one fails. cmake --build build --target fp_view_check && build\fp_view_check.exe
#include "../src/fp_view.h"
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
}  // namespace

int main() {
    Identity_();
    Hold();
    Shrink();
    Singular();
    Collapse_();
    Eye();
    std::printf(failures ? "%d FAILED\n" : "all passed\n",failures);
    return failures ? 1 : 0;
}
