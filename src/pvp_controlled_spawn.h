#pragma once
#include "pvp_controlled.h"

namespace crew {
using ControlledPvpSpawnResolver=bool(*)(const wchar_t* configuredSgo,controlled_pvp::SpawnProof* out) noexcept;
// Startup only. Exact static resource/class proof providers may register; unknown or ambiguous resources refuse.
bool RegisterControlledPvpSpawnResolver(ControlledPvpSpawnResolver) noexcept;
bool InstallControlledPvpSpawnBridge() noexcept;
}
