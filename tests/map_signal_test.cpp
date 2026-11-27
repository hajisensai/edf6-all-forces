// The M map's signals (src/map_signal.h): kept in the order they came, the oldest giving way to a ninth, each shown its
// kShowMs and gone after, whole until its last kFadeMs and fading over it; no point or no F1-F4 kind is never kept; the
// machine's list (map_signal.cpp) hands the same to the map and to the view, and a new mission empties it.
#include "../src/map_signal.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
int checks=0;
void Check(bool ok,const char* text) {++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",text);std::exit(1);}}
}  // namespace

int main() {
    using namespace crew::mapsignal;
    List l;
    const float a[3]={10.0f,2.0f,-30.0f};
    Check(l.Add(a,0,true,1000) && l.n==1,"a signal is kept");
    const float nan[3]={std::numeric_limits<float>::quiet_NaN(),0.0f,0.0f};
    Check(!l.Add(nan,1,false,1000) && !l.Add(nullptr,1,false,1000) && !l.Add(a,kKinds,false,1000) && l.n==1,
          "no point, or no F1-F4 kind: not kept");
    for(unsigned i=1;i<=kKept;++i){const float p[3]={static_cast<float>(i),0.0f,0.0f};l.Add(p,i%kKinds,false,1000+i);}
    Signal out[kKept+2]{};
    int n=l.Live(1000+kKept,out,kKept+2);
    Check(n==kKept && out[0].at[0]==1.0f && out[kKept-1].at[0]==static_cast<float>(kKept),"the oldest gives way, oldest first");
    Check(!out[0].mine && out[0].kind==1u,"its kind and whose it is kept");
    n=l.Live(1000+kKept,out,3);
    Check(n==3 && out[0].at[0]==1.0f && l.n==kKept,"at most `max` handed out, none dropped for it");
    n=l.Live(1001+kShowMs,out,kKept);   // the first one (at 1001) just past its time
    Check(n==kKept-1 && out[0].at[0]==2.0f && l.n==kKept-1,"past its time: dropped");
    n=l.Live(2000+kKept+kShowMs,out,kKept);
    Check(n==0 && l.n==0,"all past their time: none");

    Check(Alpha(0)==1.0f && Alpha(kShowMs-kFadeMs)==1.0f,"whole until its last kFadeMs");
    Check(std::fabs(Alpha(kShowMs-kFadeMs/2)-0.5f)<1e-6f && Alpha(kShowMs-1)>0.0f,"fading over it");
    Check(Alpha(kShowMs)==0.0f && Alpha(kShowMs*3)==0.0f,"gone at its time");

    // The machine's list: one for the map and the view.
    crew::ResetMapSignals();
    Check(crew::AddMapSignal(a,2,true) && !crew::AddMapSignal(a,9,false),"the machine's list keeps a signal of F1-F4 alone");
    n=crew::MapSignalViews(out,kKept);
    Check(n==1 && out[0].kind==2u && out[0].mine && out[0].at[2]==-30.0f,"the map and the view see it");
    Check(crew::MapSignalViews(nullptr,kKept)==0 && crew::MapSignalViews(out,0)==0,"nowhere to put them: none");
    crew::ResetMapSignals();
    Check(crew::MapSignalViews(out,kKept)==0,"a new mission: none");
    std::printf("map_signal: %d checks passed\n",checks);
    return 0;
}
