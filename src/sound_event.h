#pragma once
// The ground guns' shots as War Thunder layers them (docs/feature-wt-sound.md; the user, 2026-11-26: 「还有炮声呼啸声什么
// 的」「分层和距离混音，环绕声道之类的记得做好」): each shot an event of layers, each layer a group of recordings
// (wt_clips.h, a variant picked at random, never the same twice running) with its own gain, its gain over the distance,
// its low-pass over the distance, its start after the shot and whether it is a low layer sent to the LFE alone. The
// numbers are the events' in GaijinEntertainment/fmod_studio_warthunder_for_modders (Metadata: the *_ai events of
// ground/weapon/cannons and mguns, the tracks' volumes, GainEffects and their Distance automation, the multiband EQ's
// frequencyA over Distance, the MultiSounds' start, the Premaster, the bus chain from the event down to weapon_ai, the
// spatialiser's sound size, minimum extent and Doppler scale); the ones not in them are marked "ours" here and in the doc.
// Pure: tools/sound_event_check.cpp checks it offline; vehsound.cpp plays it.
#include "wt_clips.h"
#include <cmath>
#include <cstdint>

namespace crew::wtevent {
constexpr int kMostPoints=4,kMostLayers=12;
constexpr float kQuietDb=-60.0f;    // a layer heard under this is not played (FMOD's curves end at -80: silence)
// Ours: the whole set's level against the plugin's other sounds (WT's weapon_ai bus and its master are WT's mix): the
// loudest cannon layer near the gun at about full scale, the master limiter (jetaudio.cpp) taking the layers' sum.
constexpr float kTrimDb=-6.0f;

struct Point { float at,value; };
// A value over the distance (m): straight between its points in dB (FMOD's linear automation of a volume), held past
// its ends; none: 0 dB.
struct Curve { int n; Point p[kMostPoints]; };
inline float At(const Curve& c,float d) noexcept {
    if(c.n<=0)return 0.0f;
    if(!(d>c.p[0].at))return c.p[0].value;
    for(int i=1;i<c.n;++i)
        if(d<=c.p[i].at) {
            const float span=c.p[i].at-c.p[i-1].at;
            const float t=span>0.0f ? (d-c.p[i-1].at)/span : 1.0f;
            return c.p[i-1].value+t*(c.p[i].value-c.p[i-1].value);
        }
    return c.p[c.n-1].value;
}
// A low-pass cutoff over the distance (Hz): geometric between its points (FMOD shows a frequency on a log scale; how it
// interpolates the automation is not in the metadata: ours), held past its ends; none: open (24 kHz).
inline float CutoffAt(const Curve& c,float d) noexcept {
    if(c.n<=0)return 24000.0f;
    if(!(d>c.p[0].at))return c.p[0].value;
    for(int i=1;i<c.n;++i)
        if(d<=c.p[i].at) {
            const float span=c.p[i].at-c.p[i-1].at;
            const float t=span>0.0f ? (d-c.p[i-1].at)/span : 1.0f;
            return c.p[i-1].value*std::pow(c.p[i].value/c.p[i-1].value,t);
        }
    return c.p[c.n-1].value;
}

struct Layer {
    int clip;       // wt::Clip
    float db;       // its tracks' fixed gains together
    Curve gain;     // dB over the distance
    Curve cutoff;   // Hz over the distance
    float after;    // s after the shot (its MultiSound's start)
    bool sub;       // a low layer: the LFE channel's alone (surround.h)
};
struct Event {
    int layers;
    Layer layer[kMostLayers];
    float db;       // the event's master, its bus chain down to weapon_ai and kTrimDb
    Curve master;   // the Premaster's distance gain
    float size,least,doppler;   // the spatialiser's sound size (m), minimum extent (0..1 of all around), Doppler scale
};
// Ours: past kHold m the mix is WT's at kHold, falling as 1 / d (6 dB a doubling, the old reports' falloff), the
// low-pass still the distance's. WT's events end at their spatialiser's maximum distance, 2000 m, their Far and Premaster
// down to -80 dB there (a cannon 1 km off is about -68 dB): a War Thunder battlefield's; EDF's fights run to 2500 m
// (vehmix.h kFarAt), where a gun is still heard.
constexpr float kHold=500.0f;

// What a layer is heard as at a distance: its gain (linear), its low-pass and how long after the shot it starts (the
// sound's travel time is the caller's). Those under kQuietDb are left out.
struct Heard { int clip; float gain,cutoff,after; bool sub; };
inline int Hear(const Event& e,float distance,Heard* out) noexcept {
    const float d=std::isfinite(distance) && distance>0.0f ? distance : 0.0f;
    const float mixAt=d>kHold ? kHold : d,spread=d>kHold ? -20.0f*std::log10(d/kHold) : 0.0f;
    int n=0;
    for(int i=0;i<e.layers && i<kMostLayers;++i) {
        const Layer& l=e.layer[i];
        const float db=e.db+At(e.master,mixAt)+l.db+At(l.gain,mixAt)+spread;
        if(db<kQuietDb)continue;
        out[n++]=Heard{l.clip,std::pow(10.0f,db/20.0f),CutoffAt(l.cutoff,d),l.after,l.sub};
    }
    return n;
}
// The Doppler ratio at the spatialiser's scale (0.5: half the shift).
inline float Doppler(const Event& e,float ratio) noexcept {
    return std::isfinite(ratio) && ratio>0.0f ? 1.0f+e.doppler*(ratio-1.0f) : 1.0f;
}

// The variants of each group in turn at random, never the one just played (FMOD's MultiSound shuffle).
struct Rotation {
    std::uint32_t seed=0x9E3779B9u;
    std::int8_t last[wt::kCount]{};
};
inline int Next(Rotation& r,int clip) noexcept {
    if(clip<0 || clip>=wt::kCount)return 0;
    const int n=wt::kVariants[clip];
    if(n<=1)return 0;
    r.seed^=r.seed<<13;r.seed^=r.seed>>17;r.seed^=r.seed<<5;
    int v=static_cast<int>(r.seed%static_cast<std::uint32_t>(n-1));
    if(v>=r.last[clip])++v;
    r.last[clip]=static_cast<std::int8_t>(v);
    return v;
}

// --- The cannons (*_ai events) --------------------------------------------------------------------------------------
// Shot: the shot module (cannon_NN_shot / _middle / body_*), track GainEffect +3, volume 0 m +6 -> 700 m -80, the
// module's master -6 and its multiband EQ's frequencyA 22 kHz at 0 m -> 20 Hz at 1000 m. Close: the calibre's own
// recording, +3, 0 m +8 -> 700 m -80, the same EQ. Bump: the ground's thump far off, -80 at 0 m -> 0 at 98 m. Far: the
// calibre's far recording, +6, -80 at 0 m -> +8 at 93 m -> -80 at 2000 m. Premaster: 0 m 0, 500 m -6, 2000 m -80. Bus:
// heavy / med 0, cannons -6, ground 0 (weapon_ai and below: every weapon's, left out). Spatialiser: sound size 14 m,
// minimum extent 40 degrees, Doppler 50 %.
constexpr Curve kShot{2,{{0.0f,6.0f},{700.0f,-80.0f}}};
constexpr Curve kClose{2,{{0.0f,8.0f},{700.0f,-80.0f}}};
constexpr Curve kBump{2,{{0.0f,-80.0f},{98.3917f,0.0f}}};
constexpr Curve kFar{3,{{0.0f,-80.0f},{93.2203f,8.0f},{2000.0f,-80.0f}}};
constexpr Curve kPremaster{3,{{0.0f,0.0f},{500.0f,-6.0f},{2000.0f,-80.0f}}};
constexpr Curve kCannonEq{2,{{0.0f,22000.0f},{1000.0f,20.0f}}};
// Ours: the air's dulling for the layers WT leaves unfiltered (their recordings are dull already at their distances):
// jetaudio.h AirCutoff's, 16 kHz near to 1.2 kHz at 2500 m.
constexpr Curve kAir{2,{{0.0f,16000.0f},{2500.0f,1200.0f}}};
constexpr Curve kOpen{0,{}};
// The player's own gun's layers (the non-_ai events: bass, metal, the howitzer's mechanism, the tail), set against the
// _ai event's shot layer (the player's shot module at 0 dB is the _ai shot layer's level at 0 m: +6 dB for the 75 mm
// set, +9 for the bigger ones: each layer's gain below; its curve the player event's own levels). Ours: where they fade out (WT only plays them for the player's own gun, at 0 m), and
// the tail's distance (WT's tail is the player's; the open field's echo is heard far too).
//  - bass: 75 mm -10 (shot_med_06's master -10, bass 0), 122 mm -8 (+2, from 0.2 s); faded out by 150 m (ours);
//  - metal: master -16, Tank_Metal_Gain 0 m +6 -> 10 m +2, the track +2 and its own 0 m +6 -> 10 m 0 (75 mm: -3 more,
//    from 0.2 s; 122 mm from 0.15 s); out by 40 m (ours);
//  - the howitzer's mechanism: master -16, Tank_Metal_Gain, the recording at -10 (metal_big_2_2's Audio 3, from 0.45 s);
//  - tail: 6 - 12 + 3 = -3 (75 mm, from 0.5 s), 6 - 16 + 4 = -6 (122 mm, from 0.6 s), heard at every distance (ours);
//  - sub: WT has none (its spatialiser sends the whole event to the LFE at -18 dB): ours, the cannons_sub recordings to
//    the LFE alone at about that share of the near layers, out by 1000 m.
constexpr Curve kSub{3,{{0.0f,0.0f},{300.0f,-12.0f},{1000.0f,-80.0f}}};
constexpr float kSubDb=-6.0f;
constexpr Curve kBassMid{3,{{0.0f,-10.0f},{50.0f,-20.0f},{150.0f,-80.0f}}};
constexpr Curve kBassBig{3,{{0.0f,-8.0f},{50.0f,-18.0f},{150.0f,-80.0f}}};
constexpr Curve kMetalMid{3,{{0.0f,-5.0f},{10.0f,-15.0f},{40.0f,-80.0f}}};
constexpr Curve kMetalBig{3,{{0.0f,-2.0f},{10.0f,-12.0f},{40.0f,-80.0f}}};
constexpr Curve kMech{3,{{0.0f,-20.0f},{10.0f,-24.0f},{40.0f,-80.0f}}};
constexpr float kCannonBusDb=-6.0f;
constexpr float kCannonSize=14.0f,kCannonLeast=40.0f/360.0f,kDopplerScale=0.5f;

using namespace wt;
// 75 mm KwK 40 (75mm_kwk40_ai, master +2.5): the 75-105 mm guns. Its shot module's body_mid layer is not in the set.
constexpr Event kMedium{9,{
    {k_shot_mid,3.0f-6.0f+3.0f,kShot,kCannonEq,0.0f,false},
    {k_middle_mid,3.0f-6.0f-3.0f,kShot,kCannonEq,0.0f,false},
    {k_bump_med,9.0f-2.0f,kBump,kAir,0.0f,false},
    {k_close_75,3.0f,kClose,kCannonEq,0.0f,false},
    {k_far_75,6.0f,kFar,kAir,0.0f,false},
    {k_bass,6.0f,kBassMid,kAir,0.0f,false},
    {k_metal,6.0f,kMetalMid,kAir,0.2f,false},
    {k_tail_mid,6.0f-3.0f,kOpen,kAir,0.5f,false},
    {k_sub,kSubDb,kSub,kOpen,0.0f,true},
}, 2.5f+kCannonBusDb+kTrimDb, kPremaster, kCannonSize, kCannonLeast, kDopplerScale};
// 122 mm D-25T (122mm_d25t_ai, master +2): the 120 mm tank guns.
constexpr Event kTank{10,{
    {k_shot_big,3.0f-6.0f+6.0f,kShot,kCannonEq,0.0f,false},
    {k_middle_big,3.0f-6.0f,kShot,kCannonEq,0.0f,false},
    {k_body_big,3.0f-6.0f,kShot,kCannonEq,0.6f,false},
    {k_bump_big,6.0f+3.0f,kBump,kAir,0.0f,false},
    {k_close_122,3.0f,kClose,kCannonEq,0.0f,false},
    {k_far_122,6.0f,kFar,kAir,0.0f,false},
    {k_bass,9.0f,kBassBig,kAir,0.2f,false},
    {k_metal,9.0f,kMetalBig,kAir,0.15f,false},
    {k_tail_big,9.0f-6.0f,kOpen,kAir,0.6f,false},
    {k_sub,kSubDb,kSub,kOpen,0.0f,true},
}, 2.0f+kCannonBusDb+kTrimDb, kPremaster, kCannonSize, kCannonLeast, kDopplerScale};
// 128 mm PaK 44 (128mm_pak44_ai, master +2): the super-heavy guns. Its near layers are the 122's (ours).
constexpr Event kHeavy{10,{
    {k_shot_heavy,3.0f-6.0f+6.0f,kShot,kCannonEq,0.0f,false},
    {k_middle_big,3.0f-6.0f,kShot,kCannonEq,0.0f,false},
    {k_body_big,3.0f-6.0f,kShot,kCannonEq,0.8f,false},
    {k_bump_big,6.0f+3.0f,kBump,kAir,0.0f,false},
    {k_close_128,3.0f,kClose,kCannonEq,0.0f,false},
    {k_far_128,6.0f,kFar,kAir,0.0f,false},
    {k_bass,9.0f,kBassBig,kAir,0.2f,false},
    {k_metal,9.0f,kMetalBig,kAir,0.15f,false},
    {k_tail_heavy,9.0f-6.0f,kOpen,kAir,0.6f,false},
    {k_sub,kSubDb,kSub,kOpen,0.0f,true},
}, 2.0f+kCannonBusDb+kTrimDb, kPremaster, kCannonSize, kCannonLeast, kDopplerScale};
// 150 mm sFH 18 (150mm_sfh_18_ai, master +2; Close +10, Far +4 on their tracks): the howitzers. Its near layers are
// the 122's (ours), its breech's mechanism the player event's howitzer_shot_mech.
constexpr Event kHowitzer{11,{
    {k_shot_mid,3.0f-6.0f+6.0f,kShot,kCannonEq,0.0f,false},
    {k_middle_big,3.0f-6.0f,kShot,kCannonEq,0.0f,false},
    {k_body_big,3.0f-6.0f,kShot,kCannonEq,0.8f,false},
    {k_bump_big,6.0f+3.0f,kBump,kAir,0.0f,false},
    {k_close_150,10.0f+3.0f,kClose,kCannonEq,0.0f,false},
    {k_far_150,4.0f+6.0f,kFar,kAir,0.0f,false},
    {k_bass,9.0f,kBassBig,kAir,0.2f,false},
    {k_metal,9.0f,kMetalBig,kAir,0.15f,false},
    {k_mech_howitzer,9.0f,kMech,kAir,0.45f,false},
    {k_tail_heavy,9.0f-6.0f,kOpen,kAir,0.6f,false},
    {k_sub,kSubDb,kSub,kOpen,0.0f,true},
}, 2.0f+kCannonBusDb+kTrimDb, kPremaster, kCannonSize, kCannonLeast, kDopplerScale};

// --- The machine guns (12mm_m2_ai, 8mm_mg42_ai) ----------------------------------------------------------------------
// One grain a round (WT loops 0.105 s grains: one a round at the gun's rate), close and far together; the last round's
// tails when it stops. close: -12, 0 m +4 -> 300 m -80, EQ 22 kHz -> 2.6 kHz at 200 m. far: M2 -16 / MG42 -18, +10,
// EQ 22 kHz -> 3.5 kHz at 1 km -> 2.6 kHz at 2 km. Premaster +5, -6 for the tails (on_shot 0), 0 m +2 -> 500 m -4 ->
// 2000 m -80. Bus: mguns +6, ground 0. Spatialiser: the cannons' (the MG events' are not in the metadata view: ours).
constexpr Curve kMgClose{2,{{0.0f,4.0f},{300.0f,-80.0f}}};
constexpr Curve kMgCloseEq{2,{{0.0f,22000.0f},{200.565f,2600.0f}}};
constexpr Curve kMgFar{1,{{0.0f,10.0f}}};
constexpr Curve kMgFarEq{3,{{0.0f,22000.0f},{1000.0f,3500.0f},{2000.0f,2600.0f}}};
constexpr Curve kMgPremaster{3,{{0.0f,2.0f},{500.0f,-4.0f},{2000.0f,-80.0f}}};
constexpr float kMgDb=5.0f+6.0f+kTrimDb,kLastDb=-6.0f;
struct Mg { Event round,last; };
constexpr Mg kM2{
    {2,{{k_m2_close,-12.0f,kMgClose,kMgCloseEq,0.0f,false},{k_m2_far,-16.0f,kMgFar,kMgFarEq,0.0f,false}},
     kMgDb,kMgPremaster,kCannonSize,kCannonLeast,kDopplerScale},
    {2,{{k_m2_last_close,-12.0f,kMgClose,kMgCloseEq,0.0f,false},{k_m2_last_far,-16.0f,kMgFar,kMgFarEq,0.0f,false}},
     kMgDb+kLastDb,kMgPremaster,kCannonSize,kCannonLeast,kDopplerScale}};
constexpr Mg kMg42{
    {2,{{k_mg42_close,-12.0f,kMgClose,kMgCloseEq,0.0f,false},{k_mg42_far,-18.0f,kMgFar,kMgFarEq,0.0f,false}},
     kMgDb,kMgPremaster,kCannonSize,kCannonLeast,kDopplerScale},
    {2,{{k_mg42_last_close,-12.0f,kMgClose,kMgCloseEq,0.0f,false},{k_mg42_last_far,-18.0f+6.0f,kMgFar,kMgFarEq,0.0f,false}},
     kMgDb+kLastDb,kMgPremaster,kCannonSize,kCannonLeast,kDopplerScale}};
// The M2's grains for a gun slower than kFastFrames a round (the heavy machine guns, the flak), the MG42's for a faster
// one (the gatlings' buzz): ours, WT has no gatling.
constexpr float kFastFrames=4.0f;
inline const Mg& MgFor(float frames) noexcept { return frames>0.0f && frames<kFastFrames ? kMg42 : kM2; }
}  // namespace crew::wtevent
