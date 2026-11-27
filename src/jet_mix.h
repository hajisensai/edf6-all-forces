// The jets' engines mixed together (jetsound.cpp; docs/feature-wt-sound.md 「引擎的混音」, 2026-11-27): what the War
// Thunder layers (sound_engine.h) are heard through once there are many jets, far ones, new ones and gone ones.
// The user (2026-11-27): 「怎么突然有声，突然没声，距离限制这么死吗」. It was so for four reasons, each fixed here:
//  - a jet's voices were opened at their full level the frame it was first seen (a support jet spawning 2 km off was
//    there at once) and destroyed at their full level the frame it went (deleted, wrecked, its engine off): Presence,
//    a fade in and out (FMOD's AHDSR on an event's start and stop), never a voice started or stopped loud;
//  - a jet gone was cut at once although its sound was still on the way (d / 340 s: 6 s at 2 km): a gone jet's trail
//    is played on until its last sample has reached the camera (Caught), then faded;
//  - the AI's event rolls off as 1/d only out to its max distance (3000 m, FMOD's inverse rolloff) and is held there:
//    a jet 8 km off was as loud as one at 3 km, until it was deleted. FarGain carries 1/d on past it and tapers it
//    away to nothing by `reach` (FMOD culls an event past its max distance outright: here it fades out instead);
//  - first come, first served: 32 jets heard (a table of ours, no limit of the machine's), each taking its voices from
//    the shared pool as long as any were free. Now every jet is heard (the far ones made quiet by the distance, none
//    past `reach`); only when the pool has no voices left for them all does Rank keep the loudest (with a margin for
//    the ones already heard: no swapping back and forth) and the rest fade out (FMOD's virtual voices by audibility,
//    its max instances with steal quietest: JetSoundMostHeard, off by default).
// Pure: tools/jet_mix_check.cpp checks it offline.
#pragma once
#include <algorithm>
#include <cmath>

namespace crew::jetmix {
// The ini's knobs (JetSoundFade, JetSoundFar, JetSoundMostHeard), checked by Validate.
struct Params {
    float fadeIn=1.0f;    // s: a jet newly heard, from nothing to its level (0: at once, as before 2026-11-27)
    float fadeOut=1.5f;   // s: a jet gone or ranked out, its level to nothing
    float reach=8000.0f;  // m: past the event's max distance 1/d on, tapered away to nothing by this (0: held, as before)
    int most=0;           // jets heard at once, the loudest (0: every one)
};
constexpr float kDeadFade=0.25f;       // s: a wreck's engine (the blast over it), whatever fadeOut is
constexpr float kTaperFrom=0.5f;       // ...the taper begins at this share of `reach` (or the max distance, past it)
constexpr float kKeepMargin=1.41f;     // +3 dB: a jet heard keeps its place against one not this much louder

// The gain past the event's max distance `maxD` (m; 0: none, the player's own event) at `d`: 1 within it; beyond, the
// inverse rolloff carried on (maxD / d: continuous with sound_engine.h Rolloff held there) times a raised cosine from
// 1 at kTaperFrom of `reach` to 0 at `reach`. Continuous and never rising with the distance.
inline float FarGain(float maxD,float d,float reach) noexcept {
    if(!(maxD>0.0f) || !(d>maxD))return 1.0f;
    if(!(reach>0.0f))return 1.0f;   // held (0: as before)
    if(!(reach>maxD))return 0.0f;   // nothing heard past the max distance
    float g=maxD/d;
    float from=kTaperFrom*reach;
    if(from<maxD)from=maxD;
    if(d>=reach)return 0.0f;
    if(d>from) {
        const float t=(d-from)/(reach-from);
        g*=0.5f+0.5f*std::cos(3.14159265f*t);
    }
    return g;
}

// A jet's presence: 0 unheard .. 1 at its level, toward `on` at 1 / fadeIn a second up and 1 / fadeOut down (a fade of
// 0: at once). Gain is its shape (smoothstep: no kink at either end).
inline float Step(float level,bool on,float fadeIn,float fadeOut,float dt) noexcept {
    const float t=on ? fadeIn : fadeOut;
    if(!(t>0.0f) || !(dt>=0.0f))return on ? 1.0f : 0.0f;
    const float d=dt/t;
    level+=on ? d : -d;
    return level<0.0f ? 0.0f : level>1.0f ? 1.0f : level;
}
inline float Gain(float level) noexcept {
    const float x=level<0.0f ? 0.0f : level>1.0f ? 1.0f : level;
    return x*x*(3.0f-2.0f*x);
}

// The jets heard of `n` (`loud`: each one's level as it would be heard, before its presence; under `floor`: nothing
// to hear), loudest first, the ones heard before (`was`) counted kKeepMargin louder, each taken while it fits: at most
// `most` of them (0: no limit) and its voices (`need`) within `budget` (the voices to share; negative: no limit). One
// that does not fit leaves room for a quieter one that does. `order` is scratch for `n` indices; `in` gets the outcome.
// Ties keep the lower index.
inline void Rank(const float* loud,const bool* was,const int* need,int n,int most,int budget,float floor,int* order,
                 bool* in) noexcept {
    int m=0;
    for(int i=0;i<n;++i){in[i]=false;if(loud[i]>floor)order[m++]=i;}
    const auto key=[&](int i){return loud[i]*(was[i] ? kKeepMargin : 1.0f);};
    std::sort(order,order+m,[&](int a,int b){const float ka=key(a),kb=key(b);return ka>kb || (ka==kb && a<b);});
    int taken=0;
    for(int k=0;k<m && (most<=0 || taken<most);++k) {
        const int i=order[k],c=need ? need[i] : 0;
        if(budget>=0 && c>budget)continue;
        in[i]=true;++taken;
        if(budget>=0)budget-=c;
    }
}
}  // namespace crew::jetmix
