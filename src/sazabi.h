// sazabi.cpp: the Sazabi.
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>

namespace crew {
bool SazabiBodyStep(unsigned char* v,float* lin,float* ang) noexcept;     // sazabi.cpp
// sazabi.cpp: the Sazabi (docs/gundam-plan.md), a 506 body with the Sazabi mark (7401-7499): never crewed, never flown
// as a heli; with the player in seat 0 it walks and flies; its bones are posed every frame.
bool IsSazabi(const void* vehicle) noexcept;
// The local player's Sazabi as the HUD shows it (sazabi.cpp publishes it each frame on the game thread; hud.cpp reads it,
// fresh while published in the last steps, step_fresh.h). False with none (not riding one).
struct SazabiCue {
    float thruster;          // 0..1 the thrusters' charge
    float rifleReady;        // 0..1 to the beam rifle's next shot (1 ready)
    int missiles;            // shield missiles left
    float missileReady;      // 0..1 to the next salvo
    float missileLock;       // 0..1 the lock's progress (1 locked)
    float cannonCharge;      // 0..1 the chest cannon charging (0 not)
    float cannonReady;       // 0..1 to the cannon's next charge
    int funnelsOut;          // 0..6 funnels flying
    float funnelReady;       // 0..1 to the next launch
    bool guard,swinging,air; // the shield up, the tomahawk swinging, off its feet
    bool hasAim;
    float aim[3];            // the aim point (world): what the screen's centre looks at, every weapon fires at it
    bool hasLock;
    float lock[3];           // the missiles' locked target (world)
    // The flight (sazabi_flight.h) and the special weapon, for the reticle's gauges and readouts:
    float altitude;          // m: its soles over what is under them (0 on its feet)
    float speed;             // m/s over the ground
    float climb;             // m/s up (+) / down (-)
    bool boosting;           // the thrusters burning (a boost dash, a climb)
    bool overheat;           // the thrusters spent: no boost until it lands and they cool
    int special;             // the special used last: 0 shield missiles, 1 funnels, 2 mega particle cannon
    bool keys;               // the pilot on the keyboard and mouse (else a pad): which bindings the HUD names
    float aimRange;          // m from the muzzle to the aim point
    bool aimHit;             // the centre's ray meets something within the reticle's reach (else the aim is its far end)
    bool centred;            // the camera is the Sazabi's own (sazabi.cpp): the aim point is the screen's centre
    bool hasAssist;
    float assist[3];         // the aim assist's enemy (its lock point, world): the aim point is it (sazabi_assist.h)
    bool lockOn;             // ...held by the lock-on (its key pressed: the camera follows it)
};
bool PlayerSazabiCue(SazabiCue* out) noexcept;
// The player's Sazabi's riding camera (turretcam.cpp's look-at hook): seat `seat`'s eye and look point, false when the
// seat is no player's Sazabi's (the stock camera then).
bool SazabiCamera(const unsigned char* seat,const float* wasEye,const float* wasLook,float* eye,float* look) noexcept;
void SazabiFrame(unsigned char* vehicle) noexcept;   // crew.cpp InputHook, after the stock input
bool InstallSazabi() noexcept;                        // after InstallBody506
void ResetSazabi() noexcept;                          // a new mission
}  // namespace crew
