#pragma once
// The jets' air (docs/feature-wt-sound.md 「飞过、风声与机身」, 2026-11-27): what War Thunder plays of a jet besides its
// engine, copied from its own FMOD project (Metadata of the event and its buses; the clips tools/wt_sound.py
// AIR_EFFECTS). The user (2026-11-27): 「声音『怪』 加飞过时的呼啸声和风声 都抄战雷的」.
//  - The passby (Master/aircraft/effects/jet_plane_passby): another jet's whoosh as it goes by close. Its event: at
//    most 3 at once (maxVoices 3, the furthest stolen), no cooldown; no rolloff (its spatializer's off), no Doppler,
//    sound size 10, min extent 120 deg; its master +6 on the passbys' bus (-18). Three tracks: close (its MultiSound of
//    the close recordings at 0.4 s on the timeline, its volume over the distance 0.42 m: -3 .. 300 m: -10), far (-6:
//    the med recordings at 0) and tail (-2: the far recordings at 0); far and tail each with the speed sheet's
//    passby_jet_close (600..1200 km/h, per recording 0 / -3 dB). Its snapshot engines_ai (2 s, attack and release
//    0.5 s) ducks every other jet's engine by its VCA's -23 dB.
//  - The wind (Master/aircraft/effects/wind): the own jet's airflow; its master -6 on the noises' way (-26); wind_low
//    and wind_hi_4 crossed over the speed (% of top), the event's gain over the speed and the distance (here: the
//    airspeed).
//  - The airframe (Master/aircraft/common/vibration_overload): the own jet's structure groaning under g; its master
//    -14 on the noises' way, plane_overload_loop's volume over the overload (0: -80 .. 0.996: 0 dB).
// What is ours (WT runs them on its own flight model's inputs, which the plugin has not): when a passby starts (the
// closest approach predicted from the heard positions and velocities: its close recording's loudest 0.4 s after its
// start, so it starts 0.8 s ahead); the speed (the relative speed, km/h); the overload's 0..1 ((g - 1) / 7, g the
// load factor from the jet's own acceleration); the wind's "distance" parameter read as the airspeed in km/h (its range
// 150..1000 fits no distance of WT's); the sheet's two passby_jet_close copies (far's and tail's) played as one voice
// at their summed level; the overlapping passbys' duck the deepest one's.
// Pure: tools/jetsound_check.cpp checks it offline; jetsound.cpp plays it.
#include "sound_engine.h"
#include <cmath>

