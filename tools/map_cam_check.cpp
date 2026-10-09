// The map view's camera and grid math (src/map_cam.h) checked offline: where the eye stands for a view, that a drag
// grabs the ground (the point under the mouse follows it), the keys' and stick's pan, turn and tilt, the zoom's steps
// and limits (200 m to 3 km), that the grid reaches past the ground the view shows at every height and pitch, and that
// a unit's pin stem reads at the same size on the screen from 200 m to 3 km (the cameras' field of view pi/4: their
// construction 0x118AFC0).
//   cmake --build build --target map_cam_check && build\map_cam_check.exe      (exit code 1 on a failure)
#include "../src/map_cam.h"
#include "../src/map_camera_state.h"
#include <cstdio>

namespace {
constexpr float kPinPxLeast=20.0f,kPinPxMost=90.0f,kPinSteep=75.0f*mapcam::kPi/180.0f;
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
    // Restart while the map owns a camera: there need not be a final update of the
    // old camera. The new camera may also reuse its address. Exercise the actual
    // lifecycle gate used before the hook restores a stock matrix.
    {
        CameraSession session;
        CameraState state;
        int oldCamera=0,newCamera=0;
        const auto first=session.Begin(state);
        state.cam=&oldCamera;state.shown=state.haveStock=true;state.stock[12]=900.0f;
        session.Publish(first,true);
        Check(session.Owns(),"map owns the current mission camera");
        session.Begin(state);
        Check(state.cam==&oldCamera && state.shown && state.haveStock,"same mission retains camera and stock matrix");
        Check(state.shown && state.cam!=&newCamera,"second local camera is still excluded in the same mission");
        session.Reset();
        Check(!session.Owns(),"mission reset invalidates ownership before any camera callback");
        session.Publish(first,true);   // an old callback finishing after MissionStart
        Check(!session.Owns(),"late old callback cannot reclaim new mission ownership");
        const auto second=session.Begin(state);
        Check(second!=first && !state.shown && !state.haveStock && !state.cam,"new camera can claim after restart");
        state.cam=&newCamera;state.shown=state.haveStock=true;
        session.Publish(second,true);
        Check(session.Owns(),"new mission camera owns its map");
        session.Reset();
        session.Begin(state);
        Check(!state.haveStock && !state.shown && !state.cam,"reused camera address cannot restore a previous mission matrix");
    }
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
                // A pin's stem at the focus reads the same at every height: kPinPxLeast..kPinPxMost px tall on a 1080-line
                // screen (pi/4 field of view) up to kPinSteep down; steeper it shortens (seen from over it), but stays visible.
                const float pin=PinHeight(Distance(v),v.pitch);
                const float topPt[3]={v.focus[0],v.focus[1]+pin,v.focus[2]};
                float tr,tu;ScreenOf(v,topPt,&tr,&tu);
                const float px=tu*540.0f/std::tan(kPi/8.0f);
                if(v.pitch<=kPinSteep)Check(px>=kPinPxLeast && px<=kPinPxMost,"pin stem on the screen",px,pitchDeg);
                else Check(px>=4.0f,"pin stem seen from over it",px,pitchDeg);
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
    // The view's bounds (the user, 2026-10-09: "m里面视角出了地图边界以后会一闪一闪的"): every view kept over the map's
    // ground -- the focus inside, the eye inside too where the bounds hold both -- and a kept view kept as it is: a pan
    // pushed against an edge frame after frame, a zoom out and in, a turn: never back and forth between two frames.
    {
        const Bounds map{{-1500.0f,-1200.0f},{1500.0f,1800.0f}};
        auto inside=[&](const View& v,bool eye){
            float e[3],l[3];Place(v,e,l);
            const float* p=eye ? e : v.focus;
            return p[0]>=map.lo[0]-0.01f && p[0]<=map.hi[0]+0.01f && p[2]>=map.lo[1]-0.01f && p[2]<=map.hi[1]+0.01f;
        };
        int views=0;
        for(float h=kMinHeight;h<=kMaxHeight;h*=1.5f)
            for(float pitch=kMinPitch;pitch<=kMaxPitch;pitch+=0.25f)
                for(float yaw=-3.1f;yaw<=3.1f;yaw+=0.7f)
                    for(float x=-4000.0f;x<=4000.0f;x+=1300.0f)
                        for(float z=-4000.0f;z<=4000.0f;z+=1300.0f) {
                            View v{{x,10.0f,z},yaw,pitch,h};
                            Keep(v,map);++views;
                            Check(inside(v,false),"the focus kept on the map",v.focus[0],v.focus[2]);
                            const float back=h/std::tan(pitch);
                            if(back<1490.0f)Check(inside(v,true),"the eye kept over the map where the bounds hold both",back,yaw);
                            View again=v;Keep(again,map);
                            Check(Near(again.focus[0],v.focus[0],0.05f) && Near(again.focus[2],v.focus[2],0.05f),"a kept view is kept",
                                  again.focus[0]-v.focus[0],again.focus[2]-v.focus[2]);
                            Check(again.focus[1]==v.focus[1] && again.yaw==v.yaw && again.height==v.height,"only the focus x, z moved");
                        }
        Check(views>1000,"views kept",views);cases+=views;
        // A pan pushed against the edge every frame, then let go: the focus stays put at the edge, frame after frame.
        View v{{1400.0f,0.0f,0.0f},kPi*0.5f,kStartPitch,kStartHeight};   // looking along +x, towards the edge
        float prev[2]={0.0f,0.0f};int moved=0,reversed=0;float lastStep=0.0f;
        for(int frame=0;frame<240;++frame) {
            if(frame<180)Pan(v,1.0f,0.0f,1.0f/60.0f);
            Keep(v,map);
            if(frame>0) {
                const float step=v.focus[0]-prev[0];
                if(std::fabs(step)>1e-3f)++moved;
                if(step*lastStep<-1e-6f)++reversed;
                lastStep=step;
            }
            prev[0]=v.focus[0];prev[1]=v.focus[2];
        }
        Check(reversed==0,"pushed against the edge: never back and forth",reversed);
        Check(v.focus[0]<=map.hi[0]+0.01f,"pushed against the edge: on the map",v.focus[0]);
        Check(moved<=180,"let go at the edge: still",moved);
        // The player off the map (a jet past the edge): the bounds widen to hold them, so following them is not pulled
        // back to the edge (a follow and a bound fighting each frame).
        const float me[3]={2600.0f,300.0f,-50.0f};
        const Bounds wide=Around(map,me);
        Check(wide.hi[0]==2600.0f && wide.lo[0]==map.lo[0] && wide.lo[1]==map.lo[1] && wide.hi[1]==map.hi[1],"the bounds widened to hold the player");
        View follow{{me[0],0.0f,me[2]},0.0f,kStartPitch,kStartHeight};
        Keep(follow,wide);
        Check(Near(follow.focus[0],me[0],0.01f) && Near(follow.focus[2],me[2],0.01f),"following a player off the map: on them",follow.focus[0]);
    }
    std::printf("%d views checked: %s\n",cases,failures ? "FAILED" : "all ok");
    return failures ? 1 : 0;
}
