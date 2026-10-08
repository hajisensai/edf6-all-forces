// A magnified sight's picture checked offline on landscape, portrait, narrow split screens, and 8K/16K viewports:
//  - the round field: every sampled point of the screen farther than the circle (by 1%) is under a dark piece, every
//    point nearer (by 1%) under none (the pieces' straight inner edges stay within the circle's 1%);
//  - the field is round and centred, inside the screen (its radius the smaller side's kRound);
//  - the sensor fits by uniform scaling: every point outside dark, every point inside clear, always 4:3;
//  - real triangle-strip winding matches hud.cpp Seg/Rect for both triangles, and high-resolution edges have no leaks.
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

double Area(const Quad& q,int a,int b,int c) {
    return (static_cast<double>(q.x[b])-q.x[a])*(static_cast<double>(q.y[c])-q.y[a])-
           (static_cast<double>(q.y[b])-q.y[a])*(static_cast<double>(q.x[c])-q.x[a]);
}

void Winding(const Quad& q) {
    // D3D triangle strips alternate the input ordering: 0,1,2 then 2,1,3. Both must match the HUD's positive Seg area.
    Check(Area(q,0,1,2)>0.0 && Area(q,2,1,3)>0.0,"both strip triangles have the HUD primitive's winding");
}

bool Covered(const Quad* pieces,float x,float y) {
    for(int i=0;i<kSides;++i)if(Inside(pieces[i],x,y))return true;
    return false;
}

void ScreenEdge(float w,float h,float cx,float cy) {
    const float r=Radius(w,h),out=Reach(w,h,cx,cy);
    Quad pieces[kSides];
    for(int i=0;i<kSides;++i)pieces[i]=RingPiece(i,cx,cy,r,out);
    int leaks=0;
    // Include exact corners and pixel centres next to all four edges; sparse interior samples miss small corner wedges.
    for(int i=0;i<=256;++i) {
        const float x=w*static_cast<float>(i)/256.0f,y=h*static_cast<float>(i)/256.0f;
        const float points[][2]={{x,0},{x,h},{0,y},{w,y},{x,0.5f},{x,h-0.5f},{0.5f,y},{w-0.5f,y}};
        for(const auto& p:points) {
            const double dx=static_cast<double>(p[0])-cx,dy=static_cast<double>(p[1])-cy;
            if(dx*dx+dy*dy>static_cast<double>(r)*r*1.0201 && !Covered(pieces,p[0],p[1]))++leaks;
        }
    }
    Check(leaks==0,"screen corners and edge pixels outside the field are shaded",leaks,w);
}

void Round(float w,float h) {
    const float cx=w*0.5f,cy=h*0.5f,r=Radius(w,h),out=Reach(w,h,cx,cy);
    Check(r<=cx && r<=cy && r>0.4f*(w<h ? w : h),"the round field inside the screen",r,w);
    Quad q[kSides];
    for(int i=0;i<kSides;++i){q[i]=RingPiece(i,cx,cy,r,out);Winding(q[i]);}
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
    Check(hx>0.0f && hy>0.0f && hx<=0.48f*w+1e-3f && hy<=kRectHigh*h+1e-3f,
          "sensor fits both viewport dimensions",hx,hy);
    Check(std::fabs(hx/hy-4.0f/3.0f)<1e-5f,"sensor keeps 4:3 even on narrow/portrait screens",hx,hy);
    for(const auto& q:b)Winding(q);
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
    const float screens[][2]={{1920,1080},{2520,1080},{1280,1024},{1920,540},{3840,2160},
                             {960,1080},{1080,1920},{320,1080},{7680,4608},{15360,8640},{8192,1024}};
    for(const auto& sc:screens){Round(sc[0],sc[1]);Sensor(sc[0],sc[1]);ScreenEdge(sc[0],sc[1],sc[0]*0.5f,sc[1]*0.5f);}
    // Reach documents a general centre; the farthest corner need not have the centred viewport's angle.
    ScreenEdge(7680,4320,100,100);
    std::printf(failures ? "scopeview_check: %d of %d FAILED\n" : "scopeview_check: all %d ok\n",failures ? failures : cases,cases);
    return failures ? 1 : 0;
}
