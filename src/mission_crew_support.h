#pragma once
#include "support_net.h"
namespace crew {
// Called by support_dispatch after ConfigureSupportNet; only this adapter uses mission catalog1023.
void InstallMissionCrewSupport() noexcept;
bool ValidateMissionCrewPlan(const SupportPlan& plan) noexcept;
bool ApplyMissionCrewPlan(std::uint64_t transaction,const SupportPlan& plan,bool remote) noexcept;
void DestroyMissionCrewPlan(std::uint64_t transaction) noexcept;
void ResetMissionCrewSupport() noexcept;
// Once a frame on every machine (support_dispatch.cpp SupportDispatchTick): each committed deployment's soldiers keep
// quiet (no talk of their own) while they sit in its vehicle, and get their talk back once off it (mission_crew_quiet.h).
void MissionCrewQuietTick() noexcept;
// Whether `human` is one of the mission crew deployed into `vehicle` on this machine (a host's or a peer's copy):
// its boarding there is silent (npcai.cpp SetNpcMissionCrew).
bool MissionCrewRiding(const unsigned char* vehicle,const unsigned char* human) noexcept;
}
