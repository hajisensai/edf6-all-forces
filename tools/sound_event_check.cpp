// The ground guns' War Thunder layers offline (src/sound_event.h, played by vehsound.cpp): the curves are straight in
// dB and geometric in Hz between their points and held past them; near a gun its shot, its own recording, its metal and
// its bass are heard and the far layers are not; far off the far recording and the ground's bump are, the near ones
// not; past 500 m 6 dB down a doubling; the whole event quieter the farther; the low layer alone goes to the LFE; every layer names
// a recording group of the set; the variants never repeat back to back and all come round; the machine guns' tails are
// the rounds' 6 dB down, the fast guns the MG42's grains.
//
//   cmake --build build --target sound_event_check && build\sound_event_check.exe
#include "../src/sound_event.h"
#include <cmath>
#include <cstdio>

namespace {
using namespace crew::wtevent;
int failures=0;
void Check(bool ok,const char* what) {
    std::printf("%s  %s\n",ok ? "ok  " : "FAIL",what);
    if(!ok)++failures;
}
bool Near(float a,float b,float eps=1e-3f) { return std::fabs(a-b)<=eps*(1.0f+std::fabs(b)); }

bool Has(const Heard* h,int n,int clip) {
    for(int i=0;i<n;++i)if(h[i].clip==clip)return true;
    return false;
}
float GainOf(const Heard* h,int n,int clip) {
    for(int i=0;i<n;++i)if(h[i].clip==clip)return h[i].gain;
    return 0.0f;
}
float Power(const Event& e,float d) {
    Heard h[kMostLayers];
    const int n=Hear(e,d,h);
    float p=0.0f;
    for(int i=0;i<n;++i)p+=h[i].gain*h[i].gain;
    return p;
}

void Curves() {
    Check(Near(At(kShot,0.0f),6.0f) && Near(At(kShot,350.0f),-37.0f) && Near(At(kShot,5000.0f),-80.0f) &&
              Near(At(kShot,-3.0f),6.0f),"a gain curve: straight in dB, held past its ends");
    Check(Near(At(kFar,93.2203f),8.0f) && At(kFar,50.0f)<At(kFar,93.0f) && At(kFar,1000.0f)<At(kFar,200.0f),
          "the far layer: rising to 93 m, falling to 2000 m");
    Check(Near(At(kOpen,123.0f),0.0f) && Near(CutoffAt(kOpen,123.0f),24000.0f),"no curve: 0 dB, no low-pass");
    Check(Near(CutoffAt(kCannonEq,500.0f),std::sqrt(22000.0f*20.0f)) && Near(CutoffAt(kCannonEq,3000.0f),20.0f),
          "a cutoff curve: geometric between its points, held past them");
    Check(Near(CutoffAt(kMgFarEq,1000.0f),3500.0f) && CutoffAt(kMgFarEq,1500.0f)<3500.0f,"the MG far EQ: 3.5 kHz at 1 km, lower on");
}

void Layers() {
    const Event* all[]={&kMedium,&kTank,&kHeavy,&kHowitzer,&kM2.round,&kM2.last,&kMg42.round,&kMg42.last};
    bool clips=true,subs=true,times=true;
    for(const Event* e : all) {
        clips=clips && e->layers>0 && e->layers<=kMostLayers;
        for(int i=0;i<e->layers;++i) {
            const Layer& l=e->layer[i];
            clips=clips && l.clip>=0 && l.clip<crew::wt::kCount && crew::wt::kVariants[l.clip]>0;
            subs=subs && (l.sub==(l.clip==crew::wt::k_sub));
            times=times && l.after>=0.0f && l.after<1.0f;
        }
    }
    Check(clips,"every layer names a recording group of the set");
    Check(subs,"only the sub layer goes to the LFE");
    Check(times,"every layer starts within a second of the shot");
}

void Distances() {
    const Event* cannons[]={&kMedium,&kTank,&kHeavy,&kHowitzer};
    const int close[]={crew::wt::k_close_75,crew::wt::k_close_122,crew::wt::k_close_128,crew::wt::k_close_150};
    const int farOne[]={crew::wt::k_far_75,crew::wt::k_far_122,crew::wt::k_far_128,crew::wt::k_far_150};
    bool nearOk=true,farOk=true,beyond=true,falling=true,finite=true,loud=true;
    for(int k=0;k<4;++k) {
        const Event& e=*cannons[k];
        Heard h[kMostLayers];
        int n=Hear(e,10.0f,h);
        nearOk=nearOk && Has(h,n,close[k]) && Has(h,n,crew::wt::k_metal) && Has(h,n,crew::wt::k_bass) && Has(h,n,crew::wt::k_sub) &&
               !Has(h,n,farOne[k]) && !Has(h,n,crew::wt::k_bump_med) && !Has(h,n,crew::wt::k_bump_big);
        float most=0.0f;
        for(int i=0;i<n;++i){finite=finite && std::isfinite(h[i].gain) && h[i].gain>0.0f && std::isfinite(h[i].cutoff);most=std::fmax(most,h[i].gain);}
        loud=loud && most>0.5f && most<4.0f;   // about full scale near the gun (kTrimDb; the 150's Close track +10)
        const float closeNear=GainOf(h,n,close[k]);
        n=Hear(e,400.0f,h);
        farOk=farOk && Has(h,n,farOne[k]) && (Has(h,n,crew::wt::k_bump_med) || Has(h,n,crew::wt::k_bump_big)) &&
              !Has(h,n,crew::wt::k_metal) && !Has(h,n,crew::wt::k_bass) && GainOf(h,n,close[k])<closeNear*0.1f;
        n=Hear(e,1500.0f,h);
        farOk=farOk && Has(h,n,farOne[k]) && !Has(h,n,close[k]);
        beyond=beyond && Near(Power(e,2000.0f),Power(e,1000.0f)*0.25f,0.02f) && Hear(e,1e6f,h)==0;
        for(float d=100.0f;d<2000.0f;d+=50.0f)falling=falling && Power(e,d+50.0f)<Power(e,d);
    }
    Check(nearOk,"10 m from a gun: its own recording, the metal, the bass and the sub; no far layer, no bump");
    Check(loud,"near a gun its loudest layer is about full scale, every gain finite");
    Check(farOk,"400 m and 1500 m off: the far recording (and the bump), the near layers gone");
    Check(beyond,"past 500 m: 6 dB down a doubling, nothing at 1000 km");
    Check(falling,"from 100 m on, the farther the quieter");
    Heard h[kMostLayers];
    const int n=Hear(kTank,300.0f,h);
    float closeCut=0.0f,farCut=0.0f;
    for(int i=0;i<n;++i){if(h[i].clip==crew::wt::k_close_122)closeCut=h[i].cutoff;if(h[i].clip==crew::wt::k_far_122)farCut=h[i].cutoff;}
    Check(closeCut>0.0f && closeCut<4000.0f && farCut>closeCut,"300 m off: the close recording dulled by WT's EQ, the far one by the air");
    Check(Near(Doppler(kTank,1.2f),1.1f) && Near(Doppler(kTank,NAN),1.0f),"half the Doppler shift");
}

void Machine() {
    Check(&MgFor(2.0f)==&kMg42 && &MgFor(6.0f)==&kM2 && &MgFor(0.0f)==&kM2,"a fast gun the MG42's grains, a slower one the M2's");
    Check(Near(kM2.last.db,kM2.round.db-6.0f) && Near(kMg42.last.db,kMg42.round.db-6.0f),"the tails: the rounds' 6 dB down");
    Heard h[kMostLayers];
    int n=Hear(kM2.round,20.0f,h);
    Check(n==2,"20 m from an M2: its close and far grains");
    n=Hear(kM2.round,600.0f,h);
    Check(n==1 && h[0].clip==crew::wt::k_m2_far,"600 m off: the far grain alone");
}

void Rotations() {
    Rotation r{};
    bool repeat=false;
    int seen[crew::wt::kMostVariants]{};
    int last=-1;
    for(int i=0;i<200;++i) {
        const int v=Next(r,crew::wt::k_m2_close);
        repeat=repeat || v==last || v<0 || v>=crew::wt::kVariants[crew::wt::k_m2_close];
        if(v>=0 && v<crew::wt::kMostVariants)++seen[v];
        last=v;
    }
    bool all=true;
    for(int v=0;v<crew::wt::kVariants[crew::wt::k_m2_close];++v)all=all && seen[v]>0;
    Check(!repeat,"never the same variant twice running");
    Check(all,"every variant comes round");
    Check(Next(r,-1)==0 && Next(r,crew::wt::kCount)==0,"a bad group: the first variant");
}
}  // namespace

int main() {
    Curves();
    Layers();
    Distances();
    Machine();
    Rotations();
    std::printf(failures ? "%d FAILED\n" : "all passed\n",failures);
    return failures ? 1 : 0;
}
