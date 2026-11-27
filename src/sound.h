// The plugin's sounds: jetsound.cpp (the listener, positioned sounds), vehsound.cpp, trace_rec.cpp,
// blastsound.cpp.
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include "sound_place.h"

namespace crew {
void ResetJetSound() noexcept;    // jetsound.cpp
// The jets' engine sound (jetsound.cpp): checked at load; per vehicle input (it picks the plugin's jets itself);
// once a frame, the plugin off too (the camera's motion; the sounds of jets gone, or all with the plugin off, stopped).
bool InstallJetSound() noexcept;
void JetSound(unsigned char* vehicle) noexcept;
void JetSoundTick() noexcept;
float GameEffectVolume() noexcept;   // jetsound.cpp: the game's master volume times its effect volume (0..1)
// The lock-on beeps of a vehicle's weapons: kept for a local player's seat, silenced for every other (jetsound.cpp).
void LockSound(unsigned char* vehicle) noexcept;
// Where a sound at `pos` moving `vel` (m/s) is heard from the camera (jetsound.cpp, the sound system's listener 0, as the
// jets' engines are placed; SoundPlace and the arithmetic: sound_place.h). `vel` may be nullptr: a still sound. False: no
// listener this frame.
bool SoundAt(const float* pos,const float* vel,SoundPlace* out) noexcept;
bool SoundListening() noexcept;   // the listener is placed this frame (JetSoundTick ran for the jets or the vehicles)
// SoundAt's whole input besides the sound's own (the trace's, trace_rec.cpp): whether it places sounds now, the listener
// (soundplace::kListenerFloats) and its velocity.
bool ListenerState(float* listener,float* listenerVel) noexcept;
bool ListenerAt(float* pos) noexcept;   // where that listener (the camera) is this frame; false: none
// The ground vehicles' sounds (vehsound.cpp, ini VehicleSound): checked at load; every vehicle's input, the plugin off
// too (then it gives the stock sounds back); a new mission.
bool InstallVehicleSound() noexcept;
void VehicleSound(unsigned char* vehicle) noexcept;              // VehicleSoundAt at GameMs
void VehicleSoundAt(unsigned char* vehicle,ULONGLONG ms) noexcept; // `ms`: the step's one "now" (GameMs read once)
// The offline replay's recording (trace_rec.cpp, ini TraceFile; docs/trace-replay.md): a step's inputs taken before it
// runs on `vehicle`, what it does recorded until TraceEnd; TraceReset where the plugin resets the step's state (a mission).
void TraceVehicleSound(unsigned char* vehicle,ULONGLONG ms) noexcept;   // `ms`: the step's, as VehicleSoundAt is given
void TraceEnd() noexcept;
void TraceReset() noexcept;
// Inside a recorded step: where its outputs go (null outside one). A pointer, not a function, so that the sources that
// record an output (jetaudio.cpp) need nothing of the recorder's when a test builds them.
inline void (*traceOutput)(const char* name,const void* data,std::size_t size) noexcept=nullptr;
inline bool TraceRecording() noexcept { return traceOutput!=nullptr; }
inline void TraceOutput(const char* name,const void* data,std::size_t size) noexcept { if(traceOutput)traceOutput(name,data,size); }
void ResetVehicleSound() noexcept;
bool VehicleSoundHeld(const void* preset) noexcept;   // a vehicle's stock sound preset held silent (vehsound.cpp)
// The rounds' explosions as War Thunder layers them (blastsound.cpp, ini ExplosionVolume): the calls that play a round's
// AmmoHitSe redirected at load; once a frame (SharedFrameTick, after JetSoundTick: the listener); a new mission.
bool InstallBlastSound() noexcept;
void BlastSoundTick() noexcept;
void ResetBlastSound() noexcept;
}  // namespace crew
