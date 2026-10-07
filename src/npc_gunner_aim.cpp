#include "npc_gunner_aim.h"
#include "crew.h"
#include "memory.h"

namespace crew {
namespace {
constexpr unsigned kHeliAimCall=0x6525E5,kSeatAimMode=0x62E6C0,k410Vtable=0x17DF338;
// mov edx,1; mov rcx,rsi; call SeatAimMode; mov rcx,[rbp+670h]; xor rcx,rsp.
// The caller ignores the void return and overwrites RCX before its stack-cookie check. Only the call is patched.
constexpr unsigned char kCallContext[]={0xBA,1,0,0,0,0x48,0x8B,0xCE,0xE8,0xD6,0xC0,0xFD,0xFF,
    0x48,0x8B,0x8D,0x70,0x06,0,0,0x48,0x33,0xCC};
constexpr unsigned char kAimSignature[]={0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x6C,0x24,0x20,0x41,0x56,0x48,0x83,0xEC,0x30};
using AimModeFn=void(__fastcall*)(void*,int);
AimModeFn nextAim=nullptr;
bool aimReady=false;

void __fastcall NpcGunnerAimHook(void* vehicle,int mode) noexcept {
    // mode 1 excludes an empty remote seat before asking its authority, then 5FB880 destroys its received aim.
    // mode 0 is the stock CarBase policy: a local rider still aims locally, remote and empty seats consume packets.
    if(aimReady && mode==1 && Cfg().enabled && Cfg().npcGunners && InSession() && Readable(vehicle,sizeof(void*)) &&
       At<const unsigned char*>(vehicle,0)==image+k410Vtable)mode=0;
    nextAim(vehicle,mode);
}
}

bool InstallNpcGunnerAim() noexcept {
    if(aimReady)return true;
    bool matches=false;
    __try {
        matches=edf::Matches(image,kHeliAimCall-8,kCallContext,sizeof(kCallContext)) &&
            edf::Matches(image,kSeatAimMode,kAimSignature,sizeof(kAimSignature));
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    if(!matches) {
        Log("NPC gunner network aim off: 410 stock call/profile mismatch");return false;
    }
    nextAim=reinterpret_cast<AimModeFn>(image+kSeatAimMode);
    bool changed=false;
    const bool installed=RedirectCall(image+kHeliAimCall,image+kSeatAimMode,reinterpret_cast<void*>(&NpcGunnerAimHook),changed);
    // RedirectCall may have written the call even if restoring page protection failed; keep its callable target valid.
    aimReady=installed && changed;
    Log("NPC gunner network aim=%d (410 empty seats consume stock aim packets)",aimReady);
    return aimReady;
}

bool NpcGunnerAimReady() noexcept { return aimReady; }
}
