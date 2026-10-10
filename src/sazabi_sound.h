// The Sazabi's sounds (sazabi.cpp decides them; sazabi_sound.cpp plays them through the plugin's own voices, jetaudio.cpp,
// heard from the camera like the ground vehicles' sounds: left and right, far and muffled, Doppler, the game's master x
// effects volume, quiet while paused). The game has none of these (the beam rifle's shot and its rounds' hits the weapon
// SGO plays: pylib/vcobjects.py sazabi_weapons). Game thread only.
#pragma once

namespace crew {
// One-shots at a world point.
enum class SzSfx : int {
    beamShot,       // the beam rifle (the shot over the stock SGO's: its pink beam's crack)
    beamHit,        // a beam's impact
    saberOn,        // the tomahawk's blade lit
    saberOff,       // ...put out
    whoosh,         // a swing through the air
    saberHit,       // the blade biting (a hit's crackle and boom)
    cannonShot,     // the chest cannon's fan of beams
    funnelLaunch,   // a funnel leaving its pack
    funnelShot,     // a funnel's beam
    funnelDock,     // a funnel back in its pack
    missileLaunch,  // the shield's missiles
    footstep,       // a 25 m mech's foot coming down
    land,           // landing from the air
    dash,           // the thrusters' burst of a dash
    shieldBlock,    // a hit stopped on the raised shield (a heavy plate struck)
    count
};
// Loops: set every frame they should sound (`level` 0..1 its loudness and, for the thrusters and the charge, its pitch
// too); not set for a frame (or level 0), it fades out. One of each for the local player's Sazabi.
enum class SzLoop : int {
    thrusters,      // the main thrusters' roar (level: thrust)
    saberHum,       // the lit blade's hum
    cannonCharge,   // the chest cannon charging (level: the charge, rising)
    count
};
void SazabiSfx(SzSfx which,const float* pos) noexcept;
void SazabiLoop(SzLoop which,const float* pos,const float* vel,float level) noexcept;
void SazabiSoundTick() noexcept;   // once a frame (crew.cpp InputHook, any order against the Sazabi's frame): loops not set
                                   // this frame or the last fade out; the plugin or VehicleSound off: all silent at once
void ResetSazabiSound() noexcept;  // a new mission (mission.cpp): every voice let go of, nothing still on its way
}  // namespace crew
