// The jets' engines mixed together offline (src/jet_mix.h, with sound_engine.h's AI event): the gain over the distance
// continuous and never rising out to nothing, the fade's shape, the ranking's margin, and a voice's opening and closing
// over the distance with room between (no chatter at one range).
// cmake --build build --target jet_mix_check && build\jet_mix_check.exe
#include "../src/jet_mix.h"
#include "../src/sound_engine.h"
#include <cmath>
#include <cstdio>

namespace {
int failures=0;
void Check(bool pass,const char* name) {
    std::printf("%s %s\n",pass ? "PASS" : "FAIL",name);
    if(!pass)++failures;
}
using namespace crew;

// The AI event's whole level heard behind a jet at full power at `d` (m), past its max distance on as FarGain has it.
float AiLevel(float d,float reach) noexcept {
    engine::Inputs in{};
    in.v[engine::kRpm]=engine::kJetAiTop;in.v[engine::kLoad]=1.0f;in.v[engine::kSpeedShare]=100.0f;
    engine::Heard h[engine::kMostEventLayers];
    const int n=engine::Hear(engine::kJetAi,in,d,180.0f,h);
    float sum=0.0f;
    for(int k=0;k<n;++k)sum+=h[k].gain;
    return sum*jetmix::FarGain(engine::kJetAi.maxD,d,reach);
}
}  // namespace

