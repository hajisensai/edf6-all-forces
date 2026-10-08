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
// Only after a unified real-crew deployment has committed; does not touch real humans or vehicle scripts.
void ReleaseLegacyMissionRiders(unsigned char* vehicle) noexcept;
void ResetMissionCrew() noexcept;
}
