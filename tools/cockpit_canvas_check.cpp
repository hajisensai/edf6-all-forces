// The cockpit screens' canvas map (src/cockpit_canvas.h), offline:
//  - the canvas's corners land on the quad's corners (TL, TR, BR, BL), for a rectangle, a parallelogram and a quad in
//    perspective;
//  - straight lines stay straight: the canvas's centre lands on the quad's diagonals' crossing (a homography's), not on
//    the average of the corners (an affine guess);
//  - the stretch is the quad's scale over the canvas (a rectangle twice the canvas's size: 2), bigger at the near end of
//    a quad in perspective;
//  - a quad with no area is refused;
//  - the head-up glass (Glass, Through, Level, Toward, DownOnGlass) is conformal: for an aircraft at a heading, nose up
//    and banked, the horizon drawn on its glass is turned by the bank (its right end up in a right bank), lies the
//    glass's distance x tan(pitch) under the boresight, the nose lands on the glass's centre, and the bank pointer is
//    square to the horizon; level, the horizon is level and through the centre.
// Exit code 1 when one fails. cmake --build build --target cockpit_canvas_check && build\cockpit_canvas_check.exe
#include "../src/cockpit_canvas.h"
#include <cmath>
#include <cstdio>

namespace {
using namespace crew::canvas;
int failures=0;

void Expect(bool ok,const char* what,float a=0.0f,float b=0.0f) {
    std::printf("%s  %s (%.4f, %.4f)\n",ok ? "ok  " : "FAIL",what,a,b);
    if(!ok)++failures;
}

bool Near(float a,float b,float eps=1e-3f) { return std::fabs(a-b)<=eps; }

void Corners(const float* x,const float* y,const char* what) {
    Map m{};
    Expect(Onto(m,x,y,200.0f,100.0f),what);
    const float cx[4]={0,200,200,0},cy[4]={0,0,100,100};
    float worst=0.0f;
    for(int i=0;i<4;++i) {
        float px,py;
        Point(m,cx[i],cy[i],&px,&py);
        worst=std::fmax(worst,std::fmax(std::fabs(px-x[i]),std::fabs(py-y[i])));
    }
    Expect(worst<1e-2f,"...its canvas corners land on the quad's corners",worst);
}
}  // namespace

// An aircraft heading `heading` degrees (sight::HeadingOf: the world's +z 0, its -x 90), nose `pitch` up, banked `bank`
// degrees (right wing down), its glass `dist` m ahead of the eye along the nose, its axes the body's right and up.
Glass GlassOf(float heading,float pitch,float bank,float dist) {
    const float k=0.01745329252f,hh=heading*k,p=pitch*k,b=bank*k;
    const float h[3]={-std::sin(hh),0.0f,std::cos(hh)},r[3]={-h[2],0.0f,h[0]},up[3]={0.0f,1.0f,0.0f};
    float f[3],u0[3];
    for(int i=0;i<3;++i){f[i]=std::cos(p)*h[i]+std::sin(p)*up[i];u0[i]=-std::sin(p)*h[i]+std::cos(p)*up[i];}
    Glass g{};
    for(int i=0;i<3;++i) {
        g.off[i]=f[i]*dist;
        g.right[i]=std::cos(b)*r[i]-std::sin(b)*u0[i];
        g.up[i]=std::sin(b)*r[i]+std::cos(b)*u0[i];
    }
    g.w=0.14f;g.h=0.15f;g.ch=1080.0f;g.cw=g.ch*g.w/g.h;
    return g;
}

