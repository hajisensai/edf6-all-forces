// The map view's camera and grid math (src/map_cam.h) checked offline: where the eye stands for a view, that a drag
// grabs the ground (the point under the mouse follows it), the keys' and stick's pan, turn and tilt, the zoom's steps
// and limits (200 m to 3 km), and that the grid reaches past the ground the view shows at every height and pitch
// (the cameras' field of view pi/4: their construction 0x118AFC0).
//   cmake --build build --target map_cam_check && build\map_cam_check.exe      (exit code 1 on a failure)
#include "../src/map_cam.h"
#include <cstdio>

namespace {
int failures=0;
void Check(bool ok,const char* what,double a=0.0,double b=0.0) {
    if(ok)return;
    ++failures;
    std::printf("FAIL %s (%g, %g)\n",what,a,b);
}
bool Near(float a,float b,float tol) { return std::fabs(a-b)<=tol; }
float Dot(const float* a,const float* b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }

// A point's place on the screen of `v` (right, up: the view's own frame; > 0 right / above the centre's line).
void ScreenOf(const mapcam::View& v,const float* p,float* right,float* up) {
    float eye[3],look[3];
    mapcam::Place(v,eye,look);
    float f[3]={look[0]-eye[0],look[1]-eye[1],look[2]-eye[2]};
    const float len=std::sqrt(Dot(f,f));
    for(float& c:f)c/=len;
    float r[3];mapcam::Right(v.yaw,r);
    const float u[3]={r[1]*f[2]-r[2]*f[1],r[2]*f[0]-r[0]*f[2],r[0]*f[1]-r[1]*f[0]};   // right x forward: up
    const float d[3]={p[0]-eye[0],p[1]-eye[1],p[2]-eye[2]};
    const float depth=Dot(d,f);
    *right=Dot(d,r)/depth;*up=Dot(d,u)/depth;
}
}  // namespace

