// The map view's camera and grid math (map.cpp places the camera by it, hud.cpp draws the grid by it;
// tools/map_cam_check.cpp checks it offline). Pure: no game, no Windows.
//  - The view orbits a focus point on the ground: it looks at the focus from `height` m over it, `pitch` down
//    (kMinPitch..kMaxPitch), its level forward along `yaw` (atan2(x, z), the game's heading convention: turretcam.h
//    YawOf). The game's level right of a forward f is (-f.z, 0, f.x) (playerjet.cpp RightOf: +x is the left of +z).
//  - Panning moves the focus in the view's level frame; a drag grabs the ground (the world follows the mouse).
//  - Zoom is the height, kMinHeight..kMaxHeight (the user, 2026-10-06: 200 m to 3 km), by a factor a wheel notch.
#pragma once
#include <cmath>

namespace mapcam {
constexpr float kPi=3.14159265f;
constexpr float kMinHeight=200.0f,kMaxHeight=3000.0f,kStartHeight=700.0f;
constexpr float kMinPitch=30.0f*kPi/180.0f,kMaxPitch=88.0f*kPi/180.0f,kStartPitch=60.0f*kPi/180.0f;
constexpr float kNotchZoom=0.85f;      // the height's factor a wheel notch (or a +/- step) in
constexpr float kMetresPerPixel=0.0011f;   // a drag's ground metres per mouse unit, per metre of view distance
                                           // (2 tan(30 deg) / 1080: the ground under the cursor follows it)
constexpr float kPanRate=0.9f;         // keys / stick: the share of the height panned a second
constexpr float kTurnRate=1.6f;        // rad/s, keys / stick
constexpr float kDragTurn=0.004f;      // rad a mouse unit (right drag)
constexpr float kZoomRate=1.8f;        // the height's factor a second, a zoom key / trigger held

inline float Clamp(float v,float lo,float hi) noexcept { return v<lo ? lo : v>hi ? hi : v; }
inline float Wrap(float a) noexcept {
    while(a>kPi)a-=2.0f*kPi;
    while(a<-kPi)a+=2.0f*kPi;
    return a;
}
inline void Forward(float yaw,float* f) noexcept { f[0]=std::sin(yaw);f[1]=0.0f;f[2]=std::cos(yaw); }
inline void Right(float yaw,float* r) noexcept { r[0]=-std::cos(yaw);r[1]=0.0f;r[2]=std::sin(yaw); }

struct View {
    float focus[3];            // on the ground (focus[1] the ground's height there)
    float yaw,pitch,height;
};

// The eye and the point it looks at (the focus).
inline void Place(const View& v,float* eye,float* look) noexcept {
    float f[3];Forward(v.yaw,f);
    const float back=v.height/std::tan(v.pitch);
    for(int i=0;i<3;++i){eye[i]=v.focus[i]-f[i]*back;look[i]=v.focus[i];}
    eye[1]+=v.height;
}
// The eye's distance to the focus.
inline float Distance(const View& v) noexcept { return v.height/std::sin(v.pitch); }

// A mouse drag of (dx right, dy down) mouse units with the left button: the ground grabbed (the focus moves against it).
inline void Drag(View& v,float dx,float dy) noexcept {
    float f[3],r[3];Forward(v.yaw,f);Right(v.yaw,r);
    const float k=Distance(v)*kMetresPerPixel;
    for(int i=0;i<3;i+=2)v.focus[i]+=-r[i]*dx*k+f[i]*dy*k;
}
// Panning by keys or a stick for `dt` s: `ahead` / `right` in -1..1 (the view's level frame).
inline void Pan(View& v,float ahead,float right,float dt) noexcept {
    float f[3],r[3];Forward(v.yaw,f);Right(v.yaw,r);
    const float k=v.height*kPanRate*dt;
    for(int i=0;i<3;i+=2)v.focus[i]+=(f[i]*ahead+r[i]*right)*k;
}
// Turning (`turn` rad, positive to the right: the heading falls, +x being the left of +z) and tilting (`tilt` rad,
// positive looks more straight down).
inline void Turn(View& v,float turn,float tilt) noexcept {
    v.yaw=Wrap(v.yaw-turn);
    v.pitch=Clamp(v.pitch+tilt,kMinPitch,kMaxPitch);
}
// Zooming by `notches` (positive in: the height falls by kNotchZoom a notch).
inline void Zoom(View& v,float notches) noexcept {
    v.height=Clamp(v.height*std::pow(kNotchZoom,notches),kMinHeight,kMaxHeight);
}

// The ground the view may look at and look from (the user, 2026-10-09: "m里面视角出了地图边界以后会一闪一闪的"): the
// map's real ground (playarea.h, where a map ray still finds ground), widened to hold the player. Past it the game
// has no ground of its own, only far-only scenery its far pass draws from 500 m out (view_clip.h) and nothing its
// visibility was made for: a camera put there shows the void and that scenery cut and flashing as it moves. The
// view's one bound, kept by Keep after every write of the focus (follow, pan, drag, a unit centred), not eased:
//  - the focus is inside the bounds;
//  - the eye (behind the focus by height / tan(pitch)) is inside them too where they are wide enough for both: the
//    focus moves in by the eye's overshoot, never past the far edge. Where they are not (zoomed far out over a small
//    map), the focus is kept and the eye stays out.
// Keep is idempotent (a kept view is kept as it is), so nothing is pulled back and forth between two frames.
struct Bounds { float lo[2],hi[2]; };   // [0]: x, [1]: z
inline Bounds Around(Bounds b,const float* me) noexcept {
    for(int i=0;i<2;++i) {
        const float v=me[i==0 ? 0 : 2];
        if(!std::isfinite(v))continue;
        if(v<b.lo[i])b.lo[i]=v;
        if(v>b.hi[i])b.hi[i]=v;
    }
    return b;
}
inline void Keep(View& v,const Bounds& b) noexcept {
    float f[3];Forward(v.yaw,f);
    const float back=v.height/std::tan(v.pitch);
    for(int i=0;i<2;++i) {
        const int k=i==0 ? 0 : 2;
        if(!(b.lo[i]<=b.hi[i]) || !std::isfinite(v.focus[k]))continue;
        float at=Clamp(v.focus[k],b.lo[i],b.hi[i]);
        const float eye=at-f[k]*back;
        // The eye's overshoot along this axis moves the focus the same way back in, as far as the focus may go.
        if(eye<b.lo[i])at=Clamp(at+(b.lo[i]-eye),b.lo[i],b.hi[i]);
        else if(eye>b.hi[i])at=Clamp(at-(eye-b.hi[i]),b.lo[i],b.hi[i]);
        v.focus[k]=at;
    }
}

// A pin's stem (map.cpp's marks, hud.cpp MapPin): this share of the view distance tall, so it reads the same at 200 m and
// at 3 km; taller the steeper the view (a vertical stem seen from straight over it is foreshortened to nothing), at
// most 1 / kPinCosLeast of it.
constexpr float kPinShare=0.05f,kPinCosLeast=0.5f;
inline float PinHeight(float distance,float pitch) noexcept {
    const float c=std::cos(pitch);
    return kPinShare*distance/(c>kPinCosLeast ? c : kPinCosLeast);
}

// The grid's step for a view `height` m up: a round number about a third of the height (10 or so lines across).
inline float GridStep(float height) noexcept {
    const float steps[]={50.0f,100.0f,200.0f,250.0f,500.0f,1000.0f,2000.0f};
    const float want=height*0.3f;
    float best=steps[0];
    for(float s:steps)if(std::fabs(s-want)<std::fabs(best-want))best=s;
    return best;
}
// How far out from the focus the grid is drawn, in whole steps: past the ground a view of `height` at `pitch` shows
// ahead (its top edge kHalfFov over its centre; the cameras' field of view is pi/4, set by their construction 0x118AFC0)
// and behind, at most kMaxGrid lines either side.
constexpr float kHalfFov=0.4f;
constexpr int kMaxGrid=40;
inline int GridLines(float height,float pitch,float step) noexcept {
    const float top=pitch-kHalfFov>0.05f ? pitch-kHalfFov : 0.05f;
    const float ahead=height/std::tan(top)-height/std::tan(pitch)+height;
    const int n=static_cast<int>(std::ceil(Clamp(ahead,height,4.0f*kMaxHeight)/step));
    return n<2 ? 2 : n>kMaxGrid ? kMaxGrid : n;
}
}  // namespace mapcam
