// Offline flight of the hail's approach (src/playerjet_board.inc Approach / SteerAt, src/hail_glide.h) for every wing
// the player can call down, on flat ground, with the wing flight model of src/playerjet.cpp Air / AimSteer (the same
// handling functions, src/pjet_handling.h; formulas as tools/pjet_turn_sim.cpp mirrors them).
//  - the two logged hails of 2026-10-09 (EDF6VehicleCrew.log 10:34:38 and 11:24:15: fighters that crashed at 110 and
//    50 m/s of sink before their final) flown with the legacy law (aim at the entry, its lead shrinking, a 30 m floor
//    clamp) and with the production law: the legacy one must crash in at least one (the negative control: the sim
//    reproduces the fault), the production one must land in both;
//  - a sweep of hail states (heights, speeds, climbs, banks, headings round the strip): the production law never
//    meets the ground but on its touchdown, and lands within kLandSink m/s of sink, wings level.
// `hail_glide_check` (CTest): exit 1 on a miss. Flat ground and no obstacles: no claim about terrain or buildings.
#include "../src/hail_glide.h"
#include "../src/pjet_handling.h"
#include "../src/playerjet_kinds.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

namespace {
using namespace crew;
// playerjet.cpp's (lines 107-170) and playerjet_board.inc's constants.
constexpr float kG=9.8f,kHold=0.988f,kSettleSink=1.5f,kPush=0.5f,kInduced=0.3f,kVertical=0.97f,kSteer=1.6f;
constexpr float kAimBankMin=0.3f,kAimTurnFrom=0.09f,kAimSteep=0.77f,kStallFloor=25.0f,kBodyTop=340.0f,kTouch=3.0f;
constexpr float kLandSink=10.0f,kLandBank=0.77f,kLandNose=-0.26f;
constexpr float kFinal=1500.0f,kGlide=0.07f,kStripHigh=4.0f,kApproachOver=10.0f,kFinalLead=450.0f,kFinalOn=400.0f,kFinalAlign=0.85f;
constexpr float kMissPast=100.0f,kMissSide=150.0f,kMissHigh=80.0f,kMissLow=40.0f,kCatchFloor=30.0f;
constexpr float kAirbrakeOver=15.0f,kSpeedGain=0.03f;
constexpr int kTries=3;
constexpr float kDt=1.0f/60.0f,kPi=3.14159265f,kMostSeconds=240.0f;
bool trace=false;

float Dot(const float* a,const float* b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
void Cross(const float* a,const float* b,float* c){c[0]=a[1]*b[2]-a[2]*b[1];c[1]=a[2]*b[0]-a[0]*b[2];c[2]=a[0]*b[1]-a[1]*b[0];}
float Len(const float* a){return std::sqrt(Dot(a,a));}
bool Norm(float* a){const float l=Len(a);if(l<1e-6f)return false;for(int i=0;i<3;++i)a[i]/=l;return true;}
float Clamp(float x,float a,float b){return x<a ? a : x>b ? b : x;}
void Turn(float* v,const float* axis,float angle){float c[3];Cross(axis,v,c);const float co=std::cos(angle),si=std::sin(angle),a=Dot(axis,v)*(1.0f-co);
    for(int i=0;i<3;++i)v[i]=v[i]*co+c[i]*si+axis[i]*a;}
bool Across(float* u,const float* d){const float a=Dot(u,d);for(int i=0;i<3;++i)u[i]-=d[i]*a;return Norm(u);}
void BankToward(float* up,const float* dir,const float* want,float most){float c[3];Cross(up,want,c);
    Turn(up,dir,Clamp(std::atan2(Dot(c,dir),Dot(up,want)),-most,most));Across(up,dir);}
void RightOf(const float* d,float* r){r[0]=-d[2];r[1]=0;r[2]=d[0];if(!Norm(r)){r[0]=1;r[1]=0;r[2]=0;}}

struct Jet {
    float pos[3],dir[3],up[3],speed;
    float aim[3];
};
enum class Law { legacy, production };
enum class End { landed, crashed, gaveUp, refused };
struct Result { End end; float sink,speed,minClear,seconds; int phase; float beyond; };   // beyond: its furthest past the box (m, < 0 inside)

// One frame of Air with the aim's law (AimSteer) toward `aim`, at the throttle SteerAt sets for `want`.
void Fly(Jet& j,const pjet::Perf& k,const float* aim,float want){
    float dir[3];std::memcpy(dir,j.dir,12);
    const float speed=j.speed<kStallFloor ? kStallFloor : j.speed;
    float level[3]={-dir[0]*dir[1],1.0f-dir[1]*dir[1],-dir[2]*dir[1]};
    const bool vertical=!Norm(level) || std::fabs(dir[1])>kVertical;
    if(vertical){level[0]=0;level[1]=1;level[2]=0;}
    Across(j.up,dir);
    const float wing=speed<k.corner ? (speed/k.corner)*(speed/k.corner) : 1.0f;
    const float most=k.maxG*kG*wing;
    const float gPerp[3]={dir[0]*kG*dir[1],-kG+dir[1]*kG*dir[1],dir[2]*kG*dir[1]};
    const float across=Len(gPerp);
    const float hold=Clamp(across*Clamp(1.0f-(1.0f-kHold)*(1.0f-(-dir[1]*speed)/kSettleSink),kHold,1.0f),0.0f,most);
    const float c=Clamp(Dot(aim,dir),-1.0f,1.0f);
    float toward[3]={aim[0]-dir[0]*c,aim[1]-dir[1]*c,aim[2]-dir[2]*c};
    if(!Norm(toward)){if(c>0.0f)toward[0]=toward[1]=toward[2]=0.0f;else RightOf(dir,toward);}
    const float off=std::acos(c),turn=kSteer*handling::AimShare(off)*off*speed;
    float l[3];for(int i=0;i<3;++i)l[i]=toward[i]*turn-(across>1e-4f ? gPerp[i]/across*hold : 0.0f);
    float w[3];std::memcpy(w,l,12);
    const bool steep=vertical || std::fabs(dir[1])>kAimSteep;
    if(Len(l)>kAimBankMin*kG && (off>=kAimTurnFrom || !steep) && Across(w,dir))
        BankToward(j.up,dir,w,handling::AimRoll(handling::PathRoll(k.roll,1.0f),off)*kDt);
    const float lift=Clamp(Dot(l,j.up),-kPush*most,most);
    float next[3];for(int i=0;i<3;++i)next[i]=dir[i]+(j.up[i]*lift+gPerp[i])*kDt/speed;
    Norm(next);Across(j.up,next);
    // SteerAt's throttle and airbrake, Air's engine and drag.
    const float throttle=Clamp((want-k.minAir)/(k.top-k.minAir)+(want-speed)*kSpeedGain,0.0f,1.0f);
    const float wantT=k.minAir+throttle*(k.top-k.minAir),top2=k.top*k.top,g=lift/kG,slow=k.corner/speed;
    const float thrust=k.thrust*wantT*wantT/top2,drag=k.thrust*speed*speed/top2+kInduced*g*g*slow*slow;
    const float brake=speed>want+kAirbrakeOver ? k.brake*speed*speed/top2 : 0.0f;
    j.speed=Clamp(speed+(thrust-drag-brake-kG*next[1])*kDt,kStallFloor,kBodyTop);
    std::memcpy(j.dir,next,12);
    for(int i=0;i<3;++i)j.pos[i]+=j.dir[i]*j.speed*kDt;
}

// playerjet_board.inc FerryRadius: the ferry's turns, at its speed (k.rotate + kApproachOver * 4).
float FerryRadius(const pjet::Perf& k){
    const float speed=k.rotate+kApproachOver*4.0f,wing=speed<k.corner ? (speed/k.corner)*(speed/k.corner) : 1.0f;
    return hail::TurnRadius(speed,hail::LevelTurnLateral(k.maxG*kG*wing));
}
const hail::Box kOpen{{-1e6f,-1e6f},{1e6f,1e6f}};
// The hail from `start` to a strip touching down at `touch` along heading `head` deg, on flat ground at touch[1], the
// map's walls `box` (the production law plans its approach inside it, hail_glide.h PlanPattern: refused if none fits).
Result Hail(const pjet::Perf& k,Jet j,const float* touch,float head,Law law,const hail::Box& box=kOpen){
    const float d[3]={std::sin(head*kPi/180.0f),0.0f,std::cos(head*kPi/180.0f)};
    const float ground=touch[1];
    Result r{End::gaveUp,0,0,1e9f,0,0,-1e9f};
    hail::Pattern pattern;
    if(law==Law::production) {
        pattern=hail::PlanPattern(touch,d,FerryRadius(k),box);
        if(!pattern.ok){r.end=End::refused;return r;}
    } else pattern.room=1e9f;
    const float entry[3]={touch[0]-d[0]*kFinal,touch[1]+kStripHigh+kFinal*kGlide,touch[2]-d[2]*kFinal};
    bool final=false,inbound=false;int tries=0;
    for(float t=0;t<kMostSeconds;t+=kDt) {
        const float clear=j.pos[1]-ground;
        if(clear<r.minClear)r.minClear=clear;
        const float past=std::fmax(std::fmax(box.lo[0]-j.pos[0],j.pos[0]-box.hi[0]),std::fmax(box.lo[1]-j.pos[2],j.pos[2]-box.hi[1]));
        if(past>r.beyond)r.beyond=past;
        float track[3]={j.dir[0],0.0f,j.dir[2]};Norm(track);
        float goal[3]{};float want=k.rotate+kApproachOver*4.0f;
        if(final) {   // Approach's final: a missed approach goes back to the ferry (at most kTries)
            const float rel[3]={j.pos[0]-touch[0],0.0f,j.pos[2]-touch[2]};
            const float along=rel[0]*d[0]+rel[2]*d[2],side=std::fabs(rel[0]*d[2]-rel[2]*d[0]);
            const float glide=touch[1]+kStripHigh-along*kGlide;
            if(along>kMissPast || (along<-kFinalLead && side>kMissSide) || j.pos[1]>glide+kMissHigh || j.pos[1]<glide-kMissLow) {
                if(++tries>=kTries){r.seconds=t;return r;}
                final=false;inbound=false;   // Approach: back to kHailFerry, the outer point
            } else {
                const float ahead=along+kFinalLead;
                goal[0]=touch[0]+d[0]*ahead;goal[1]=touch[1]+kStripHigh-ahead*kGlide;goal[2]=touch[2]+d[2]*ahead;
                want=k.rotate+kApproachOver;
            }
        }
        if(!final) {   // the ferry: the legacy goal, or hail_glide.h FerryGoal
            const float out=std::hypot(j.pos[0]-entry[0],j.pos[2]-entry[2]);
            if(law==Law::legacy){const float lead=Clamp(out*0.5f,0.0f,2000.0f);
                goal[0]=entry[0]-d[0]*lead;goal[1]=entry[1];goal[2]=entry[2]-d[2]*lead;}
            else {
                const float wing=j.speed<k.corner ? (j.speed/k.corner)*(j.speed/k.corner) : 1.0f;
                hail::FerryGoal(j.pos,track,entry,d,pattern.room,hail::TurnRadius(j.speed,hail::LevelTurnLateral(k.maxG*kG*wing)),inbound,goal);
            }
            if(out<kFinalOn && Dot(track,d)>kFinalAlign && (law==Law::legacy || hail::FinalReady(j.pos,entry,d,kMissHigh,kMissLow,kMissSide)))final=true;   // the final's goal from the next frame, as Approach
        }
        float to[3]={goal[0]-j.pos[0],goal[1]-j.pos[1],goal[2]-j.pos[2]};
        if(law==Law::legacy){if(!final && clear<kCatchFloor && to[1]<0.0f)to[1]=0.0f;}
        else {
            const float vel[3]={j.dir[0]*j.speed,j.dir[1]*j.speed,j.dir[2]*j.speed};
            hail::LimitAim(to,vel,clear,true,final);
            const float wing=j.speed<k.corner ? (j.speed/k.corner)*(j.speed/k.corner) : 1.0f;
            hail::LimitTurn(to,vel,hail::LevelTurnLateral(k.maxG*kG*wing),kSteer);
        }
        if(Norm(to))std::memcpy(j.aim,to,12);
        if(trace && std::fmod(t,1.0f)<kDt)std::printf("  t=%5.1f pos=(%6.0f,%5.0f,%6.0f) v=%5.1f climb=%5.1f up.y=%5.2f %s goal=(%6.0f,%5.0f,%6.0f) aim.y=%5.2f\n",
            t,j.pos[0],j.pos[1],j.pos[2],j.speed,std::asin(Clamp(j.dir[1],-1,1))*57.3f,j.up[1],final ? "FINAL" : "ferry",goal[0],goal[1],goal[2],j.aim[1]);
        Fly(j,k,j.aim,want);
        if(j.pos[1]-ground<=kTouch && j.dir[1]<0.0f) {
            const float sink=-j.dir[1]*j.speed;
            const bool level=j.up[1]>=kLandBank,nose=j.dir[1]>=kLandNose;
            r.sink=sink;r.speed=j.speed;r.seconds=t;r.phase=final ? 1 : 0;
            r.end=sink<=kLandSink && level && nose && j.speed<=k.landMax && final ? End::landed : End::crashed;
            return r;
        }
    }
    r.seconds=kMostSeconds;return r;
}

Jet Start(float x,float y,float z,float speed,float climbDeg,float headDeg,float bankDeg){
    Jet j{};j.pos[0]=x;j.pos[1]=y;j.pos[2]=z;j.speed=speed;
    const float c=climbDeg*kPi/180.0f,h=headDeg*kPi/180.0f;
    j.dir[0]=std::cos(c)*std::sin(h);j.dir[1]=std::sin(c);j.dir[2]=std::cos(c)*std::cos(h);
    float level[3]={-j.dir[0]*j.dir[1],1.0f-j.dir[1]*j.dir[1],-j.dir[2]*j.dir[1]};Norm(level);
    std::memcpy(j.up,level,12);Turn(j.up,j.dir,bankDeg*kPi/180.0f);
    std::memcpy(j.aim,j.dir,12);
    return j;
}
const char* Name(End e){return e==End::landed ? "landed" : e==End::crashed ? "CRASHED" : e==End::refused ? "refused" : "gave up";}
}  // namespace

int main(int argc,char** argv){
    int fails=0;trace=argc>1;
    if(argc>7) {   // one case traced: kind y speed climb bank bearing(0-3) heading
        for(const auto& b:pjet::kBoardable)if(std::strcmp(b.perf.name,argv[1])==0) {
            const float a=std::atoi(argv[6])*kPi/2.0f;const float touch[3]={0,0,0};
            const Jet s=Start(std::sin(a)*1500.0f,(float)std::atof(argv[2]),std::cos(a)*1500.0f,(float)std::atof(argv[3]),
                              (float)std::atof(argv[4]),(float)std::atof(argv[7]),(float)std::atof(argv[5]));
            const Result r=Hail(b.perf,s,touch,0.0f,Law::production);
            std::printf("%s sink %.1f speed %.1f\n",Name(r.end),r.sink,r.speed);
        }
        return 0;
    }
    const pjet::Perf* fighter=nullptr;
    for(const auto& b:pjet::kBoardable)if(std::strcmp(b.perf.name,"fighter")==0)fighter=&b.perf;
    if(!fighter){std::printf("FAIL no fighter perf\n");return 1;}
    // The logged hails (EDF6VehicleCrew.log, 2026-10-09).
    struct Logged { const char* when; Jet start; float touch[3],head; };
    const Logged logged[]={
        {"10:34:38",Start(-769,288,-666,133,32.7f,-47,65),{112,1,-89},-60},
        {"11:24:15",Start(-795,607,-187,131,-3.2f,28,172),{-456,0,-250},30},
    };
    int legacyCrashes=0;
    for(const auto& l:logged) {
        const Result old=Hail(*fighter,l.start,l.touch,l.head,Law::legacy),now=Hail(*fighter,l.start,l.touch,l.head,Law::production);
        legacyCrashes+=old.end==End::crashed;
        const bool ok=now.end==End::landed;
        std::printf("%s logged hail %s: legacy %s (sink %.1f m/s, %.0f s), production %s (sink %.1f m/s at %.0f m/s, %.0f s, lowest %.0f m before)\n",
                    ok ? "ok  " : "FAIL",l.when,Name(old.end),old.sink,old.seconds,Name(now.end),now.sink,now.speed,now.seconds,now.minClear);
        fails+=!ok;
    }
    const bool control=legacyCrashes>0;
    std::printf("%s negative control: the legacy law crashes %d of %zu logged hails in this model\n",control ? "ok  " : "FAIL",legacyCrashes,
                sizeof(logged)/sizeof(logged[0]));
    fails+=!control;
    // The sweep: every wing kind, hail states round a strip at the origin heading north.
    const float touch[3]={0,0,0};
    int flown=0,landed=0,gaveUpShown=0;
    for(const auto& b:pjet::kBoardable) {
        if(b.frame!=pjet::Airframe::wing)continue;
        const auto& k=b.perf;int kindFails=0;const int landedBefore=landed,flownBefore=flown;
        for(float y:{250.0f,500.0f,800.0f})for(float sp:{k.minAir+20.0f,0.5f*(k.minAir+k.top),k.top-10.0f})
        for(float climb:{-20.0f,0.0f,30.0f})for(float bank:{0.0f,90.0f,175.0f})for(int bearing=0;bearing<4;++bearing)for(int head=0;head<4;++head) {
            const float a=bearing*kPi/2.0f;
            const Jet s=Start(std::sin(a)*1500.0f,y,std::cos(a)*1500.0f,sp,climb,head*90.0f+20.0f,bank);
            const Result r=Hail(k,s,touch,0.0f,Law::production);
            ++flown;
            if(r.end==End::landed)++landed;
            else if(r.end==End::gaveUp && trace && gaveUpShown++<6)std::printf("  gave up: %s y %.0f speed %.0f climb %.0f bank %.0f bearing %d heading %.0f\n",
                k.name,y,sp,climb,bank,bearing,head*90.0f+20.0f);
            // Never into the ground: a hail that does not land must give up in the air (HandBack), not crash.
            if(r.end==End::crashed){
                if(kindFails++<3)std::printf("FAIL %s from (%.0f,%.0f,%.0f) %.0f m/s climb %.0f bank %.0f heading %.0f: crashed at %.1f m/s sink, %.0f m/s (phase %s)\n",
                    k.name,s.pos[0],s.pos[1],s.pos[2],sp,climb,bank,head*90.0f+20.0f,r.sink,r.speed,r.phase ? "final" : "ferry");
            }
        }
        fails+=kindFails;
        std::printf("%s %-12s sweep: %d crashes, %d of %d landed\n",kindFails ? "FAIL" : "ok  ",k.name,kindFails,landed-landedBefore,
                    flown-flownBefore);
    }
    // Most of the sweep must actually land (a law that never descends would not crash either).
    const bool lands=landed*10>=flown*9;
    std::printf("%s %d of %d swept hails landed\n",lands ? "ok  " : "FAIL",landed,flown);
    fails+=!lands;
    // 2026-10-09 16:53:56 (EDF6VehicleCrew.log): a gunship hailed on a map whose walls stand at +-1600 flew 4 minutes
    // along the wall at z -1600 for an outer point past it, until the hail ran out. Negative control: that approach's
    // outer point (straight behind a 1500 m final, 4 turn radii) lies outside the walls.
    const hail::Box walls{{-1600.0f,-1600.0f},{1597.0f,1597.0f}};
    const pjet::Perf* gunship=nullptr;
    for(const auto& b:pjet::kBoardable)if(std::strcmp(b.perf.name,"gunship")==0)gunship=&b.perf;
    if(!gunship){std::printf("FAIL no gunship perf\n");return 1;}
    {
        const float touch16[3]={347,24,129},head16=-30.0f,d16[2]={std::sin(head16*kPi/180.0f),std::cos(head16*kPi/180.0f)};
        const float room=std::fmax(hail::kTurnRoom,hail::kOuterTurns*FerryRadius(*gunship));
        const float oldOuter[2]={touch16[0]-d16[0]*(kFinal+room),touch16[2]-d16[1]*(kFinal+room)};
        const bool outside=!hail::InBox(walls,oldOuter[0],oldOuter[1],0.0f);
        std::printf("%s negative control: the 16:53 gunship's old outer point (%.0f,%.0f) lies %s the walls\n",outside ? "ok  " : "FAIL",
                    oldOuter[0],oldOuter[1],outside ? "outside" : "inside");
        fails+=!outside;
        const Result r=Hail(*gunship,Start(-601,399,161,75,0,-6,57),touch16,head16,Law::production,walls);
        const bool honest=r.end==End::refused || (r.end==End::landed && r.beyond<0.0f);
        std::printf("%s the 16:53 gunship hail now: %s (never flown at the wall: refused at once, or landed inside it)\n",honest ? "ok  " : "FAIL",Name(r.end));
        fails+=!honest;
    }
    {   // The entry fits, the outer point (4 turn radii further back) does not: refused, not flown at the wall.
        const pjet::Perf* fighter2=fighter;const hail::Box mid{{-2500.0f,-2500.0f},{2500.0f,2500.0f}};
        const float t0[3]={0,0,0},north[3]={0,0,1},R=FerryRadius(*fighter2);
        const bool entryIn=hail::InBox(mid,0.0f,-hail::kFinalLength,0.5f*R);
        const bool refused=!hail::PlanPattern(t0,north,R,mid).ok;
        std::printf("%s a strip whose final fits but whose outer point does not is refused (entry inside %d)\n",entryIn && refused ? "ok  " : "FAIL",entryIn);
        fails+=!(entryIn && refused);
    }
    // Every wing on that map: strips round its middle (12 headings), hailed from states 600 m from the middle. Where an
    // approach fits (PlanPattern) it lands, never meets the ground and never goes past a wall (there the wall turns it back);
    // where none fits the hail is refused at once.
    const hail::Box big{{-4000.0f,-4000.0f},{4000.0f,4000.0f}};   // a BigWorld map's
    for(const hail::Box* box:{&walls,&big})for(const auto& b:pjet::kBoardable) {
        if(b.frame!=pjet::Airframe::wing)continue;
        const auto& k=b.perf;int planned=0,down=0,out=0,crashed=0,cases=0;float worst=-1e9f;
        for(int h=0;h<12;++h)for(float tx:{-400.0f,0.0f,400.0f}) {
            const float touch2[3]={tx,0,tx*0.5f},head=h*30.0f;
            const float dd[3]={std::sin(head*kPi/180.0f),0.0f,std::cos(head*kPi/180.0f)};
            if(!hail::PlanPattern(touch2,dd,FerryRadius(k),*box).ok)continue;
            ++planned;
            for(float y:{250.0f,500.0f})for(float bank:{0.0f,90.0f})for(int bearing=0;bearing<4;++bearing)for(int hd=0;hd<4;++hd) {
                const float a=bearing*kPi/2.0f;
                const Result r=Hail(k,Start(std::sin(a)*600.0f,y,std::cos(a)*600.0f,0.5f*(k.minAir+k.top),0.0f,hd*90.0f+20.0f,bank),touch2,head,Law::production,*box);
                ++cases;down+=r.end==End::landed;crashed+=r.end==End::crashed;
                if(r.beyond>worst)worst=r.beyond;
                if(r.beyond>0.0f)++out;
            }
        }
        const bool ok=!crashed && !out && (!cases || down*10>=cases*8);
        std::printf("%s %-12s (turns of %.0f m) on a +-%.0f map: %d of 36 strips fit, %d of %d hails landed, %d crashed, %d went past a wall (furthest %.0f m)\n",
                    ok ? "ok  " : "FAIL",k.name,FerryRadius(k),box->hi[0],planned,down,cases,crashed,out,worst);
        fails+=!ok;
    }
    std::printf(fails ? "hail_glide_check: %d FAILED\n" : "hail_glide_check: all passed\n",fails);
    return fails ? 1 : 0;
}
