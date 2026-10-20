// The ultimate strike's show without the game (src/ultimate_plan.h as the plugin replays it; docs/plan-2026-10-19-ultimate.md):
//  - the timeline: the phases in order, each frame in exactly one, every ring inside the cannon phase;
//  - the replay: the same seed gives the same bolts, rings and fires on any frame whatever was replayed before it (a late
//    joiner starts mid-show), another seed another storm;
//  - the area: every bolt and fire lands inside it, the rings reach its edge, each ring closed (its blasts touch);
//  - the rays from the circles: the aims leave the sky circles' rims (on their height) onto the edge and close in on the
//    centre, growing; the bolts leave the lowest sky circle's disc; the gather's rays are only in the gather, the bolts stop;
//  - the shock waves: each runs out before the show ends, the great one past the area's edge with the last ring;
//  - the load: bolts a frame and objects alive at once (a bolt's rounds live kBoltFrames) printed, under a ceiling.
//
//   cmake --build build --target ultimate_check && build\ultimate_check.exe        exit code 0: every check passed
#include "../src/ultimate_plan.h"
#include <cmath>
#include <cstdio>

namespace {
using namespace crew::ultimate;
int failures=0;
void Check(bool ok,const char* what) {
    std::printf("%s  %s\n",ok ? "ok  " : "FAIL",what);
    if(!ok)++failures;
}

void TimelineChecks() {
    bool ordered=true;
    Phase last=Phase::charge;
    for(int f=0;f<=kAfterEnd;++f) {
        const Phase p=PhaseAt(f);
        if(static_cast<int>(p)<static_cast<int>(last) || static_cast<int>(p)>static_cast<int>(last)+1)ordered=false;
        last=p;
    }
    Check(ordered && PhaseAt(0)==Phase::charge && PhaseAt(kAfterEnd)==Phase::over,"phases come in order, one after another");
    int rings=0,lastRing=-1;
    for(int f=0;f<kCannonEnd-kStormEnd;++f) {
        const int k=RingAt(f);
        if(k<0)continue;
        if(k!=lastRing+1)rings=-1000;
        lastRing=k;++rings;
    }
    Check(rings==kRings,"every ring goes off once, in order, inside the cannon phase");
    Check(kSkyBackAt>=kCannonEnd && kSkyBackAt+static_cast<int>(kSkyBackSec*kFps)<=kAfterEnd,"the sky is back before the show ends");
}

bool SameBolts(std::uint64_t a,std::uint64_t b,int frame) {
    Bolt x[kMaxBoltsAFrame],y[kMaxBoltsAFrame];
    const int n=BoltsAt(a,frame,x),m=BoltsAt(b,frame,y);
    if(n!=m)return false;
    for(int k=0;k<n;++k)
        if(x[k].at[0]!=y[k].at[0] || x[k].at[1]!=y[k].at[1] || x[k].from[0]!=y[k].from[0])return false;
    return true;
}

void ReplayChecks() {
    constexpr std::uint64_t seed=0x1234ABCDull;
    bool same=true,differs=false;
    for(int f=0;f<kStormEnd-kChargeEnd;++f) {
        same=same && SameBolts(seed,seed,f);
        differs=differs || !SameBolts(seed,seed+1,f);
    }
    // backwards: a frame's bolts never depend on the frames replayed before it
    Bolt late[kMaxBoltsAFrame],early[kMaxBoltsAFrame];
    const int nl=BoltsAt(seed,200,late);
    for(int f=0;f<200;++f){Bolt tmp[kMaxBoltsAFrame];BoltsAt(seed,f,tmp);}
    const int ne=BoltsAt(seed,200,early);
    same=same && nl==ne && (nl==0 || late[0].at[0]==early[0].at[0]);
    Check(same,"one seed, one storm: every frame's bolts the same, whatever was replayed before");
    Check(differs,"another seed, another storm");
    float a[2],b[2];
    Check(FireAt(seed,8,a) && FireAt(seed,8,b) && a[0]==b[0] && a[1]==b[1],"the fires replay too");
    Check(RingOf(5,seed).phase==RingOf(5,seed).phase && RingOf(5,seed).phase!=RingOf(5,seed+7).phase,"the rings' turn replays");
}

void AreaChecks() {
    bool inside=true;
    int most=0,total=0,alive=0,peak=0;
    int window[kBoltFrames]{};
    for(std::uint64_t seed=1;seed<=20;++seed) {
        for(int f=0;f<kStormEnd-kChargeEnd;++f) {
            Bolt b[kMaxBoltsAFrame];
            const int n=BoltsAt(seed,f,b);
            for(int k=0;k<n;++k) {
                if(std::hypot(b[k].at[0],b[k].at[1])>BoltReach(f)+1e-3f)inside=false;
                const float fromR=std::hypot(b[k].from[0],b[k].from[1]);
                if(std::hypot(b[k].at[0],b[k].at[1])>kRadius+1e-3f ||
                   fromR>kRadius*kBoltFrom+kBoltLean+1e-3f || fromR>kModelRadius[static_cast<int>(CircleModel::sky)]*(1.0f-kSqueeze))
                    inside=false;
            }
            if(n>most)most=n;
            total+=n;
            alive+=n-window[f%kBoltFrames];window[f%kBoltFrames]=n;
            if(alive>peak)peak=alive;
        }
        for(int f=0;f<kAfterEnd-kCannonEnd;++f) {
            float at[2];
            if(FireAt(seed,f,at) && std::hypot(at[0],at[1])>kRadius)inside=false;
        }
    }
    Check(inside,"every bolt and fire lands in the area (a bolt in the disc spreading from the centre), every bolt leaves the lowest sky circle's disc (drawn in or not)");
    char line[200];
    std::snprintf(line,sizeof(line),"the storm: %.0f bolts on average, at most %d a frame, at most %d bolt beams alive at once",
                  total/20.0,most,peak);
    Check(most<=kMaxBoltsAFrame && peak<=kMaxBoltsAFrame*kBoltFrames,line);
    const Ring edge=RingOf(kRings-1,1);
    Check(std::fabs(edge.radius-kRadius)<1e-3f && RingOf(0,1).radius==0.0f && RingOf(0,1).count==1,"the rings run from the centre to the edge");
    bool closed=true;
    int blasts=0;
    for(int k=1;k<kRings;++k) {
        const Ring r=RingOf(k,1);
        blasts+=r.count;
        if(2.0f*kPi*r.radius/r.count>2.0f*kRingBlast)closed=false;   // neighbours' blasts overlap: a closed wall
    }
    std::snprintf(line,sizeof(line),"every ring a closed wall of %0.f m blasts (%d blasts in all)",kRingBlast,blasts+1);
    Check(closed,line);
}

void AimChecks() {
    bool ok=true,fromCircle=true;
    for(int i=0;i<kAims;++i) {
        const int start=AimStart(i);
        ok=ok && !AimAt(i,start-1).shown && AimAt(i,start).shown && !AimAt(i,kChargeEnd).shown;
        const Aim s=AimAt(i,start),e=AimAt(i,kChargeEnd-1);
        ok=ok && std::fabs(std::hypot(s.at[0],s.at[1])-kRadius)<1e-2f && std::hypot(e.at[0],e.at[1])<1.0f && s.size<e.size;
        float last=1e9f;
        for(int f=start;f<kChargeEnd;f+=10) {
            const Aim a=AimAt(i,f);
            const float r=std::hypot(a.at[0],a.at[1]);
            ok=ok && r<=last+1e-3f;
            last=r;
            const int c=kSkyLow+i%3;
            const CirclePose p=CircleAt(c,f);
            const float rim=kModelRadius[static_cast<int>(CircleModel::sky)]*p.scale*kAimRim;
            fromCircle=fromCircle && p.shown && std::fabs(std::hypot(a.from[0],a.from[2])-rim)<1e-2f &&
                       std::fabs(a.from[1]-p.lift)<1e-3f;
        }
    }
    Check(ok,"each aim beam comes out once its circle is open, onto the edge, closing in on the centre, growing");
    Check(fromCircle,"every aim beam leaves its sky circle's rune band, at the circle's height, turning with it");
    Check(BeamScale(0)>0.0f && BeamScale(kBeamGrow)==1.0f && BeamScale(kCannonEnd-kStormEnd)==0.0f &&
          BeamScale(kCannonEnd-kStormEnd-1)<0.1f,"the main beam grows in and thins away");
}
void GatherChecks() {
    bool only=true,sized=true;
    for(int k=0;k<kGatherRays;++k) {
        only=only && !GatherAt(k,kGatherAt-1).shown && GatherAt(k,kGatherAt).shown && !GatherAt(k,kStormEnd).shown;
        sized=sized && GatherAt(k,kStormEnd-1).size>GatherAt(k,kGatherAt).size && CircleAt(GatherAt(k,kGatherAt).circle,kGatherAt).shown;
    }
    Check(only && sized,"the gather's rays: only in the gather, from circles that are there, growing");
    bool room=true;   // every ray longer than a pulse (the ground's plane up to kPlaneMost over the centre), its pulses ending on the centre
    for(int f=kGatherAt;f<kStormEnd;++f)
        for(int k=0;k<kGatherRays;++k) {
            const GatherRay g=GatherAt(k,f);
            float p[3];
            CirclePoint(g.circle,f,kGatherRim,g.angle,p);
            const float lift=kCircles[g.circle].sky ? p[1] : p[1]+kPlaneMost;
            const float d=std::hypot(std::hypot(p[0],p[2]),CircleAt(kSkyTop,f).lift-lift);
            room=room && d>kGatherReach+20.0f && g.slide>=0.0f && g.slide<1.0f;
        }
    Check(room,"every gather ray is longer than a pulse: each pulse runs from the rim and ends on the top circle's centre");
    int late=0;
    for(std::uint64_t seed=1;seed<=20;++seed)
        for(int f=kGatherAt-kChargeEnd+kBoltCalm;f<kStormEnd-kChargeEnd;++f){Bolt b[kMaxBoltsAFrame];late+=BoltsAt(seed,f,b);}
    Check(late==0,"the bolts have stopped for the gather's quiet");
    bool in=true;
    for(int i=0;i<kCircleCount;++i)
        in=in && CircleAt(i,kStormEnd-1).scale<CircleAt(i,kGatherAt-1).scale*(1.0f-kSqueeze*0.9f);
    Check(in,"every circle is drawn in by the gather");
}
void WaveChecks() {
    bool ends=true,grows=true;
    for(int w=0;w<kWaveCount;++w) {
        ends=ends && kWaves[w].at+WaveFrames(w)<=kAfterEnd && !WaveAt(w,kWaves[w].at-1).shown && WaveAt(w,kWaves[w].at).shown &&
             !WaveAt(w,kWaves[w].at+WaveFrames(w)).shown;
        grows=grows && WaveAt(w,kWaves[w].at+WaveFrames(w)-1).radius>WaveAt(w,kWaves[w].at+10).radius;
    }
    Check(ends && grows,"every shock wave runs out from the centre and is gone before the show ends");
    const WaveSpec& g=kWaves[kGreatWave];
    Check(g.at==kLastRing && g.reach>kRadius+kRingBlast,"the great shock wave goes with the last ring, out past the area's edge");
}
void CircleChecks() {
    bool inCharge=true,gone=true,bounded=true,smooth=true,shownWhole=true;
    for(int i=0;i<kCircleCount;++i) {
        const CircleSpec& c=kCircles[i];
        inCharge=inCharge && c.appear>=0 && c.appear+kCircleOpen<=kChargeEnd;
        gone=gone && !CircleAt(i,kAfterEnd-1).shown && !CircleAt(i,c.appear-1).shown && CircleAt(i,c.appear).shown;
        float lastAngle=CircleAt(i,c.appear).angle;
        for(int f=c.appear;f<kAfterEnd;++f) {
            const CirclePose p=CircleAt(i,f);
            if(!p.shown) {
                shownWhole=shownWhole && f>=kCircleCloseAt+i*kCircleStagger+kCircleClose;
                continue;
            }
            bounded=bounded && p.scale>0.0f && p.scale<=c.size*kPlaneReach && p.lift>=0.0f && p.lift<=c.lift+1e-3f;
            smooth=smooth && std::fabs(p.angle-lastAngle)<0.25f;   // never more than ~14 degrees a frame
            lastAngle=p.angle;
        }
    }
    Check(inCharge,"every circle opens out within the charge");
    Check(gone && shownWhole,"every circle is there from its frame until it has closed, and gone before the show ends");
    Check(bounded,"no circle grows past kPlaneReach of its size (the open's overshoot: its plane is clear of the ground that far) or rises past its height");
    Check(smooth,"the circles turn without jumps, the phases' paces joined");
    Check(std::fabs(SpinTime(kChargeEnd)-kChargeEnd/static_cast<float>(kFps))<1e-3f &&
          SpinTime(kStormEnd)>SpinTime(kChargeEnd),"the turn's time adds up phase by phase");
}
}  // namespace

int main() {
    TimelineChecks();
    ReplayChecks();
    AreaChecks();
    AimChecks();
    GatherChecks();
    WaveChecks();
    CircleChecks();
    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n",failures);
    return failures ? 1 : 0;
}
