#pragma once
// The ground guns' shots as War Thunder layers them (docs/feature-wt-sound.md; the user, 2026-11-26: 「还有炮声呼啸声什么
// 的」「分层和距离混音，环绕声道之类的记得做好」): each shot an event of layers, each layer a group of recordings
// (wt_clips.h, a variant picked at random, never the same twice running) with its own gain, its gain over the distance,
// its low-pass over the distance, its start after the shot and whether it is a low layer sent to the LFE alone. The
// numbers are the events' in GaijinEntertainment/fmod_studio_warthunder_for_modders (Metadata: the *_ai events of
// ground/weapon/cannons and mguns, the tracks' volumes, GainEffects and their Distance automation, the multiband EQ's
// frequencyA over Distance, the MultiSounds' start, the Premaster, the bus chain from the event down to weapon_ai, the
// spatialiser's sound size, minimum extent and Doppler scale); the ones not in them are marked "ours" here and in the doc.
// The explosions the same way, by blast radius; the rounds going by (their crack, bypass and whiz by calibre and
// speed) and coming down (the incoming whistle). Pure: tools/sound_event_check.cpp checks it offline; vehsound.cpp
// and blastsound.cpp play it.
#include "wt_clips.h"
#include <cmath>
#include <cstdint>

namespace crew::wtevent {
constexpr int kMostPoints=5,kMostLayers=12;
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
    float ratio=1.0f;   // its playback rate (a MultiSound's pitch: 2^(semitones / 12))
};
// Ours: past kHold m the mix is WT's at kHold, falling as 1 / d (6 dB a doubling, the old reports' falloff), the
// low-pass still the distance's. WT's events end at their spatialiser's maximum distance, 2000 m, their Far and Premaster
// down to -80 dB there (a cannon 1 km off is about -68 dB): a War Thunder battlefield's; EDF's fights run to 2500 m
// (vehmix.h kFarAt), where a gun is still heard.
// An event whose curves run on that far (the explosions', to 5000 m) holds there instead (its own hold).
constexpr float kHold=500.0f;
struct Event {
    int layers;
    Layer layer[kMostLayers];
    float db;       // the event's master, its bus chain down to weapon_ai and kTrimDb
    Curve master;   // the Premaster's distance gain
    float size,least,doppler;   // the spatialiser's sound size (m), minimum extent (0..1 of all around), Doppler scale
    float hold=kHold;           // m: past it WT's mix there, falling as 1 / d
    const char* name="";        // WT's event (the Debug tallies, jetaudio.h Note)
};

// What a layer is heard as at a distance: its gain (linear), its low-pass and how long after the shot it starts (the
// sound's travel time is the caller's). Those under kQuietDb are left out.
struct Heard { int clip; float gain,cutoff,after; bool sub; float ratio=1.0f; };
inline int Hear(const Event& e,float distance,Heard* out) noexcept {
    const float d=std::isfinite(distance) && distance>0.0f ? distance : 0.0f;
    const float hold=e.hold>0.0f ? e.hold : kHold;
    const float mixAt=d>hold ? hold : d,spread=d>hold ? -20.0f*std::log10(d/hold) : 0.0f;
    int n=0;
    for(int i=0;i<e.layers && i<kMostLayers;++i) {
        const Layer& l=e.layer[i];
        const float db=e.db+At(e.master,mixAt)+l.db+At(l.gain,mixAt)+spread;
        if(db<kQuietDb)continue;
        out[n++]=Heard{l.clip,std::pow(10.0f,db/20.0f),CutoffAt(l.cutoff,d),l.after,l.sub,l.ratio};
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
}, 2.5f+kCannonBusDb+kTrimDb, kPremaster, kCannonSize, kCannonLeast, kDopplerScale, kHold, "75mm_kwk40_ai"};
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
}, 2.0f+kCannonBusDb+kTrimDb, kPremaster, kCannonSize, kCannonLeast, kDopplerScale, kHold, "122mm_d25t_ai"};
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
}, 2.0f+kCannonBusDb+kTrimDb, kPremaster, kCannonSize, kCannonLeast, kDopplerScale, kHold, "128mm_pak44_ai"};
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
}, 2.0f+kCannonBusDb+kTrimDb, kPremaster, kCannonSize, kCannonLeast, kDopplerScale, kHold, "150mm_sfh_18_ai"};

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
     kMgDb,kMgPremaster,kCannonSize,kCannonLeast,kDopplerScale,kHold,"12mm_m2_ai"},
    {2,{{k_m2_last_close,-12.0f,kMgClose,kMgCloseEq,0.0f,false},{k_m2_last_far,-16.0f,kMgFar,kMgFarEq,0.0f,false}},
     kMgDb+kLastDb,kMgPremaster,kCannonSize,kCannonLeast,kDopplerScale,kHold,"12mm_m2_ai_last"}};
