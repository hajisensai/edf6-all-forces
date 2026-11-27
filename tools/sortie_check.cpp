// The sorties' geometry offline (src/sortie_plan.h as src/jet_sortie.cpp and src/heli.cpp fly them;
// docs/plan-2026-10-25-strikes.md §1.2):
//  - the pass line: along the heading through the mark, a formation's aircraft keeping their places abreast (at most
//    kMaxOffset across), along it negative short of the mark;
//  - the carpet: bombs let go level at kCarpetAlt land where they are reckoned to, the window kCarpetHalf either side, and
//    the gap spreads a load over it;
//  - the lap: a circle counted either way round across the angle's wrap, done after one full turn, not before;
//  - the circle's velocity: round the centre at its speed on the radius, back onto it from inside and outside;
//  - the missile rain's hold: it fires only over the target area with its nose settled for kRainHoldMs (RainHoldStep);
//  - the missile rain's clearance: its 256 rounds (src/salvo_flight.h) fired level from kRainAlt come down on the target,
//    not on the ground short of it, on flat ground and over a ridge; the old dive at the target wasted a third of them;
//  - the missile rain of the 2026-11-27 log (「这个天顶根本没开火」) flown again: spawned off the map past two soft sides, its
//    target 570 m off the mark and 117 m inside the third, at 110 m/s; it must fire on its way in (RainLogReplay).
//
//   cmake --build build --target sortie_check && build\sortie_check.exe        exit code 0: every check passed
#include "../src/sortie_plan.h"
#include "../src/salvo_flight.h"
#include "../src/airbound.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

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
    const float speed=200.0f;
    // Where each bomb lands is the game's (jet_sortie.cpp SortieFire: StoreLands); the window takes the landing points.
    int inside=0;float first=0,last=0;
    for(float x=-3000;x<1000;x+=5.0f) {
        const float at[3]={x,0.0f,0.0f};
        if(InCarpet(l,at)){if(!inside)first=at[0];last=at[0];++inside;}
    }
    Check(Near(first,-kCarpetHalf,5.1f) && Near(last,kCarpetHalf,5.1f),"the carpet's bombs land within kCarpetHalf of the mark");
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
    Check(!RainHoldStep(phase,since,kRainFrom-10.0f,0.0f,true,1000) && phase==0,"no fire on arrival outside the target area");
    Check(!RainHoldStep(phase,since,kRainOpen-10.0f,kRainSteadyCone+0.05f,true,2000) && phase==0,"no fire with the nose off its aim");
    bool early=false;
    for(unsigned long long t=3000;t<3000+kRainHoldMs;t+=16)early=early || RainHoldStep(phase,since,kRainOpen-10.0f,0.1f,true,t);
    Check(!early && phase==1 && since==3000,"the hold runs its kRainHoldMs settled");
    Check(!RainHoldStep(phase,since,kRainOpen-10.0f,0.6f,true,3000+kRainHoldMs-100) && phase==0,"a wobble starts it again");
    Check(!RainHoldStep(phase,since,kRainOpen-10.0f,0.1f,true,6000) && !RainHoldStep(phase,since,kRainOpen-10.0f,0.1f,true,6000+kRainHoldMs-1),
          "counted again from the settled nose");
    Check(!RainHoldStep(phase,since,kRainOpen-10.0f,0.1f,false,6000+kRainHoldMs) && phase==1,
          "settled but the target not yet steep under it: held, still settled");
    Check(RainHoldStep(phase,since,kRainOpen-10.0f,0.1f,true,6000+kRainHoldMs+16) && phase==2,"open after kRainHoldMs settled, once steep");
    Check(RainHoldStep(phase,since,kRainTo+1.0f,0.7f,false,9000),"open stays open through the lock's dwell and the burst");
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

// The straight run in (StraightRunIn; the user, 2026-11-27: 「还是会进场先转圈」): from an entry that far out on its line a
// marked strafe (jet_sortie.cpp MarkedRun phase 0) never calls its line missed for want of room, whatever height it
// comes down from and whatever its lead; it still has a settle on the line before its nose goes down. The jets' kinds'
// attack speeds (jet_internal.h kKinds: 215-245 m/s) and their guns' close (120-150 m).
void StraightIn() {
    bool ok=true;
    const float attacks[]={215.0f,225.0f,235.0f,245.0f},halves[]={kRunMin*0.5f,300.0f,kRunMax*0.5f};
    const float overs[]={kStrafeFloor*1.06f,kStrafeLow,450.0f,kStrafeTop};
    for(float attack:attacks)for(float half:halves)for(float over:overs) {
            const float settle=attack*kStrafeSettleS,run=half+settle*0.5f;
            const float lead=StrafeLead(run,over,120.0f*2.6f,kStrafeLeadMost);
            const float t=StraightRunIn(true,half,attack,1500.0f);
            const float room=-half-settle-lead+t;   // MarkedRun: along = -t at the entry
            ok=ok && room>=settle;
        }
    Check(ok,"a marked strafe's entry leaves a settle of room on its line before its nose goes down: no miss, no lap");
    Check(StraightRunIn(true,0.0f,215.0f,1500.0f)>=1500.0f,"an unmarked gun run comes in from past its dive start");
    Check(StraightRunIn(false,300.0f,215.0f,1500.0f)==0.0f && StraightRunIn(true,300.0f,0.0f,1500.0f)==0.0f,
          "no run in asked for a pass that is not a jet's gun run");
}

