// World points on the screen: the one clip transform every world-to-screen projection of the plugin's HUD, sights,
// world text and map commands uses, and the picture in the picture's two views of the world on one screen.
//
// The view-projection `vp` is the camera's row-vector matrix as the gauge drawer 0x804300 gets it (docs/hud-re.md §0):
// clip[k] = x*vp[k] + y*vp[4+k] + z*vp[8+k] + w*vp[12+k] (w = 1 a point, 0 a direction).
//
// The scope's picture in the picture (InfantryScope=2, scope_pip.h, docs/scope-pip-re.md §3): the frame is the
// camera's unmagnified view (the HUD pass's `vp`); the lens round the screen's centre (radius r) is viewport 3's
// render, the same camera's world matrix (scope_lens.cpp Aim copies it) at the lens's narrow field, its NDC +-1 drawn
// on +-r px (scope_pip.h LensSample). The same view with a longer focal: a direction `d` px off the centre in the frame
// is `k d` px off it in the lens, k = the lens's focal over the frame's (scope_pip.h LensPower). So on the screen:
//  - a direction under r / k px from the centre shows in the lens, k times as far out;
//  - one beyond the scope's tube (RimOut(r)) shows in the frame where the frame's view has it;
//  - one between is seen nowhere (the lens shows other directions there, the tube is black).
// A world line is straight in both views (both perspective, one eye): its frame projection is cut by the two circles,
// the inner piece scaled by k round the centre. 2026-11-27 (the user: 「开镜以后线会分离」): the HUD's world lines were
// drawn through the frame's view over the lens too, off the magnified world under them.
#pragma once
#include <cmath>

namespace crew {
namespace screenview {
// The clip coordinates of `p` (w 1: a point; 0: a direction) through `vp`.
inline void Clip(const float* vp,const float* p,float w,float* c) noexcept {
    for(int k=0;k<4;++k)c[k]=p[0]*vp[k]+p[1]*vp[4+k]+p[2]*vp[8+k]+w*vp[12+k];
}
// Normalized device x, y to viewport pixels (y down).
inline void Pixel(float nx,float ny,float width,float height,float* sx,float* sy) noexcept {
    *sx=0.5f*width*(1.0f+nx);*sy=0.5f*height*(1.0f-ny);
}

// The lens over the frame (pixels): its centre, its radius, the scope's tube's outer radius, its power over the frame.
// k 0: no lens (the whole screen is one view).
struct Lens {
    float cx=0.0f,cy=0.0f,r=0.0f,rim=0.0f,k=0.0f;
    bool On() const noexcept { return k>1.0f && r>0.0f && rim>=r && std::isfinite(k); }
};
// The lens of radius `r` (tube out to `rim`) at the screen's centre, showing the world at focal `lensFocal` px
// (scope_pip.h LensFocal of the lens's shown FOV) over a frame of focal `frameFocal` px. No lens when either is unusable
// or it does not magnify.
inline Lens LensOf(float width,float height,float r,float rim,float lensFocal,float frameFocal) noexcept {
    Lens l{};
    if(!(width>0.0f) || !(height>0.0f) || !(r>0.0f) || !(rim>=r) || !(lensFocal>0.0f) || !(frameFocal>0.0f))return l;
    const float k=lensFocal/frameFocal;
    if(!(k>1.0f) || !std::isfinite(k))return l;
    l.cx=0.5f*width;l.cy=0.5f*height;l.r=r;l.rim=rim;l.k=k;
    return l;
}

enum class Shown { frame, lens, hidden };
// Where the direction the frame's view puts at (x, y) shows on the screen: moved into the lens (scaled by k round its
// centre), kept in the frame, or hidden by the scope's tube.
inline Shown Show(const Lens& l,float* x,float* y) noexcept {
    if(!l.On())return Shown::frame;
    const float dx=*x-l.cx,dy=*y-l.cy,d2=dx*dx+dy*dy;
    if(d2*l.k*l.k<l.r*l.r){*x=l.cx+l.k*dx;*y=l.cy+l.k*dy;return Shown::lens;}
    return d2>l.rim*l.rim ? Shown::frame : Shown::hidden;
}

// The parameters [t0, t1] (t0 < t1) where the segment a + t (b - a) is within `radius` of (cx, cy); false: never.
inline bool Within(float ax,float ay,float bx,float by,float cx,float cy,float radius,float* t0,float* t1) noexcept {
    const float dx=bx-ax,dy=by-ay,fx=ax-cx,fy=ay-cy;
    const float a=dx*dx+dy*dy,b=2.0f*(dx*fx+dy*fy),c=fx*fx+fy*fy-radius*radius;
    if(!(a>1e-12f))return false;   // a point: no length to draw
    const float disc=b*b-4.0f*a*c;
    if(!(disc>0.0f))return false;
    const float root=std::sqrt(disc);
    *t0=(-b-root)/(2.0f*a);*t1=(-b+root)/(2.0f*a);
    return *t0<*t1;
}
constexpr int kMostPieces=3;   // the frame's before the tube, the lens's, the frame's after it
// The pieces {x0, y0, x1, y1} of the frame view's screen segment (ax, ay)-(bx, by) as the screen shows them (see the
// top): with no lens the segment itself. Returns how many (0..kMostPieces).
inline int Pieces(const Lens& l,float ax,float ay,float bx,float by,float (*out)[4]) noexcept {
    auto put=[&](int n,float from,float to,float scale) noexcept {
        const float dx=bx-ax,dy=by-ay;
        const float x0=ax+dx*from,y0=ay+dy*from,x1=ax+dx*to,y1=ay+dy*to;
        out[n][0]=l.cx+(x0-l.cx)*scale;out[n][1]=l.cy+(y0-l.cy)*scale;
        out[n][2]=l.cx+(x1-l.cx)*scale;out[n][3]=l.cy+(y1-l.cy)*scale;
    };
    if(!l.On()){out[0][0]=ax;out[0][1]=ay;out[0][2]=bx;out[0][3]=by;return 1;}
    int n=0;
    float t0=0.0f,t1=0.0f;
    // The frame's: outside the tube.
    if(!Within(ax,ay,bx,by,l.cx,l.cy,l.rim,&t0,&t1)) {
        // The line misses the tube's circle (or is a point): wholly outside it, unless it is a point inside.
        const float dx=ax-l.cx,dy=ay-l.cy;
        if(dx*dx+dy*dy>l.rim*l.rim)put(n++,0.0f,1.0f,1.0f);
    } else if(t1<=0.0f || t0>=1.0f) {
        put(n++,0.0f,1.0f,1.0f);   // the line crosses the circle beyond the segment's ends
    } else {
        if(t0>0.0f)put(n++,0.0f,t0,1.0f);
        if(t1<1.0f)put(n++,t1,1.0f,1.0f);
    }
    // The lens's: within r / k of the centre, k times as far out.
    if(Within(ax,ay,bx,by,l.cx,l.cy,l.r/l.k,&t0,&t1) && t1>0.0f && t0<1.0f)
        put(n++,t0>0.0f ? t0 : 0.0f,t1<1.0f ? t1 : 1.0f,l.k);
    return n;
}
}  // namespace screenview
}  // namespace crew