constexpr Mg kMg42{
    {2,{{k_mg42_close,-12.0f,kMgClose,kMgCloseEq,0.0f,false},{k_mg42_far,-18.0f,kMgFar,kMgFarEq,0.0f,false}},
     kMgDb,kMgPremaster,kCannonSize,kCannonLeast,kDopplerScale,kHold,"8mm_mg42_ai"},
    {2,{{k_mg42_last_close,-12.0f,kMgClose,kMgCloseEq,0.0f,false},{k_mg42_last_far,-18.0f+6.0f,kMgFar,kMgFarEq,0.0f,false}},
     kMgDb+kLastDb,kMgPremaster,kCannonSize,kCannonLeast,kDopplerScale,kHold,"8mm_mg42_ai_last"}};
// The M2's grains for a gun slower than kFastFrames a round (the heavy machine guns, the flak), the MG42's for a faster
// one (the gatlings' buzz): ours, WT has no gatling.
constexpr float kFastFrames=4.0f;
inline const Mg& MgFor(float frames) noexcept { return frames>0.0f && frames<kFastFrames ? kMg42 : kM2; }

// --- The explosions (explosion_ground_20_37, explosion_ground, art_explosion_ground, bomb_explosion_ground) -----------
// A round's explosion by its blast radius (BlastTier): a small one's (under 3 m: the autocannons' and the explosive
// bullets'), a shell's or a grenade's (3-8 m), an artillery shell's or a big rocket's (8-16 m), a bomb's (16 m on). Each
// event's layers as WT's: the near crack and attack, the far attack that takes over past 200 m, the body far off, the
// debris coming down after it, each with its own gain over the distance; the Premaster (Explosions_*_Distance_Gain) to
// 5000 m; the spatialiser's sound size and minimum extent; no Doppler (it stands still). Bus: explosions (minor / rapid /
// massive) -12 -4 +4 = -12 dB against the cannons' -18 (with weapon_ai, which kCannonBusDb leaves out): 0 dB here. The
// EQ over the distance is a low-pass here (WT's multiband EQ: ours, by where its cut falls). Ours: the tail (the open
// field's echo, tails-NNN) and the rumble to the LFE alone (explosion_rumble; WT sends the whole event there at -12 / -8
// dB). The level: 0 m from a shell's explosion about +9 dB against a cannon's +1.5 at its muzzle (WT's own levels).
constexpr float kBlastBusDb=0.0f,kBlastHold=5000.0f;
constexpr Curve kSmallPremaster{3,{{0.0f,0.0f},{2012.7f,-4.0f},{5000.0f,-80.0f}}};
constexpr Curve kBlastPremaster{3,{{0.0f,0.0f},{2012.7f,-10.0f},{5000.0f,-80.0f}}};
constexpr Curve kBombPremaster{3,{{0.0f,0.0f},{2012.7f,0.0f},{5000.0f,-18.0f}}};
constexpr Curve kSmallEq{3,{{0.0f,22000.0f},{374.0f,9000.0f},{3000.0f,1500.0f}}};
constexpr Curve kBlastEq{3,{{0.0f,22000.0f},{2000.0f,6500.0f},{5000.0f,1500.0f}}};
constexpr Curve kBombEq{3,{{0.0f,22000.0f},{2000.0f,8000.0f},{5000.0f,3000.0f}}};
constexpr Curve kBodyEq{2,{{0.0f,22000.0f},{3000.0f,350.0f}}};
constexpr Curve kRumble{3,{{0.0f,0.0f},{1000.0f,-12.0f},{5000.0f,-60.0f}}};   // ours
constexpr Curve kDebris{2,{{0.0f,0.0f},{403.8f,-80.0f}}};
// explosion_ground_20_37 (master -6): hit_explosion_small_close_rapid +8, 0 m +6 -> 400 m -80; bullethit_far +10, 0 m +4
// -> 200 m 0 -> 400 m -80. Spatialiser: 10 m, 120 degrees. Ours: the rumble -12.
constexpr Event kBlastSmall{3,{
    {k_xs_close,8.0f,{2,{{0.0f,6.0f},{400.0f,-80.0f}}},kSmallEq,0.0f,false},
    {k_xs_far,10.0f,{3,{{0.0f,4.0f},{200.0f,0.0f},{400.0f,-80.0f}}},kSmallEq,0.0f,false},
    {k_ex_sub,-12.0f,kRumble,kOpen,0.0f,true},
}, -6.0f+kBlastBusDb+kTrimDb, kSmallPremaster, 10.0f, 120.0f/360.0f, 0.0f, kBlastHold, "explosion_ground_20_37"};
// explosion_ground (master -6): aa_explosion (the sweetener) +11, 0 m +7 -> 190.7 m +6 -> 1000 m -80; heavy_explosion
// _attack_3 +11, 0 m +10 -> 2000 m -80, its _far +11, -80 at 0 m -> +6 at 204.8 m -> -80 at 5000 m; expl_body_med -5,
// -80 at 0 m -> +9.9 at 190.7 m -> -56.7 at 5000 m, its EQ 22 kHz -> 350 Hz at 3000 m; dirt_debris_small +10, 0 m 0 ->
// 403.8 m -80, from 0.2 s. Spatialiser: 2 m, 60 degrees; at most 4 at once, 60 ms apart. Ours: the tail -12, the rumble -6.
constexpr Event kBlastMid{7,{
    {k_ex_sweet,11.0f,{3,{{0.0f,7.0f},{190.7f,6.0f},{1000.0f,-80.0f}}},kBlastEq,0.0f,false},
    {k_ex_attack,11.0f,{2,{{0.0f,10.0f},{2000.0f,-80.0f}}},kBlastEq,0.0f,false},
    {k_ex_attack_far,11.0f,{3,{{0.0f,-80.0f},{204.8f,6.0f},{5000.0f,-80.0f}}},kBlastEq,0.0f,false},
    {k_ex_body,-5.0f,{3,{{0.0f,-80.0f},{190.7f,9.9f},{5000.0f,-56.7f}}},kBodyEq,0.0f,false},
    {k_debris_small,10.0f,kDebris,kBlastEq,0.2f,false},
    {k_ex_tail,-12.0f,kOpen,kBlastEq,0.1f,false},
    {k_ex_sub,-6.0f,kRumble,kOpen,0.0f,true},
}, -6.0f+kBlastBusDb+kTrimDb, kBlastPremaster, 2.0f, 60.0f/360.0f, 0.0f, kBlastHold, "explosion_ground"};
// art_explosion_ground (master -4): art_attack +12, 0 m +10 -> 1000 m -80, its _far +9.5, -80 at 0 m -> +1.11 at 218.9 m
// -> -2.32 at 981.6 m -> -4 at 5000 m; art_body +6, -80 at 7.06 m -> +5.22 at 190.7 m -> -80 at 1000 m; aa_explosion
// +10 between 50 m (-0.2) and 1000 m (-4) only; dirt_debris_med +12, 0 m 0 -> 403.8 m -80, from 1.0 s. Premaster and
// spatialiser: explosion_ground's. Ours: the tail -8, the rumble -3.
constexpr Event kBlastBig{7,{
    {k_art_attack,12.0f,{2,{{0.0f,10.0f},{1000.0f,-80.0f}}},kBlastEq,0.0f,false},
    {k_art_attack_far,9.5f,{4,{{0.0f,-80.0f},{218.9f,1.11f},{981.6f,-2.32f},{5000.0f,-4.0f}}},kBlastEq,0.0f,false},
    {k_art_body,6.0f,{3,{{7.06f,-80.0f},{190.7f,5.22f},{1000.0f,-80.0f}}},kBlastEq,0.0f,false},
    {k_ex_sweet,10.0f,{4,{{49.0f,-80.0f},{50.0f,-0.2f},{1000.0f,-4.0f},{1001.0f,-80.0f}}},kBlastEq,0.0f,false},
    {k_debris_med,12.0f,kDebris,kBlastEq,1.0f,false},
    {k_ex_tail,-8.0f,kOpen,kBlastEq,0.15f,false},
    {k_ex_sub,-3.0f,kRumble,kOpen,0.0f,true},
}, -4.0f+kBlastBusDb+kTrimDb, kBlastPremaster, 2.0f, 60.0f/360.0f, 0.0f, kBlastHold, "art_explosion_ground"};
// bomb_explosion_ground (its master not in the metadata view: 0, ours): aa_explosion +12, 0 m +10 -> 1000 m -80;
// slow_attack_big +6, 0 m +6 -> 190.7 m +6 -> 5000 m -80; bomb_body_heavy +6, 0 m -6 -> +6 at 981.6 m -> -5 at 4046.6 m
// -> -80 at 5000 m, from 0.5 s; bomb_debirs -4, 0 m 0 -> 300 m -59.4, then silent, from 1.4 s. Premaster: 0 to 2012.7
// m, -18 at 5000 m. Spatialiser: 120 m, 140 degrees; at most 3 at once. Ours: the tail -6, the rumble 0.
constexpr Event kBlastHeavy{6,{
    {k_ex_sweet,12.0f,{2,{{0.0f,10.0f},{1000.0f,-80.0f}}},kBombEq,0.0f,false},
    {k_bomb_body,6.0f,{3,{{0.0f,6.0f},{190.7f,6.0f},{5000.0f,-80.0f}}},kBombEq,0.0f,false},
    {k_bomb_far,6.0f,{4,{{0.0f,-6.0f},{981.6f,6.0f},{4046.6f,-5.0f},{5000.0f,-80.0f}}},kBombEq,0.5f,false},
    {k_debris_bomb,-4.0f,{3,{{0.0f,0.0f},{300.0f,-59.4f},{300.1f,-80.0f}}},kBombEq,1.4f,false},
    {k_ex_tail,-6.0f,kOpen,kBombEq,0.2f,false},
    {k_ex_sub,0.0f,kRumble,kOpen,0.0f,true},
}, 0.0f+kBlastBusDb+kTrimDb, kBombPremaster, 120.0f, 140.0f/360.0f, 0.0f, kBlastHold, "bomb_explosion_ground"};

