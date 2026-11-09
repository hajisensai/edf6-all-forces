// The Katyusha's firing floor over its cab (test site #36, 2026-11-02: 「让喀秋莎的向前射角限制一下，他不应该穿过车头设计」):
// its rails sit on the bed, lower than the cab ahead of them, and its elevation stops (0..80 deg) are the same all the way
// round the turntable, so pointed over the cab low its rockets flew out through it. katyusha_cab_gen.h
// (tools/gen_katyusha_cab.py, measured on the model: pylib/katyusha_model.py cab_floor_scan) holds, per turntable
// azimuth, the lowest elevation from which every rocket leaves clear of the truck; both plugins keep the launcher at or
// over it: EDF6VehicleCrew poses the launcher's bone no lower (src/katyusha.cpp Launcher: the player's, an NPC crew's
// and a remote player's Katyusha alike, so the rockets, which leave along that bone, never go through the cab) and
// aims the player's loft over it (src/launcher.cpp LoftWant); EDF6AutoTurret takes no arc under it (autoturret
// Ballistic). Pure: no game memory (tools/katyusha_cab_check.cpp runs it offline).
#pragma once
#include "edf/katyusha_cab_gen.h"
#include <cmath>

namespace edf::katyusha {
// The floor (rad up) at turntable azimuth `azimuth` (rad, in the vehicle's frame: atan2(across, ahead), 0 over the cab,
// positive towards the model's +x), linear between the table's two entries either side (each is at or over every
// measured clearance within a step of it either way, pylib/katyusha_model.py cab_floor_table, so the line between them
// is too). An azimuth that is no number (an unreadable pose) gets the table's highest.
inline float CabFloor(float azimuth) noexcept {
    constexpr float kRad=3.14159265f/180.0f;
    if(!std::isfinite(azimuth)) {
        float top=0.0f;
        for(const float f:kCabFloorDeg)top=f>top ? f : top;
        return top*kRad;
    }
    float deg=std::fmod(azimuth/kRad,360.0f);
    if(deg<0.0f)deg+=360.0f;
    const float s=deg/kCabStepDeg;
    int i=static_cast<int>(s);
    float f=s-static_cast<float>(i);
    if(i>=kCabSteps){i=0;f=0.0f;}   // 360 - a rounding hair
    const float lo=kCabFloorDeg[i],hi=kCabFloorDeg[(i+1)%kCabSteps];
    return (lo+(hi-lo)*f)*kRad;
}

// One frame of the launcher's elevation (rad; src/katyusha.cpp Launcher): `elev` eased toward `target` by at most
// `step`, then lifted onto `cab` (CabFloor at the turntable's azimuth) at once when under it: the turntable turning over
// the cab raises the launcher with it, no frame below the floor.
inline float StepElevation(float elev,float target,float step,float cab) noexcept {
    elev+=target-elev>step ? step : target-elev<-step ? -step : target-elev;
    return elev<cab ? cab : elev;
}
}  // namespace edf::katyusha
