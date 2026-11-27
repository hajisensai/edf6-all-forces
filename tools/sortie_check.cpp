// The sorties' geometry offline (src/sortie_plan.h as src/jet_sortie.cpp and src/heli.cpp fly them;
// docs/plan-2026-10-25-strikes.md §1.2):
//  - the pass line: along the heading through the mark, a formation's aircraft keeping their places abreast (at most
//    kMaxOffset across), along it negative short of the mark;
//  - the carpet: bombs let go level at kCarpetAlt land where they are reckoned to, the window kCarpetHalf either side, and
//    the gap spreads a load over it;
//  - the lap: a circle counted either way round across the angle's wrap, done after one full turn, not before;
//  - the circle's velocity: round the centre at its speed on the radius, back onto it from inside and outside;
//  - the missile rain's hold: it fires only over the target area with its nose settled for kRainHoldMs (RainHoldStep).
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
    // The way in alone (a flown call's heading, no line): unit, half 0; any other length refused on the wire.
    const RunSpec way=HeadingRun(3.0f,4.0f);
    Check(HasHeading(way) && RunValid(way) && way.half==0.0f && Near(way.x,0.6f,1e-5f) && Near(way.z,0.8f,1e-5f),
          "a heading alone is a valid request, made unit");
    Check(!HasHeading(HeadingRun(0.0f,0.0f)) && RunValid(RunSpec{}) && !RunValid(RunSpec{2.0f,0.0f,0.0f}) &&
          !RunValid(RunSpec{1.0f,0.0f,5.0f}),"no heading is none; a non-unit heading or a too short line is refused");
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
// The rain's hold (RainHoldStep): no fire on arrival, outside kRainOpen or with the nose swinging; kRainHoldMs settled
// over the target area opens it, a wobble before that starts the count again, and once open it stays open.
void RainHold() {
    int phase=0;unsigned long long since=0;
    Check(!RainHoldStep(phase,since,kRainFrom-10.0f,0.0f,1000) && phase==0,"no fire on arrival outside the target area");
    Check(!RainHoldStep(phase,since,kRainOpen-10.0f,kRainSteadyCone+0.05f,2000) && phase==0,"no fire with the nose off its aim");
    bool early=false;
    for(unsigned long long t=3000;t<3000+kRainHoldMs;t+=16)early=early || RainHoldStep(phase,since,kRainOpen-10.0f,0.1f,t);
    Check(!early && phase==1 && since==3000,"the hold runs its kRainHoldMs settled");
    Check(!RainHoldStep(phase,since,kRainOpen-10.0f,0.6f,3000+kRainHoldMs-100) && phase==0,"a wobble starts it again");
    Check(!RainHoldStep(phase,since,kRainOpen-10.0f,0.1f,6000) && !RainHoldStep(phase,since,kRainOpen-10.0f,0.1f,6000+kRainHoldMs-1),
          "counted again from the settled nose");
    Check(RainHoldStep(phase,since,kRainOpen-10.0f,0.1f,6000+kRainHoldMs) && phase==2,"open after kRainHoldMs settled");
    Check(RainHoldStep(phase,since,kRainTo+1.0f,0.7f,9000),"open stays open through the lock's dwell and the burst");
    const float closes=kRainOpen-kRainSpeed*static_cast<float>(kRainHoldMs+kRainBurstMs)/1000.0f;
    Check(closes>kRainTo,"from kRainOpen the hold and the burst end before the window does");
}
}  // namespace

