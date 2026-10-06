// vehmix.h: what a ground vehicle sounds like, worked out from what it does (vehsound.cpp reads the game, jetaudio.cpp
// plays; docs/sound-re.md §9). No game and no XAudio2 in here: tools/vsound_check.cpp checks these against cases.
#pragma once
#include <cmath>

namespace crew::vmix {
inline float Clamp01(float x) noexcept { return x<0.0f ? 0.0f : x>1.0f ? 1.0f : x; }
inline float Smooth(float x) noexcept { x=Clamp01(x);return x*x*(3.0f-2.0f*x); }

// --- The engine ---
// The revs (0 idle .. 1 governed top): the load (the stock engine's own, CarBase +0x1A80: how hard it is driven) brings
// them up at once, the speed holds them (a tank at speed in a low gear is not idling). The engine's pitch rises
// kRevPitch-fold from idle to the top, each layer as the load says: idle fades out as the load fades the load layer in.
struct EngineClass { float topSpeed,revPitch,idleGain,loadGain; };
constexpr EngineClass kHeavy{16.0f,2.3f,0.55f,1.0f},kLight{22.0f,2.6f,0.5f,0.9f};
inline float Revs(float speed,float load,const EngineClass& c) noexcept {
    return Clamp01(0.55f*Clamp01(load)+0.6f*Clamp01(std::fabs(speed)/c.topSpeed));
}
struct EngineMix { float idle,load,ratio; };
// `on` 0..1: the engine starting / stopping (its share of running).
inline EngineMix Engine(float revs,float load,float on,const EngineClass& c) noexcept {
    const float l=Smooth(0.35f*Clamp01(load)+0.75f*revs);
    EngineMix m;
    m.idle=on*c.idleGain*(1.0f-0.6f*l);
    m.load=on*c.loadGain*l;
    m.ratio=(1.0f+(c.revPitch-1.0f)*revs)*(0.75f+0.25f*on);   // it winds up as it starts, down as it stops
    return m;
}
// The tracks: silent standing, louder and faster with the speed (made at vsynth::kTrackSpeed).
struct TrackMix { float gain,ratio; };
inline TrackMix Tracks(float speed,float made) noexcept {
    const float s=std::fabs(speed);
    return TrackMix{0.7f*Smooth(s/3.0f)*(0.6f+0.4f*Clamp01(s/made)),0.35f+0.65f*s/made};
}

// --- The turret ---
// `slew` the fastest of the aim's axes against their own top rate (0..1): its drive whines up with it.
struct TurretMix { float gain,ratio; };
inline TurretMix Turret(float slew) noexcept {
    const float s=Clamp01(slew);
    return TurretMix{0.75f*Smooth(s*4.0f)*(0.55f+0.45f*s),0.7f+0.45f*s};
}
// A traverse stops (its stop sound) when the slew falls under kTurretStill after it was over kTurretMoving.
constexpr float kTurretMoving=0.35f,kTurretStill=0.05f;

// --- The main gun ---
// Which of a vehicle's weapons is a main gun (its report replaced): a shell-firing class (the HUD's label: CANNON a
// cannon or a rail gun round, BEAM the E551's EfsBullet shell, GREN a howitzer's or a grenade cannon's), unguided, a
// shot at most every kMainGunFrames (a cannon's breech, not an autocannon's belt), a fire sound that is one shot (not a
// loop: guns that loop it are machine guns and beams) and heard (the drill's is set silent: it is no gun).
constexpr int kMainGunFrames=60;
enum class Round { cannon, beam, grenade, other };
inline bool MainGun(Round r,bool homing,int fireFrames,bool loopedFire,float fireVolume) noexcept {
    return r!=Round::other && !homing && fireFrames>=kMainGunFrames && !loopedFire && fireVolume>0.0f;
}
// The report's mix at `d` m: near (the crack and the punch) within kGunNear, far (the rumble) from kGunFar, the two
// faded across in log distance; heard at full within kGunRef, ref / d beyond; it arrives d / kSoundSpeed s late.
constexpr float kGunNear=60.0f,kGunFar=700.0f,kGunRef=25.0f,kSoundSpeed=340.0f;
struct GunMix { float nearGain,farGain,delay; };
inline GunMix Gun(float d) noexcept {
    const float f=d>kGunNear ? Clamp01(std::log(d/kGunNear)/std::log(kGunFar/kGunNear)) : 0.0f;
    const float at=d>kGunRef ? kGunRef/d : 1.0f;
    return GunMix{at*std::sqrt(1.0f-f),at*std::sqrt(f)*1.2f,d/kSoundSpeed};
}

// --- The reload ---
// A gun's wait between rounds (its fire interval, or its magazine's reload: frames) heard as a loader's work: the spent
// case thrown out kEjectFrames after the shot, the next round rammed kLoadFrames before it is ready, the breech closed
// kCloseFrames before. Only a wait of kReloadFrames or more is a loader's (a shorter one is an autoloader's: nothing).
constexpr float kEjectFrames=24.0f,kLoadFrames=54.0f,kCloseFrames=16.0f,kReloadFrames=90.0f;
enum Cue : unsigned { kCueEject=1u,kCueLoad=2u,kCueClose=4u };
// The cues due this frame: the wait went from `before` to `left` frames of `total`; `since` frames since the shot.
inline unsigned ReloadCues(float before,float left,float total,float sinceBefore,float since) noexcept {
    if(total<kReloadFrames)return 0u;
    unsigned c=0u;
    if(sinceBefore<kEjectFrames && since>=kEjectFrames)c|=kCueEject;
    if(before>kLoadFrames && left<=kLoadFrames)c|=kCueLoad;
    if(before>kCloseFrames && left<=kCloseFrames)c|=kCueClose;
    return c;
}

// --- Hearing ---
// Each group's level in the mix (times its ini volume and the game's own) and the distance it is heard at full within
// (m), the rest falling off as Falloff says: the engine 1 / r beyond kEngineRef, the turret and the loader faster (their
// sounds are small ones, heard from close by).
constexpr float kEngineShare=0.6f,kTurretShare=0.5f,kReloadShare=0.7f,kGunShare=1.0f;
constexpr float kEngineRef=12.0f,kTracksRef=10.0f,kTurretRef=5.0f,kReloadRef=4.0f;
constexpr float kTrackHalf=1.8f;   // m: a tank's track from its middle (an E551's half width): turning on the spot runs them
// Gain at `d` m of a sound heard at full within `ref`, falling as (ref / d)^`fall` beyond.
inline float Falloff(float d,float ref,float fall) noexcept { return d>ref ? std::pow(ref/d,fall) : 1.0f; }
}  // namespace crew::vmix
