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
    const float start[3]={10.0f,20.0f,40.0f},end[3]={310.0f,20.0f,440.0f};
    RunSpec request;
    float centre[3]{};
    Check(RunFromPoints(start,end,&request,centre) && RunValid(request) &&
          Near(request.half,250.0f,0.01f) && Near(request.x,0.6f,0.001f) && Near(centre[2],240.0f,0.01f),
          "two ground points define the requested direction, midpoint and distance");
    Check(!RunFromPoints(start,start,&request,centre),"a zero-length run cannot be called");
    const auto shortGap=CarpetGapMs(6,speed,100.0f),longGap=CarpetGapMs(6,speed,250.0f);
    Check(shortGap<longGap && Near(static_cast<float>(longGap)*0.001f*speed*5.0f,500.0f,2.0f),
          "release timing follows the length of the marked attack run");
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
// The salvo's volleys (src/salvo_volley.h): the rhythm, the rails, and the rounds over the enemies locked.
void Volleys() {
    namespace salvo=crew::salvo;
    Check(salvo::VolleyFrames(salvo::kRounds)==93 && salvo::VolleyFrames(8)==0 && salvo::VolleyFrames(9)==3 &&
          salvo::VolleyFrames(0)==0,"32 volleys three frames apart");
    Check(salvo::kVolley>=salvo::kMuzzles && salvo::kVolley%salvo::kMuzzles==0,"every rail fires each volley");
    // Nodes: enemy A twice (two lock points), B, a dead lock, C.
    int a=0,b=0,c=0;
    const void* objects[]={&a,&b,&a,nullptr,&c};
    int enemyOf[5];
    Check(salvo::NumberEnemies(objects,5,enemyOf)==3 && enemyOf[0]==0 && enemyOf[1]==1 && enemyOf[2]==0 && enemyOf[3]==-1 &&
          enemyOf[4]==2,"enemies numbered in the order first locked, a dead lock none");
    const int first[]={0,1,4,2,1,4};   // A, B, C, then A's second point, B again, C again
    bool order=true;
    for(int r=0;r<6;++r)order=order && salvo::SalvoNode(r,enemyOf,5,3)==first[r];
    Check(order,"every enemy one round before any gets a second; an enemy's lock points in turn");
    int per[3]={},points[2]={};
    bool live=true;
    for(int r=0;r<salvo::kRounds;++r) {
        const int node=salvo::SalvoNode(r,enemyOf,5,3);
        live=live && node>=0 && enemyOf[node]>=0;
        if(node>=0 && enemyOf[node]>=0)++per[enemyOf[node]];
        if(node==0 || node==2)++points[node/2];
    }
    Check(live && per[0]==86 && per[1]==85 && per[2]==85 && points[0]==43 && points[1]==43,
          "256 rounds over 3 enemies: 86 85 85, never the dead lock");
    const void* dead[]={nullptr,nullptr};
    int none[2];
    Check(salvo::NumberEnemies(dead,2,none)==0 && salvo::SalvoNode(0,none,2,0)==-1 && salvo::SalvoNode(0,enemyOf,0,0)==-1,
          "no live lock: the round goes unguided");
    int one=0;
    const void* single[]={&one};
    int so[1];
    bool all=salvo::NumberEnemies(single,1,so)==1;
    for(int r=0;r<salvo::kRounds;++r)all=all && salvo::SalvoNode(r,so,1,1)==0;
    Check(all,"one lock takes the whole salvo");
}
}  // namespace

int main() {
    Lines();
    Carpet();
    Laps();
    Circle();
    Check(SalvoLocked(256,1) && SalvoLocked(256,64) && SalvoLocked(256,256) && !SalvoLocked(256,0) && !SalvoLocked(0,256),
          "missile rain fires on its first lock");
    Volleys();
    const unsigned long long burst=static_cast<unsigned long long>(crew::salvo::VolleyFrames(crew::salvo::kRounds))*1000/60;
    Check(SalvoDraining(1000+burst,1000) && !SalvoDraining(1000+kRainBurstMs+1,1000) && !SalvoDraining(1000,0),
          "the jet stays on its pass until the volleys are out");
    std::printf(failures ? "\n%d check(s) FAILED\n" : "\nall checks passed\n",failures);
    return failures ? 1 : 0;
}
