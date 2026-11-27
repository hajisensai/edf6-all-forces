// The HUD's world points and lines in the scope's picture in the picture (src/screen_view.h), offline, against the two
// real views the screen shows (2026-11-27, the user: 「开镜以后线会分离」, a support route drawn through the frame's view
// straight across the lens):
//  - the frame: the camera's unmagnified view-projection (the HUD pass's viewProj, pi/4, the screen's aspect);
//  - the lens: viewport 3's camera (scope_lens.cpp Aim: viewport 0's camera's world matrix, the lens's field
//    scope_pip.h LensViewFov, square), its NDC +-1 drawn on +-r px round the screen's centre (scope_pip.h LensSample).
// Every world point is projected through the view that really shows it there and compared with screenview::Show of
// its frame projection; a world line's samples must lie on the pieces screenview::Pieces gives, and none of them on
// the old single frame segment inside the lens. The shared clip transform (screenview::Clip / Pixel) is the one the
// HUD, the sights (sight.h), the world text (world_text.h) and the map commands (mapcmd_logic.h) project with.
#include "../src/screen_view.h"
#include "../src/scope_pip.h"
#include "../src/sight.h"
#include "../src/world_text.h"
#include "../src/mapcmd_logic.h"
#include <cmath>
#include <initializer_list>
#include <cstdio>

namespace {
using namespace crew;
int checks=0,failures=0;
void Check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}}
bool Near(float a,float b,float tol){return std::fabs(a-b)<=tol;}

