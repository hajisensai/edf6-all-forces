// The scoped weapons' spread, zoomed or not, for the player's own rounds (docs/scope-accuracy-re.md; the rule:
// scope_spread_logic.h). NPCs and other machines' players keep the stock spread.
#pragma once
#include "scope_spread_logic.h"

namespace crew {
// At plugin load: the player weapon creation's call (0x5A4484, the loadout loop of the player creation 0x5A3F90) checked
// and redirected so each player weapon is noted with its weapon table row's category, and the shot entry hook (heli.cpp
// InstallShotEntry) installed. A site not as read: one SCOPESPREAD REFUSED line, the feature off (stock spread).
bool InstallScopeSpread() noexcept;

// One round's change (heli.cpp MedicShotHook, around the stock shot entry 0x696FD0): ScopeSpreadShot writes the
// weapon's +0xE14 / +0x378 for this round when it is a local player's round of a scoped weapon noted at creation;
// ScopeSpreadRestore puts back exactly what it wrote. `weapon` null: nothing written.
struct ScopeShot { unsigned char* weapon=nullptr; float cone=0.0f,accuracy=0.0f; };
void ScopeSpreadShot(unsigned char* weapon,bool replay,ScopeShot* out) noexcept;
void ScopeSpreadRestore(const ScopeShot& shot) noexcept;
}  // namespace crew
