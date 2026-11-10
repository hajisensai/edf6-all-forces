// The picture-in-picture scope's arithmetic (src/scope_pip.h), offline:
//  - InfantryScope's values: 0 stock, 1 the whole screen, 2 (and above) the picture in the picture, the default 2;
//  - the lens shows exactly the stock zoomed camera's angles: the lens view (viewport 3, docs/scope-pip-re.md §5) at
//    LensViewFov puts a point at angle a off the axis where the stock zoom would have put it, whatever the frame's
//    resolution or aspect; LensShownFov gives the scope's FOV back from the lens view's projection; the focal ratio
//    (Magnification, LensPower) is a little more than the scope's power;
//  - a frame the stock zoom already rendered (the camera step not yet un-zoomed) gets no lens (power 1);
//  - the lens view's side is the lens's diameter in px (1:1, so its clarity does not drop with the power);
//  - the camera step unzooms only the stock step's own write, and only while the lens can draw (KeepUnzoomed);
//  - the lens and its rim stay inside every view shape (landscape, portrait, split screens), smaller than the
//    full-screen eyepiece's field (scopeview.h), its scissor covers it, LensSample spans the lens view across the disc.
//   cmake --build build --target scope_pip_check && build\scope_pip_check.exe      (exit code 1 on a failure)
#include "../src/scope_pip.h"
#include "../src/scopeview.h"
#include <cmath>
#include <cstdio>
#include <initializer_list>

namespace {
using namespace crew::scopepip;
int failures=0,cases=0;
void Check(bool ok,const char* what,double a=0.0,double b=0.0) {
    ++cases;
    if(ok)return;
    ++failures;
    std::printf("FAIL %s (%g, %g)\n",what,a,b);
}
bool Near(double a,double b,double tol) { return std::fabs(a-b)<=tol; }
}  // namespace

