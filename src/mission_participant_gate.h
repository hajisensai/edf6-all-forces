#pragma once
namespace crew {
using MissionParticipantAdmission=bool(*)(int missionIndex) noexcept;
// Guards the CreatePlayer path whose direct caller assumes a non-null Soldier. The callback
// resolves the pre-creation User and checks the frozen world cohort; it must allow pre-seal setup.
bool InstallMissionParticipantGate(MissionParticipantAdmission allowed) noexcept;
bool MissionParticipantGateReady() noexcept;
}
