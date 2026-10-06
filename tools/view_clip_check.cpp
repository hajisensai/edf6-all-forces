// The view distance's clip planes (src/view_clip.h) checked offline: raising the near pass (ViewDistance 3000, the
// map's MapViewDistance 6000, the 10000 limit) never takes a distance away from the far pass, which alone draws the
// far-only scenery. The case that broke (2026-10-06): the simulator mission on NW_HENDEN, whose far mountain ring
// ig_FarMt_Henden_Out (Chunk02.cpk, far-only) rises from 1500 m out to 7 km (its slopes reach 243 m at 2500 m); the
// old rule moved the far pass's start to ViewDistance-500 = 2500 m, cutting the slopes nearer than that, and the rest
// of the ring hung in the sky as a band. The stock clip sets: NW_HENDEN_FINECLOUD.MAE sets _farClipZ 1000 and
// _distantFarClipZ 20000 and no _distantNearClipZ (the LightEnv default 500, 0x1648D0); other MAEs 35000 far.
//   cmake --build build --target view_clip_check && build\view_clip_check.exe      (exit code 1 on a failure)
#include "../src/view_clip.h"
#include <cstdio>

namespace {
int failures=0;
void Check(bool ok,const char* what,double a=0.0,double b=0.0) {
    if(ok)return;
    ++failures;
    std::printf("FAIL %s (%g, %g)\n",what,a,b);
}
}  // namespace

int main() {
    using viewclip::Clip;
    int cases=0;
    const Clip stocks[]={{1000.0f,500.0f,20000.0f},{1000.0f,500.0f,35000.0f},{1500.0f,700.0f,20000.0f}};
    const float wants[]={3000.0f,6000.0f,10000.0f};
    for(const Clip& stock:stocks) {
        for(float want:wants) {
            Clip c=stock;
            Check(viewclip::Raise(c,want),"raised",stock.farClip,want);
            Check(c.farClip==want,"near pass reaches the view distance",c.farClip,want);
            Check(c.distantNear==stock.distantNear,"far pass start kept",c.distantNear,stock.distantNear);
            Check(c.distantFar>=want,"far pass end no nearer than the near one's",c.distantFar,want);
            // Every distance the stock far pass drew (far-only scenery), it still draws.
            for(float d=stock.distantNear;d<=stock.distantFar;d+=50.0f) {
                ++cases;
                if(viewclip::FarPassDraws(stock,d))Check(viewclip::FarPassDraws(c,d),"far-only scenery still drawn",d,want);
            }
            // No gap between the passes: past the near pass's end the far pass draws.
            Check(c.distantNear<=c.farClip,"the passes meet",c.distantNear,c.farClip);
            ++cases;
        }
    }
    // NW_HENDEN's far mountain ring with ViewDistance 3000: its slopes from 1500 m (where it rises) out.
    {
        Clip c{1000.0f,500.0f,20000.0f};
        viewclip::Raise(c,3000.0f);
        for(float d=1500.0f;d<=7100.0f;d+=100.0f) { ++cases; Check(viewclip::FarPassDraws(c,d),"Henden ring drawn",d); }
    }
    // Never lowered; an unread (zero) clip left alone.
    {
        Clip c{8000.0f,500.0f,20000.0f};
        Check(!viewclip::Raise(c,3000.0f) && c.farClip==8000.0f,"never lowered",c.farClip);
        Clip z{0.0f,0.0f,0.0f};
        Check(!viewclip::Raise(z,3000.0f) && z.farClip==0.0f,"zero left alone",z.farClip);
        cases+=2;
    }
    std::printf("view_clip_check: %d cases, %d failures\n",cases,failures);
    return failures ? 1 : 0;
}
