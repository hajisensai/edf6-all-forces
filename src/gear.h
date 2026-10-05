// Landing gear of the jet models (gear.cpp; the models' bones: pylib/jet_gear.py). Game thread only, except
// GearHudLatest (any thread).
#pragma once
#include "crew.h"

namespace crew {
constexpr int kGearLegs=3;   // the nose, the left and the right main (pylib/jet_gear.py LEGS)
struct GearState {
    bool fitted;             // the model has the three gear bones (else every other field is meaningless)
    bool wantUp;             // the last command: up (true) or down
    float at[kGearLegs];     // each leg: 0 down and locked .. 1 up and locked
    bool down,up;            // every leg down and locked / up and locked
};
// The gear of `v` turned toward its command (`wantUp`) by `dt` s, and posed (each leg's bone, src: GearPose).
// `snap`: put there at once (a jet first seen). Not fitted: nothing done.
GearState GearStep(unsigned char* v,bool wantUp,float dt,bool snap) noexcept;
// The gear's state as last stepped (not fitted: never stepped, or no gear in its model).
GearState GearOf(const void* v) noexcept;
// An NPC jet's gear (jet_flight.cpp Wing): up in flight, down near the ground and slow (landing / taxiing).
void NpcGear(unsigned char* v,float speed,float dt) noexcept;
void ResetGear() noexcept;   // a new mission (mission.cpp MissionStart)

// The player's jet: the gear key / pad button toggles it (not up with weight on the wheels), what it does to the flight
// and the cockpit's lights (playerjet.cpp, hud.cpp). `air`: in flight (not rolling, parked); `held`: the key / button
// down now (a press toggles); `speed`, `clear`, `climb`: m/s, m over the floor, m/s.
struct GearHud {
    bool shown;              // a player jet with gear, flown now
    float at[kGearLegs];
    bool wantUp;
    bool overspeed;          // not up and faster than kGearLimit
    bool warn;               // up (or not down) low and slow: the gear warning
    bool blocked;            // an up command refused (weight on the wheels) within kGearBlockedMs
    bool keys;               // flown on the keyboard and mouse (the hint names the key, else the pad button)
    ULONGLONG tick;          // GetTickCount64 when published
};
GearState PlayerGear(unsigned char* v,bool held,bool keys,bool air,float speed,float clear,float climb,float dt) noexcept;
float GearDragShare(const void* v) noexcept;   // parasitic drag added, a share of the clean jet's (0 up)
bool GearDown(const void* v) noexcept;         // down and locked, or no gear in the model: a landing on wheels
void GearHudClear() noexcept;                  // no player jet flown this frame
bool GearHudLatest(GearHud* out) noexcept;
constexpr float kGearLimit=128.6f;             // m/s: 250 kt, a jet's gear-down speed limit
constexpr ULONGLONG kGearBlockedMs=1500;
}  // namespace crew
