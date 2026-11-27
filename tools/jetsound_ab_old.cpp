// The "A" side of tools/jetsound_ab.py: the jets' engine tables of an older sound_engine.h (the driver puts it first on
// the include path), in its own namespace (crew_old) so its inline functions never meet the current ones at link time.
// Only what both versions have is used: Inputs' first six parameters, Hear, Doppler, the two jet events.
#define crew crew_old
#include "sound_engine.h"
#undef crew

struct AbIn { float rpm,load,wep,kmh,share,speedRel,direction,g,seed,seed2; };
struct AbLayer { int clip; float gain,ratio,cutoff; };

int AbHearOld(int ev,const AbIn& x,float d,float cone,float doppler,AbLayer* out,float* maxD) {
    using namespace crew_old::engine;
    const Event& e=ev==0 ? kJetPlayer : kJetAi;
    Inputs in{};
    in.v[kRpm]=x.rpm;in.v[kLoad]=x.load;in.v[kWep]=x.wep;in.v[kSpeed]=x.kmh;in.v[kTurn]=0.0f;in.v[kSpeedShare]=x.share;
    Heard h[kMostEventLayers];
    const int n=Hear(e,in,d,cone,h);
    const float dop=Doppler(e,doppler,cone);
    for(int k=0;k<n;++k)out[k]=AbLayer{h[k].clip,h[k].gain,h[k].ratio*dop,0.0f};
    *maxD=e.maxD;
    return n;
}