constexpr float kW=2000.0f,kH=1125.0f,kNear=0.5f,kFar=4000.0f;
// Row-vector view-projection (clip = [x y z 1] * M, M row-major as the HUD's vp): a camera at `eye` with the unit basis
// right / up / forward (left-handed: forward is +z in view), a perspective of vertical scale ys and horizontal xs.
void ViewProj(const float* eye,const float* right,const float* up,const float* fwd,float xs,float ys,float* out){
    float v[16]{};
    for(int i=0;i<3;++i){v[i*4+0]=right[i];v[i*4+1]=up[i];v[i*4+2]=fwd[i];}
    auto dot=[&](const float* a){return eye[0]*a[0]+eye[1]*a[1]+eye[2]*a[2];};
    v[12]=-dot(right);v[13]=-dot(up);v[14]=-dot(fwd);v[15]=1.0f;
    float p[16]{};
    p[0]=xs;p[5]=ys;p[10]=kFar/(kFar-kNear);p[11]=1.0f;p[14]=-kNear*kFar/(kFar-kNear);
    for(int r=0;r<4;++r)for(int c=0;c<4;++c){float s=0;for(int k=0;k<4;++k)s+=v[r*4+k]*p[k*4+c];out[r*4+c]=s;}
}
struct Rig {
    float frame[16],lens[16];
    float r,rim,yFrame,yLens;
    screenview::Lens view;
};
// The scoped soldier's camera (looking a little down and to the side, as a real view is) and the lens the PiP renders
// at power `zoom`.
Rig MakeRig(float zoom){
    Rig g{};
    const float eye[3]={120.0f,35.0f,-60.0f};
    const float yaw=0.4f,pitch=-0.12f;
    const float fwd[3]={std::sin(yaw)*std::cos(pitch),std::sin(pitch),std::cos(yaw)*std::cos(pitch)};
    const float right[3]={std::cos(yaw),0.0f,-std::sin(yaw)};
    const float up[3]={fwd[1]*right[2]-fwd[2]*right[1],fwd[2]*right[0]-fwd[0]*right[2],fwd[0]*right[1]-fwd[1]*right[0]};
    g.yFrame=scopepip::YScale(scopepip::kBaseFov);
    ViewProj(eye,right,up,fwd,g.yFrame*kH/kW,g.yFrame,g.frame);
    g.r=scopepip::LensRadius(kW,kH);g.rim=scopepip::RimOut(g.r);
    const float lensFov=scopepip::LensViewFov(kH,g.r,scopepip::ScopeFov(zoom));
    g.yLens=scopepip::YScale(lensFov);
    ViewProj(eye,right,up,fwd,g.yLens,g.yLens,g.lens);   // square
    // As hud_scope.cpp ScopeLensView: the lens's shown focal (the reticle's) over the frame's centre focal.
    const float shownFov=scopepip::LensShownFov(kH,g.r,g.yLens);
    g.view=screenview::LensOf(kW,kH,g.r,g.rim,scopepip::LensFocal(kH,shownFov),0.5f*kH*g.yFrame);
    return g;
}
bool FramePixel(const Rig& g,const float* p,float* x,float* y){
    float c[4];screenview::Clip(g.frame,p,1.0f,c);
    if(!(c[3]>1e-6f))return false;
    screenview::Pixel(c[0]/c[3],c[1]/c[3],kW,kH,x,y);
    return true;
}
// Where the composited lens shows `p` (LensSample inverted), false when not in the lens's circle.
bool LensPixel(const Rig& g,const float* p,float* x,float* y){
    float c[4];screenview::Clip(g.lens,p,1.0f,c);
    if(!(c[3]>1e-6f))return false;
    const float nx=c[0]/c[3],ny=c[1]/c[3];
    if(nx*nx+ny*ny>1.0f)return false;
    *x=0.5f*kW+nx*g.r;*y=0.5f*kH-ny*g.r;
    return true;
}
// A world point straight down the camera's view at `ahead` m, offset `dx`, `dy` pixels-worth in the frame.
void PointAt(const Rig& g,float fx,float fy,float ahead,float* out){
    // Invert the frame projection along the ray at frame pixel (fx, fy), depth `ahead` (view z).
    float inv[16];mapcmd::Invert4(g.frame,inv);
    const float nx=(fx-0.5f*kW)/(0.5f*kW),ny=(0.5f*kH-fy)/(0.5f*kH);
    const float zc=kFar/(kFar-kNear)-kNear*kFar/((kFar-kNear)*ahead);
    const float h[4]={nx,ny,zc,1.0f};
    float w[4];
    for(int k=0;k<4;++k)w[k]=h[0]*inv[k]+h[1]*inv[4+k]+h[2]*inv[8+k]+h[3]*inv[12+k];
    for(int i=0;i<3;++i)out[i]=w[i]/w[3];
}
// The distance from (x, y) to the segment (a piece).
float ToPiece(const float* s,float x,float y){
    const float dx=s[2]-s[0],dy=s[3]-s[1],l2=dx*dx+dy*dy;
    float t=l2>0.0f ? ((x-s[0])*dx+(y-s[1])*dy)/l2 : 0.0f;
    t=t<0.0f ? 0.0f : t>1.0f ? 1.0f : t;
    const float ex=s[0]+dx*t-x,ey=s[1]+dy*t-y;
    return std::sqrt(ex*ex+ey*ey);
}

void LensPower(){
    for(float zoom:{2.0f,4.0f,8.0f}) {
        const Rig g=MakeRig(zoom);
        Check(g.view.On(),"the lens is on at every power");
        // The HUD's power is the composite's (scope_pip_gpu.cpp Composite's LensPower) and the stock zoom's.
        Check(Near(g.view.k,scopepip::LensPower(kH,g.r,g.yFrame,g.yLens),1e-3f*g.view.k),"k is the lens's power over the frame");
        const float stock=scopepip::Magnification(g.yFrame,scopepip::ScopeFov(zoom));
        Check(Near(g.view.k,stock,1e-3f*stock),"k is the stock zoomed frame's magnification");
    }
    Check(!screenview::LensOf(kW,kH,100.0f,116.0f,500.0f,500.0f).On(),"no magnification: no lens");
    Check(!screenview::LensOf(kW,kH,100.0f,116.0f,500.0f,0.0f).On(),"no frame focal: no lens");
    Check(!screenview::Lens{}.On(),"default: no lens");
}