// The rain's whole magazine fired at a target on the ground at the origin by a jet coming in along +z at `height` over
// it, its first volley `start` m out (level), the rest kVolleyGap frames apart as it flies on; `dive`: the nose
// straight at the target (the old NoseOn) instead of RainWant's.
crew::salvo_flight::Tally Rain(float height,float start,bool dive,crew::salvo_flight::GroundFn ground) {
    namespace sf=crew::salvo_flight;
    const float aim[3]={0.0f,ground(0.0f,0.0f)+2.0f,0.0f};
    const float step=kRainSpeed/60.0f;
    sf::Rng rng{12345u};
    sf::Tally tally;
    for(int v=0;v*crew::salvo::kVolley<crew::salvo::kRounds;++v) {
        const float pos[3]={0.0f,height,-(start-step*static_cast<float>(v*crew::salvo::kVolleyGap))};
        float los[3]={aim[0]-pos[0],aim[1]-pos[1],aim[2]-pos[2]};
        crew::vec::Normalize(los);
        const float level[3]={0.0f,0.0f,1.0f};   // at its height: Level's climb is nothing
        float nose[3];
        if(dive)std::memcpy(nose,los,12);
        else RainWant(level,pos,aim,nose);
        const float vel[3]={nose[0]*step,nose[1]*step,nose[2]*step};
        for(int k=0;k<crew::salvo::kVolley;++k)sf::Carrier(pos,nose,vel,aim,3.0f,ground,rng,tally);
    }
    return tally;
}
// Where the level rain's first volley goes from `height`: its hold opened (kRainOpen, kRainHoldMs settled, RainSteep)
// and the lock's kLockMs dwelt.
float RainStart(float height) {
    const float settled=kRainOpen-kRainSpeed*static_cast<float>(kRainHoldMs)/1000.0f,steep=height/kRainSlope;
    return (settled<steep ? settled : steep)-kRainSpeed*static_cast<float>(kLockMs)/1000.0f;
}
float Flat(float,float) { return 0.0f; }
// An 80 m ridge across the way in, 400 m short of the target.
float Ridge(float,float z) { const float d=(z+400.0f)/150.0f; return 80.0f*std::exp(-d*d); }

void RainClearance() {
    namespace sf=crew::salvo_flight;
    const float ahead[3]={0.0f,0.0f,1.0f},pos[3]={0.0f,400.0f,-2000.0f};
    const float ground[3]={0.0f,0.0f,0.0f},air[3]={0.0f,900.0f,0.0f};
    float want[3];
    RainWant(ahead,pos,ground,want);
    Check(want[1]==0.0f && want[2]==1.0f,"rain: level over a ground target, not down at it");
    RainWant(ahead,pos,air,want);
    Check(want[1]>0.2f && Near(crew::vec::Len(want),1.0f,1e-5f),"rain: the nose up on a target in the air above it");
    const float near[3]={0.0f,400.0f,-1199.0f};
    const float low[3]={0.0f,300.0f,0.0f};   // a flier 300 m up, under it
    Check(!RainSteep(pos,ground,0.0f) && RainSteep(near,ground,0.0f) && RainSteep(pos,air,0.0f) && !RainSteep(pos,low,0.0f) &&
          RainSteep(near,low,0.0f) && !RainSteep(pos,ground,300.0f),
          "rain: steep from kRainSlope over the ground under the target, a target above it at once");
    for(const float h:{320.0f,400.0f,500.0f}) {
        const float settled=kRainOpen-kRainSpeed*static_cast<float>(kRainHoldMs+kLockMs)/1000.0f;
        const sf::Tally was=Rain(h,settled,false,Flat),now=Rain(h,RainStart(h),false,Flat);
        std::printf("      level %.0f m: from %.0f m out %d of %d wasted; steep, from %.0f m out %d (%d hit, %d close)\n",h,settled,
                    was.wasted,was.Rounds(),RainStart(h),now.wasted,now.hit,now.close);
    }
    const sf::Tally flat=Rain(kRainAlt,RainStart(kRainAlt),false,Flat),ridge=Rain(kRainAlt,RainStart(kRainAlt),false,Ridge);
    const sf::Tally old=Rain(170.0f,2000.0f,true,Flat);
    std::printf("      the old dive, from 2000 m out 170 m up: %d of %d wasted; over the ridge now %d\n",old.wasted,old.Rounds(),
                ridge.wasted);
    const int rounds=crew::salvo::kRounds*pyconst::kSalvoChildren;
    Check(flat.Rounds()==rounds && flat.wasted*100<=flat.Rounds(),
          "rain: level at kRainAlt, steep, at most 1% of the rounds come down off the target");
    Check(ridge.wasted*100<=ridge.Rounds(),"rain: over an 80 m ridge as well");
    Check(flat.air==0 && flat.hit+flat.close+flat.wasted==flat.Rounds(),"rain: none left in the air");
    Check(old.wasted*4>old.Rounds(),"rain: the old dive at the target wasted over a quarter (the report)");
}

