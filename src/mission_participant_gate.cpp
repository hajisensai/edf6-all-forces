#include "mission_participant_gate.h"
#include "crew.h"
#include "memory.h"

namespace crew {
namespace mission_admission {
constexpr unsigned kUpperCall=0x22B626,kUpperCreate=0x22AB90;
constexpr unsigned char kCallerBytes[]={0xE8,0x65,0xF5,0xFF,0xFF,0x4C,0x8B,0xC0,0x0F,0x57,0xC0,
    0x66,0x0F,0x7F,0x44,0x24,0x60,0x48,0x8B,0x50,0x08};
constexpr unsigned char kUpperBytes[]={0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8D,0xAC};
// Win64: four register arguments, then six stack arguments. Native reads low dwords of 5/6/7/9,
// argument 8 is a bool and 10 a pointer. Preserve the complete established call ABI on admission.
using UpperFn=ObjRef*(__fastcall*)(void*,ObjRef*,const float*,int,int,int,int,bool,int,void*);
UpperFn original=nullptr;
MissionParticipantAdmission admission=nullptr;
bool ready=false;
ObjRef* __fastcall UpperHook(void* context,ObjRef* out,const float* matrix,int index,
                            int a5,int a6,int a7,bool a8,int a9,void* a10) noexcept {
    if(ready && InSession() && admission && !admission(index)) {
        // 22AB90 returns a weak_ptr in its caller-provided output, not a Soldier*. Its caller
        // checks the ctrl at 22B637, then the locked object at 22B694: this is its valid empty path.
        // Returning null from the inner 591130 instead would crash at 22AC3F.
        if(out)*out=ObjRef{};
        return out;
    }
    return original(context,out,matrix,index,a5,a6,a7,a8,a9,a10);
}
}
bool InstallMissionParticipantGate(MissionParticipantAdmission allowed) noexcept {
    using namespace mission_admission;
    if(!allowed)return false;
    if(ready){admission=allowed;return true;}
    __try {
        if(!Matches(kUpperCall,kCallerBytes,sizeof(kCallerBytes)) || !Matches(kUpperCreate,kUpperBytes,sizeof(kUpperBytes)))return false;
        original=reinterpret_cast<UpperFn>(image+kUpperCreate);admission=allowed;
        bool changed=false;
        ready=RedirectCall(image+kUpperCall,image+kUpperCreate,reinterpret_cast<void*>(&UpperHook),changed) && changed;
    } __except(EXCEPTION_EXECUTE_HANDLER){ready=false;}
    return ready;
}
bool MissionParticipantGateReady() noexcept { return mission_admission::ready; }
}
