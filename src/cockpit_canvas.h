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

// How much the canvas is stretched round (cx, cy): the square root of the map's area scale there.
inline float Stretch(const Map& k,float cx,float cy) noexcept {
    float x0,y0,x1,y1,x2,y2;
    Point(k,cx,cy,&x0,&y0);Point(k,cx+1.0f,cy,&x1,&y1);Point(k,cx,cy+1.0f,&x2,&y2);
    return std::sqrt(std::fabs((x1-x0)*(y2-y0)-(x2-x0)*(y1-y0)));
}
}  // namespace canvas
}  // namespace crew
