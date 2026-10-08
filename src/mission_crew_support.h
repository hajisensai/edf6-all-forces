#pragma once
#include "support_net.h"
namespace crew {
// Called by support_dispatch after ConfigureSupportNet; only this adapter uses mission catalog1023.
void InstallMissionCrewSupport() noexcept;
bool ValidateMissionCrewPlan(const SupportPlan& plan) noexcept;
bool ApplyMissionCrewPlan(std::uint64_t transaction,const SupportPlan& plan,bool remote) noexcept;
void DestroyMissionCrewPlan(std::uint64_t transaction) noexcept;
void ResetMissionCrewSupport() noexcept;
}
