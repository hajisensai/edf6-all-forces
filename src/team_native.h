#pragma once
#include "team_native_logic.h"

namespace crew {
// Plugin initialization only, before the application's init builds the TeamManager: the constructor and the every-team
// walk take team_native::kTeamCapacity rows. All sites or none; refused (stock 7 rows) when the manager already exists.
bool InstallTeamCapacity() noexcept;
// The manager's live row count (kTeamCapacity once installed and built, 7 stock), 0 with no manager.
int NativeTeamRows() noexcept;
// Whether `team` is a row of the live manager.
bool NativeTeamValid(int team) noexcept;
// Game thread. Writes the overlay's relations now and again after every stock reset until cleared. False when the
// capacity is not installed, the overlay is invalid or the manager is unreadable (nothing written then).
bool SetTeamOverlay(const team_native::Overlay& overlay) noexcept;
// Game thread. Writes the stock relations back (faction rows neutral) and stops re-applying.
void ClearTeamOverlay() noexcept;
bool TeamOverlayActive() noexcept;
// The live relation a -> b (0 neutral, 1 friend, 2 enemy), -1 when either is not a row.
int NativeTeamRelation(int a,int b) noexcept;
}  // namespace crew