// The salvo's trigger (RainTrigger) level over a ground target: open from where RainSteep opens in to the burst's run
// short of it, and every first volley it lets go there brings its rounds down on the target; at the 2026-11-27 log's own
// open-fire point (15:35:01.861: the jet at (-544,668,-294) heading (-0.40,-0.92) at 110 m/s, its target at
// (-630,164,-560), 280 m ahead and 504 m under it: 1.07 rad off the nose, past the 0.8 rad cone it had then).
void RainTriggerCheck() {
    namespace sf=crew::salvo_flight;
    const float nose[3]={0.0f,0.0f,1.0f},flat[3]={0.0f,0.0f,0.0f};
    bool down=true,room=true;
    int worst=0;
    for(const float h:{250.0f,300.0f,350.0f,400.0f,450.0f,504.0f,600.0f}) {
        // Every first volley it lets go from where it turns steep (RainSteep) in, the rest as Rain flies them on.
        float nearest=0.0f;
        for(float d=h/kRainSlope;d>0.0f;d-=25.0f) {
            const float pos[3]={0.0f,h,-d};
            if(!RainTrigger(pos,nose,kRainSpeed,flat))continue;
            nearest=d;
            const sf::Tally t=Rain(h,d,false,Flat);
            if(t.wasted>worst)worst=t.wasted;
            down=down && t.wasted*100<=t.Rounds();
        }
        const float window=h/kRainSlope-nearest;
        std::printf("      trigger %.0f m up: open from %.0f m out to %.0f m%s",h,h/kRainSlope,nearest,"\n");
        room=room && nearest>0.0f && window>=kRainSpeed*static_cast<float>(kLockMs)/1000.0f;
    }
    std::printf("      from any first volley the trigger lets go: at most %d of %d wasted%s",worst,
                crew::salvo::kRounds*pyconst::kSalvoChildren,"\n");
    Check(down,"rain: from any first volley its trigger lets go (steep, in the cone, room for the burst) at most 1% come down off the target");
    Check(room,"rain: from 250 to 600 m up the trigger is open for the lock's dwell (kLockMs at kRainSpeed) past where it turns steep");
    const float pos[3]={-544.0f,668.0f,-294.0f},aim[3]={-630.0f,164.0f,-560.0f};
    float n[3]={-0.40f,0.0f,-0.92f};crew::vec::Normalize(n);
    const float to[3]={aim[0]-pos[0],aim[1]-pos[1],aim[2]-pos[2]};
    const float off=std::acos(crew::vec::Dot(to,n)/crew::vec::Len(to));
    std::printf("      the log's open-fire point: the target %.2f rad off the level nose%s",off,"\n");
    Check(off>0.8f && RainTrigger(pos,n,110.0f,aim),"rain: the log's open-fire point fires (past the 0.8 rad cone it had)");
    const float air[3]={-544.0f,900.0f,-294.0f},above[3]={-0.40f*0.6f,0.8f,-0.92f*0.6f};
    Check(RainTrigger(pos,above,110.0f,air),"rain: a flier above it, its nose on it, close in: fires");
}

