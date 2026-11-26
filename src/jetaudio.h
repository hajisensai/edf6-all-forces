// jetaudio.cpp: the jets' engine sound and the cockpit's tones and warnings out of the plugin's own XAudio2 voices (the
// game's sound system has no sound of a jet engine, no Doppler and no cockpit warnings, docs/sound-re.md). jetsound.cpp
// decides what each jet sounds like from the game, warn.cpp what the cockpit warns of; this file only plays it. Called
// on the game thread; the watchdog and the Windows voice's rendering are threads of their own.
#pragma once
#include <cmath>
namespace crew::audio {
// What one jet sounds like this frame: the roar's and the whine's gain, each one's playback rate (1 = as made: the
// engine's pitch and the Doppler ratio together), how far off it is heard (0 near .. 1 far: the air takes its highs)
// and where (surround.h: `side` +1 right, `front` +1 ahead of the camera, `spread` 0 a point .. 1 all around).
struct Mix { float roar,whine; float roarRatio,whineRatio; float distance; float side,front,spread; };
// The engine, its mastering voice and the sounds (the file next to the DLL, <dll name>_jet.wav, as the roar when it is
// there; else both made here). False: no sound (logged once).
bool Start() noexcept;
// A jet's voices: a slot, or -1 (all in use, or no engine).
int Open() noexcept;
void Set(int slot,const Mix& mix) noexcept;
void Close(int slot) noexcept;
// The cockpit's lock tone this frame (the player's picked store, playerjet.cpp), at Cfg().warnVolume: 2 locked, a steady high tone; 1
// locking, beeps quickening with `progress` (0..1); 0 none. Not called for kToneStaleMs, it goes quiet.
void LockTone(int state,float progress,const void* weapon,const void* target) noexcept;
// The cockpit's warnings this frame (warn.cpp WarnTick), heard at Cfg().warnVolume:
//  - `threat`: 2 a missile homing on it, fast high beeps; 1 an enemy's missile lock on it, slower low beeps; 0 none.
//    `launch`: a missile just launched at it, a two-tone warble over the beeps;
//  - `stall`: the stall horn, steady while it is on;
//  - `callouts` (1 << Callout): the spoken warnings on now, one at a time, the most urgent first (it cuts a lesser one
//    short), each again after its own pause while it stays on (PULL UP back to back, a missile once a launch). Spoken by
//    the Windows voice (Cfg().warnVoice) or read from <dll name>_warn_<name>.wav next to the DLL; neither: a tone.
// Not called for kToneStaleMs, everything goes quiet.
enum Callout : int { kCallPullUp, kCallMissile, kCallStall, kCallTerrain, kCallSinkRate, kCallGear, kCallCount };
struct Cockpit { int threat; bool launch,stall; unsigned callouts; };
void Warn(const Cockpit& c) noexcept;
bool Running() noexcept;   // Start succeeded (Beat keeps it going)

// --- The ground vehicles' sounds (vehsound.cpp decides them, vsynth.h makes them; docs/sound-re.md §9) ---
// Each clip is the player's <dll name>_veh_<kClipName>.wav next to the DLL (16-bit PCM, mono or stereo, any rate; a loop
// made at its idle / its made speed / rate) when there is one, else made here (vsynth.h) once, on a thread of its own:
// until they are ready (ClipsReady) no voice opens and nothing plays. Engine and tracks loops, the turret's loop and
// stop, the main gun's report near and far, the loader's three sounds; the bikes' engine; a machine gun's and a
// gatling's burst loops and a burst's tail, an autocannon's round, the cases raining and one landing; a launch. The
// Sazabi's (sazabi_sound.cpp): its beams' shots and hits, its blade lit, put out, swung and biting, its chest cannon's
// shot, its funnels leaving, firing and docking, its footfalls, landing and dash; its thrusters', blade's and cannon's
// charge's loops (its missiles launch with kClipMissile). Then each calibre's own (vehmix.h kProfiles): a 75-105 mm
// gun's, a 155 mm howitzer's and a super-heavy gun's reports near and far (the 120 mm tank gun's are gun_near / gun_far),
// a rail gun's discharge near and far, its capacitors charging and its ready click; a 40 mm grenade launcher's thump, a
// rocket off its rail and the next one latched on; the cases landing (a medium gun's brass, a tank gun's stub base, a
// grenade's aluminium); the separate loading's breech opening, shell rammed, charge module and primer. Last the Q mark's
// cues (qmark.cpp): this machine's player marked, a teammate marked, a mark let go (not heard where they are: UI cues).
enum Clip : int { kClipHeavyIdle, kClipHeavyLoad, kClipLightIdle, kClipLightLoad, kClipTracks, kClipTurret, kClipTurretStop,
                  kClipGunNear, kClipGunFar, kClipEject, kClipLoad, kClipClose, kClipBikeIdle, kClipBikeLoad, kClipMg, kClipGatling,
                  kClipBurstTail, kClipAutocannon, kClipBrass, kClipCaseSmall, kClipMissile, kClipSzBeamShot, kClipSzBeamHit,
                  kClipSzSaberOn, kClipSzSaberOff, kClipSzWhoosh, kClipSzSaberHit, kClipSzCannonShot, kClipSzFunnelLaunch,
                  kClipSzFunnelShot, kClipSzFunnelDock, kClipSzFootstep, kClipSzLand, kClipSzDash, kClipSzThrusters,
                  kClipSzSaberHum, kClipSzCharge, kClipGunMediumNear, kClipGunMediumFar, kClipHowitzerNear, kClipHowitzerFar,
                  kClipGunHeavyNear, kClipGunHeavyFar, kClipRailShot, kClipRailFar, kClipRailCharge, kClipRailReady,
                  kClipGrenadeShot, kClipRocketRail, kClipRocketLoad, kClipCaseMedium, kClipCaseStub, kClipCaseGrenade,
                  kClipBreechOpen, kClipShellRam, kClipCharge, kClipPrimer, kClipMarkOwn, kClipMarkTeam, kClipMarkOff,
                  kClipSzShieldBlock, kClipCount };
// The Sazabi's one-shots' clips in sazabi_sound.h SzSfx's order (its missiles the vehicles' launch), its loops' in SzLoop's.
constexpr int kSazabiSfxClip[]={kClipSzBeamShot,kClipSzBeamHit,kClipSzSaberOn,kClipSzSaberOff,kClipSzWhoosh,kClipSzSaberHit,
                                kClipSzCannonShot,kClipSzFunnelLaunch,kClipSzFunnelShot,kClipSzFunnelDock,kClipMissile,kClipSzFootstep,
                                kClipSzLand,kClipSzDash,kClipSzShieldBlock};
constexpr int kSazabiLoopClip[]={kClipSzThrusters,kClipSzSaberHum,kClipSzCharge};
// How a sound is heard this frame: its gain (the caller's volume in it), where (surround.h Placing: `side` +1 right,
// `front` +1 ahead of the camera, `spread` 0 a point .. 1 all around; `sub`: a low layer, the LFE channel's alone), its
// playback rate (pitch and the Doppler ratio together) and the low-pass it is heard through (Hz: AirCutoff for the
// air's dulling over a distance).
struct Heard { float gain,side,front,spread; float ratio,cutoff; bool sub; };
// The air's dulling `away` (0 near .. 1 at vmix::kFarAt) off: the cutoff falling geometrically from 16 kHz to 1.2 kHz.
inline float AirCutoff(float away) noexcept {
    const float d=away<0.0f || !(away==away) ? 0.0f : away>1.0f ? 1.0f : away;
    return 16000.0f*std::pow(1200.0f/16000.0f,d);
}
bool ClipsReady() noexcept;               // Start succeeded and the clips are made (the first call has them made)
int OpenLoop(int clip) noexcept;          // a looping voice of `clip`, silent until set; -1: none free, or not ready
void SetLoop(int loop,const Heard& h) noexcept;
void CloseLoop(int loop) noexcept;
void PlayOnce(int clip,const Heard& h) noexcept;   // a one-shot (dropped with every one-shot voice busy)
// The ultimate strike's sounds (ultimate.cpp): only the player's recordings, <dll name>_ult_<kUltClipName>.wav next to the
// DLL (tools/sound_pack.py builds them from a sound archive), read with the vehicles' clips; a clip with no recording is not
// there (UltClipThere false) and the show plays the game's own cue for it instead.
enum UltClip : int { kUltCharge, kUltCircle, kUltThunder, kUltGather, kUltBeam, kUltBlast, kUltShock, kUltClipCount };
bool UltClipThere(int clip) noexcept;     // ClipsReady and its recording read
void PlayUlt(int clip,const Heard& h) noexcept;
// The War Thunder recordings (wt_clips.h): read with the vehicles' clips from <dll name>_wt\<name>_<i>.wav, kept in
// stereo when they are; a group with a variant missing is not there (logged once) and plays nothing.
bool WtThere(int clip) noexcept;          // ClipsReady and every variant of it read
void PlayWt(int clip,int variant,const Heard& h) noexcept;
// Once a game frame: the game runs. With no beat for kQuietMs (paused, loading, a menu) the watchdog silences
// everything until the next beat, at `volume` (the game's master and effect volume; each sound's own share is its
// caller's: the engines' JetSoundVolume in their Mix, the cockpit's Cfg().warnVolume here).
void Beat(float volume) noexcept;
// The Debug tallies (Cfg().debug; docs/feature-wt-sound.md §验证): an event started (its name, the layers queued, the
// distance and how its first layer is heard: logged with its output matrix at most once per kNoteMs per name, counted
// always), a count of anything else (`what` a string literal: the stock SE starts, the stock hit sounds taken), and once
// every kStatsMs with anything counted a line of them with the one-shot and loop voices' use and the limiter.
void Note(const char* event,int layers,float distance,const Heard& first) noexcept;
void Count(const char* what,int n=1) noexcept;
void StatsTick() noexcept;
}  // namespace crew::audio
