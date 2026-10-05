// jetaudio.cpp: the jets' engine sound and the cockpit's tones and warnings out of the plugin's own XAudio2 voices (the
// game's sound system has no sound of a jet engine, no Doppler and no cockpit warnings, docs/sound-re.md). jetsound.cpp
// decides what each jet sounds like from the game, warn.cpp what the cockpit warns of; this file only plays it. Called
// on the game thread; the watchdog and the Windows voice's rendering are threads of their own.
#pragma once
namespace crew::audio {
// What one jet sounds like this frame: the roar's and the whine's gain in the left and right ear, each one's playback
// rate (1 = as made: the engine's pitch and the Doppler ratio together) and how far off it is heard (0 near .. 1 far:
// the air takes its highs).
struct Mix { float roarL,roarR,whineL,whineR; float roarRatio,whineRatio; float distance; };
// The engine, its mastering voice and the sounds (the file next to the DLL, <dll name>_jet.wav, as the roar when it is
// there; else both made here). False: no sound (logged once).
bool Start() noexcept;
// A jet's voices: a slot, or -1 (all in use, or no engine).
int Open() noexcept;
void Set(int slot,const Mix& mix) noexcept;
void Close(int slot) noexcept;
// The cockpit's lock tone this frame (the player's picked store, playerjet.cpp), at Cfg().warnVolume: 2 locked, a steady high tone; 1
// locking, beeps quickening with `progress` (0..1); 0 none. Not called for kToneStaleMs, it goes quiet.
void LockTone(int state,float progress) noexcept;
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
// Once a game frame: the game runs. With no beat for kQuietMs (paused, loading, a menu) the watchdog silences
// everything until the next beat, at `volume` (the game's master and effect volume; each sound's own share is its
// caller's: the engines' JetSoundVolume in their Mix, the cockpit's Cfg().warnVolume here).
void Beat(float volume) noexcept;
}  // namespace crew::audio
