// The AS wrappers run independently of slot7 AI. Guard the exact command sites,
// keep uncontrolled/offline vanilla, and send controlled host changes through the
// same accepted child outcome stream used by the player's commands.
#include "enemy_control_timeship_pods_script.h"
#include "enemy_control_timeship_pods_net.h"
#include "enemy_control_action_native.h"
#include "enemy_control.h"
#include "crew.h"
#include "memory.h"
#include <cstring>

namespace crew::enemy_native {
#ifdef ENEMY_CONTROL_TIMESHIP_PODS_TEST
void* TimeShipPodsTestFunction(unsigned) noexcept;
#endif
namespace timeship_pods_script {
using CreateFn=void(__fastcall*)(void*,unsigned);
using DropFn=void(__fastcall*)(void*,const float*,float,float,int);
using DropOneFn=void(__fastcall*)(void*,unsigned,int,const float*);
using CrueFn=void*(__fastcall*)(void*,unsigned,const void*,unsigned,float,bool);
template<class T>T Fn(unsigned rva) noexcept {
#ifdef ENEMY_CONTROL_TIMESHIP_PODS_TEST
    return reinterpret_cast<T>(TimeShipPodsTestFunction(rva));
#else
    return reinterpret_cast<T>(image+rva);
#endif
}
bool ControlledOnline(const void* object) noexcept {
    bool authority=false;if(NativeActionRole(object,&authority))return true;
        bool online=false;
    if(EnemyControlNetworkContext(object,&online))return online;
    // A core-owned target with missing immutable context is unknown, not offline.
    // Autonomous mission objects outside this control core retain vanilla AS.
    return EnemyControlTargetActive(object);
}
void __fastcall Create(void* object,unsigned count) {
    if(!ControlledOnline(object)){Fn<CreateFn>(0x3EC680)(object,count);return;}
    NativeLease lease{};if(!TimeShipPodScriptLease(object,&lease))return;
    const auto sequence=TimeShipPodScriptSequence(lease);
    if(sequence)ExecuteTimeShipPodsNetworkCreate(lease,count,sequence);
}
void __fastcall Drop(void* object,const float* point,float inner,float outer,int seed) {
    if(!ControlledOnline(object)){Fn<DropFn>(0x3ECC40)(object,point,inner,outer,seed);return;}
    NativeLease lease{};if(!TimeShipPodScriptLease(object,&lease))return;
    const auto sequence=TimeShipPodScriptSequence(lease);
    if(sequence)ExecuteTimeShipPodsNetworkDrop(lease,point,inner,outer,seed,sequence);
}
void __fastcall DropOne(void* object,unsigned index,int delay,const float* point) {
    if(!ControlledOnline(object)){Fn<DropOneFn>(0x3ECC10)(object,index,delay,point);return;}
    NativeLease lease{};if(!TimeShipPodScriptLease(object,&lease))return;
    const auto sequence=TimeShipPodScriptSequence(lease);
    if(sequence)ExecuteTimeShipPodsNetworkDropOne(lease,index,delay,point,sequence);
}
void __fastcall Hatch(void* object,bool open) noexcept {
    __try {
        if(!ControlledOnline(object)) {
            // This is the exact store the original AS wrapper performs.
            auto* controller=At<unsigned char*>(object,0x1110);controller[0x123]=open ? 1u : 0u;ObserveTimeShipVanillaHatch(object,open);return;
        }
        NativeLease lease{};if(TimeShipPodScriptLease(object,&lease))AcceptTimeShipScriptHatch(lease,open);
    } __except(EXCEPTION_EXECUTE_HANDLER){}
}
const void* CrueRoot(const void* wrapper) noexcept {
    if(!Readable(wrapper,0x18))return nullptr;
    const auto* actor=At<const unsigned char*>(wrapper,8);const auto* ref=At<const unsigned char*>(wrapper,16);
    if(!Readable(ref,16) || At<LONG>(ref,8)<=0 || !Readable(actor,0x40) || At<const void*>(actor,0x30)!=ref)return nullptr;
    if(At<const void*>(actor,0)==image+0x17BA800) {
        const auto* owner=At<const unsigned char*>(actor,0x38);
        if(Readable(owner,0x38))return owner;
    }
    return actor;
}
CrueFn originalCrue=nullptr;
void* __fastcall Crue(void* wrapper,unsigned index,const void* type,unsigned count,float level,bool active) {
    const auto* root=CrueRoot(wrapper);
    // Passenger creation must not bypass the stable-ID child transaction. The
    // exact recipe bridge owns enabling this controlled-online operation.
    if(root && ControlledOnline(root))return wrapper;
    return originalCrue ? originalCrue(wrapper,index,type,count,level,active) : wrapper;
}
struct Site {unsigned rva,target;void* replacement;unsigned char original[5]{},patched[5]{};void* thunk=nullptr;bool changed=false;};
Site sites[]={
    {0x1CB0CB,0x3EC680,reinterpret_cast<void*>(&Create)},
    {0x1CB20F,0x3ECC40,reinterpret_cast<void*>(&Drop)},
    {0x1CB48B,0x3ECC10,reinterpret_cast<void*>(&DropOne)}
};
constexpr unsigned kHatch=0x1CB594,kCrue=0x1CABB0;
constexpr unsigned char kHatchOriginal[]={0x48,0x8B,0x80,0x10,0x11,0x00,0x00,0x88,0x98,0x23,0x01,0x00,0x00};
constexpr unsigned char kCrueOriginal[]={0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57};
unsigned char* installedImage=nullptr;void* hatchThunk=nullptr;void* crueTrampoline=nullptr;
unsigned char hatchPatched[sizeof(kHatchOriginal)]{},cruePatched[sizeof(kCrueOriginal)]{};
bool hatchChanged=false,crueChanged=false;
void Absolute(unsigned char* out,const void* target) noexcept {
    out[0]=0x48;out[1]=0xB8;const auto address=reinterpret_cast<std::uintptr_t>(target);std::memcpy(out+2,&address,8);out[10]=0xFF;out[11]=0xE0;
}
bool Restore() noexcept {
    if(!installedImage)return true;bool ok=true;
    if(hatchChanged){if(edf::PatchCode(installedImage+kHatch,hatchPatched,kHatchOriginal,sizeof(kHatchOriginal)))hatchChanged=false;else ok=false;}
    if(crueChanged){if(edf::PatchCode(installedImage+kCrue,cruePatched,kCrueOriginal,sizeof(kCrueOriginal)))crueChanged=false;else ok=false;}
    for(auto& site:sites){
        if(site.changed){if(edf::PatchCode(installedImage+site.rva,site.patched,site.original,5))site.changed=false;else ok=false;}
        if(!site.changed && site.thunk){VirtualFree(site.thunk,0,MEM_RELEASE);site.thunk=nullptr;}
    }
    if(!hatchChanged && hatchThunk){VirtualFree(hatchThunk,0,MEM_RELEASE);hatchThunk=nullptr;}
    if(!crueChanged && crueTrampoline){VirtualFree(crueTrampoline,0,MEM_RELEASE);crueTrampoline=nullptr;originalCrue=nullptr;}
    if(ok)installedImage=nullptr;return ok;
}
bool Complete() noexcept {
    if(!installedImage || installedImage!=image || !hatchChanged || !crueChanged ||
       !Readable(image+kHatch,sizeof(kHatchOriginal)) || std::memcmp(image+kHatch,hatchPatched,sizeof(kHatchOriginal)) ||
       !Readable(image+kCrue,sizeof(kCrueOriginal)) || std::memcmp(image+kCrue,cruePatched,sizeof(kCrueOriginal)))return false;
    for(const auto& s:sites)if(!s.changed || !Readable(image+s.rva,5) || std::memcmp(image+s.rva,s.patched,5))return false;
    return true;
}
}
bool InstallTimeShipPodScriptGates() noexcept {
    using namespace timeship_pods_script;
    __try {
        if(!image)return false;if(Complete())return true;
        if(installedImage && (installedImage!=image || !Restore()))return false;
        if(!Readable(image+kHatch,sizeof(kHatchOriginal)) || std::memcmp(image+kHatch,kHatchOriginal,sizeof(kHatchOriginal)) ||
           !Readable(image+kCrue,sizeof(kCrueOriginal)) || std::memcmp(image+kCrue,kCrueOriginal,sizeof(kCrueOriginal)))return false;
        for(auto& s:sites){
            const auto* code=image+s.rva;if(!Readable(code,5) || code[0]!=0xE8)return false;
            std::int32_t relative=0;std::memcpy(&relative,code+1,4);if(code+5+relative!=image+s.target)return false;
            std::memcpy(s.original,code,5);std::memcpy(s.patched,code,5);
        }
        installedImage=image;
        for(auto& s:sites){
            s.thunk=AllocateNearThunk(image+s.rva,s.replacement);if(!s.thunk){Restore();return false;}
            const auto delta=static_cast<unsigned char*>(s.thunk)-(image+s.rva+5);
            if(delta>0x7FFFFFF0 || delta<-0x7FFFFFF0){Restore();return false;}
            const auto relative=static_cast<std::int32_t>(delta);std::memcpy(s.patched+1,&relative,4);
        }
        // The wrapper already reserved 0x30 bytes and aligned RSP. Only RAX,
        // RCX/RDX and volatile registers are dead after this original store.
        unsigned char hatchCode[30]={0x48,0x8B,0xC8,0x0F,0xB6,0xD3,0x48,0xB8};
        const auto hook=reinterpret_cast<std::uintptr_t>(&Hatch);std::memcpy(hatchCode+8,&hook,8);
        hatchCode[16]=0xFF;hatchCode[17]=0xD0;Absolute(hatchCode+18,image+kHatch+sizeof(kHatchOriginal));
        hatchThunk=AllocateNearCode(image+kHatch,hatchCode,sizeof(hatchCode));if(!hatchThunk){Restore();return false;}
                // RAX carries the native TimeShip at this inline store. The detour must
        // preserve it, so use volatile R11 rather than an RAX absolute jump.
        hatchPatched[0]=0x49;hatchPatched[1]=0xBB;const auto destination=reinterpret_cast<std::uintptr_t>(hatchThunk);
        std::memcpy(hatchPatched+2,&destination,8);hatchPatched[10]=0x41;hatchPatched[11]=0xFF;hatchPatched[12]=0xE3;
        // Relocate the complete 16-byte prologue (no RIP-relative operands), then
        // jump into the native wrapper body. Its native unwind info still owns
        // every original stack/frame operation once the trampoline has finished.
        unsigned char crueCode[sizeof(kCrueOriginal)+12]{};std::memcpy(crueCode,kCrueOriginal,sizeof(kCrueOriginal));
        Absolute(crueCode+sizeof(kCrueOriginal),image+kCrue+sizeof(kCrueOriginal));
        crueTrampoline=AllocateNearCode(image+kCrue,crueCode,sizeof(crueCode));if(!crueTrampoline){Restore();return false;}
        originalCrue=reinterpret_cast<CrueFn>(crueTrampoline);std::memset(cruePatched,0x90,sizeof(cruePatched));Absolute(cruePatched,reinterpret_cast<void*>(&Crue));
        for(auto& s:sites){
            const bool patched=edf::PatchCode(image+s.rva,s.original,s.patched,5);s.changed=Readable(image+s.rva,5) && !std::memcmp(image+s.rva,s.patched,5);
            if(!patched || !s.changed){Restore();return false;}
        }
        bool patched=edf::PatchCode(image+kHatch,kHatchOriginal,hatchPatched,sizeof(kHatchOriginal));
        hatchChanged=Readable(image+kHatch,sizeof(kHatchOriginal)) && !std::memcmp(image+kHatch,hatchPatched,sizeof(kHatchOriginal));
        if(!patched || !hatchChanged){Restore();return false;}
        patched=edf::PatchCode(image+kCrue,kCrueOriginal,cruePatched,sizeof(kCrueOriginal));
        crueChanged=Readable(image+kCrue,sizeof(kCrueOriginal)) && !std::memcmp(image+kCrue,cruePatched,sizeof(kCrueOriginal));
        if(!patched || !crueChanged){Restore();return false;}return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){timeship_pods_script::Restore();return false;}
}
bool RestoreTimeShipPodScriptGates() noexcept {
    __try {return timeship_pods_script::Restore();} __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
