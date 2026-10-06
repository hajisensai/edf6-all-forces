// The flyers' soft edge (src/airbound.h) offline: its band, its boxes and clamps, the reach of a turn back, and two
// stand-in flyers run against it (a wing turning at its g after a roll, a rotor braking at its brake), each chasing a
// target out past the edge from many speeds and headings: neither may go more than kTol past the soft line, and one
// started past it must come back in and stay off the hard edge. The jets' own flight code against it:
// jet_obstacle_sim --edge-suite.
//   cmake --build build --target airbound_check && build\airbound_check.exe
#include "../src/airbound.h"
#include <cmath>
#include <cstdio>
#include <initializer_list>

using namespace crew::airbound;

namespace {
int fails=0;
void Check(bool ok,const char* what) {
    if(!ok){std::printf("FAIL %s\n",what);++fails;}
}
bool Near(float a,float b,float tol) { return std::fabs(a-b)<=tol; }
constexpr float kTol=50.0f,kDt=1.0f/60.0f,kPi=3.14159265f;

// A wing: speed s, heading turned toward `want` at most g*kGrav/s rad/s, the turn biting only after `react` s of
// asking it (the roll); the turn's direction the shorter way round, as JetSteer's.
struct Wing { float pos[3],vel[3]; float asked; };
void WingStep(Wing& w,const float* want,float g,float react) {
    const float s=std::sqrt(w.vel[0]*w.vel[0]+w.vel[2]*w.vel[2]);
    const float have=std::atan2(w.vel[2],w.vel[0]),aim=std::atan2(want[2],want[0]);
    float d=aim-have;
    while(d>kPi)d-=2.0f*kPi;
    while(d<-kPi)d+=2.0f*kPi;
    w.asked=std::fabs(d)>0.02f ? w.asked+kDt : 0.0f;
    const float rate=w.asked>react ? g*kGrav/s : 0.0f,step=rate*kDt;
    const float turn=d>step ? step : d<-step ? -step : d;
    const float h=have+turn;
    w.vel[0]=std::cos(h)*s;w.vel[2]=std::sin(h)*s;
    for(int i=0;i<3;++i)w.pos[i]+=w.vel[i]*kDt;
}

// Runs a wing from `depth` inside the soft line of a 5250 m edge, `heading` deg off straight out (+x), at `speed`,
// chasing a point far out along its heading (or, `along`, flying along the edge out there). Returns the most it went
// past the soft line; `end` its depth at the end, `hard` the most past the hard edge.
float RunWing(float speed,float g,float roll,float heading,float depth,bool along,float seconds,float* end,float* hard,bool keep=true) {
    const Box edge=Square(5250.0f);
    const float r=TurnRadius(speed,g);
    const Box soft=Inset(edge,Band(r,1.0f,600.0f,5250.0f));
    const float react=1.0f+0.5f*kPi*0.5f/roll;
    const float a=heading*kPi/180.0f;
    Wing w{{soft.hi[0]-depth,500.0f,0.0f},{std::cos(a)*speed,0.0f,std::sin(a)*speed},0.0f};
    float goal[3]={w.pos[0]+std::cos(a)*30000.0f,500.0f,w.pos[2]+std::sin(a)*30000.0f};
    if(along){goal[0]=5850.0f;goal[2]=-3000.0f;}
    bool back=false;
    signed char turn=0;
    float most=-1e9f,worst=-1e9f;
    for(int f=0;f<static_cast<int>(seconds*60.0f);++f) {
        if(along)goal[2]+=150.0f*kDt;
        float want[3]={goal[0]-w.pos[0],0.0f,goal[2]-w.pos[2]};
        const float l=std::sqrt(want[0]*want[0]+want[2]*want[2]);
        want[0]/=l;want[2]/=l;
        if(keep)KeepIn(soft,w.pos,w.vel,r,react,want,&back,&turn);
        WingStep(w,want,g,react);
        const float past=-Depth(soft,w.pos),pastHard=-Depth(edge,w.pos);
        if(past>most)most=past;
        if(pastHard>worst)worst=pastHard;
    }
    *end=Depth(soft,w.pos);*hard=worst;
    return most;
}

// A rotor: its velocity closes on the wanted one at `brake`, after `react` s; it wants `speed` toward a goal out
// past the edge. The most it went past the soft line.
float RunRotor(float speed,float brake,float heading,float depth,float seconds,float* end) {
    const Box soft=Inset(Square(1000.0f),150.0f);
    const float a=heading*kPi/180.0f,react=0.5f;
    float pos[3]={soft.hi[0]-depth,50.0f,0.0f},vel[3]={std::cos(a)*speed,0.0f,std::sin(a)*speed};
    float lag[3]={vel[0],0.0f,vel[2]};   // the wanted velocity as it was `react` s ago (a first-order lag)
    float most=-1e9f;
    for(int f=0;f<static_cast<int>(seconds*60.0f);++f) {
        float want[3]={std::cos(a)*speed,0.0f,std::sin(a)*speed};
        LimitOut(soft,pos,brake,react,10.0f,want);
        for(int i=0;i<3;i+=2) {
            lag[i]+=(want[i]-lag[i])*kDt/react;
            const float dv=lag[i]-vel[i],step=brake*kDt;
            vel[i]+=dv>step ? step : dv<-step ? -step : dv;
            pos[i]+=vel[i]*kDt;
        }
        const float past=-Depth(soft,pos);
        if(past>most)most=past;
    }
    *end=Depth(soft,pos);
    return most;
}
}  // namespace

