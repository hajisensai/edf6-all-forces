// dreadnought.cpp: the Dreadnought (its decks, hangars, gunners and camera).
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include "playerjet.h"

namespace crew {
// Whether the local player leaving `fighter` is put aboard its dreadnought (dreadnought_hangar.inc: its own bay's hangar,
// running here, has it at that bay's mouth putting its pilot out onto the bridge): no parachute, no catch
// (playerjet.cpp Leave / Left), the fighter handed back. Anywhere else, false: they eject.
bool DreadnoughtTakesBack(const unsigned char* fighter) noexcept;
// A board press of `human` on foot that visits Dreadnought `vehicle` (crew.cpp FindSeatHook): true when the local player
// stands over one of its fitted hangars and the press is taken for it (a fighter of that hangar out to its bay's mouth
// for them: dreadnought_hangar.inc HangarBoard), no seat of the ship's.
bool DreadnoughtHangarBoard(unsigned char* vehicle,unsigned char* human) noexcept;
bool DreadnoughtBodyStep(unsigned char* v,float* lin,float* ang) noexcept;   // dreadnought.cpp
// dreadnought.cpp: the Dreadnought (docs/dreadnought-plan.md), a 506 body with its mark (7601-7699): never crewed, never
// flown as a heli; with the player in seat 0 the plugin flies it, empty after that it settles on the ground.
bool IsDreadnought(const void* vehicle) noexcept;
// `human` at a Dreadnought's bridge on the keyboard, and `vk` one of its strafe keys (dreadnought.cpp): the bridge holds the
// key, the Q mark yields to it (npcai.cpp; plan-2026-10-25-keybinds.md §3.3 B).
bool DreadnoughtBridgeClaims(const unsigned char* human,int vk) noexcept;
void DreadnoughtFrame(unsigned char* vehicle) noexcept;   // crew.cpp InputHook, after the stock input
void DreadnoughtInput(unsigned char* vehicle) noexcept;   // crew.cpp InputHook, before it: the local rider's aim stick
bool InstallDreadnought() noexcept;                   // after InstallBody506
void ResetDreadnought() noexcept;                     // a new mission
// The Dreadnought aboard (dreadnought_turrets.inc): the local player's camera on its bridge or at a turret (turretcam.cpp
// LookHook), their sight (hud.cpp GunnerMarks), and a round of a turret (jet_bay.cpp: the gunship's cannon round, from
// `from` at `at`; false: not fired).
bool DreadnoughtCamera(const unsigned char* seat,float* eye,float* look) noexcept;
bool DreadnoughtGunnerHud(GunnerReadout* out) noexcept;
// The ship's readout for the local player in any of its seats (hud.cpp DreadnoughtHud): `seat` 0 its bridge, 1 + n turret
// n; its way (m/s), climb (m/s), throttle (-0.35 .. 1), boost left (0 .. 1), keel over the ground (m, -1 none seen),
// heading (deg, 0 north, clockwise), hull; its turrets by bit (trained by this player, by other players, by the commander
// for the guns nobody mans, on their aim now); its hangar (fighters stowed, out, on the lifts' way up or down; s till the
// next flight may go); keyboard or pad; first person.
struct DreadnoughtCue {
    int seat,turrets;
    float speed,climb,throttle,boost,keel,heading,hp,hpMax;
    bool boosting;
    std::uint8_t mine,others,commanded,onAim;
    int stowed,out,lifting;
    float flightWait;
    bool keys,firstPerson;
};
bool PlayerDreadnoughtCue(DreadnoughtCue* out) noexcept;
}  // namespace crew