// The 2026-11-27 log's missile rain flown again (vc.log.1 15:34:28.670 - vc.log 15:35:08.134): a wing at 110 m/s (its
// real speed) turning at 3.6 g (its ~340 m circles) held level at mark + kRainAlt, its nose toward its target as
// RainWant's, the stock map's soft box (x -747..750, z -1250..250), spawned at (-1575,1200) on its line to the mark
// (-182,264,-962), its target at (-630,164,-560). `arriving`: the soft edge lets it be till it has been inside
// (airbound::Arriving), else KeepIn's `back` from its spawn as before; `trigger` RainTrigger, else the 0.8 rad cone it
// had. The ms after it came its salvo goes, 0 if its window ended first.
unsigned long long RainLogReplay(bool arriving,bool trigger) {
    namespace ab=crew::airbound;
    const ab::Box soft{{-747.0f,-1250.0f},{750.0f,250.0f}};
    const float mark[3]={-182.0f,264.0f,-962.0f},aim[3]={-630.0f,164.0f,-560.0f};
    const float speed=110.0f,g=3.6f,react=1.0f+0.5f*3.14159265f*0.5f/1.4f,r=ab::TurnRadius(speed,g),dt=1.0f/60.0f;
    float pos[3]={-1575.0f,mark[1]+kRainAlt,1200.0f},head[3]={0.54f,0.0f,-0.84f};
    crew::vec::Normalize(head);
    float vel[3]={head[0]*speed,0.0f,head[2]*speed};
    const Line line=LineThrough(mark,pos,head);
    bool back=false,arrived=false;
    signed char turn=0;
    float asked=0.0f;
    int phase=0;
    unsigned long long since=0,lockAt=0;
    for(unsigned long long ms=0;ms<2*kSearchMs;ms+=16) {
        const float off[3]={pos[0]-mark[0],0.0f,pos[2]-mark[2]};
        const float dist=crew::vec::Len(off);
        if(dist<kRainTo || Along(line,pos)>kRainTo)return 0;
        float want[3]={aim[0]-pos[0],0.0f,aim[2]-pos[2]};
        crew::vec::Normalize(want);
        float nose[3]={vel[0],0.0f,vel[2]};
        crew::vec::Normalize(nose);
        const float c=crew::vec::Dot(nose,want);
        const float noseOff=std::acos(c>1.0f ? 1.0f : c<-1.0f ? -1.0f : c);
        if(RainHoldStep(phase,since,dist,noseOff,RainSteep(pos,aim,mark[1]),ms)) {
            const float to[3]={aim[0]-pos[0],aim[1]-pos[1],aim[2]-pos[2]};
            const bool in=trigger ? RainTrigger(pos,nose,speed,aim) : crew::vec::Dot(to,nose)>=std::cos(0.8f)*crew::vec::Len(to);
            if(!in)lockAt=0;
            else if(!lockAt)lockAt=ms;
            if(lockAt && ms-lockAt>=kLockMs)return ms;
        }
        float steer[3]={want[0],0.0f,want[2]};
        if(!(arriving && ab::Arriving(soft,pos,&arrived)))ab::KeepIn(soft,pos,vel,r,react,steer,&back,&turn);
        // The wing (airbound_check's): its heading turned toward `steer` at g, after `react` s of asking.
        const float have=std::atan2(vel[2],vel[0]),to=std::atan2(steer[2],steer[0]);
        float d=to-have;
        while(d>3.14159265f)d-=6.2831853f;
        while(d<-3.14159265f)d+=6.2831853f;
        asked=std::fabs(d)>0.02f ? asked+dt : 0.0f;
        const float step=(asked>react ? g*ab::kGrav/speed : 0.0f)*dt;
        const float h=have+(d>step ? step : d<-step ? -step : d);
        vel[0]=std::cos(h)*speed;vel[2]=std::sin(h)*speed;
        pos[0]+=vel[0]*dt;pos[2]+=vel[2]*dt;
    }
    return 0;
}

void RainLog() {
    const unsigned long long was=RainLogReplay(false,false),now=RainLogReplay(true,true);
    const unsigned long long edgeOnly=RainLogReplay(true,false),coneOnly=RainLogReplay(false,true);
    std::printf("      the log's rain: as it flew %llu ms (0: never); now %llu ms; the edge fixed alone %llu, the cone alone %llu\n",was,
                now,edgeOnly,coneOnly);
    Check(was==0,"the log's rain as it flew: its window ended with no salvo (the report)");
    Check(now>0 && now<20000,"the log's rain now: its salvo goes on its way in, inside 20 s of its coming");
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
    StraightIn();
    RainClearance();
    RainTriggerCheck();
    RainLog();
    const unsigned long long burst=static_cast<unsigned long long>(crew::salvo::VolleyFrames(crew::salvo::kRounds))*1000/60;
    Check(SalvoDraining(1000+burst,1000) && !SalvoDraining(1000+kRainBurstMs+1,1000) && !SalvoDraining(1000,0),
          "the jet stays on its pass until the volleys are out");
    std::printf(failures ? "\n%d check(s) FAILED\n" : "\nall checks passed\n",failures);
    return failures ? 1 : 0;
}