// The marked strafe (jet_sortie.cpp MarkedRun; the user, 2026-11-26: 「扫射时间是不是不对」「只有机枪」「需要绕一或n圈」).
void Strafe() {
    // The strike jet's: gunClose 120, the guns' reach 1000, attack 215 m/s.
    const float least=120.0f*2.6f,preferred=900.0f,speed=215.0f;
    // The old run: the nose 260 m on from 160 m up was at the 70 m floor 215 m (2.5 s) in, on a line of 500.
    Check(StrafeDescent(160.0f,260.0f,215.0f)<=70.5f,"the old fixed lead came down to its floor 2.5 s into the line");
    float last=0.0f;bool covered=true,longer=true,rockets=true;
    const float halves[]={100.0f,250.0f,400.0f,600.0f};
    for(float half:halves) {
        const float over=StrafeHeight(half,preferred),lead=StrafeLead(half,over,least,kStrafeLeadMost);
        // In at its height, the nose's point walking the whole line: never under the floor before the end.
        covered=covered && StrafeDescent(over,lead,2.0f*half)>=kStrafeFloor*0.99f && over>=kStrafeLow && over<=kStrafeTop;
        // The guns reach the point all the way (lead within reach) and the rockets fire on it (past 2.5 gunClose,
        // inside kRocketOpen 1400: jet_combat.cpp Fire), so a strafe is guns and stores, not guns only.
        rockets=rockets && lead>120.0f*2.5f && lead<1400.0f && lead<=1000.0f;
        const float s=StrafeSeconds(half,speed);
        longer=longer && s>last;last=s;
    }
    Check(covered,"the strafe's height and lead last the whole line down to the floor");
    Check(rockets,"the lead lies inside the guns' reach and the rockets' window all the line");
    Check(longer && Near(StrafeSeconds(250.0f,speed),500.0f/speed,1e-4f),"the fire window grows with the line: 2 half / speed");
    Check(!StrafeOpen(-600.0f,300.0f,250.0f) && StrafeOpen(-550.0f,300.0f,250.0f) && !StrafeOver(0.0f,200.0f,250.0f) &&
          StrafeOver(100.0f,200.0f,250.0f),"the window opens with the point at the line's start, over past its end");
    // The capture: on the line along it needs nothing; beside it, turns it can make; heading away, a lap (never flown).
    const float r=900.0f;
    Check(Near(CaptureDistance(0.0f,0.0f,r),0.0f,1e-3f),"on the line along it: captured");
    Check(CaptureDistance(100.0f,1.6f,r)>=1e8f && CaptureDistance(-50.0f,-1.6f,r)>=1e8f,"heading away from along it: no capture");
    const float s1=CaptureDistance(50.0f,0.0f,r),s2=CaptureDistance(200.0f,0.0f,r);
    Check(s1>0.0f && s2>s1 && s2<2.0f*r+1.0f,"beside it, parallel: an S onto it, longer the farther off");
    Check(CaptureDistance(200.0f,-0.3f,r)<CaptureDistance(200.0f,0.3f,r),"closing on it takes less than drifting off it");
}

