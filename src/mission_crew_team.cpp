#include "mission_crew_team.h"
#include "crew.h"
#include "memory.h"

namespace crew {
namespace {
constexpr unsigned kRecursiveTeam=0x1C36D0;
constexpr unsigned kTeamCalls[]={0x1C3ACB,0x1C3706};
constexpr unsigned char kRecursiveTeamSig[]={0x48,0x89,0x5C,0x24,8,0x48,0x89,0x6C,0x24,0x10,
    0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xB9,0x50,5,0,0};
constexpr unsigned char kRootContext[]={0x8B,0xD3,0xE8,0,0xFC,0xFF,0xFF,0x90,0x48,0x8B,0x4C,0x24,0x28};
constexpr unsigned char kChildContext[]={0x48,0x8B,0x4B,0x10,0x8B,0xD6,0xE8,0xC5,0xFF,0xFF,0xFF,0x48,0x8B,0x1B,0x48,0x3B,0xDF};
// 1C36D0 ends in a tail jump to SetTeam 54EE70, whose bool return is AL.
using RecursiveTeamFn=bool(__fastcall*)(void*,int);
RecursiveTeamFn nativeTeam=nullptr;
MissionCrewTeamFn teamCallback=nullptr;
bool teamHooksReady=false;

bool __fastcall MissionTeamHook(void* object,int team) noexcept {
    const bool accepted=nativeTeam(object,team);
    if(accepted && teamHooksReady && teamCallback)teamCallback(object,team);
    return accepted;
}
}

bool InstallMissionCrewTeam(MissionCrewTeamFn onTeam) noexcept {
    if(teamHooksReady)return teamCallback==onTeam;
    if(!onTeam)return false;
    bool matches=false;
    __try {
        matches=Matches(kRecursiveTeam,kRecursiveTeamSig,sizeof(kRecursiveTeamSig)) &&
            Matches(kTeamCalls[0]-2,kRootContext,sizeof(kRootContext)) &&
            Matches(kTeamCalls[1]-6,kChildContext,sizeof(kChildContext));
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    if(!matches){Log("Mission crew team off: script call profile mismatch");return false;}
    nativeTeam=reinterpret_cast<RecursiveTeamFn>(image+kRecursiveTeam);
    teamCallback=onTeam;
    unsigned char original[2][5]{};
    bool changed[2]{};
    for(unsigned i=0;i<2;++i)std::memcpy(original[i],image+kTeamCalls[i],5);
    for(unsigned i=0;i<2;++i) {
        if(!RedirectCall(image+kTeamCalls[i],image+kRecursiveTeam,
                         reinterpret_cast<void*>(&MissionTeamHook),changed[i]) || !changed[i]) {
            // A failed protection restore can still leave a redirected call;
            // roll back both sites, while any remaining hook safely forwards.
            for(unsigned j=0;j<=i;++j) {
                if(!changed[j])continue;
                unsigned char current[5]{};std::memcpy(current,image+kTeamCalls[j],5);
                edf::PatchCode(image+kTeamCalls[j],current,original[j],5);
            }
            teamCallback=nullptr;
            Log("Mission crew team off: script call patch failed");return false;
        }
    }
    teamHooksReady=true;
    Log("Mission crew team: root and recursive native script calls hooked");
    return true;
}
}
