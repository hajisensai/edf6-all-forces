// Every lock-on weapon's lock, the one lock code (src/lockon.cpp, docs/stores-re.md §7, docs/lockon-re.md): the game's
// own lock (its cone, range, team, LockonTime, hold and time-out, max locks, its duplicate rule) for every weapon, the
// jets' stores and the stock weapons alike, with the order it locks in the plugin's: a weapon the player holds (in
// hand, or in any seat of a vehicle) locks the target nearest the screen's centre first (the jets' since 2026-10-06,
// every stock weapon's since the user's "the stock lock like the planes'"), a multi-lock weapon distinct targets
// before repeats, and a target the player cycled away from last. The stock HUD draws a stock weapon's locks; the
// stores' are drawn by the plugin (stores.cpp).
#pragma once

namespace crew {
// Weapon `w`'s lock: 2 locked (`point` its first lock's target's lock point), 1 locking (`progress` 0..1 of its
// LockonTime, `point` the target), 0 none.
int WeaponLock(const unsigned char* w,float* point,float* progress) noexcept;
// Drops weapon `w`'s locks now (it relocks on its next tick).
void ClearWeaponLock(unsigned char* w) noexcept;
// The target cycle (the jets' target key): the lock in progress dropped, or with none in progress every lock dropped;
// either way the targets dropped come last in the order for a while, so the next one in the cone is locked.
// Partial cancellation sends the game's type-6 notification before dropping the local entry; completed locks stay.
void NextLockTarget(unsigned char* w) noexcept;
// Whether the player on this machine holds weapon `w`: a soldier's weapon in their hand, or a vehicle weapon in the
// holders of a seat they ride.
bool PlayerHolds(const unsigned char* w) noexcept;
// The lock search's order (the per-candidate keeper 0x691310): at load.
bool InstallLockon() noexcept;
}  // namespace crew
