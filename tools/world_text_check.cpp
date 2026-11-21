// The world text's math (src/world_text.h, docs/world-text-re.md §5) checked offline: the camera's frame read off a
// viewProj (a plain one, an off-centre / jittered one, the map's look straight down, a rolled one), that a billboard's
// glyph corners land through the full viewProj exactly where the label's screen rectangle says (so the GPU's perspective
// draws what the HUD laid out), that it faces the camera, the size held between kFarLeast and kNearMost of the 2D size
// from 0.5 m to 5 km, the font scale on the glyph cache's grid with the rest of the size in the matrix, and a point behind
// the camera or a viewProj without perspective refused.
//   cmake --build build --target world_text_check && build\world_text_check.exe      (exit code 1 on a failure)
#include "../src/world_text.h"
#include <cstdio>

namespace {
using namespace crew::worldtext;
int failures=0,cases=0;
void Check(bool ok,const char* what,double a=0.0,double b=0.0) {
    ++cases;
    if(ok)return;
    ++failures;
    std::printf("FAIL %s (%g, %g)\n",what,a,b);
}
void Cross(const float* a,const float* b,float* o) { o[0]=a[1]*b[2]-a[2]*b[1];o[1]=a[2]*b[0]-a[0]*b[2];o[2]=a[0]*b[1]-a[1]*b[0]; }
void Unit(float* v) { const float l=std::sqrt(Dot(v,v));for(int i=0;i<3;++i)v[i]/=l; }

// A camera's viewProj as the game's (row vectors, D3D depth 0..1): at `eye` looking along `look`, `up` the world's up
// (rolled by `roll` rad), vertical field of view `fov` rad on a `w` x `h` screen; `jx`, `jy` an off-centre shift (clip
// x / y += j * w: a TAA jitter, a split screen's off-centre frustum).
void Camera(const float* eye,const float* lookIn,float roll,float fov,float w,float h,float jx,float jy,float* vp) {
    float f[3]={lookIn[0],lookIn[1],lookIn[2]};Unit(f);
    const float worldUp[3]={0.0f,1.0f,0.0f};
    float r[3],u[3];
    Cross(worldUp,f,r);
    if(Dot(r,r)<1e-8f){const float z[3]={0.0f,0.0f,1.0f};Cross(z,f,r);}   // straight down: any right
    Unit(r);Cross(f,r,u);
    const float c=std::cos(roll),s=std::sin(roll);
    float rr[3],uu[3];
    for(int i=0;i<3;++i){rr[i]=r[i]*c+u[i]*s;uu[i]=u[i]*c-r[i]*s;}
    const float fy=1.0f/std::tan(fov*0.5f),fx=fy*h/w,zn=0.5f,zf=20000.0f,A=zf/(zf-zn),B=-zn*A;
    for(int i=0;i<3;++i){vp[i*4]=rr[i]*fx+f[i]*jx;vp[i*4+1]=uu[i]*fy+f[i]*jy;vp[i*4+2]=f[i]*A;vp[i*4+3]=f[i];}
    vp[12]=-Dot(eye,rr)*fx-Dot(eye,f)*jx;vp[13]=-Dot(eye,uu)*fy-Dot(eye,f)*jy;vp[14]=-Dot(eye,f)*A+B;vp[15]=-Dot(eye,f);
}

// One label through one camera: laid out as hud.cpp's WorldLabel lays it out, drawn as its DrawWorld draws it.
void Label(const char* name,const float* vp,float w,float h,const float* anchor,float dx,float dy,int align,float ref) {
    Frame f;
    const bool ok=FrameAt(vp,w,h,anchor,&f);
    Check(ok,name);
    if(!ok)return;
    // The frame: unit, square to each other, the forward along the view's depth.
    Check(std::fabs(Dot(f.right,f.right)-1.0f)<1e-4f && std::fabs(Dot(f.down,f.down)-1.0f)<1e-4f,"frame axes unit");
    Check(std::fabs(Dot(f.right,f.down))<1e-4f && std::fabs(Dot(f.right,f.forward))<1e-4f && std::fabs(Dot(f.down,f.forward))<1e-4f,
          "frame axes square");
    // A metre across / down in the plane through the anchor: as many pixels as the projection says.
    float ax=0.0f,ay=0.0f,bx=0.0f,by=0.0f,cx=0.0f,cy=0.0f;
    const float pr[3]={anchor[0]+f.right[0],anchor[1]+f.right[1],anchor[2]+f.right[2]};
    const float pd[3]={anchor[0]+f.down[0],anchor[1]+f.down[1],anchor[2]+f.down[2]};
    Check(ToScreen(vp,w,h,anchor,&ax,&ay) && ToScreen(vp,w,h,pr,&bx,&by) && ToScreen(vp,w,h,pd,&cx,&cy),"anchor on the screen");
    Check(std::fabs((bx-ax)-f.pxX)<f.pxX*1e-3f+1e-3f && std::fabs(by-ay)<f.pxX*1e-3f+1e-3f,"a metre right",bx-ax,f.pxX);
    Check(std::fabs((cy-ay)-f.pxY)<f.pxY*1e-3f+1e-3f && std::fabs(cx-ax)<f.pxY*1e-3f+1e-3f,"a metre down",cy-ay,f.pxY);
    // The label: a 2D font scale 0.42 (kLineScale x 0.7), 9 glyph px a character... its size at its depth.
    const float nominal=0.42f,step=1.0f/32.0f;
    const Fit fit=FontFor(nominal,SizeFactor(f.depth,ref),step);
    const float localW=180.0f*fit.fd,localH=32.0f*fit.fd;          // what the game's measure gives at fd
    const float sw=localW*fit.unitPx,sh=localH*fit.unitPx;         // on the screen
    float left,top;
    Place(ax,ay,dx,dy,align,sw,sh,&left,&top);
    float m[16];
    Billboard(f,anchor,ax,ay,left,top,fit.unitPx,m);
    // Its corners through m and the viewProj (the vertex shader: g_xgl_world, view identity, g_xgl_projection).
    const float corner[4][2]={{0.0f,0.0f},{localW,0.0f},{0.0f,localH},{localW,localH}};
    for(int i=0;i<4;++i) {
        float p[3],sx=0.0f,sy=0.0f;
        Local(m,corner[i][0],corner[i][1],p);
        const bool on=ToScreen(vp,w,h,p,&sx,&sy);
        const float ex=left+corner[i][0]*fit.unitPx,ey=top+corner[i][1]*fit.unitPx;
        Check(on && std::fabs(sx-ex)<0.05f && std::fabs(sy-ey)<0.05f,name,sx-ex,sy-ey);
    }
    // It faces the camera: its plane square to the forward, the local x along the screen's right, y along its down.
    Check(std::fabs(Dot(m,f.forward))<1e-5f && std::fabs(Dot(m+4,f.forward))<1e-5f,"faces the camera");
    Check(Dot(m+8,f.forward)>0.999f && m[3]==0.0f && m[7]==0.0f && m[11]==0.0f && m[15]==1.0f,"its 3rd and 4th columns");
    // Its height on the screen: the 2D size times the factor, held.
    const float h2d=32.0f*nominal;
    Check(sh>=h2d*kFarLeast-1e-3f && sh<=h2d*kNearMost+1e-3f,"held size",sh,h2d);
}
}  // namespace