// The tiers by blast radius (m) and their voice budgets: at most WT's maxVoices (the small and the big one's ours) started
// within `life` s (about one's length: ours), none within `cooldown` s of the last (explosion_ground's 60 ms; the others'
// ours).
enum class Blast : std::uint8_t { minor, shell, art, bomb };   // kBlastSmall, kBlastMid, kBlastBig, kBlastHeavy
constexpr int kBlastTiers=4;
struct Budget { int most; float cooldown,life; };
constexpr Budget kBlastBudget[kBlastTiers]={{6,0.03f,1.5f},{4,0.06f,3.0f},{4,0.06f,4.0f},{3,0.1f,6.0f}};
constexpr float kSmallBelow=3.0f,kMidBelow=8.0f,kBigBelow=16.0f;
inline Blast BlastTier(float radius) noexcept {
    const float r=std::isfinite(radius) && radius>0.0f ? radius : 0.0f;
    return r<kSmallBelow ? Blast::minor : r<kMidBelow ? Blast::shell : r<kBigBelow ? Blast::art : Blast::bomb;
}
inline const Event& BlastEvent(Blast b) noexcept {
    return b==Blast::minor ? kBlastSmall : b==Blast::shell ? kBlastMid : b==Blast::art ? kBlastBig : kBlastHeavy;
}
// Whether a sound `distance` m off may start against the `n` of its kind started `ages` s ago `dists` m off: fewer than
// its budget live (younger than its life), or nearer than the farthest of them (whose place it takes: that one's voices
// play on, it just no longer counts); never within the cooldown of the last. Returns the place to take (n: a new one),
// -1: not heard.
inline int BudgetSlot(const Budget& b,float distance,const float* ages,const float* dists,int n) noexcept {
    int live=0,farthest=-1,spent=-1;
    for(int i=0;i<n;++i) {
        if(!(ages[i]<b.life)){if(spent<0)spent=i;continue;}
        if(ages[i]<b.cooldown)return -1;
        ++live;
        if(farthest<0 || dists[i]>dists[farthest])farthest=i;
    }
    if(live<b.most)return spent>=0 ? spent : n;
    return farthest>=0 && distance<dists[farthest] ? farthest : -1;
}