int main() {
    // The band: a 245 m/s 5 g fighter turns on 1225 m, one diameter 2450 m; inside a 4000 m edge only what leaves
    // kRoomTurns radii inside it; inside a stock 2400 m edge (no room for that) the ini's width.
    const float r=TurnRadius(245.0f,5.0f);
    Check(Near(r,1225.0f,1.0f),"turn radius 245 m/s at 5 g");
    Check(Near(Band(r,1.0f,600.0f,5250.0f),2450.0f,2.0f),"band: one full-speed turn diameter");
    Check(Near(Band(r,1.0f,600.0f,2400.0f),600.0f,0.1f),"band: a box too small for kRoomTurns radii keeps the ini's width");
    Check(Near(Band(r,1.0f,600.0f,1500.0f),275.0f,0.1f),"band: ...while a radius of room stays");
    Check(Near(Band(r,1.0f,600.0f,4000.0f),4000.0f-kRoomTurns*r,0.1f),"band: the soft box keeps kRoomTurns radii");
    Check(Near(Band(100.0f,1.0f,600.0f,5250.0f),600.0f,0.1f),"band: at least the ini's width");
    // Boxes.
    const Box soft=Inset(Square(5250.0f),2450.0f);
    Check(Near(soft.hi[0],2800.0f,0.1f) && Near(soft.lo[1],-2800.0f,0.1f),"inset");
    const Box tiny=Inset(Square(100.0f),300.0f);
    Check(tiny.lo[0]==0.0f && tiny.hi[0]==0.0f,"an inset past the middle stops there");
    const Box o=Overlap(Square(5900.0f),Box{{-999.0f,-999.0f},{999.0f,999.0f}});
    Check(Near(o.hi[0],999.0f,0.1f) && Near(o.lo[1],-999.0f,0.1f),"overlap");
    // A guard post (a map command's point) out past the soft edge is put inside it, less its circle; one inside stays.
    float post[3]={4000.0f,30.0f,-5000.0f};
    Check(ClampIn(soft,post,1400.0f) && Near(post[0],1400.0f,0.1f) && Near(post[2],-1400.0f,0.1f) && post[1]==30.0f,"post clamped in");
    float home[3]={100.0f,0.0f,-200.0f};
    Check(!ClampIn(soft,home,1400.0f) && home[0]==100.0f && home[2]==-200.0f,"post inside kept");
    // A turn's reach: straight out, its radius plus the roll; along the line, nothing; halfway, r (1 - cos 30).
    Check(Near(Excursion(200.0f,200.0f,1000.0f,1.0f),1200.0f,0.5f),"excursion straight out");
    Check(Excursion(200.0f,0.0f,1000.0f,1.0f)==0.0f && Excursion(200.0f,-50.0f,1000.0f,1.0f)==0.0f,"excursion along / in");
    Check(Near(Excursion(200.0f,100.0f,1000.0f,0.0f),1000.0f*(1.0f-std::sqrt(0.75f)),0.5f),"excursion 30 deg out");
    // Past the line heading out: back mode, turned square to its way (its turn back at its most); heading in already:
    // kept pointing in by kBackIn; back until kBackDepth inside.
    {
        bool back=false;
        signed char turn=0;
        float want[3]={1.0f,0.0f,0.0f};
        const float vel[3]={200.0f,0.0f,0.0f},past[3]={2810.0f,0.0f,0.0f};
        Check(KeepIn(soft,past,vel,1000.0f,1.0f,want,&back,&turn)==3 && back && turn!=0 && std::fabs(want[0])<1e-3f,
              "past the line heading out: turned back at its most");
        float want1[3]={1.0f,0.0f,0.0f};
        const float home1[3]={-200.0f,0.0f,10.0f};
        Check(KeepIn(soft,past,home1,1000.0f,1.0f,want1,&back,&turn)==3 && turn==0 && want1[0]<=-kBackIn+1e-3f,
              "past the line heading in: kept pointing in");
        float in[3]={2800.0f-kBackDepth*0.5f,0.0f,0.0f},want2[3]={1.0f,0.0f,0.0f};
        Check(KeepIn(soft,in,home1,1000.0f,1.0f,want2,&back,&turn)==3 && back,"still back until kBackDepth in");
        float deep[3]={2800.0f-kBackDepth-10.0f,0.0f,0.0f},want3[3]={-1.0f,0.0f,0.0f};
        KeepIn(soft,deep,home1,1000.0f,1.0f,want3,&back,&turn);
        Check(!back,"back inside past kBackDepth");
        // Into a corner (heading +z along the +x side): it turns back toward -x, out of the corner, not round into +x.
        bool b2=false;
        signed char t2=0;
        float corner[3]={2700.0f,0.0f,2000.0f},want4[3]={0.0f,0.0f,1.0f};
        const float up[3]={0.0f,0.0f,200.0f};
        KeepIn(soft,corner,up,1000.0f,1.0f,want4,&b2,&t2);
        Check(want4[0]<-0.9f,"into a corner: turned out of it");
    }
    // A climb at the soft ceiling: pushed over once its push-over would reach it.
    {
        float want[3]={0.0f,0.8f,0.6f};
        Check(CapClimb(900.0f,100.0f,200.0f,1000.0f,800.0f,1.0f,0.15f,want) && Near(want[1],-0.15f,1e-4f),"climb capped");
        float want2[3]={0.0f,0.8f,0.6f};
        Check(!CapClimb(0.0f,100.0f,200.0f,1000.0f,800.0f,1.0f,0.15f,want2) && want2[1]==0.8f,"low climb kept");
    }

    // The wing at the edge: the three NPC kinds' speeds and g (jet_internal.h kKinds: attack 215 / 235 / 245 m/s,
    // 5 g, roll 1.4 / 2.4 / 2.4 rad/s), at 60% and all of it, from far, near and at the line, every heading out. One
    // with room for its turn (Excursion, at the start) must not cross the soft line by more than kTol; one without
    // (started too near, too fast: what the band is for) must stay off the edge and end inside.
    const float kinds[3][3]={{215.0f,5.0f,1.4f},{235.0f,5.0f,2.4f},{245.0f,5.0f,2.4f}};
    float worst=-1e9f,worstNoKeep=-1e9f,worstTight=-1e9f;
    int runs=0,tight=0;
    for(const auto& k:kinds)
        for(const float share:{0.6f,1.0f})
            for(const float head:{0.0f,15.0f,45.0f,75.0f,89.0f})
                for(const float depth:{4000.0f,2000.0f,1500.0f,600.0f,100.0f}) {
                    const float s=k[0]*share,react=1.0f+0.5f*kPi*0.5f/k[2];
                    const float need=Excursion(s,s*std::cos(head*kPi/180.0f),TurnRadius(s,k[1]),react)+kSlack;
                    float end,hard;
                    const float past=RunWing(s,k[1],k[2],head,depth,false,60.0f,&end,&hard);
                    ++runs;
                    if(depth>=need) {
                        if(past>worst)worst=past;
                        Check(past<=kTol,"wing with room kept inside the soft line");
                        if(past>kTol)std::printf("  wing %.0f m/s head %.0f depth %.0f (needs %.0f): %.1f past, end %.0f\n",s,head,depth,need,past,end);
                        float e2,h2;
                        const float loose=RunWing(s,k[1],k[2],head,depth,false,60.0f,&e2,&h2,false);
                        if(loose>worstNoKeep)worstNoKeep=loose;
                    } else {
                        ++tight;
                        if(hard>worstTight)worstTight=hard;
                        Check(hard<=0.0f && end>=0.0f,"wing without room: off the edge, back inside");
                        if(hard>0.0f || end<0.0f)std::printf("  wing %.0f m/s head %.0f depth %.0f (needs %.0f): %.1f past the edge, end %.0f\n",s,head,depth,need,hard,end);
                    }
                }
    for(const auto& k:kinds) {
        float end,hard;
        const float past=RunWing(k[0],k[1],k[2],0.0f,2000.0f,true,90.0f,&end,&hard);
        ++runs;
        if(past>worst)worst=past;
        Check(past<=kTol,"wing chasing a target along the edge out there");
        const float back=RunWing(k[0],k[1],k[2],0.0f,-500.0f,false,90.0f,&end,&hard);
        ++runs;
        Check(hard<=0.0f && end>=0.0f,"wing started past the soft line comes back, never at the edge");
        std::printf("wing %3.0f m/s started 500 m past the soft line heading out: %5.0f m further out at most (edge %4.0f m on), %5.0f m inside at the end\n",
                    k[0],back-500.0f,-hard,end);
    }
    std::printf("wing: %d runs; with room for the turn at most %.1f m past the soft line (tolerance %.0f; the same runs without KeepIn: %.0f m);"
                " %d without room: at most %.1f m past the edge\n",runs,worst,kTol,worstNoKeep,tight,worstTight);
    Check(worstNoKeep>1000.0f,"the runs reach the line at all (without KeepIn they cross it)");

    // The rotor: helis (40-70 m/s, braking 4-8 m/s^2) chasing straight or slanted out; room as for the wing (its stop).
    float worstRotor=-1e9f;
    for(const float speed:{20.0f,40.0f,70.0f})
        for(const float brake:{4.0f,8.0f})
            for(const float head:{0.0f,45.0f,80.0f})
                for(const float depth:{800.0f,600.0f,100.0f,10.0f}) {
                    const float out=speed*std::cos(head*kPi/180.0f),need=out*0.5f+out*out/(2.0f*brake);
                    float end;
                    const float past=RunRotor(speed,brake,head,depth,90.0f,&end);
                    if(depth>=need) {
                        if(past>worstRotor)worstRotor=past;
                        Check(past<=kTol,"rotor with room kept inside the soft line");
                        if(past>kTol)std::printf("  rotor %.0f m/s brake %.0f head %.0f depth %.0f: %.1f past\n",speed,brake,head,depth,past);
                    } else Check(end>=-1.0f,"rotor without room ends back at the line");
                }
    {
        float end;
        RunRotor(40.0f,6.0f,0.0f,-100.0f,90.0f,&end);
        Check(end>=0.0f,"rotor started past the soft line comes back in");
    }
    std::printf("rotor: with room at most %.1f m past the soft line\n",worstRotor);
    std::printf("airbound_check: %s (%d failed)\n",fails ? "FAILED" : "ok",fails);
    return fails ? 1 : 0;
}