int main() {
    const float w=1920.0f,h=1080.0f,fov=3.14159265f/4.0f;
    float vp[16];
    // The player's view: level, a little down, labels near / at the reference / far, every alignment and offset.
    const float eye[3]={10.0f,3.0f,-20.0f};
    const float look[3]={0.3f,-0.1f,1.0f};
    Camera(eye,look,0.0f,fov,w,h,0.0f,0.0f,vp);
    const float near1[3]={11.0f,2.5f,-14.0f},atRef[3]={18.0f,1.0f,8.0f},far1[3]={160.0f,-20.0f,480.0f};
    for(int align=0;align<3;++align) {
        Label("view near",vp,w,h,near1,0.0f,-28.0f,align,kViewRef);
        Label("view at ref",vp,w,h,atRef,4.0f,-10.0f,align,kViewRef);
        Label("view far",vp,w,h,far1,0.0f,26.0f,align,kViewRef);
    }
    // Off-centre (TAA jitter / a split screen's frustum), on a split viewport.
    Camera(eye,look,0.0f,fov,w,h*0.5f,0.0007f,-0.0004f,vp);
    Label("jittered",vp,w,h*0.5f,atRef,0.0f,-14.0f,1,kViewRef);
    // Rolled 20 degrees: the label rolls with the screen (it is the screen's right, not the world's level).
    Camera(eye,look,0.35f,fov,w,h,0.0f,0.0f,vp);
    Label("rolled",vp,w,h,atRef,0.0f,-14.0f,1,kViewRef);
    // A narrow sight (a 6x zoom).
    Camera(eye,look,0.0f,fov/6.0f,w,h,0.0f,0.0f,vp);
    Label("zoomed",vp,w,h,far1,0.0f,-14.0f,1,kViewRef);
    // The map: 700 m up at 60 degrees, and straight down from 1500 m (its reference its camera's distance).
    const float mapEye[3]={0.0f,700.0f,-404.0f},mapLook[3]={0.0f,-700.0f,404.0f};
    Camera(mapEye,mapLook,0.0f,fov,w,h,0.0f,0.0f,vp);
    const float ground1[3]={60.0f,0.0f,40.0f},ground2[3]={-300.0f,0.0f,900.0f};
    Label("map 60",vp,w,h,ground1,4.0f,-10.0f,0,808.0f);
    Label("map 60 far",vp,w,h,ground2,0.0f,-24.0f,1,808.0f);
    const float topEye[3]={0.0f,1500.0f,0.0f},down[3]={0.0f,-1.0f,0.0f};
    Camera(topEye,down,0.0f,fov,w,h,0.0f,0.0f,vp);
    Label("map straight down",vp,w,h,ground1,0.0f,22.0f,1,1500.0f);

    // The size: the 2D size at the reference, the perspective's around it, held at both ends.
    Check(std::fabs(SizeFactor(kViewRef,kViewRef)-1.0f)<1e-6f,"at the reference: the 2D size");
    Check(std::fabs(SizeFactor(kViewRef*1.25f,kViewRef)-0.8f)<1e-5f,"perspective between the bounds");
    Check(SizeFactor(0.5f,kViewRef)==kNearMost && SizeFactor(5000.0f,kViewRef)==kFarLeast,"held at both ends");
    for(float d=0.5f;d<5000.0f;d*=1.3f) {
        const float k=SizeFactor(d,kViewRef);
        Check(k>=kFarLeast && k<=kNearMost,"held",d,k);
    }
    // The font scale: on the grid, the rest in the matrix, the step at least.
    const float step=1.0f/32.0f;
    for(float want=0.01f;want<3.0f;want+=0.0173f) {
        const Fit fit=FontFor(want,1.0f,step);
        const float units=fit.fd/step;
        Check(std::fabs(units-std::round(units))<1e-4f && fit.fd>=step,"fd on the grid",want,fit.fd);
        Check(std::fabs(fit.fd*fit.unitPx-want)<1e-5f,"fd x unitPx is the size wanted",want,fit.fd*fit.unitPx);
        if(want>=step)Check(std::fabs(fit.fd-want)<=step*0.5f+1e-6f,"nearest grid point",want,fit.fd);
    }
    Check(Snap(0.3f,step)==Snap(0.3001f,step) && Snap(0.0f,step)==step,"snap");
    // Refused: behind the camera, a viewProj without perspective, none.
    Camera(eye,look,0.0f,fov,w,h,0.0f,0.0f,vp);
    Frame f;
    const float behind[3]={eye[0]-0.3f*10.0f,eye[1],eye[2]-10.0f};
    Check(!FrameAt(vp,w,h,behind,&f),"behind the camera");
    float ortho[16]={2.0f/w,0.0f,0.0f,0.0f, 0.0f,-2.0f/h,0.0f,0.0f, 0.0f,0.0f,1.0f,0.0f, -1.0f,1.0f,0.0f,1.0f};
    Check(!FrameAt(ortho,w,h,atRef,&f),"an orthographic projection (the HUD's own)");
    const float zero[16]{};
    Check(!FrameAt(zero,w,h,atRef,&f),"no viewProj");
    // The glow and the shimmer.
    const float amber[4]={1.0f,0.7f,0.15f,0.95f};
    float g[4];Glow(amber,g);
    Check(g[0]==1.0f && g[1]>amber[1] && g[2]>amber[2] && std::fabs(g[3]-amber[3]*kGlowAlpha)<1e-6f,"glow: lighter, fainter");
    float least=2.0f,most=-1.0f;
    for(unsigned long long ms=0;ms<3000;ms+=7){const float a=Shimmer(ms,0.3f);least=a<least ? a : least;most=a>most ? a : most;}
    Check(least>=1.0f-kShimmer-1e-4f && most<=1.0f+1e-6f && most-least>kShimmer*0.9f,"shimmer range",least,most);
    const float p0=PhaseOf(atRef);
    Check(p0>=0.0f && p0<1.0f,"phase",p0);

    std::printf(failures ? "world text: %d of %d FAILED\n" : "world text: %d checks, all good\n",failures ? failures : cases,cases);
    return failures ? 1 : 0;
}
