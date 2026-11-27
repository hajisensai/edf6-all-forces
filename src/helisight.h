// helisight.cpp: the player's helicopter's own sight and its arms.
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include "roundaim.h"
#include "gunsight.h"

namespace crew {
void ResetHeliSight() noexcept;   // helisight.cpp
// helisight.cpp: the weapons' sight of a stock helicopter (heli.cpp IsHelicopter, no plugin body) the player flies or
// mans (ini PlayerHeliGunSight). The gun (the primary trigger's: `gun`): `bore` the way its muzzle points (a
// direction), `pipper` where a round fired now first hits the map along its real arc (hit), else where it is at the
// end of its life; `range` m from the muzzle to it. The other weapon (the secondary button's: `arm`, HeliArm):
// `armBore` its muzzle's way; a missile's lock (`lock` 2 locked / 1 locking, `lockProgress` 0..1, `armAt` the target's
// lock point, as the jets' stores read it: stores.h StoreLock) or with none its LockonRange (`lockRange`); the rockets'
// (or a dropped weapon's) mark `armAt` where their path as the game flies them meets the map (`armHit`; vhud.h
// RoundLands), `armRange` m to it, `armLabel` what it is (RKT, GREN...). HeliSightFrame from every vehicle's input
// (game thread); PlayerHeliSight the last one, false with none this moment (hud.cpp HudPublish).
enum class HeliArm : std::uint8_t { none, missile, rockets };
struct HeliSightReadout {
    bool gun,hit;
    bool physicalOnly=true;
    int paths=0;
    roundaim::Impact path[roundaim::kSightPaths]{};
    float bore[3],pipper[3],range;
    HeliArm arm;
    bool armHit;
    int lock;
    float armBore[3],armAt[3],armRange,lockProgress,lockRange;
    const char* armLabel;
    gunsight::Ladder ladder;     // the gun's range ladder (gunsight.h; no ticks: none), under its boresight
};
bool PlayerHeliOwnSight(const void* vehicle) noexcept;
void HeliSightFrame(unsigned char* vehicle) noexcept;
bool PlayerHeliSight(HeliSightReadout* out) noexcept;
}  // namespace crew