// --- The rounds going by and coming down --------------------------------------------------------------------------
// bullet_passby (one event, its layers gated by the round's calibre and speed): at most 4 at once, 50 ms apart, heard
// within 120 m of the round's path. Three group tracks: the cracks (gain +6; the supersonic ones only: bullet_crack +2
// at 50 % from 600 m/s under 31 mm, shell_small_crack +4 from 600 m/s 31-65 mm, shell_mid_crack +4 from 400 m/s 65-140
// mm, shell_big_crack +2 from 600 m/s 140 mm on), the bypasses (gain 0: +3 at 50 % under 31 mm, +4 at 70.5 % 31-140 mm,
// +4 at 69.5 % on) and the whizzes (gain -14: +6 at 50 % under 31 mm, +10 at 50 % 31-65 mm, +10 at 70.5 % 65-160 mm,
// +10 at 69.5 % on). Its spatialiser has no distance curve (custom rolloff, none drawn): ours, 0 m 0 -> 20 m -6 -> 120
// m -30, the round's closest point to the ear. shell_whiz (the shells' own whiz, tanks/impacts): its group -7.26, 0 m
// 0 -> 10.5 m -6 -> 20 m -80 over that distance, at most 4. Bus: ours, kTrimDb. EDF has no calibre: the blast radius
// stands for it (ours: CalibreOf), a round with none a bullet.
enum class Calibre : std::uint8_t { rifle, light, medium, heavy };   // under 20 mm, 20-50, 50-150, 150 on
constexpr float kLightBelow=3.0f,kMediumBelow=8.0f;
inline Calibre CalibreOf(float radius) noexcept {
    const float r=std::isfinite(radius) && radius>0.0f ? radius : 0.0f;
    return r<=0.0f ? Calibre::rifle : r<kLightBelow ? Calibre::light : r<kMediumBelow ? Calibre::medium : Calibre::heavy;
}
constexpr float kPassbyFar=120.0f;     // bullet_passby's maximum distance: a round farther off at its closest not heard
constexpr float kPassbyFrom=30.0f;     // ours: a round heard going by only once it has flown this far (not one's own gun's)
constexpr Budget kPassbyBudget{4,0.05f,1.5f};   // maxVoices 4, triggerCooldown 50 ms; the life ours (a whiz's length)
constexpr Curve kMiss{3,{{0.0f,0.0f},{20.0f,-6.0f},{120.0f,-30.0f}}};    // ours
constexpr Curve kWhizMiss{3,{{0.0f,0.0f},{10.5f,-6.0f},{20.0f,-80.0f}}};
constexpr float kCrackDb=6.0f,kBypassDb=0.0f,kWhizDb=-14.0f,kShellWhizDb=-7.26f;
// A layer of a passby: its recording, gain and curve over the closest distance, the speed it needs (m/s, 0: any), the
// chance it sounds (FMOD's trigger probability).
struct Pass { int clip; float db; Curve gain; float faster,chance; };
struct Passby { int layers; Pass pass[4]; };
constexpr Passby kPassRifle{3,{
    {k_crack_bullet,kCrackDb+2.0f,kMiss,600.0f,0.5f},
    {k_bypass_bullet,kBypassDb+3.0f,kMiss,0.0f,0.5f},
    {k_whiz_small,kWhizDb+6.0f,kMiss,0.0f,0.5f},
}};
constexpr Passby kPassLight{3,{
    {k_crack_small,kCrackDb+4.0f,kMiss,600.0f,1.0f},
    {k_bypass_bullet,kBypassDb+4.0f,kMiss,0.0f,0.705f},
    {k_whiz_small,kWhizDb+10.0f,kMiss,0.0f,0.5f},
}};
constexpr Passby kPassMedium{4,{
    {k_crack_mid,kCrackDb+4.0f,kMiss,400.0f,1.0f},
    {k_bypass_big,kBypassDb+4.0f,kMiss,0.0f,0.705f},
    {k_whiz_med,kWhizDb+10.0f,kMiss,0.0f,0.705f},
    {k_whiz,kShellWhizDb,kWhizMiss,0.0f,1.0f},
}};
constexpr Passby kPassHeavy{4,{
    {k_crack_big,kCrackDb+2.0f,kMiss,600.0f,1.0f},
    {k_bypass_big,kBypassDb+4.0f,kMiss,0.0f,0.695f},
    {k_whiz_big,kWhizDb+10.0f,kMiss,0.0f,0.695f},
    {k_whiz,kShellWhizDb,kWhizMiss,0.0f,1.0f},
}};
inline const Passby& PassbyFor(Calibre c) noexcept {
    return c==Calibre::rifle ? kPassRifle : c==Calibre::light ? kPassLight : c==Calibre::medium ? kPassMedium : kPassHeavy;
}
// A passby `miss` m off at its closest, the round at `speed` m/s; `rolls` one in [0, 1) a layer (the chances). Its
// layers as Hear's (after 0: at the closest point; no low layer). Past kPassbyFar: none.
inline int HearPass(const Passby& p,float miss,float speed,const float* rolls,Heard* out) noexcept {
    if(!(miss>=0.0f) || miss>kPassbyFar || !std::isfinite(speed))return 0;
    int n=0;
    for(int i=0;i<p.layers && i<4;++i) {
        const Pass& l=p.pass[i];
        if(speed<l.faster || !(rolls[i]<l.chance))continue;
        const float db=kTrimDb+l.db+At(l.gain,miss);
        if(db<kQuietDb)continue;
        out[n++]=Heard{l.clip,std::pow(10.0f,db/20.0f),24000.0f,0.0f,false};
    }
    return n;
}
// Where a round `r` (its position less the ear's) moving `v` (m/s) comes closest to the ear: in `t` s (negative: it
// has passed), `miss` m off. False: it stands still.
inline bool Closest(const float* r,const float* v,float* t,float* miss) noexcept {
    const float vv=v[0]*v[0]+v[1]*v[1]+v[2]*v[2];
    if(!(vv>1.0f) || !std::isfinite(vv))return false;
    const float at=-(r[0]*v[0]+r[1]*v[1]+r[2]*v[2])/vv;
    float m=0.0f;
    for(int k=0;k<3;++k){const float c=r[k]+v[k]*at;m+=c*c;}
    *t=at;*miss=std::sqrt(m);
    return std::isfinite(*miss);
}