// A ground run's least line (the user, 2026-11-27: as long as it takes to let every ground store go) and the map's drag.
void Drag() {
    const auto nearly=[](float a,float b){return std::fabs(a-b)<0.5f;};
    // 12 bombs 150 ms apart at 200 m/s: 330 m; 38 rockets in ripples of 4 a 600 ms: 9 gaps, 1080 m; 6 missiles: a 700 ms
    // lock and 5 gaps, 4.2 s, 840 m; the slowest store sets it.
    Check(nearly(RunLeast(RunLoad{12,0,0},200.0f,false),330.0f),"bombs: their gaps at its speed");
    Check(nearly(RunLeast(RunLoad{0,38,0},200.0f,false),1080.0f),"rockets: a ripple of four every 600 ms");
    Check(nearly(RunLeast(RunLoad{0,0,6},200.0f,false),840.0f),"missiles: the lock, then one a gap");
    Check(nearly(RunLeast(RunLoad{12,38,6},200.0f,false),1080.0f),"several stores: the slowest to empty");
    Check(nearly(RunLeast(RunLoad{12,38,6},200.0f,true),330.0f),"a carpet lets go its bombs only");
    Check(nearly(RunLeast(RunLoad{},215.0f,false),kRunMin) && nearly(RunLeast(RunLoad{1,4,0},215.0f,false),kRunMin),
          "nothing to space out: the shortest line");
    Check(nearly(RunLeast(RunLoad{200,0,0},300.0f,false),kRunMax),"never past the longest line");
    Check(nearly(RunLeast(RunLoad{12,0,0},-1.0f,false),kRunMin) && nearly(RunLeast(RunLoad{12,0,0},NAN,false),kRunMin),
          "no speed: the shortest");
    // The drag: its way from the press toward the pointer, as long as dragged within least..kRunMax.
    const float a[3]={100.0f,5.0f,200.0f};
    RunSpec r;float c[3],e[3];
    const float b[3]={100.0f,7.0f,800.0f};
    Check(RunFromDrag(a,b,330.0f,1.0f,0.0f,&r,c,e) && nearly(r.z,1.0f) && nearly(r.x,0.0f) && nearly(r.half,300.0f) &&
          nearly(c[2],500.0f) && nearly(e[2],800.0f) && nearly(e[0],100.0f) && RunValid(r),"a long drag: its way, its length");
    const float shortDrag[3]={160.0f,5.0f,200.0f};
    Check(RunFromDrag(a,shortDrag,330.0f,0.0f,1.0f,&r,c,e) && nearly(r.x,1.0f) && nearly(r.half,165.0f) && nearly(e[0],430.0f) &&
          nearly(c[0],265.0f),"a drag short of the least: its way, the least long");
    const float distant[3]={100.0f,5.0f,5000.0f};
    Check(RunFromDrag(a,distant,330.0f,1.0f,0.0f,&r,c,e) && nearly(r.half,kRunMax*0.5f) && RunValid(r),"never past the longest");
    const float click[3]={110.0f,5.0f,205.0f};
    Check(RunFromDrag(a,click,330.0f,0.0f,-2.0f,&r,c,e) && nearly(r.z,-1.0f) && nearly(r.half,165.0f) && nearly(e[2],-130.0f),
          "a click: the given way (normalised), the least long");
    Check(!RunFromDrag(a,a,330.0f,0.0f,0.0f,&r,c,e),"a click with no way: none");
    Check(RunFromDrag(a,a,10.0f,1.0f,0.0f,&r,c,e) && nearly(r.half,kRunMin*0.5f),"a least under the shortest line: the shortest");
    // The host lengthens a shorter line from the same start.
    RunSpec line{1.0f,0.0f,60.0f};float centre[3]={0.0f,0.0f,0.0f};
    RunLengthen(line,centre,330.0f);
    Check(nearly(line.half,165.0f) && nearly(centre[0],105.0f) && nearly(centre[2],0.0f),"lengthened: the start kept");
    RunLengthen(line,centre,200.0f);
    Check(nearly(line.half,165.0f) && nearly(centre[0],105.0f),"a line long enough stays");
    RunSpec heading=HeadingRun(1.0f,0.0f);float none[3]={0.0f,0.0f,0.0f};
    RunLengthen(heading,none,330.0f);
    Check(heading.half==0.0f && none[0]==0.0f,"a heading-only way is no line");
}

int main() {
    Lines();
    Strafe();
    Carpet();
    Laps();
    Circle();
    Drag();
    Check(SalvoLocked(256,1) && SalvoLocked(256,64) && SalvoLocked(256,256) && !SalvoLocked(256,0) && !SalvoLocked(0,256),
          "missile rain fires on its first lock");
    Volleys();
    RainHold();
    const unsigned long long burst=static_cast<unsigned long long>(crew::salvo::VolleyFrames(crew::salvo::kRounds))*1000/60;
    Check(SalvoDraining(1000+burst,1000) && !SalvoDraining(1000+kRainBurstMs+1,1000) && !SalvoDraining(1000,0),
          "the jet stays on its pass until the volleys are out");
    std::printf(failures ? "\n%d check(s) FAILED\n" : "\nall checks passed\n",failures);
    return failures ? 1 : 0;
}