int main() {
    using namespace mapcam;
    int cases=0;
    // The eye: `height` over the focus, height / tan(pitch) behind it along the heading, looking down `pitch` at it.
    for(float yawDeg=-180.0f;yawDeg<=180.0f;yawDeg+=45.0f)
        for(float pitchDeg=30.0f;pitchDeg<=88.0f;pitchDeg+=14.5f)
            for(float h=kMinHeight;h<=kMaxHeight;h*=2.0f) {
                ++cases;
                View v{{100.0f,40.0f,-300.0f},yawDeg*kPi/180.0f,pitchDeg*kPi/180.0f,h};
                float eye[3],look[3];
                Place(v,eye,look);
                Check(Near(eye[1]-v.focus[1],h,1e-3f*h),"eye height",eye[1]-v.focus[1],h);
                const float back=std::sqrt((eye[0]-v.focus[0])*(eye[0]-v.focus[0])+(eye[2]-v.focus[2])*(eye[2]-v.focus[2]));
                Check(Near(back,h/std::tan(v.pitch),1e-3f*h),"eye back",back,h/std::tan(v.pitch));
                Check(Near(look[0],v.focus[0],1e-3f) && Near(look[2],v.focus[2],1e-3f),"looks at the focus");
                float f[3];Forward(v.yaw,f);
                Check((v.focus[0]-eye[0])*f[0]+(v.focus[2]-eye[2])*f[2]>0.0f,"the focus ahead of the eye");
                Check(Near(Distance(v),std::sqrt(back*back+h*h),1e-3f*h),"distance",Distance(v),std::sqrt(back*back+h*h));
                // A drag right / down: the ground point that was at the screen's centre is now right of it / under it
                // by the drag (the ground follows the mouse), and a drag back undoes it.
                View d=v;
                Drag(d,100.0f,0.0f);
                float sr,su;ScreenOf(d,v.focus,&sr,&su);
                Check(sr>0.0f && Near(su,0.0f,1e-3f),"drag right: the ground moves right",sr,su);
                Drag(d,-100.0f,0.0f);
                Check(Near(d.focus[0],v.focus[0],1e-2f) && Near(d.focus[2],v.focus[2],1e-2f),"drag back");
                d=v;Drag(d,0.0f,100.0f);ScreenOf(d,v.focus,&sr,&su);
                Check(su<0.0f && Near(sr,0.0f,1e-3f),"drag down: the ground moves down",sr,su);
                // The drag's ground metres per mouse unit at the focus: kMetresPerPixel of the distance.
                d=v;Drag(d,1000.0f,0.0f);
                const float moved=std::sqrt((d.focus[0]-v.focus[0])*(d.focus[0]-v.focus[0])+(d.focus[2]-v.focus[2])*(d.focus[2]-v.focus[2]));
                Check(Near(moved,1000.0f*kMetresPerPixel*Distance(v),1e-2f*moved),"drag scale",moved,1000.0f*kMetresPerPixel*Distance(v));
                // Keys: W pans ahead (the focus moves on along the heading), D to the right (the view moves right: the
                // old focus shows left of the centre).
                d=v;Pan(d,1.0f,0.0f,0.5f);
                Check(Near((d.focus[0]-v.focus[0])*f[0]+(d.focus[2]-v.focus[2])*f[2],h*kPanRate*0.5f,1e-2f*h),"pan ahead");
                d=v;Pan(d,0.0f,1.0f,0.5f);ScreenOf(d,v.focus,&sr,&su);
                Check(sr<0.0f,"pan right: the old centre shows left",sr,su);
                // The grid reaches past the farthest ground the view's top edge shows, and past the nearest behind.
                const float step=GridStep(h);
                const int n=GridLines(h,v.pitch,step);
                const float topAngle=v.pitch-kHalfFov;
                if(topAngle>0.06f) {
                    const float farAhead=h/std::tan(topAngle)-h/std::tan(v.pitch);
                    if(farAhead<4.0f*kMaxHeight && farAhead/step<kMaxGrid)Check(static_cast<float>(n)*step>=farAhead,"grid ahead",n*step,farAhead);
                }
                const float bottom=v.pitch+kHalfFov;
                const float nearBehind=bottom<kPi*0.5f ? h/std::tan(v.pitch)-h/std::tan(bottom) : h/std::tan(v.pitch);
                Check(static_cast<float>(n)*step>=nearBehind,"grid behind",n*step,nearBehind);
                Check(n>=2 && n<=kMaxGrid,"grid line count",n);
            }
    // Turn: positive turns the view right (the old ahead point shows left), wraps; tilt is held to its limits.
    {
        View v{{0.0f,0.0f,0.0f},-3.0f,kStartPitch,kStartHeight};
        Turn(v,0.5f,0.0f);
        Check(v.yaw>-kPi && v.yaw<=kPi && Near(v.yaw,2.0f*kPi-3.5f,1e-4f),"turn wraps",v.yaw);
        View t{{0.0f,0.0f,0.0f},0.0f,kStartPitch,kStartHeight};
        const float ahead[3]={0.0f,0.0f,500.0f};
        Turn(t,0.3f,0.0f);
        float sr,su;ScreenOf(t,ahead,&sr,&su);
        Check(sr<0.0f,"turn right: the old ahead shows left",sr);
        Turn(t,0.0f,10.0f);Check(Near(t.pitch,kMaxPitch,1e-6f),"tilt top",t.pitch);
        Turn(t,0.0f,-10.0f);Check(Near(t.pitch,kMinPitch,1e-6f),"tilt bottom",t.pitch);
    }
    // Zoom: a notch in is kNotchZoom of the height; the limits hold; GridStep never falls as the height grows.
    {
        View v{{0.0f,0.0f,0.0f},0.0f,kStartPitch,1000.0f};
        Zoom(v,1.0f);Check(Near(v.height,850.0f,1e-2f),"a notch in",v.height);
        Zoom(v,-1.0f);Check(Near(v.height,1000.0f,1e-2f),"a notch out",v.height);
        Zoom(v,100.0f);Check(Near(v.height,kMinHeight,1e-3f),"zoom floor",v.height);
        Zoom(v,-100.0f);Check(Near(v.height,kMaxHeight,1e-3f),"zoom ceiling",v.height);
        float last=0.0f;
        for(float h=kMinHeight;h<=kMaxHeight;h+=10.0f){const float s=GridStep(h);Check(s>=last,"grid step monotone",h,s);last=s;}
        Check(GridStep(kMinHeight)==50.0f && GridStep(kMaxHeight)==1000.0f,"grid steps at the ends",GridStep(kMinHeight),GridStep(kMaxHeight));
    }
    std::printf("%d views checked: %s\n",cases,failures ? "FAILED" : "all ok");
    return failures ? 1 : 0;
}
