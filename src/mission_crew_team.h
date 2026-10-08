#pragma once
namespace crew {
// Called after a successful native script team assignment. The integration
// callback selects its owned mission vehicles and updates their real soldiers.
using MissionCrewTeamFn=void(*)(void* object,int team) noexcept;
bool InstallMissionCrewTeam(MissionCrewTeamFn onTeam) noexcept;
}
