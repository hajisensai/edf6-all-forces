// The sorties' geometry offline (src/sortie_plan.h as src/jet_sortie.cpp and src/heli.cpp fly them;
// docs/plan-2026-10-25-strikes.md §1.2):
//  - the pass line: along the heading through the mark, a formation's aircraft keeping their places abreast (at most
//    kMaxOffset across), along it negative short of the mark;
//  - the carpet: bombs let go level at kCarpetAlt land where they are reckoned to, the window kCarpetHalf either side, and
//    the gap spreads a load over it;
//  - the lap: a circle counted either way round across the angle's wrap, done after one full turn, not before;
//  - the circle's velocity: round the centre at its speed on the radius, back onto it from inside and outside.
//
//   cmake --build build --target sortie_check && build\sortie_check.exe        exit code 0: every check passed
#include "../src/sortie_plan.h"
#include <cmath>
#include <cstdio>

namespace {
using namespace crew::sortie;
int failures=0;
void Check(bool ok,const char* what) {
    std::printf("%s  %s\n",ok ? "ok  " : "FAIL",what);
    if(!ok)++failures;
}
bool Near(float a,float b,float tol) { return std::fabs(a-b)<=tol; }

void Lines() {
    const float mark[3]={100,20,50},heading[3]={0,0,1};
    const float lead[3]={100,300,-2000},left[3]={130,300,-2000},wide[3]={500,300,-2000};
    const Line a=LineThrough(mark,lead,heading),b=LineThrough(mark,left,heading),c=LineThrough(mark,wide,heading);
    Check(Near(a.at[0],100,1e-3f) && Near(a.at[2],50,1e-3f) && Near(a.dir[2],1,1e-5f),"the lead's line runs through the mark");
    Check(Near(b.at[0]-a.at[0],30,1e-3f) && Near(b.dir[0],a.dir[0],1e-6f),"a wingman's line beside it, as far as it flies abreast");
    Check(Near(std::fabs(c.at[0]-mark[0]),kMaxOffset,1e-3f),"no line farther than kMaxOffset across from the mark");
    Check(Near(Along(a,lead),-2050,1e-2f) && Along(a,mark)==0.0f,"along: negative short of the mark");
    float p[3];Ahead(a,lead,200,p);
    Check(Near(p[2],-1800,1e-2f) && Near(p[0],100,1e-3f),"ahead: on the line, on from where it is");
    const float none[3]={0,0,0};
    const Line d=LineThrough(mark,lead,none);
    Check(Near(d.dir[2],1,1e-4f),"no heading: toward the mark");
}

void Carpet() {
    const float mark[3]={0,0,0},heading[3]={1,0,0},from[3]={-3000,kCarpetAlt,0};
    const Line l=LineThrough(mark,from,heading);
    const float g=9.8f,speed=200.0f;
    const float vel[3]={speed,0,0};
    // Let go at each point along the run: what lands inside the window.
    int inside=0;float first=0,last=0;
    for(float x=-3000;x<1000;x+=5.0f) {
        const float pos[3]={x,kCarpetAlt,0};
        float at[3];Impact(pos,vel,0.0f,g,at);
        if(InCarpet(l,at)){if(!inside)first=at[0];last=at[0];++inside;}
    }
    Check(Near(first,-kCarpetHalf,5.1f) && Near(last,kCarpetHalf,5.1f),"the carpet's bombs land within kCarpetHalf of the mark");
    const float t=FallTime(kCarpetAlt,0.0f,g);
    Check(Near(0.5f*g*t*t,kCarpetAlt,0.01f),"a bomb's fall time from its height");
    const unsigned long long gap=CarpetGapMs(6,speed);
    Check(Near(static_cast<float>(gap)*0.001f*speed*5.0f,2.0f*kCarpetHalf,2.0f),"six bombs a gap apart cover the carpet");
}

void Laps() {
    const float c[3]={0,0,0};
    Lap l;bool early=false;
    for(int i=0;i<=360;++i) {
        const float a=static_cast<float>(i)*kPi/180.0f+2.5f;   // across the angle's wrap at pi
        const float p[3]={std::cos(a)*100.0f,0,std::sin(a)*100.0f};
        LapStep(l,c,p);
        if(i<359 && LapDone(l))early=true;
    }
    Check(!early && LapDone(l),"a lap counts done after one full turn, not before (across the wrap)");
    Lap r;
    for(int i=0;i<=360;++i){const float a=-static_cast<float>(i)*kPi/180.0f;const float p[3]={std::cos(a)*100.0f,0,std::sin(a)*100.0f};LapStep(r,c,p);}
    Check(LapDone(r) && r.swept<0.0f,"the other way round as well");
}

void Circle() {
    const float c[3]={0,0,0};
    float v[3];
    const float on[3]={kCircleRadius,0,0};
    CircleVelocity(c,on,kCircleRadius,kCircleSpeed,1,v);
    Check(Near(v[0],0,1e-4f) && Near(v[2],kCircleSpeed,1e-4f),"on the circle: its speed, tangent to it");
    const float in[3]={kCircleRadius-20.0f,0,0},out[3]={kCircleRadius+20.0f,0,0};
    CircleVelocity(c,in,kCircleRadius,kCircleSpeed,1,v);
    const bool outward=v[0]>0.0f;
    CircleVelocity(c,out,kCircleRadius,kCircleSpeed,1,v);
    Check(outward && v[0]<0.0f,"back onto the radius from inside and from outside");
    CircleVelocity(c,on,kCircleRadius,kCircleSpeed,-1,v);
    Check(Near(v[2],-kCircleSpeed,1e-4f),"the other way round");
}
}  // namespace

int main() {
    Lines();
    Carpet();
    Laps();
    Circle();
    std::printf(failures ? "\n%d check(s) FAILED\n" : "\nall checks passed\n",failures);
    return failures ? 1 : 0;
}