int main() {
    // Modes.
    Check(ModeOf(0)==Mode::stock && ModeOf(-3)==Mode::stock,"0 and below: the stock scope");
    Check(ModeOf(1)==Mode::fullScreen,"1: the whole screen zoomed under the eyepiece");
    Check(ModeOf(2)==Mode::pictureInPicture && ModeOf(9)==Mode::pictureInPicture,"2 and above: the picture in the picture");
    Check(ModeOf(kModeDefault)==Mode::pictureInPicture && kModeLowest==0 && kModeHighest==2,"the default is the picture in the picture");

    // The lens shows the stock zoom's angles.
    const float base=YScale(kBaseFov);
    for(float zoom:{1.5f,2.0f,4.0f,5.5f,8.0f,12.0f}) {
        const float m=Magnification(base,ScopeFov(zoom));
        Check(m>zoom && m<zoom*1.1f,"the lens magnifies a little more than the power (focal ratio, not angle ratio)",m,zoom);
        Check(Near(Magnification(YScale(ScopeFov(zoom)),ScopeFov(zoom)),1.0,1e-4),"a frame the stock zoom rendered: no lens");
        for(float h:{720.0f,1080.0f,2160.0f}) {
            // The base frame's focal times the magnification is the stock zoomed frame's focal: the reticle's marks
            // (angles at LensFocal) are the same size as on the full-screen scope.
            const float baseFocal=0.5f*h*base;
            Check(Near(baseFocal*m,LensFocal(h,ScopeFov(zoom)),1e-2*h/1080.0f),"lens focal = base focal x magnification",baseFocal*m,LensFocal(h,ScopeFov(zoom)));
            Check(Near(LensFocal(h,ScopeFov(zoom)),0.5f*h/std::tan(0.5f*kBaseFov/zoom),1e-3*h),"lens focal = the stock zoomed camera's (hud.cpp's fallback)");
            // A point 0.5 deg off the axis: where the lens view draws it (its own focal r * cot(phi / 2) across the
            // disc), mapped onto the screen by LensSample, is where the stock zoom puts it.
            const float a=0.5f*3.14159265f/180.0f;
            for(float aspect:{16.0f/9.0f,21.0f/9.0f,4.0f/3.0f,9.0f/16.0f}) {
                const float w=h*aspect;
                const Lens l=LensFor(w,h);
                const float phi=LensViewFov(h,l.r,ScopeFov(zoom));
                Check(phi>0.0f && phi<ScopeFov(zoom)*1.01f,"the lens view's field is narrower than the scope's own",phi,ScopeFov(zoom));
                // The lens view's texture coordinate of the point (0.5 + tan(a) * cot(phi/2) / 2), back onto the screen.
                const float u=0.5f+0.5f*std::tan(a)/std::tan(0.5f*phi);
                const float x=l.cx+(u-0.5f)*2.0f*l.r;
                Check(Near(x,l.cx+LensFocal(h,ScopeFov(zoom))*std::tan(a),0.05),"an angle lands where the stock zoom puts it",x,
                      l.cx+LensFocal(h,ScopeFov(zoom))*std::tan(a));
                float su,sv;LensSample(l,x,l.cy,&su,&sv);
                Check(Near(su,u,1e-4) && Near(sv,0.5,1e-6),"LensSample is the lens view's coordinate there",su,u);
                const float lensY=YScale(phi);
                Check(Near(LensShownFov(h,l.r,lensY),ScopeFov(zoom),1e-5),"LensShownFov gives the scope's FOV back",LensShownFov(h,l.r,lensY),ScopeFov(zoom));
                Check(Near(LensPower(h,l.r,base,lensY),m,1e-3*m),"the lens's power over an unmagnified frame is the focal ratio",LensPower(h,l.r,base,lensY),m);
                Check(Near(LensPower(h,l.r,YScale(ScopeFov(zoom)),lensY),1.0,1e-4),"over a stock-zoomed frame: none");
                Check(LensViewSide(l.r)>=2.0f*l.r && LensViewSide(l.r)<2.0f*l.r+1.0f,"the lens view's side is the lens's diameter",LensViewSide(l.r),2.0f*l.r);
            }
        }
    }
    Check(Magnification(0.0f,ScopeFov(4.0f))==0.0f && Magnification(NAN,ScopeFov(4.0f))==0.0f &&
          Magnification(base,0.0f)==0.0f && Magnification(base,3.2f)==0.0f,"unusable projections give no magnification");
    Check(ScopeFov(0.0f)==kBaseFov,"no power: the base FOV");
    Check(LensViewFov(0.0f,10.0f,0.2f)==0.0f && LensViewFov(1080.0f,0.0f,0.2f)==0.0f && LensViewFov(1080.0f,300.0f,3.2f)==0.0f &&
          LensShownFov(1080.0f,300.0f,0.0f)==0.0f && LensShownFov(1080.0f,300.0f,NAN)==0.0f && LensPower(1080.0f,300.0f,0.0f,4.0f)==0.0f &&
          LensViewSide(0.0f)==0 && LensViewSide(NAN)==0 && LensViewSide(1.0e5f)==0,"unusable lens views give nothing");
    Check(kLensViewport==3,"the lens view is the engine's fourth viewport");

    // The lens's geometry on every view shape.
    const float shapes[][2]={{1920,1080},{3840,2160},{2560,1080},{1080,1920},{1920,540},{960,1080},{1280,1024},{7680,4320}};
    for(const auto& s:shapes) {
        const float w=s[0],h=s[1];
        const Lens l=LensFor(w,h);
        Check(l.cx==0.5f*w && l.cy==0.5f*h,"the lens is centred on the view (the camera's line of sight)");
        Check(RimOut(l.r)<0.5f*(w<h ? w : h),"the rim stays inside the view",RimOut(l.r),0.5f*(w<h ? w : h));
        Check(l.r<crew::scopeview::Radius(w,h,true),"the lens is smaller than the full-screen eyepiece's field");
        Check(l.r>0.2f*(w<h ? w : h),"the lens is big enough to aim through");
        float u,v;LensSample(l,l.cx,l.cy,&u,&v);
        Check(u==0.5f && v==0.5f,"the centre shows the lens view's centre (the aim point does not move)");
        Check(InLens(l,l.cx+l.r*0.99f,l.cy) && !InLens(l,l.cx+l.r*1.01f,l.cy),"InLens is the circle");
        long left,top,right,bottom;Scissor(l,w,h,&left,&top,&right,&bottom);
        Check(left<=l.cx-l.r && right>=l.cx+l.r && top<=l.cy-l.r && bottom>=l.cy+l.r,"the scissor covers the lens");
        Check(left>=0 && top>=0 && right<=static_cast<long>(w) && bottom<=static_cast<long>(h),"the scissor is inside the target");
        // The lens's edge samples the lens view's edge, inside its texture.
        for(int k=0;k<32;++k) {
            const float t=6.2831853f*k/32;
            LensSample(l,l.cx+l.r*std::cos(t),l.cy+l.r*std::sin(t),&u,&v);
            Check(u>=-1e-5f && u<=1.00001f && v>=-1e-5f && v<=1.00001f && Near(std::hypot(u-0.5f,v-0.5f),0.5,1e-4),
                  "the lens's edge samples the lens view's inscribed circle");
        }
    }
    // The camera step: only the stock step's own zoomed FOV goes back to pi/4.
    for(float zoom:{1.5f,4.0f,8.0f,12.0f})
        Check(Unzoomed(kBaseFov/zoom,zoom)==kBaseFov,"the stock step's zoomed FOV is unzoomed to pi/4",zoom);
    Check(Unzoomed(0.3f,4.0f)==0.0f,"a FOV another logic wrote (the close-up's easing) is left alone");
    Check(Unzoomed(kBaseFov,1.0f)==0.0f && Unzoomed(kBaseFov/4.0f,0.0f)==0.0f && Unzoomed(NAN,4.0f)==0.0f,
          "no zoom, no camera zoom, a broken FOV: nothing written");
    // When the camera may keep the frame unmagnified.
    const unsigned long long t=100000;
    Check(KeepUnzoomed(true,true,false,t,t,0),"the scope just opened: the probation, before any lens drew");
    Check(KeepUnzoomed(true,true,false,t+kProbationMs,t,0) && !KeepUnzoomed(true,true,false,t+kProbationMs+1,t,0),
          "the probation ends: no lens drawn, the stock zoom");
    Check(KeepUnzoomed(true,true,false,t+10000,t,t+10000-kLensFreshMs) && KeepUnzoomed(true,true,false,t+10000,t,t),
          "past the probation the lens keeps it once it drew in this opening, however long ago (a pause, a hitch)");
    Check(!KeepUnzoomed(true,true,false,t+10000,t,t-1),"a lens drawn before this opening proves nothing");
    Check(!KeepUnzoomed(false,true,false,t,t,t) && !KeepUnzoomed(true,false,false,t,t,t) && !KeepUnzoomed(true,true,true,t,t,t),
          "not wanted, the world's end not hooked, the surface refused: the stock zoom");
    Check(!KeepUnzoomed(true,true,false,t,0,0) && !LensDrew(t,0) && !LensDrew(t,t+1),"nothing opened, no lens, a clock gone back");
    for(int b=0;b<kFeathers;++b)
        Check(FeatherFrom(b)<FeatherTo(b) && FeatherTo(b)<=1.0f && (b==0 || kFeatherAlpha[b]>kFeatherAlpha[b-1]),"the vignette darkens outward");

    std::printf("scope_pip_check: %d cases, %d failures\n",cases,failures);
    return failures ? 1 : 0;
}