void HeadUpGlassChecks() {
    static const float cases[3][3]={{30.0f,8.0f,25.0f},{200.0f,-12.0f,-40.0f},{0.0f,0.0f,0.0f}};   // heading, pitch, bank
    for(const auto& c:cases) {
        const float heading=c[0],pitch=c[1],bank=c[2],dist=0.8f;
        const Glass g=GlassOf(heading,pitch,bank,dist);
        float nose[3];for(int i=0;i<3;++i)nose[i]=g.off[i]/dist;
        float h[3],r[3],a[3],b[3],nx=0,ny=0,x0=0,y0=0,x1=0,y1=0;
        Expect(Level(nose,h,r),"the nose's level frame");
        Toward(h,r,0.0f,-0.2f,a);Toward(h,r,0.0f,0.2f,b);
        Expect(Through(g,nose,&nx,&ny) && Near(nx,g.cw*0.5f,0.05f) && Near(ny,g.ch*0.5f,0.05f),"the nose lands on the glass's centre",nx,ny);
        Expect(Through(g,a,&x0,&y0) && Through(g,b,&x1,&y1),"the horizon either side of the heading crosses the glass");
        const float turn=std::atan2(y1-y0,x1-x0)/0.01745329252f;
        Expect(Near(turn,-bank,0.05f),"the horizon on the glass is turned by the bank (its right end up in a right bank)",turn,-bank);
        // The boresight's distance from the drawn horizon, in metres on the glass: the glass's distance x tan(pitch),
        // the horizon under the boresight with the nose up.
        const float ux=(x1-x0)/g.cw*g.w,uy=(y1-y0)/g.ch*g.h,len=std::sqrt(ux*ux+uy*uy);
        const float cx=(nx-x0)/g.cw*g.w,cy=(ny-y0)/g.ch*g.h,across=(ux*cy-uy*cx)/len;
        Expect(Near(across,-dist*std::tan(pitch*0.01745329252f),2e-4f),"the horizon lies dist x tan(pitch) from the boresight, under it nose up",
               across,-dist*std::tan(pitch*0.01745329252f));
        const float pointer=DownOnGlass(bank),square=std::atan2(y1-y0,x1-x0)+1.5707963f;
        Expect(Near(pointer,square,1e-3f),"the bank pointer is square to the drawn horizon, toward the ground",pointer,square);
    }
    float x=0,y=0;
    const Glass g=GlassOf(0.0f,0.0f,0.0f,0.8f);
    const float behind[3]={0.0f,0.0f,-1.0f};
    Expect(!Through(g,behind,&x,&y),"a direction behind the eye never lands on the glass");
    float ax=-10,ay=50,bx=300,by=50;
    Expect(Clip(ax,ay,bx,by,0,0,100,100) && Near(ax,0) && Near(bx,100),"a segment is cut to the glass's edges",ax,bx);
    ax=-10;ay=-10;bx=-5;by=-1;
    Expect(!Clip(ax,ay,bx,by,0,0,100,100),"a segment off the glass is left out");
}

int main() {
    HeadUpGlassChecks();
    const float rx[4]={100,500,500,100},ry[4]={50,50,250,250};
    Corners(rx,ry,"a rectangle");
    const float px[4]={100,500,560,160},py[4]={50,50,250,250};
    Corners(px,py,"a parallelogram");
    const float qx[4]={300,500,700,100},qy[4]={100,100,400,400};   // a screen seen tilted away: the top narrower
    Corners(qx,qy,"a quad in perspective");

    Map m{};
    Onto(m,qx,qy,200.0f,100.0f);
    float cx,cy;
    Point(m,100.0f,50.0f,&cx,&cy);
    // The diagonals TL-BR and TR-BL cross where the canvas's centre must land.
    const float d1x=qx[2]-qx[0],d1y=qy[2]-qy[0],d2x=qx[3]-qx[1],d2y=qy[3]-qy[1];
    const float t=((qx[1]-qx[0])*d2y-(qy[1]-qy[0])*d2x)/(d1x*d2y-d1y*d2x);
    const float ix=qx[0]+t*d1x,iy=qy[0]+t*d1y;
    Expect(Near(cx,ix,0.05f) && Near(cy,iy,0.05f),"the centre lands where the quad's diagonals cross",cx-ix,cy-iy);
    Expect(!Near(cy,(qy[0]+qy[1]+qy[2]+qy[3])*0.25f,1.0f),"...not on the corners' average (a homography, not affine)",cy);

    Map r{};
    Onto(r,rx,ry,200.0f,100.0f);
    Expect(Near(Stretch(r,50.0f,50.0f),2.0f),"a rectangle twice the canvas: stretch 2",Stretch(r,50.0f,50.0f));
    Expect(Stretch(m,100.0f,95.0f)>Stretch(m,100.0f,5.0f),"the near (wide) end of a tilted screen is stretched more",
           Stretch(m,100.0f,95.0f),Stretch(m,100.0f,5.0f));

    Map z{};
    const float lx[4]={0,100,200,300},ly[4]={0,0,0,0};
    Expect(!Onto(z,lx,ly,200.0f,100.0f),"a quad with no area is refused");
    std::printf(failures ? "%d FAILED\n" : "all passed\n",failures);
    return failures ? 1 : 0;
}
