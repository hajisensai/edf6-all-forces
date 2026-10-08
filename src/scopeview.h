// A magnified sight's picture (the user, 2026-10-08: "the gunner's sight: the zoomed one too, and not only ground
// vehicles: everything that has a sight"): while the sight is magnified (sightzoom.cpp, SightZoomNow > 1) the screen
// is the sight's field of view, the rest dark. A round one for an optical sight (a tank's, a gun's), centred on the
// screen (the camera's line of sight: what the sight looks along); a rectangle (4:3) for the gunship gunner's sensor.
// The dark is drawn with the HUD's own strip quads (hud.cpp Quad4): the round field's outside as kSides ring pieces
// from the circle out past the screen's farthest corner, the rectangle's as four bands. Pure arithmetic (no EDF.dll):
// tools/scopeview_check.cpp samples the screen: dark exactly outside the field, clear inside it.
#pragma once
#include <cmath>

namespace crew {
namespace scopeview {
constexpr int kSides=64;
constexpr float kRound=0.46f;     // the round field's radius: this share of the screen's smaller side
constexpr float kRectHigh=0.40f;  // the sensor's half height: this share of the screen's height (its width 4:3, held to the screen)
constexpr float kPi=3.14159265f;

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
// The sensor's half size on a w x h screen.
inline void RectHalf(float w,float h,float* hx,float* hy) noexcept {
    *hy=kRectHigh*h;
    *hx=*hy*4.0f/3.0f;
    if(*hx>0.48f*w){*hx=0.48f*w;*hy=*hx*3.0f/4.0f;} // fit narrow/split-screen viewports without squeezing the sensor
}
// The four dark bands round the sensor's field (above, under, left, right).
inline void RectBands(float w,float h,Quad* out) noexcept {
    float hx,hy;
    RectHalf(w,h,&hx,&hy);
    const float cx=w*0.5f,cy=h*0.5f,l=cx-hx,r=cx+hx,t=cy-hy,b=cy+hy;
    const auto box=[](float x0,float y0,float x1,float y1){return Quad{{x0,x1,x0,x1},{y0,y0,y1,y1}};};
    out[0]=box(0.0f,0.0f,w,t);out[1]=box(0.0f,b,w,h);out[2]=box(0.0f,t,l,b);out[3]=box(r,t,w,b);
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