int main() {
    const float maxD=engine::kJetAi.maxD,reach=jetmix::Params{}.reach;
    // FarGain: 1 within the max distance, continuous there and on, never rising, nothing at `reach` and past it.
    {
        bool cont=true,falls=true;
        float prev=jetmix::FarGain(maxD,0.0f,reach);
        for(float d=1.0f;d<=reach+2000.0f;d+=1.0f) {
            const float g=jetmix::FarGain(maxD,d,reach);
            if(std::fabs(g-prev)>0.002f)cont=false;
            if(g>prev+1e-6f)falls=false;
            prev=g;
        }
        Check(cont,"FarGain: no step anywhere (1 m apart, under 0.002)");
        Check(falls,"FarGain: never rises with the distance");
        Check(jetmix::FarGain(maxD,maxD,reach)==1.0f && std::fabs(jetmix::FarGain(maxD,2.0f*maxD*0.66f,reach)-maxD/(2.0f*maxD*0.66f))<1e-4f,
              "FarGain: 1 at the max distance, 1/d on past it before the taper");
        Check(jetmix::FarGain(maxD,reach,reach)==0.0f && jetmix::FarGain(maxD,reach*0.999f,reach)>0.0f,"FarGain: nothing at `reach`, something just short of it");
        Check(jetmix::FarGain(maxD,9000.0f,0.0f)==1.0f,"FarGain: `reach` 0 holds the max distance's level (as before)");
        Check(jetmix::FarGain(0.0f,9000.0f,reach)==1.0f,"FarGain: the player's event (no max distance) untouched");
    }
    // The AI event's whole level over the distance: before, held from 3000 m on (a jet 8 km off as loud as at 3 km);
    // now continuous, falling past 3 km, under the voices' opening level well before `reach`.
    {
        const float at3=AiLevel(3000.0f,reach),at6=AiLevel(6000.0f,reach),held=AiLevel(8000.0f,0.0f);
        std::printf("  AI level behind at full power: 100 m %.4f, 1 km %.4f, 3 km %.4f, 6 km %.4f, 7.9 km %.6f; held at 8 km %.4f\n",
                    AiLevel(100.0f,reach),AiLevel(1000.0f,reach),at3,at6,AiLevel(7900.0f,reach),held);
        Check(held>engine::kOpenGain*5.0f,"before: the AI event held at its max distance's level (audible however reach)");
        bool cont=true;float worstAt=0.0f,worst=0.0f;
        float prev=AiLevel(0.0f,reach);
        for(float d=1.0f;d<=reach;d+=1.0f) {
            const float g=AiLevel(d,reach),step=std::fabs(g-prev)/(prev>engine::kCloseGain ? prev : engine::kCloseGain);
            if(step>worst){worst=step;worstAt=d;}
            prev=g;
        }
        cont=worst<0.02f;
        std::printf("  its steepest metre: %.2f%% at %.0f m\n",100.0f*worst,worstAt);
        Check(cont,"the AI level over the distance: no metre's step over 2% (of it, or of the closing level)");
        Check(at6<at3 && AiLevel(reach,reach)==0.0f,"the AI level falls on past 3 km to nothing at `reach`");
        // A voice opens above kOpenGain and closes below kCloseGain: going out, it closes farther than coming in it
        // opens, and each happens once (no chatter at one range).
        float openAt=-1.0f,closeAt=-1.0f;int opens=0,closes=0;bool open=false;
        for(float d=reach;d>=0.0f;d-=5.0f){const float g=AiLevel(d,reach);if(!open && g>engine::kOpenGain){open=true;++opens;openAt=d;}}
        open=true;
        for(float d=0.0f;d<=reach;d+=5.0f){const float g=AiLevel(d,reach);if(open && g<engine::kCloseGain){open=false;++closes;closeAt=d;}}
        std::printf("  the whole event's voices open coming in at %.0f m, close going out at %.0f m\n",openAt,closeAt);
        Check(opens==1 && closes==1 && closeAt>openAt+100.0f,"open / close over the distance: once each, with room between");
    }
    // The fade: from nothing to its level in fadeIn, back in fadeOut, smooth at both ends; 0 at once.
    {
        float l=0.0f;int frames=0;
        while(l<1.0f && frames<1000){l=jetmix::Step(l,true,1.5f,1.5f,1.0f/60.0f);++frames;}
        Check(frames>=89 && frames<=91,"fade in: 1.5 s at 60 fps");
        float prev=jetmix::Gain(0.0f),worst=0.0f;
        for(float x=0.0f;x<=1.0f;x+=1.0f/90.0f){const float g=jetmix::Gain(x);worst=std::fmax(worst,std::fabs(g-prev));prev=g;}
        Check(worst<0.02f && jetmix::Gain(0.0f)==0.0f && jetmix::Gain(1.0f)==1.0f,"fade's gain: 0 to 1, no frame's step over 2%");
        Check(jetmix::Step(0.0f,true,0.0f,0.0f,1.0f/60.0f)==1.0f && jetmix::Step(1.0f,false,0.0f,0.0f,1.0f/60.0f)==0.0f,
              "a fade of 0: at once (as before)");
        Check(jetmix::Step(0.5f,false,1.5f,jetmix::kDeadFade,0.125f)==0.0f,"a wreck fades out in kDeadFade");
    }
    // The ranking: the loudest `most`, those heard keeping their place against one not 3 dB louder.
    {
        const float loud[5]={0.1f,0.05f,0.3f,0.0001f,0.2f};
        bool was[5]{},in[5]{};
        jetmix::Rank(loud,was,5,2,engine::kOpenGain,in);
        Check(in[2] && in[4] && !in[0] && !in[1] && !in[3],"rank: the two loudest");
        jetmix::Rank(loud,was,5,9,engine::kOpenGain,in);
        Check(in[0] && in[1] && in[2] && in[4] && !in[3],"rank: none under the opening level, however many allowed");
        const float a[2]={0.10f,0.13f};   // the newcomer 2.3 dB louder: the one heard stays
        bool w[2]={true,false},r[2]{};
        jetmix::Rank(a,w,2,1,engine::kOpenGain,r);
        Check(r[0] && !r[1],"rank: one heard keeps its place against one under 3 dB louder");
        const float b[2]={0.10f,0.15f};   // 3.5 dB louder: it takes it
        jetmix::Rank(b,w,2,1,engine::kOpenGain,r);
        Check(!r[0] && r[1],"rank: ...and gives it up to one over 3 dB louder");
    }
    std::printf("%d failures\n",failures);
    return failures ? 1 : 0;
}
