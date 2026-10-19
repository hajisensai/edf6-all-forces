// Exact shipped EDF4.1 summon resources; separate from original Root.cpk manifests.
#pragma once
#include "pvp_controlled.h"
namespace crew {
bool ControlledPvpPortedSpawnProof(const wchar_t* configuredSgo,controlled_pvp::SpawnProof*) noexcept;
bool InstallControlledPvpPortedSpawnResolver() noexcept;
}
