// jetaudio.cpp: the jets' engine sound out of the plugin's own XAudio2 voices (the game's sound system has no
// sound of a jet engine and no Doppler, docs/sound-re.md). jetsound.cpp decides what each jet sounds like from the
// game; this file only plays it. Called on the game thread; the watchdog is its own thread.
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
// The cockpit's lock tone this frame (the player's picked store, playerjet.cpp): 2 locked, a steady high tone; 1
// locking, beeps quickening with `progress` (0..1); 0 none. Not called for kToneStaleMs, it goes quiet.
void LockTone(int state,float progress) noexcept;
// The cockpit's threat warning this frame (playerjet.cpp): 2 a missile homing on it, fast high beeps; 1 an enemy's
// missile lock on it, slower low beeps; 0 none. Not called for kToneStaleMs, it goes quiet.
void ThreatTone(int state) noexcept;
// Once a game frame: the game runs. With no beat for kQuietMs (paused, loading, a menu) the watchdog silences
// everything until the next beat, at `volume` (the game's master and effect volume, times the plugin's own).
void Beat(float volume) noexcept;
}  // namespace crew::audio
