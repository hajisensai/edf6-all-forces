// A magnified sight's picture (the user, 2026-10-08: "the gunner's sight: the zoomed one too, and not only ground
// vehicles: everything that has a sight"): while the sight is magnified (sightzoom.cpp, SightZoomNow > 1) the screen
// is the sight's field of view, the rest dark. A round one for an optical sight (a tank's, a gun's), centred on the
// screen (the camera's line of sight: what the sight looks along). An electronic sensor (the gunship gunner's, a remote
// weapon station's monitor) is a screen of its own and fills the whole view (the user, 2026-10-25: "武器站改成全屏显示，
// 周围好多黑块"; it was a 4:3 box in black bands): only its bezel and corner marks, a little inside the view's edge.
// The dark is drawn with the HUD's own strip quads (hud.cpp Quad4): the round field's outside as kSides ring pieces
// from the circle out past the screen's farthest corner. Pure arithmetic (no EDF.dll): tools/scopeview_check.cpp
// samples the screen: dark exactly outside the round field, clear inside it; the sensor's frame inside the view.
#pragma once
#include <cmath>

namespace crew {
namespace scopeview {
constexpr int kSides=64;
constexpr float kRound=0.46f;     // the round field's radius: this share of the screen's smaller side
constexpr float kRectInset=0.03f;  // the sensor's bezel: this share of the screen's smaller side in from each edge
constexpr float kPi=3.14159265f;
// The eyepiece's edge (2026-10-11, the sight redone as a real optic): a real exit pupil does not end in a drawn line;
// the image darkens over the last few percent of the field (vignetting) into the black of the eyepiece. kFeathers
// bands from kFeatherIn of the radius to the field's edge, each darker (kFeatherAlpha), under the black outside.
constexpr int kFeathers=4;
constexpr float kFeatherIn=0.925f;
constexpr float kFeatherAlpha[kFeathers]={0.10f,0.24f,0.45f,0.72f};
// Band `b` of the feather: its inner and outer radius as shares of the field's.
constexpr float FeatherFrom(int b) noexcept { return kFeatherIn+(1.0f-kFeatherIn)*static_cast<float>(b)/kFeathers; }
constexpr float FeatherTo(int b) noexcept { return kFeatherIn+(1.0f-kFeatherIn)*static_cast<float>(b+1)/kFeathers; }

// Four corners of a strip quad (hud.cpp's kStrip=5: triangles 0 1 2 and 2 1 3, alternating strip order).
struct Quad { float x[4],y[4]; };

inline float Radius(float w,float h) noexcept { return kRound*(w<h ? w : h); }
// The OUTER POLYGON, not only its vertices, must pass the farthest screen corner. Each straight edge is only
// out*cos(pi/kSides) from the centre; choose the vertex radius so even that edge has four pixels of clearance.
inline float Reach(float w,float h,float cx,float cy) noexcept {
    const float dx=cx>w-cx ? cx : w-cx,dy=cy>h-cy ? cy : h-cy;
    return (std::sqrt(dx*dx+dy*dy)+4.0f)/std::cos(kPi/kSides);
}
// Ring piece `i` of kSides, from radius r out to `out` round (cx, cy).
inline Quad RingPiece(int i,float cx,float cy,float r,float out) noexcept {
    const float a=2.0f*kPi*static_cast<float>(i)/kSides,b=2.0f*kPi*static_cast<float>(i+1)/kSides;
    const float ca=std::cos(a),sa=std::sin(a),cb=std::cos(b),sb=std::sin(b);
    return Quad{{cx+ca*r,cx+ca*out,cx+cb*r,cx+cb*out},{cy+sa*r,cy+sa*out,cy+sb*r,cy+sb*out}};
}
// The sensor's bezel's half size on a w x h screen: the whole view less kRectInset of its smaller side each way (no
// dark band: the picture is the whole screen).
inline void RectHalf(float w,float h,float* hx,float* hy) noexcept {
    const float in=kRectInset*(w<h ? w : h);
    *hx=0.5f*w-in;*hy=0.5f*h-in;
}

// Whether (px, py) is inside the quad (either of its triangles).
inline bool InTri(float ax,float ay,float bx,float by,float cx,float cy,float px,float py) noexcept {
    const float d1=(px-bx)*(ay-by)-(ax-bx)*(py-by),d2=(px-cx)*(by-cy)-(bx-cx)*(py-cy),d3=(px-ax)*(cy-ay)-(cx-ax)*(py-ay);
    const bool neg=d1<0.0f || d2<0.0f || d3<0.0f,pos=d1>0.0f || d2>0.0f || d3>0.0f;
    return !(neg && pos);
}
inline bool Inside(const Quad& q,float px,float py) noexcept {
    return InTri(q.x[0],q.y[0],q.x[1],q.y[1],q.x[2],q.y[2],px,py) || InTri(q.x[2],q.y[2],q.x[1],q.y[1],q.x[3],q.y[3],px,py);
}
}  // namespace scopeview
}  // namespace crew