namespace crew::jetair {
using engine::Curve;
using engine::Region;

// --- The passby ---
constexpr int kMostPassbys=3;                          // WT: the event's maxVoices
constexpr float kPassbyReach=300.0f;                   // m: its distance parameter's range (ours: none predicted past)
constexpr float kPassbyLead=0.8f;                      // s: started this far ahead of the closest approach (ours)
constexpr float kPassbySlowest=300.0f/3.6f;            // m/s: its speed parameter's floor (ours: none slower)
constexpr float kPassbyDb=-18.0f+6.0f+engine::kJetTrimDb;   // the passbys' bus, the event's master, the jets' trim
constexpr float kCloseAt=0.4f;                         // s: the close track's MultiSound on the timeline
constexpr Curve kCloseDist{2,{{0.42f,-3.0f},{300.0f,-10.0f}}};
constexpr float kMedDb=-6.0f,kFarDb=-2.0f;             // the far track's, the tail's
constexpr float kFastFrom=600.0f;                      // km/h: the speed sheet's passby_jet_close from here
constexpr float kFastDb[2]={0.0f,-3.0f};               // its recordings' (-003, -001)
constexpr float kPassbySize=10.0f,kPassbyLeast=120.0f/360.0f;   // its spatializer's sound size, min extent
constexpr float kDuckDb=-23.0f,kDuckAttack=0.5f,kDuckLength=2.0f,kDuckRelease=0.5f;   // the snapshot engines_ai
enum Track : int { kClose, kMed, kFar, kFast, kTracks };

inline float Dot3(const float* a,const float* b) noexcept { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }

// The closest approach of a source `r` from the listener (m) moving `v` against it (m/s): in `t` s, `d` m off.
struct Approach { float t,d; };
inline Approach Closest(const float* r,const float* v) noexcept {
    const float vv=Dot3(v,v);
    if(!(vv>1e-6f))return Approach{0.0f,std::sqrt(Dot3(r,r))};
    const float t=-Dot3(r,v)/vv;
    const float at[3]={r[0]+v[0]*t,r[1]+v[1]*t,r[2]+v[2]*t};
    return Approach{t,std::sqrt(Dot3(at,at))};
}
// Whether a source armed for one starts a passby now (`speed` its relative speed, m/s): its closest approach near and
// ahead within the lead.
inline bool Fires(const Approach& a,float speed) noexcept {
    return a.d<kPassbyReach && a.t>0.0f && a.t<=kPassbyLead && speed>=kPassbySlowest;
}
// Whether one that has passed is armed again: its closest approach behind it (one passby a pass).
inline bool Rearms(const Approach& a) noexcept { return !(a.t>0.0f); }

// A passby's tracks' gains (amplitude, before the volume) `distance` m off, started at `kmh` (its speed sheet's
// choice, made at its start), its passby_jet_close recording `fast` (0, 1).
struct Gains { float g[kTracks]; };
inline Gains PassbyAt(float distance,float kmh,int fast) noexcept {
    Gains o{};
    o.g[kClose]=engine::Amp(kPassbyDb+engine::At(kCloseDist,distance));
    o.g[kMed]=engine::Amp(kPassbyDb+kMedDb);
    o.g[kFar]=engine::Amp(kPassbyDb+kFarDb);
    const float f=kFastDb[fast>0 ? 1 : 0];
    o.g[kFast]=kmh>=kFastFrom ? engine::Amp(kPassbyDb+kMedDb+f)+engine::Amp(kPassbyDb+kFarDb+f) : 0.0f;
    return o;
}
// The snapshot's intensity `age` s into a passby (0..1): in over its attack, held to its end, out over its release.
inline float DuckAt(float age) noexcept {
    if(!(age>=0.0f))return 0.0f;
    if(age<kDuckAttack)return age/kDuckAttack;
    if(age<=kDuckLength)return 1.0f;
    if(age<kDuckLength+kDuckRelease)return 1.0f-(age-kDuckLength)/kDuckRelease;
    return 0.0f;
}
// The other jets' engines' gain under it (the deepest intensity of those playing).
inline float DuckGain(float intensity) noexcept { return engine::Amp(kDuckDb*intensity); }

// --- The wind and the airframe (the own jet) ---
constexpr float kNoiseDb=-32.0f+10.0f-4.0f+engine::kJetTrimDb;   // the noises', aircraft's and engines' buses
constexpr float kWindDb=-6.0f+kNoiseDb,kOverloadDb=-14.0f+kNoiseDb;
constexpr Curve kWindShare{5,{{0.0f,-19.49f},{40.96f,-9.94f},{81.93f,-5.46f},{90.96f,-2.31f},{100.0f,0.0f}}};
constexpr Curve kWindAir{3,{{150.0f,-80.0f},{575.0f,-6.02f},{1000.0f,0.0f}}};   // over the airspeed, km/h (ours)
constexpr Region kWindLowR{0.0f,0.0f,16.98f,89.57f},kWindHighR{16.98f,89.57f,100.0f,100.0f};
constexpr Curve kOverload{2,{{0.0f,-80.0f},{0.996f,0.0f}}};
constexpr float kOverloadSpan=7.0f;                    // g past 1 to the overload's full (ours)

// The wind's two loops' gains at `share` % of the jet's top speed and `kmh` km/h.
struct Wind { float low,high; };
inline Wind WindAt(float share,float kmh) noexcept {
    const float g=engine::Amp(kWindDb+engine::At(kWindShare,share)+engine::At(kWindAir,kmh));
    return Wind{g*engine::Fade(kWindLowR,share),g*engine::Fade(kWindHighR,share)};
}
// The airframe's loop's gain at the load factor `g`.
inline float OverloadAt(float g) noexcept {
    float x=(g-1.0f)/kOverloadSpan;
    x=!(x>0.0f) ? 0.0f : x>1.0f ? 1.0f : x;
    return engine::Amp(kOverloadDb+engine::At(kOverload,x));
}
// The load factor of a body accelerating `accel` (m/s², y up): what its wings carry against gravity, in g (1 level).
inline float LoadFactor(const float* accel) noexcept {
    const float a[3]={accel[0],accel[1]+9.8f,accel[2]};
    const float g=std::sqrt(Dot3(a,a))/9.8f;
    return std::isfinite(g) ? g : 1.0f;
}
}  // namespace crew::jetair
