#pragma once
#include <cstddef>
namespace crew {
using MissionCrewFactoryFn=bool(*)(unsigned char* vehicle,bool restored) noexcept;
using MissionCrewTickFn=void(*)(unsigned char* vehicle) noexcept;
using MissionVehicleIdFn=bool(*)(const unsigned char* vehicle,unsigned char* id32) noexcept;
// The unified support deployment owns creation/networking; this module owns stock RideAi interception.
void ConfigureMissionCrew(MissionCrewFactoryFn factory,MissionVehicleIdFn readId,MissionCrewTickFn tick=nullptr) noexcept;
void RetryMissionCrew(unsigned char* vehicle) noexcept;
bool InstallMissionCrewHooks(const unsigned* vtables,std::size_t count) noexcept;
void MissionCrewVehicleFrame(unsigned char* vehicle) noexcept;
unsigned char* MissionVehicleById(const unsigned char* canonicalId32) noexcept;
// A vehicle the mission made an AI unit of: the stock RideAi was called on it (a script's CreateFriend, a transporter
// formation's AI cargo 0x5EA932, a snapshot), whoever drives it now (its real crew, mission_crew_support.cpp), or a
// Dummy (the stock RideAi's rider) still holds its wheel. Its AI is the script's: the plugin's post keeping leaves
// such a Barga alone (npcpost.cpp; docs/feedback-2026-10-17-balam-npc.md).
bool MissionAiVehicle(const unsigned char* vehicle) noexcept;
// Only after a unified real-crew deployment has committed; does not touch real humans or vehicle scripts.
void ReleaseLegacyMissionRiders(unsigned char* vehicle) noexcept;
void ResetMissionCrew() noexcept;
}
