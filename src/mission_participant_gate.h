#pragma once
namespace crew {
struct ObjRef;
using MissionParticipantAdmission=bool(*)(int missionIndex) noexcept;
using MissionPlayerCreated=void(*)(int missionIndex,const ObjRef& actor) noexcept;
// Guards the CreatePlayer path whose direct caller assumes a non-null Soldier. The callback
// resolves the pre-creation User and checks the frozen world cohort; it must allow pre-seal setup.
bool InstallMissionParticipantGate(MissionParticipantAdmission allowed,MissionPlayerCreated created=nullptr) noexcept;
bool MissionParticipantGateReady() noexcept;
}
