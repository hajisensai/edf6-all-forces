// vehmix.h: what a ground vehicle sounds like, worked out from what it does (vehsound.cpp reads the game, jetaudio.cpp
// plays; docs/sound-re.md §9). No game and no XAudio2 in here: tools/vsound_check.cpp checks these against cases.
#pragma once
#include "jetaudio.h"
#include "sazabi_sound.h"
#include <cmath>
#include <cstdint>

namespace crew::vmix {
inline float Clamp01(float x) noexcept { return x<0.0f ? 0.0f : x>1.0f ? 1.0f : x; }
inline float Smooth(float x) noexcept { x=Clamp01(x);return x*x*(3.0f-2.0f*x); }

// --- The engine ---
// The revs (0 idle .. 1 governed top): the load (the stock engine's own, CarBase +0x1A80: how hard it is driven) brings
// them up at once, the speed holds them (a tank at speed in a low gear is not idling). The engine's pitch rises
// kRevPitch-fold from idle to the top, each layer as the load says: idle fades out as the load fades the load layer in.
struct EngineClass { float topSpeed,revPitch,idleGain,loadGain; };
constexpr EngineClass kHeavy{16.0f,2.3f,0.55f,1.0f},kLight{22.0f,2.6f,0.5f,0.9f},kBike{30.0f,3.2f,0.45f,0.9f};
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

// --- The guns ---
// Every vehicle weapon heard by what it is (the user, 2026-10-06: "the vehicles' sounds: change the like ones too";
// 2026-10-07: "the tanks' and the other vehicles' weapon sounds closer to the real thing, by calibre and round: the
// loading, the shot, the case landing"). The game has no calibre: what stands for it is read off the weapon once
// (vehsound.cpp GunFor): its round's class (rounds.cpp's, the rail gun's SolidBullet01Rail apart from the other CANNON
// rounds), guided or not, FireInterval (frames), FireBurstCount (+0x370), a looped fire sound and its volume (the drill's
// is set silent: it is no gun), AmmoDamage (+0x89C, the SGO's: before the request's tier), AmmoExplosion (+0x8B0, m)
// and AmmoSpeed (m a frame). docs/sound-re.md §9.5 has the stock weapons' numbers each case is drawn from.
enum class Round { cannon, rail, beam, grenade, gun, missile, other };
struct GunFacts { Round round; bool homing,looped; int frames,burst; float volume,damage,blast,speed; };
// The calibres, as ProfileOf tells them apart:
//  - stock: keeps its own sound (beams, lasers, flames, acid, homing lasers, a beam held, a slow gun's looped sound: the
//    Barga's; a bomb let fall: no gun fires it; anything silent);
//  - mg: a belt-fed gun (7.62 mm to the flak's 35 mm: the machine guns, the gatlings, the Kepler's twin guns and its
//    grenade belt) firing every kRapidFrames or faster, or whose stock sound loops: a burst loop while it fires;
//  - autocannon: any other bullet or a fast shell gun (20-40 mm: the Striker's, the robot truck's rifle, the 410's guns):
//    a round's report each shot and its case;
//  - grenade: a 40 mm low-pressure launcher (the Begaruta's, the Titan's side launchers, the Blacker's
//    DLC grenade cannons): a hollow thump, a light aluminium case;
//  - medium: a 75-105 mm gun (shells under kTankDamage: the Titan's sub gun, the Begaruta's cannon): its report, a
//    hand loader, the brass case out on the turret floor;
//  - tank: a 120 mm tank gun (the Blacker, the E551): its report, the loader, the combustible case's steel stub base
//    dropped inside the turret (heard muffled);
//  - howitzer: a 155 mm gun (a lobbed shell of kHowitzerDamage and more: the self-propelled artillery): separate
//    loading, the shell rammed, the charge modules one by one, the breech closed, the primer; no case of its own;
//  - heavy: a super-heavy gun (kHeavyDamage and a kHeavyBlast blast: the Titan's main gun), loaded the same way;
//  - rail: a rail gun (SolidBullet01Rail): no powder and no case, the capacitors' charge whining up before it is ready
//    and the discharge's crack;
//  - rocket: unguided rockets rippled off rails (a lobbed volley of kRippleBurst and more: the Katyusha): each rail's
//    ignition, no case, the next rockets latched onto the rails while it waits;
//  - missile: a missile or a rocket with a motor (MSL / RKT): its launch.
enum class Bore : int { stock, mg, autocannon, grenade, medium, tank, howitzer, heavy, rail, rocket, missile, count };
constexpr int kMainGunFrames=60,kRapidFrames=5,kRippleBurst=10;
constexpr float kDropSpeed=0.4f;   // m a frame: a lobbed round let go slower than this is dropped, not fired (the bombs)
constexpr float kHowitzerDamage=1000.0f,kHeavyDamage=600.0f,kHeavyBlast=25.0f,kTankDamage=300.0f;
inline Bore ProfileOf(const GunFacts& f) noexcept {
    if(!(f.volume>0.0f))return Bore::stock;
    if(f.round==Round::missile)return Bore::missile;   // a missile's homing is its round's: it still launches
    const bool slow=f.frames>=kMainGunFrames;
    if(f.homing || f.round==Round::other || (f.round==Round::beam && (f.looped || !slow)))return Bore::stock;
    if(f.round==Round::grenade) {
        if(f.speed<kDropSpeed)return Bore::stock;
        if(f.burst>=kRippleBurst)return Bore::rocket;
        if(f.frames<=kRapidFrames || f.looped)return Bore::mg;
        return slow && f.damage>=kHowitzerDamage ? Bore::howitzer : Bore::grenade;
    }
    if(f.looped)return slow ? Bore::stock : Bore::mg;
    if(f.frames<=kRapidFrames)return Bore::mg;
    if(!slow || f.round==Round::gun)return Bore::autocannon;
    if(f.round==Round::rail)return Bore::rail;
    if(f.damage>=kHeavyDamage && f.blast>=kHeavyBlast)return Bore::heavy;
    return f.damage>=kTankDamage ? Bore::tank : Bore::medium;
}
// What a calibre sounds like. Its group (the ini's volume: VehicleGunVolume the main guns, VehicleMgVolume the small
// guns, VehicleMissileVolume the launches); its shot (a report: near and far clips faded across by Gun below and
// gathered into one per vehicle a frame; a round: one clip a shot; a burst: Rapid's loop); its share of the mix and the
// distance it is heard at full within; its case (the clip of it landing, after `delay` s, `muffle` more of the air's
// dulling: inside the turret; none: no case, or the weapon's own physical one, ShellCase, landing with its own sound);
// its loader's steps (each `frames` after the shot, or `frames` before it is ready again, its clip at `pitch`).
enum class Group { none, main, mg, missile };
enum class Fire { none, report, round, burst };
struct Step { float frames; bool fromReady; int clip; float pitch; };
struct Casing { int clip; float delay,muffle; };
constexpr int kMostSteps=8;
struct Profile { Bore bore; Group group; Fire fire; int nearClip,farClip; float share,ref; Casing casing; int steps; Step step[kMostSteps]; };
constexpr float kGunShare=1.0f,kRapidShare=0.55f,kAutoShare=0.75f,kGrenadeShare=0.7f,kBrassShare=0.35f,kMissileShare=0.8f,
                kRocketShare=0.45f;
constexpr float kGunRef=25.0f,kRapidRef=20.0f,kMissileRef=15.0f;   // m: heard at full within, ref / d beyond
constexpr Profile kProfiles[static_cast<int>(Bore::count)]={
    {Bore::stock,Group::none,Fire::none,-1,-1,0.0f,0.0f,{-1,0.0f,0.0f},0,{}},
    {Bore::mg,Group::mg,Fire::burst,audio::kClipMg,audio::kClipGatling,kRapidShare,kRapidRef,{audio::kClipBrass,0.0f,0.0f},0,{}},
    {Bore::autocannon,Group::mg,Fire::round,audio::kClipAutocannon,-1,kAutoShare,kRapidRef,{audio::kClipCaseSmall,0.3f,0.0f},0,{}},
    {Bore::grenade,Group::mg,Fire::round,audio::kClipGrenadeShot,-1,kGrenadeShare,kRapidRef,{audio::kClipCaseGrenade,0.35f,0.0f},0,{}},
    // The breech opened and the case drawn out, the next round rammed and the breech closed: a hand loader's.
    {Bore::medium,Group::main,Fire::report,audio::kClipGunMediumNear,audio::kClipGunMediumFar,kGunShare,kGunRef,{audio::kClipCaseMedium,0.55f,0.35f},3,
     {{20.0f,false,audio::kClipEject,1.1f},{46.0f,true,audio::kClipLoad,1.1f},{14.0f,true,audio::kClipClose,1.1f}}},
    {Bore::tank,Group::main,Fire::report,audio::kClipGunNear,audio::kClipGunFar,kGunShare,kGunRef,{audio::kClipCaseStub,0.62f,0.45f},3,
     {{24.0f,false,audio::kClipEject,1.0f},{54.0f,true,audio::kClipLoad,1.0f},{16.0f,true,audio::kClipClose,1.0f}}},
    // Separate loading: the breech open, the shell rammed, three charge modules pushed in after it, the breech closed,
    // the primer seated in its vent.
    {Bore::howitzer,Group::main,Fire::report,audio::kClipHowitzerNear,audio::kClipHowitzerFar,kGunShare,kGunRef,{-1,0.0f,0.0f},7,
     {{30.0f,false,audio::kClipBreechOpen,1.0f},{170.0f,true,audio::kClipShellRam,1.0f},{132.0f,true,audio::kClipCharge,1.0f},
      {104.0f,true,audio::kClipCharge,1.05f},{76.0f,true,audio::kClipCharge,0.96f},{42.0f,true,audio::kClipClose,0.85f},{14.0f,true,audio::kClipPrimer,1.0f}}},
    {Bore::heavy,Group::main,Fire::report,audio::kClipGunHeavyNear,audio::kClipGunHeavyFar,kGunShare,kGunRef,{-1,0.0f,0.0f},4,
     {{36.0f,false,audio::kClipBreechOpen,0.85f},{150.0f,true,audio::kClipShellRam,0.85f},{96.0f,true,audio::kClipCharge,0.9f},{40.0f,true,audio::kClipClose,0.75f}}},
    {Bore::rail,Group::main,Fire::report,audio::kClipRailShot,audio::kClipRailFar,kGunShare,kGunRef,{-1,0.0f,0.0f},2,
     {{90.0f,true,audio::kClipRailCharge,1.0f},{8.0f,true,audio::kClipRailReady,1.0f}}},
    {Bore::rocket,Group::missile,Fire::round,audio::kClipRocketRail,-1,kRocketShare,kMissileRef,{-1,0.0f,0.0f},5,
     {{150.0f,false,audio::kClipRocketLoad,1.0f},{240.0f,false,audio::kClipRocketLoad,1.06f},{330.0f,false,audio::kClipRocketLoad,0.96f},
      {420.0f,false,audio::kClipRocketLoad,1.03f},{510.0f,false,audio::kClipRocketLoad,0.98f}}},
    {Bore::missile,Group::missile,Fire::round,audio::kClipMissile,-1,kMissileShare,kMissileRef,{-1,0.0f,0.0f},0,{}},
};
inline const Profile& ProfileFor(Bore b) noexcept {
    const int i=static_cast<int>(b);
    return kProfiles[i>=0 && i<static_cast<int>(Bore::count) ? i : 0];
}
// A rapid gun's burst: the loop made at `madeRate` rounds a second played at its own rate (frames a round), the pitch
// kept within kBurstRatio of the made one; a gun counts as firing kBurstHold frames past its interval after a round.
constexpr float kBurstRatioLo=0.75f,kBurstRatioHi=1.35f,kBurstHold=3.0f;
inline float BurstRatio(float frames,float madeRate) noexcept {
    const float r=frames>0.5f ? 60.0f/frames/madeRate : kBurstRatioHi;
    return r<kBurstRatioLo ? kBurstRatioLo : r>kBurstRatioHi ? kBurstRatioHi : r;
}
inline bool Firing(float sinceRound,float frames) noexcept { return sinceRound<=(frames>1.0f ? frames : 1.0f)+kBurstHold; }
// The report's mix at `d` m: near (the crack and the punch) within kGunNear, far (the rumble) from kGunFar, the two
// faded across in log distance; heard at full within kGunRef, ref / d beyond; it arrives d / kSoundSpeed s late.
constexpr float kGunNear=60.0f,kGunFar=700.0f,kSoundSpeed=340.0f;
struct GunMix { float nearGain,farGain,delay; };
inline GunMix Gun(float d) noexcept {
    const float f=d>kGunNear ? Clamp01(std::log(d/kGunNear)/std::log(kGunFar/kGunNear)) : 0.0f;
    const float at=d>kGunRef ? kGunRef/d : 1.0f;
    return GunMix{at*std::sqrt(1.0f-f),at*std::sqrt(f)*1.2f,d/kSoundSpeed};
}
// A shot another machine fired (online, docs/online-re.md §10): the weapon's received shot count (+0x1544, stored by
// 0x690540) went up past the shots its own copy fired (+0xBD0: 0x690420 fires them and counts them up only for a gun
// whose operator is this machine's), so no round left this copy and its rounds and wait never moved. A copy that fires
// its own shots copies +0xBD0 into +0x1544 to send it (0x6947AD): never past it.
inline bool RemoteShot(std::int32_t before,std::int32_t now,std::int32_t fired) noexcept { return now>before && now>fired; }

// --- The loader ---
// A gun's wait between rounds (its fire interval, or its magazine's reload: frames) heard as its calibre's steps
// (kProfiles), each when its time comes: `frames` after the shot, or `frames` before the gun is ready. The steps are as
// made when the wait has room for them all (the last after the shot kStepGap frames before the first before the end),
// else drawn in together in their order; a wait under kReloadFrames is an autoloader's (no steps).
constexpr float kReloadFrames=90.0f,kStepGap=8.0f;
inline float StepScale(const Profile& p,float total) noexcept {
    float after=0.0f,before=0.0f;
    for(int i=0;i<p.steps;++i) {
        float& at=p.step[i].fromReady ? before : after;
        at=p.step[i].frames>at ? p.step[i].frames : at;
    }
    const float need=after+before+kStepGap;
    return need<=total ? 1.0f : total/need;
}
// The steps due this frame (bit i: step i): the wait went from `before` to `left` frames of `total`; `since` frames since
// the shot.
inline unsigned ReloadCues(const Profile& p,float before,float left,float total,float sinceBefore,float since) noexcept {
    if(total<kReloadFrames || p.steps<=0)return 0u;
    const float k=StepScale(p,total);
    unsigned c=0u;
    for(int i=0;i<p.steps && i<kMostSteps;++i) {
        const float f=p.step[i].frames*k;
        if(p.step[i].fromReady ? before>f && left<=f : sinceBefore<f && since>=f)c|=1u<<i;
    }
    return c;
}
// A case landing inside the turret: heard through the air's dulling of `dulled` (0 near .. 1 far) and `muffle` more.
inline float Muffled(float dulled,float muffle) noexcept { return Clamp01(dulled+muffle); }

// --- Hearing ---
// Each group's level in the mix (times its ini volume and the game's own) and the distance it is heard at full within
// (m), the rest falling off as Falloff says: the engine 1 / r beyond kEngineRef, the turret and the loader faster (their
// sounds are small ones, heard from close by).
constexpr float kEngineShare=0.6f,kTurretShare=0.5f,kReloadShare=0.7f;   // the guns' shares: kProfiles
constexpr float kEngineRef=12.0f,kTracksRef=10.0f,kTurretRef=5.0f,kReloadRef=4.0f;
constexpr float kTrackHalf=1.8f;   // m: a tank's track from its middle (an E551's half width): turning on the spot runs them
// Gain at `d` m of a sound heard at full within `ref`, falling as (ref / d)^`fall` beyond.
inline float Falloff(float d,float ref,float fall) noexcept { return d>ref ? std::pow(ref/d,fall) : 1.0f; }
constexpr float kFarAt=2500.0f;    // m: the air's dulling at its fullest (the jets'): audio::Heard's distance is d / kFarAt

// --- The Sazabi (sazabi_sound.cpp; docs/sound-re.md §10) ---
// Each of its sounds: the ini group it is heard at (its movement the engine's, VehicleEngineVolume; its beams, cannon,
// funnels and blade the main guns', VehicleGunVolume; its missiles the launches', VehicleMissileVolume), its share of
// the mix, the distance it is heard at full within (m) and its fall beyond ((ref / d)^fall), and how much each play's
// pitch may differ (+-: footfalls and shots that are not all alike).
enum class SzGroup { move, gun, missile };
struct SzSound { SzGroup group; float share,ref,fall,jitter; };
constexpr SzSound kSzSfx[static_cast<int>(SzSfx::count)]={
    {SzGroup::gun,0.9f,25.0f,1.0f,0.04f},       // beamShot
    {SzGroup::gun,0.8f,20.0f,1.0f,0.06f},       // beamHit
    {SzGroup::gun,0.6f,10.0f,1.2f,0.0f},        // saberOn
    {SzGroup::gun,0.6f,10.0f,1.2f,0.0f},        // saberOff
    {SzGroup::gun,0.7f,15.0f,1.2f,0.06f},       // whoosh
    {SzGroup::gun,0.9f,20.0f,1.0f,0.05f},       // saberHit
    {SzGroup::gun,1.0f,30.0f,1.0f,0.0f},        // cannonShot
    {SzGroup::gun,0.5f,8.0f,1.2f,0.05f},        // funnelLaunch
    {SzGroup::gun,0.55f,12.0f,1.1f,0.06f},      // funnelShot
    {SzGroup::gun,0.45f,8.0f,1.2f,0.05f},       // funnelDock
    {SzGroup::missile,kMissileShare,kMissileRef,1.0f,0.04f},   // missileLaunch (the vehicles' launch, kClipMissile)
    {SzGroup::move,0.85f,30.0f,1.0f,0.04f},     // footstep
    {SzGroup::move,1.0f,30.0f,1.0f,0.0f},       // land
    {SzGroup::move,0.8f,20.0f,1.0f,0.03f},      // dash
};
constexpr SzSound kSzLoop[static_cast<int>(SzLoop::count)]={
    {SzGroup::move,0.7f,20.0f,1.0f,0.0f},       // thrusters
    {SzGroup::gun,0.45f,10.0f,1.2f,0.0f},       // saberHum
    {SzGroup::gun,0.6f,15.0f,1.1f,0.0f},        // cannonCharge
};
constexpr float kSzHear=3000.0f;                  // m: no voice farther
constexpr float kSzFadeIn=0.06f,kSzFadeOut=0.3f;  // s: a loop's level coming up / going out (0 to 1 and back)
// A loop at `level` (0..1): its gain (before its share and the distance) and pitch. The thrusters roar louder and
// higher with the thrust, the charge's whine climbs an octave and more as it fills, the blade hums at its level. 0: silent.
struct SzLoopMix { float gain,ratio; };
inline SzLoopMix SazabiLoopMix(SzLoop which,float level) noexcept {
    const float l=std::isfinite(level) ? Clamp01(level) : 0.0f;
    if(l<=0.0f)return SzLoopMix{0.0f,1.0f};
    switch(which) {
    case SzLoop::thrusters: return SzLoopMix{0.3f+0.7f*l,0.75f+0.5f*l};
    case SzLoop::cannonCharge: return SzLoopMix{0.35f+0.65f*l,0.5f+1.3f*l};
    default: return SzLoopMix{l,1.0f};
    }
}
// A loop's gain `dt` s later going to `target`: up over kSzFadeIn, down over kSzFadeOut (full scale), never past it.
inline float SzFade(float gain,float target,float dt) noexcept {
    const float step=dt/(target>gain ? kSzFadeIn : kSzFadeOut);
    return target>gain ? (gain+step<target ? gain+step : target) : (gain-step>target ? gain-step : target);
}
}  // namespace crew::vmix