// art_shells_incoming: master +6, its MultiSound +4 at 70 %, its group +6, against the art bus's -16: 0 dB; over the
// distance 0 m 0 -> 600.3 m -80, the EQ's frequencyA 22 kHz -> 12 kHz at 103.1 m -> 1.6 kHz at 400 m; a point (no
// extent); at most 4, 200 ms apart. bombs_incoming: the same and a GainEffect +6; no distance curve of its own (ours:
// the shells'); at most 1. Ours: the distance is the one from the ear to where the round will land, heard from the
// round from `kIncomingLead` s before (about the recordings' lengths: 2.3-2.4 s, 2.05 s) when it lands within
// kIncomingWithin m.
constexpr Curve kIncomingGain{2,{{0.0f,0.0f},{600.28f,-80.0f}}};
constexpr Curve kIncomingEq{3,{{0.0f,22000.0f},{103.107f,12000.0f},{400.0f,1600.0f}}};
constexpr float kArtBusDb=-16.0f,kIncomingWithin=300.0f,kIncomingChance=0.7f;
constexpr Event kIncomingArt{1,{
    {k_incoming_art,4.0f+6.0f,kIncomingGain,kIncomingEq,0.0f,false},
}, 6.0f+kArtBusDb+kTrimDb, kOpen, 0.0f, 0.0f, 0.0f, 600.28f, "art_shells_incoming"};
constexpr Event kIncomingBomb{1,{
    {k_incoming_bomb,4.0f+6.0f+6.0f,kIncomingGain,kIncomingEq,0.0f,false},
}, 6.0f+kArtBusDb+kTrimDb, kOpen, 0.0f, 0.0f, 0.0f, 600.28f, "bombs_incoming"};
constexpr float kIncomingLeadArt=2.3f,kIncomingLeadBomb=2.0f;
constexpr Budget kIncomingBudget[2]={{4,0.2f,2.5f},{1,0.2f,2.5f}};   // the life ours (the recording's length)
// When and where a round at `pos` moving `vel` (m/s) under `acc` (m/s^2: the core's gravity, vel += acc / 60 a frame)
// comes down to the height `floorY`: the first time ahead it crosses it falling. False: it does not (rising away, level).
inline bool Lands(const float* pos,const float* vel,const float* acc,float floorY,float* t,float* at) noexcept {
    const float a=0.5f*acc[1],b=vel[1],c=pos[1]-floorY;
    float when=-1.0f;
    if(std::fabs(a)<1e-6f) { if(b<0.0f)when=-c/b; }
    else {
        const float disc=b*b-4.0f*a*c;
        if(disc<0.0f)return false;
        const float s=std::sqrt(disc),t1=(-b-s)/(2.0f*a),t2=(-b+s)/(2.0f*a);
        // The root where it is falling (2 a t + b < 0), the first ahead.
        const float roots[2]={t1<t2 ? t1 : t2,t1<t2 ? t2 : t1};
        for(const float x:roots)
            if(x>0.0f && 2.0f*a*x+b<0.0f){when=x;break;}
    }
    if(!(when>0.0f) || !std::isfinite(when))return false;
    *t=when;
    for(int k=0;k<3;++k)at[k]=pos[k]+vel[k]*when+0.5f*acc[k]*when*when;
    return true;
}
// --- The rounds hitting armour (hit_mgun, rico_mgun, hit_small_unpierce, hit_big_unpierce, hit_big / hit_aphe_big) ----
// A round striking an armoured target (blastsound.cpp Armoured: the vehicles, the robots, the ships and their parts)
// is heard as WT's tank hits: a bullet's hit_mgun, its glancing blow rico_mgun; a heavy plain round's (a railgun's, a
// shell's with no blast) hit_big_unpierce, its glancing blow rico, its going through (AmmoIsPenetration) the pierce; an
// explosive round's explosion with the hit_big / hit_aphe_big layers its tier calls for over it (ArmourEvent). EDF has
// no calibre and no armour: the round's class stands for the calibre (a bullet class or not), the angle it strikes at
// for the ricochet (HitKind; ours). Bus: ours, kTrimDb. hit_mgun (master -4): tank_hit_bullet -6 +10, 0 m 0 -> 500 m
// -80, its EQ 7.5 kHz 0 dB -> -17 dB at 500 m (a low-pass here, ours), heard 0-200 m; at most 4. rico_mgun (+3.84
// semitones; its master not in the view: hit_mgun's, ours): tank_hit_bullet -6 +6, 0 m 0 -> 250 m -80; bullet_whiz -6,
// 0 m 0 -> 11.7 m -3.9 -> 23.5 m -11.2 -> 60.9 m -16.3 -> 98.4 m -80, heard 0-100 m. hit_small_unpierce / hit_big_unpierce
// (gain -22): the impact (tank_hit_mid / _big) -5 +10, 0 m +6 -> 23.8 m -2 -> 47.7 m -11.6 -> 124 m -16.8 -> 200.8 m -80;
// hit_dull_small from 0.18 s +6 +6 / hit_dull from 0.2 s +3, -6 semitones (the dull layer's distance: the impact's,
// ours); at most 3. hit_big / hit_aphe_big (master +6, gain -16): the impact (tank_hit_*) -5 +10, 0 m 0 -> 99.5 m -2.9
// -> 500 m -80; the distant one (hit_*_far) from 0.19 s +6, -80 at 0 m -> +10 at 100 m -> -80 at 500 m; their hit_expl
// layers are the explosion's own (kBlast*). Spatialiser: 4 m, 80 degrees; maximum distance 500 m. rico (the heavy
// rounds'): rico_hit -4, rico_whiz -4 -8.2, their distance ours (rico_mgun's impact's). The pierce: tank_hit_pierce,
// hit_big_unpierce's numbers (ours: WT's hit_ap events are the player's own, inside the tank).
constexpr float kHitHold=500.0f;
constexpr Curve kHitEq{2,{{0.0f,22000.0f},{500.0f,4000.0f}}};   // ours
constexpr Curve kHitBulletGain{2,{{0.0f,0.0f},{500.0f,-80.0f}}};
constexpr Curve kRicoGain{2,{{0.0f,0.0f},{250.0f,-80.0f}}};
constexpr Curve kRicoWhizGain{5,{{0.0f,0.0f},{11.7f,-3.9f},{23.5f,-11.2f},{60.9f,-16.3f},{98.4f,-80.0f}}};
constexpr Curve kUnpierceGain{5,{{0.0f,6.0f},{23.8f,-2.0f},{47.7f,-11.6f},{124.0f,-16.8f},{200.8f,-80.0f}}};
constexpr Curve kArmourGain{3,{{0.0f,0.0f},{99.5f,-2.9f},{500.0f,-80.0f}}};
constexpr Curve kArmourFar{3,{{0.0f,-80.0f},{100.0f,10.0f},{500.0f,-80.0f}}};
constexpr float kRicoPitch=1.2484f,kDullPitch=0.7071f;   // +3.84 and -6 semitones
constexpr float kHitSize=4.0f,kHitLeast=80.0f/360.0f;
constexpr Event kHitBullet{1,{
    {k_hit_bullet,-6.0f+10.0f,kHitBulletGain,kHitEq,0.0f,false},
}, -4.0f+kTrimDb, kOpen, 0.0f, 0.0f, 0.0f, kHitHold, "hit_mgun"};
constexpr Event kRicoBullet{2,{
    {k_hit_bullet,-6.0f+6.0f,kRicoGain,kHitEq,0.0f,false,kRicoPitch},
    {k_bullet_whiz,-6.0f,kRicoWhizGain,kHitEq,0.0f,false,kRicoPitch},
}, -4.0f+kTrimDb, kOpen, 0.0f, 0.0f, 0.0f, kHitHold, "rico_mgun"};
constexpr Event kHitHeavy{2,{
    {k_hit_big,-5.0f+10.0f,kUnpierceGain,kHitEq,0.0f,false},
    {k_dull_med,3.0f,kUnpierceGain,kHitEq,0.2f,false,kDullPitch},
}, -22.0f+kTrimDb, kOpen, kHitSize, kHitLeast, 0.0f, kHitHold, "hit_big_unpierce"};
constexpr Event kRicoHeavy{2,{
    {k_rico_hit,-4.0f,kRicoGain,kHitEq,0.0f,false},
    {k_rico_whiz,-4.0f-8.2f,kRicoWhizGain,kHitEq,0.0f,false},
}, kTrimDb, kOpen, kHitSize, kHitLeast, 0.0f, kHitHold, "rico"};
constexpr Event kHitPierce{2,{
    {k_hit_pierce,-5.0f+10.0f,kUnpierceGain,kHitEq,0.0f,false},
    {k_dull_med,3.0f,kUnpierceGain,kHitEq,0.2f,false,kDullPitch},
}, -22.0f+kTrimDb, kOpen, kHitSize, kHitLeast, 0.0f, kHitHold, "hit_pierce"};
// Over an explosive round's explosion on armour, by its tier: the small ones' hit_small_unpierce impact (tank_hit_small,
// its dull layer hit_dull_small), the rest hit_big's / hit_aphe_big's impact and distant layers.
constexpr Event kArmourSmall{2,{
    {k_hit_small,-5.0f+10.0f,kUnpierceGain,kHitEq,0.0f,false},
    {k_dull_small,6.0f+6.0f,kUnpierceGain,kHitEq,0.18f,false,kDullPitch},
}, -22.0f+kTrimDb, kOpen, kHitSize, kHitLeast, 0.0f, kHitHold, "hit_small_unpierce"};
constexpr Event kArmourMid{2,{
    {k_hit_mid,-5.0f+10.0f,kArmourGain,kHitEq,0.0f,false},
    {k_hit_med_far,6.0f,kArmourFar,kHitEq,0.19f,false},
}, 6.0f-16.0f+kTrimDb, kOpen, kHitSize, kHitLeast, 0.0f, kHitHold, "hit_aphe_mid"};
constexpr Event kArmourBig{2,{
    {k_hit_big,-5.0f+10.0f,kArmourGain,kHitEq,0.0f,false},
    {k_hit_big_far,6.0f,kArmourFar,kHitEq,0.19f,false},
}, 6.0f-16.0f+kTrimDb, kOpen, kHitSize, kHitLeast, 0.0f, kHitHold, "hit_aphe_big"};
// What a plain round's strike on armour is heard as: `heavy` not a bullet class, `pierce` its AmmoIsPenetration (a
// bullet going through is still a bullet's hit), `facing` the cosine between its way and the face it strikes (1 head
// on, 0 grazing). Under kGlancing (about 75 degrees off the face's normal: ours) it glances off, unless it goes through.
enum class Hit : std::uint8_t { bullet, ricoBullet, heavy, ricoHeavy, pierce };
constexpr int kHitKinds=5;
constexpr float kGlancing=0.26f;
inline Hit HitKind(bool heavy,bool pierce,float facing) noexcept {
    const bool glancing=std::isfinite(facing) && facing<kGlancing;
    if(heavy)return pierce ? Hit::pierce : glancing ? Hit::ricoHeavy : Hit::heavy;
    return glancing && !pierce ? Hit::ricoBullet : Hit::bullet;
}
inline const Event& HitEvent(Hit h) noexcept {
    return h==Hit::bullet ? kHitBullet : h==Hit::ricoBullet ? kRicoBullet : h==Hit::heavy ? kHitHeavy :
           h==Hit::ricoHeavy ? kRicoHeavy : kHitPierce;
}
inline const Event& ArmourEvent(Blast b) noexcept {
    return b==Blast::minor ? kArmourSmall : b==Blast::shell ? kArmourMid : kArmourBig;
}
// m: past it a hit is not heard (hit_mgun's trigger range, rico_mgun's, the others' maximum distance).
constexpr float kHitFar[kHitKinds]={200.0f,250.0f,500.0f,500.0f,500.0f};
// At most WT's maxVoices (hit_mgun 4, the others 3) within about one's length (ours), 20 / 50 ms apart (ours).
constexpr Budget kHitBudget[kHitKinds]={{4,0.02f,0.9f},{3,0.05f,0.9f},{3,0.05f,2.0f},{3,0.05f,1.5f},{3,0.05f,2.0f}};
}  // namespace crew::wtevent