void Points(){
    for(float zoom:{2.5f,6.0f}) {
        const Rig g=MakeRig(zoom);
        int inLens=0,inFrame=0,hidden=0;
        for(float step:{48.0f,4.0f})for(int i=-20;i<=20;++i)for(int j=-12;j<=12;++j) {   // the screen, then round the lens
            float fx=0.5f*kW+i*step,fy=0.5f*kH+j*step*0.94f,p[3];
            PointAt(g,fx,fy,300.0f+17.0f*(i+j+40),p);
            float x,y;
            if(!FramePixel(g,p,&x,&y))continue;
            float lx,ly;
            const bool lensHas=LensPixel(g,p,&lx,&ly) && (lx-0.5f*kW)*(lx-0.5f*kW)+(ly-0.5f*kH)*(ly-0.5f*kH)<g.r*g.r;
            const float d2=(x-0.5f*kW)*(x-0.5f*kW)+(y-0.5f*kH)*(y-0.5f*kH);
            float sx=x,sy=y;
            const screenview::Shown s=screenview::Show(g.view,&sx,&sy);
            if(lensHas) {
                ++inLens;
                Check(s==screenview::Shown::lens,"a point the lens shows is put in the lens");
                Check(Near(sx,lx,0.05f) && Near(sy,ly,0.05f),"where the lens view really shows it");
            } else if(d2>g.rim*g.rim) {
                ++inFrame;
                Check(s==screenview::Shown::frame && sx==x && sy==y,"a point beyond the tube stays the frame's");
            } else {
                ++hidden;
                Check(s==screenview::Shown::hidden,"a point the lens does not show, under the tube or lens: hidden");
            }
        }
        if(!(inLens>10 && inFrame>100 && hidden>10))std::printf("grid %d %d %d\n",inLens,inFrame,hidden);
        Check(inLens>10 && inFrame>100 && hidden>10,"the grid covers the lens, the frame and the tube");
    }
    // No lens: every point is the frame's, untouched.
    float x=123.0f,y=456.0f;
    Check(screenview::Show(screenview::Lens{},&x,&y)==screenview::Shown::frame && x==123.0f && y==456.0f,"no lens: the frame's");
}

