// A magnified sight's picture (src/scopeview.h) checked offline, on 16:9, 21:9, 5:4, a split screen's half and 4K:
//  - the round field: every sampled point of the screen farther than the circle (by 1%) is under a dark piece, every
//    point nearer (by 1%) under none (the pieces' straight inner edges stay within the circle's 1%);
//  - the field is round and centred, inside the screen (its radius the smaller side's kRound);
//  - the sensor's rectangle: every point outside it dark, every point inside clear; 4:3 unless the screen is narrower.
//   cmake --build build --target scopeview_check && build\scopeview_check.exe      (exit code 1 on a failure)
#include "../src/scopeview.h"
#include <cstdio>

namespace {
using namespace crew::scopeview;
int failures=0,cases=0;
void Check(bool ok,const char* what,double a=0.0,double b=0.0) {
    ++cases;
    if(ok)return;
    ++failures;
    std::printf("FAIL %s (%g, %g)\n",what,a,b);
}

void Round(float w,float h) {
    const float cx=w*0.5f,cy=h*0.5f,r=Radius(w,h),out=Reach(w,h,cx,cy);
    Check(r<=cx && r<=cy && r>0.4f*(w<h ? w : h),"the round field inside the screen",r,w);
    Quad q[kSides];
    for(int i=0;i<kSides;++i)q[i]=RingPiece(i,cx,cy,r,out);
    int wrongDark=0,wrongClear=0;
    for(float y=0.0f;y<=h;y+=h/97.0f)
        for(float x=0.0f;x<=w;x+=w/151.0f) {
            const float d=std::sqrt((x-cx)*(x-cx)+(y-cy)*(y-cy));
            bool dark=false;
            for(const Quad& p:q)dark=dark || Inside(p,x,y);
            if(d>r*1.01f && !dark)++wrongClear;
            if(d<r*0.99f && dark)++wrongDark;
        }
    // The corners themselves.
    const float corner[4][2]={{0.0f,0.0f},{w,0.0f},{0.0f,h},{w,h}};
    for(const auto& c:corner) {
        bool dark=false;
        for(const Quad& p:q)dark=dark || Inside(p,c[0],c[1]);
        wrongClear+=!dark;
    }
    Check(wrongClear==0,"outside the circle: dark everywhere",wrongClear,w);
    Check(wrongDark==0,"inside the circle: clear",wrongDark,w);
}

void Sensor(float w,float h) {
    Quad b[4];
    RectBands(w,h,b);
    float hx,hy;
    RectHalf(w,h,&hx,&hy);
    Check(hx<=0.48f*w+1e-3f && (std::fabs(hx-hy*4.0f/3.0f)<1e-3f || hx<hy*4.0f/3.0f),"the sensor 4:3, held to the screen",hx,hy);
    int wrong=0;
    const float cx=w*0.5f,cy=h*0.5f;
    for(float y=0.5f;y<h;y+=h/97.0f)
        for(float x=0.5f;x<w;x+=w/151.0f) {
            const bool in=std::fabs(x-cx)<hx-0.5f && std::fabs(y-cy)<hy-0.5f,outside=std::fabs(x-cx)>hx+0.5f || std::fabs(y-cy)>hy+0.5f;
            bool dark=false;
            for(const Quad& p:b)dark=dark || Inside(p,x,y);
            if((in && dark) || (outside && !dark))++wrong;
        }
    Check(wrong==0,"the sensor: dark outside, clear inside",wrong,w);
}
}  // namespace

int main() {
    const float screens[][2]={{1920.0f,1080.0f},{2520.0f,1080.0f},{1280.0f,1024.0f},{1920.0f,540.0f},{3840.0f,2160.0f}};
    for(const auto& sc:screens){Round(sc[0],sc[1]);Sensor(sc[0],sc[1]);}
    std::printf(failures ? "scopeview_check: %d of %d FAILED\n" : "scopeview_check: all %d ok\n",failures ? failures : cases,cases);
    return failures ? 1 : 0;
}
