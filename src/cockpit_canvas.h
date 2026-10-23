// A cockpit screen's canvas mapped onto the screen's quad on the view (hud.cpp cockpit_screens.inc): the homography
// from the canvas rectangle (cw x ch, y down) onto four points (top left, top right, bottom right, bottom left), Heckbert's
// square -> quad scaled by the canvas. Straight lines stay straight, so the HUD's quads land on the screen in
// perspective. Pure: tools/cockpit_canvas_check.cpp.
#pragma once
#include <cmath>

namespace crew {
namespace canvas {
struct Map { bool on; float cw,ch; float a,b,c,d,e,f,g,h; };

// The map from a cw x ch canvas onto the quad x/y (TL, TR, BR, BL); false: the quad has no area.
inline bool Onto(Map& k,const float* x,const float* y,float cw,float ch) noexcept {
    const float sx=x[0]-x[1]+x[2]-x[3],sy=y[0]-y[1]+y[2]-y[3];
    if(std::fabs(sx)<1e-4f && std::fabs(sy)<1e-4f) {
        k.a=x[1]-x[0];k.b=x[3]-x[0];k.c=x[0];k.d=y[1]-y[0];k.e=y[3]-y[0];k.f=y[0];k.g=k.h=0.0f;
    } else {
        const float dx1=x[1]-x[2],dx2=x[3]-x[2],dy1=y[1]-y[2],dy2=y[3]-y[2],den=dx1*dy2-dx2*dy1;
        if(std::fabs(den)<1e-6f)return false;
        k.g=(sx*dy2-dx2*sy)/den;k.h=(dx1*sy-sx*dy1)/den;
        k.a=x[1]-x[0]+k.g*x[1];k.b=x[3]-x[0]+k.h*x[3];k.c=x[0];
        k.d=y[1]-y[0]+k.g*y[1];k.e=y[3]-y[0]+k.h*y[3];k.f=y[0];
    }
    k.cw=cw;k.ch=ch;
    const float area=(x[1]-x[0])*(y[3]-y[0])-(x[3]-x[0])*(y[1]-y[0]);
    return std::isfinite(k.a+k.b+k.c+k.d+k.e+k.f+k.g+k.h) && std::fabs(area)>1e-6f;
}

inline void Point(const Map& k,float cx,float cy,float* px,float* py) noexcept {
    const float u=cx/k.cw,v=cy/k.ch,q=k.g*u+k.h*v+1.0f;
    *px=(k.a*u+k.b*v+k.c)/q;*py=(k.d*u+k.e*v+k.f)/q;
}

// --- A head-up glass (a 'hud' screen of a physical cockpit: the F-16's combiner, a helmet's display, the C-130J's HUD).
// Its symbols are conformal: a world direction from the eye is drawn where the line of sight along it crosses the
// glass, so through the glass the drawn horizon lies on the world's (the user, 2026-10-15: "要么是模型上的hud").
// The glass: its centre `off` from the eye and its unit axes `right`, `up` (world), w x h metres, its canvas cw x ch
// (y down), as firstperson.h CockpitScreen and cockpit_screens.inc lay a screen out.
struct Glass { float off[3],right[3],up[3],w,h,cw,ch; };

// The canvas point where the line of sight along world direction `d` (from the eye) crosses the glass's plane; false:
// along it the plane is behind the eye or edge on.
inline bool Through(const Glass& g,const float* d,float* x,float* y) noexcept {
    const float n[3]={g.right[1]*g.up[2]-g.right[2]*g.up[1],g.right[2]*g.up[0]-g.right[0]*g.up[2],
                      g.right[0]*g.up[1]-g.right[1]*g.up[0]};
    const float den=n[0]*d[0]+n[1]*d[1]+n[2]*d[2],num=n[0]*g.off[0]+n[1]*g.off[1]+n[2]*g.off[2];
    if(!(std::fabs(den)>1e-6f) || !(g.w>0.0f && g.h>0.0f))return false;
    const float t=num/den;
    if(!(t>0.0f))return false;
    float rel[3];
    for(int i=0;i<3;++i)rel[i]=d[i]*t-g.off[i];
    const float u=rel[0]*g.right[0]+rel[1]*g.right[1]+rel[2]*g.right[2],v=rel[0]*g.up[0]+rel[1]*g.up[1]+rel[2]*g.up[2];
    *x=(u/g.w+0.5f)*g.cw;*y=(0.5f-v/g.h)*g.ch;
    return std::isfinite(*x+*y);
}

// The segment (ax, ay)-(bx, by) cut to the rectangle lx..hx, ly..hy (Liang-Barsky); false: wholly outside.
inline bool Clip(float& ax,float& ay,float& bx,float& by,float lx,float ly,float hx,float hy) noexcept {
    const float dx=bx-ax,dy=by-ay;
    float enter=0.0f,leave=1.0f;
    auto edge=[&](float p,float q) {
        if(std::fabs(p)<1e-9f)return q>=0.0f;
        const float r=q/p;
        if(p<0.0f){if(r>leave)return false;if(r>enter)enter=r;}
        else{if(r<enter)return false;if(r<leave)leave=r;}
        return true;
    };
    if(!edge(-dx,ax-lx) || !edge(dx,hx-ax) || !edge(-dy,ay-ly) || !edge(dy,hy-ay))return false;
    bx=ax+dx*leave;by=ay+dy*leave;ax+=dx*enter;ay+=dy*enter;
    return true;
}

// The level frame the ladder goes by: `ref` (the flight path, else the nose) flattened, its level right (the HUD's
// Ladder: playerjet.cpp RightOf); false: straight up or down.
inline bool Level(const float* ref,float* h,float* r) noexcept {
    const float len=std::sqrt(ref[0]*ref[0]+ref[2]*ref[2]);
    if(!(len>1e-4f))return false;
    h[0]=ref[0]/len;h[1]=0.0f;h[2]=ref[2]/len;
    r[0]=-h[2];r[1]=0.0f;r[2]=h[0];
    return true;
}

// The world direction `elevation` degrees over the level `h`, `aside` radians along its level right `r`.
inline void Toward(const float* h,const float* r,float elevation,float aside,float* d) noexcept {
    const float e=elevation*0.01745329252f,ce=std::cos(e),se=std::sin(e),ca=std::cos(aside),sa=std::sin(aside);
    for(int i=0;i<3;++i)d[i]=ce*(h[i]*ca+r[i]*sa);
    d[1]+=se;
}

// The bank scale's pointer on the glass (canvas angle: 0 to the right, growing clockwise, y down): where the world's
// down is with the aircraft banked `bank` degrees (right wing down positive): straight down level, turned toward the
// right in a right bank, square to the drawn horizon (Through) whatever the bank.
inline float DownOnGlass(float bank) noexcept { return 1.5707963f-bank*0.01745329252f; }

// How much the canvas is stretched round (cx, cy): the square root of the map's area scale there.
inline float Stretch(const Map& k,float cx,float cy) noexcept {
    float x0,y0,x1,y1,x2,y2;
    Point(k,cx,cy,&x0,&y0);Point(k,cx+1.0f,cy,&x1,&y1);Point(k,cx,cy+1.0f,&x2,&y2);
    return std::sqrt(std::fabs((x1-x0)*(y2-y0)-(x2-x0)*(y1-y0)));
}
}  // namespace canvas
}  // namespace crew