// A support route crossing the scope (the screenshot: from the screen's top across the tube into the lens).
void Lines(){
    for(float zoom:{3.0f,8.0f}) {
        const Rig g=MakeRig(zoom);
        float a[3],b[3];
        PointAt(g,0.5f*kW-60.0f,-200.0f,900.0f,a);         // high up, far
        PointAt(g,0.5f*kW-25.0f,0.5f*kH+15.0f,700.0f,b);    // just off the scope's centre
        float ax=0,ay=0,bx=0,by=0;
        Check(FramePixel(g,a,&ax,&ay) && FramePixel(g,b,&bx,&by),"the route projects");
        float pieces[screenview::kMostPieces][4];
        const int n=screenview::Pieces(g.view,ax,ay,bx,by,pieces);
        Check(n==2,"one piece in the frame, one in the lens");
        int lensSamples=0,frameSamples=0,offOld=0;
        for(int i=0;i<=4000;++i) {
            const float t=i/4000.0f;
            const float p[3]={a[0]+(b[0]-a[0])*t,a[1]+(b[1]-a[1])*t,a[2]+(b[2]-a[2])*t};
            float x,y,lx,ly;
            if(!FramePixel(g,p,&x,&y))continue;
            float best=1e9f;
            float target[2];
            const bool lensHas=LensPixel(g,p,&lx,&ly) && (lx-0.5f*kW)*(lx-0.5f*kW)+(ly-0.5f*kH)*(ly-0.5f*kH)<g.r*g.r*0.999f;
            const float d2=(x-0.5f*kW)*(x-0.5f*kW)+(y-0.5f*kH)*(y-0.5f*kH);
            if(lensHas){target[0]=lx;target[1]=ly;++lensSamples;}
            else if(d2>g.rim*g.rim*1.001f){target[0]=x;target[1]=y;++frameSamples;}
            else continue;   // under the tube: on no piece's interior
            for(int k=0;k<n;++k)best=std::fmin(best,ToPiece(pieces[k],target[0],target[1]));
            Check(best<0.1f,"each sample of the world line on a piece where its view shows it");
            if(lensHas) {
                const float old[4]={ax,ay,bx,by};
                if(ToPiece(old,lx,ly)>2.0f)++offOld;
            }
        }
        if(!(lensSamples>20 && frameSamples>20))std::printf("route %d %d\n",lensSamples,frameSamples);
        Check(lensSamples>20 && frameSamples>20,"the route is seen in the lens and in the frame");
        // The bug: the old single frame segment is not where the lens shows the route.
        Check(offOld>lensSamples/2,"the old frame-view line misses the lens's route (the separation)");
    }
    {   // No lens: the segment itself.
        float pieces[screenview::kMostPieces][4];
        Check(screenview::Pieces(screenview::Lens{},1,2,3,4,pieces)==1 && pieces[0][0]==1 && pieces[0][3]==4,"no lens: one piece");
    }
    {   // A line wholly in the frame, one under the tube, one through the lens both ways.
        const Rig g=MakeRig(4.0f);
        const float cx=0.5f*kW,cy=0.5f*kH;
        float pieces[screenview::kMostPieces][4];
        Check(screenview::Pieces(g.view,10,10,200,20,pieces)==1,"far from the scope: the frame's");
        const float mid=(g.r/g.view.k+g.rim)*0.5f;
        Check(screenview::Pieces(g.view,cx+mid,cy-1,cx+mid,cy+1,pieces)==0,"under the tube: nothing");
        Check(screenview::Pieces(g.view,cx-g.rim*2,cy,cx+g.rim*2,cy,pieces)==3,"across: frame, lens, frame");
        bool lensSpans=false;
        for(int k=0;k<3;++k)
            if(Near(std::fabs(pieces[k][2]-pieces[k][0]),2.0f*g.r,0.5f))lensSpans=true;
        Check(lensSpans,"the lens's piece spans the lens's diameter");
        Check(screenview::Pieces(g.view,cx-1,cy,cx+1,cy,pieces)==1 && Near(pieces[0][0],cx-g.view.k,1e-3f),"inside the lens: scaled");
    }
}

// The shared transform keeps the projections it replaced (sight.h, world_text.h, mapcmd_logic.h).
void Shared(){
    const Rig g=MakeRig(4.0f);
    float p[3];PointAt(g,700.0f,400.0f,250.0f,p);
    float c[4];
    for(int k=0;k<4;++k)c[k]=p[0]*g.frame[k]+p[1]*g.frame[4+k]+p[2]*g.frame[8+k]+g.frame[12+k];
    const float ex=kW*0.5f*(1.0f+c[0]/c[3]),ey=kH*0.5f*(1.0f-c[1]/c[3]);
    float x,y;
    Check(sight::ToScreen(g.frame,p,1.0f,kW,kH,&x,&y) && Near(x,ex,1e-2f) && Near(y,ey,1e-2f),"sight::ToScreen");
    Check(worldtext::ToScreen(g.frame,kW,kH,p,&x,&y) && Near(x,ex,1e-2f) && Near(y,ey,1e-2f),"worldtext::ToScreen");
    Check(mapcmd::Project(g.frame,p,kW,kH,&x,&y) && Near(x,ex,1e-2f) && Near(y,ey,1e-2f),"mapcmd::Project");
    Check(Near(x,700.0f,0.05f) && Near(y,400.0f,0.05f),"the rig's point where it was put");
}
}  // namespace

int main(){
    LensPower();
    Points();
    Lines();
    Shared();
    std::printf("screen_view: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
