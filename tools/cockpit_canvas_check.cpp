// The cockpit screens' canvas map (src/cockpit_canvas.h), offline:
//  - the canvas's corners land on the quad's corners (TL, TR, BR, BL), for a rectangle, a parallelogram and a quad in
//    perspective;
//  - straight lines stay straight: the canvas's centre lands on the quad's diagonals' crossing (a homography's), not on
//    the average of the corners (an affine guess);
//  - the stretch is the quad's scale over the canvas (a rectangle twice the canvas's size: 2), bigger at the near end of
//    a quad in perspective;
//  - a quad with no area is refused.
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

int main() {
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
