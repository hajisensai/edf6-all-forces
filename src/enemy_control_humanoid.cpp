#include "enemy_control_humanoid.h"
#include "enemy_control_types.h"
#include "crew.h"
#include "memory.h"
#include <cstring>
#include <iterator>
#include <climits>

namespace crew::enemy_native {
namespace {
using FireFn=bool(__fastcall*)(void*);
struct WeaponHook {
    const char* name;
    std::uint32_t vtable,fire;
    unsigned char signature[16];
    FireFn wrapper,next=nullptr;
};
bool __fastcall BasicFire(void* self);
bool __fastcall HeavyFire(void* self);
bool __fastcall MartianFire(void* self);
bool __fastcall BarrageFire(void* self);
WeaponHook weaponHooks[]={
    {"HumanoidWeapon_Basic",0x17C4920,0x4A5720,{0x40,0x53,0x48,0x83,0xEC,0x30,0x48,0x8B,0x01,0x48,0x8B,0xD9,0xFF,0x90,0x90,0x00},&BasicFire},
    {"HumanoidWeapon_Heavy",0x17C4B50,0x4A6000,{0x40,0x53,0x48,0x83,0xEC,0x30,0x48,0x8B,0x01,0x48,0x8B,0xD9,0xFF,0x90,0x90,0x00},&HeavyFire},
    {"EDF6_MartianWeapon_Basic",0x17C1490,0x454CC0,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0x01,0x48,0x8B,0xD9,0xFF,0x90,0x90,0x00},&MartianFire},
    {"EDF6_MartianWeapon_Barrage",0x17C11F8,0x454CC0,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0x01,0x48,0x8B,0xD9,0xFF,0x90,0x90,0x00},&BarrageFire}
};
constexpr std::uint32_t kBusy[]={0x4A57F0,0x4A60D0,0x454E00,0x454E00};
struct FireScope { const NativeLease* lease=nullptr;const void* root=nullptr;const void* control=nullptr;std::uint64_t mask=~0ULL; };
thread_local FireScope fireScope{};
bool installed=false;
constexpr std::size_t kWeaponSlot=17,kWeapons=0x1120,kCount=0x1130,kWeaponOwner=0x5B0;
bool Identity(const NativeLease& lease) noexcept {
    return lease.object && lease.profile && lease.profile->kind==Kind::humanoid && lease.control &&
        Readable(lease.object,kCount+8) && Readable(lease.control,12) && At<LONG>(lease.control,8)>0 &&
        At<const void*>(lease.object,0x30)==lease.control && !(lease.object[0x18]&4) && !lease.object[0x2E8];
}
const WeaponHook* HookOf(const void* weapon) noexcept {
    if(!image || !Readable(weapon,8))return nullptr;
    const void* vt=At<const void*>(weapon,0);
    for(const auto& hook:weaponHooks)if(vt==image+hook.vtable)return &hook;
    return nullptr;
}
bool Equipment(const NativeLease& lease,const unsigned char** rows,std::size_t* count) noexcept {
    if(!Identity(lease))return false;
    *rows=At<const unsigned char*>(lease.object,kWeapons);*count=At<std::size_t>(lease.object,kCount);
    return *count<=64 && (!*count || Readable(*rows,*count*16));
}
const void* WeaponAt(const NativeLease& lease,const unsigned char* rows,std::size_t index) noexcept {
    const auto row=rows+index*16;
    const void* weapon=At<const void*>(row,0),*control=At<const void*>(row,8);
    if(!weapon || !control || !Readable(control,24) || At<LONG>(control,8)<=0 ||
        At<const void*>(control,16)!=weapon || !Readable(weapon,kWeaponOwner+8) ||
        At<const void*>(weapon,0x30)!=control || At<const void*>(weapon,kWeaponOwner)!=lease.object)return nullptr;
    return HookOf(weapon) ? weapon : nullptr;
}
bool Allowed(const void* weapon) noexcept {
    const auto* lease=fireScope.lease;
    if(!lease)return true;
    __try {
        // Another human's or an autonomous enemy's weapon is never filtered by
        // the selected player's native input call.
        if(!Readable(weapon,kWeaponOwner+8))return false;
        const void* owner=At<const void*>(weapon,kWeaponOwner);
        if(!owner || !Readable(owner,0x38))return false;
        if(owner!=fireScope.root)return true;
        // A native callback may end this controller reentrantly. Do not compare
        // against the cleared/reused descriptor and misclassify our own old
        // body's remaining weapons as somebody else's unfiltered weapons.
        if(!fireScope.root || !fireScope.control || lease->object!=fireScope.root ||
            lease->control!=fireScope.control || !lease->borrowed)return false;
        const unsigned char* rows=nullptr;std::size_t count=0;
        if(!Equipment(*lease,&rows,&count))return false;
        for(std::size_t i=0;i<count;++i)if(WeaponAt(*lease,rows,i)==weapon)return (fireScope.mask&(1ULL<<i))!=0;
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Dispatch(void* weapon,unsigned index) noexcept {
    const auto next=weaponHooks[index].next;
    return next && Allowed(weapon) ? next(weapon) : false;
}
bool __fastcall BasicFire(void* self) { return Dispatch(self,0); }
bool __fastcall HeavyFire(void* self) { return Dispatch(self,1); }
bool __fastcall MartianFire(void* self) { return Dispatch(self,2); }
bool __fastcall BarrageFire(void* self) { return Dispatch(self,3); }
bool Replace(WeaponHook& hook,const void* expected,const void* desired) noexcept {
    auto* cell=reinterpret_cast<void* volatile*>(image+hook.vtable+kWeaponSlot*8);
    DWORD old=0;
    if(!VirtualProtect(const_cast<void**>(cell),sizeof(void*),PAGE_READWRITE,&old))return false;
    const void* was=InterlockedCompareExchangePointer(cell,const_cast<void*>(desired),const_cast<void*>(expected));
    DWORD ignored=0;const bool protectedAgain=VirtualProtect(const_cast<void**>(cell),sizeof(void*),old,&ignored)!=0;
    return was==expected && protectedAgain;
}
}
bool InstallHumanoidControl() noexcept {
    if(installed)return true;
    if(!image)return false;
    __try {
        for(const auto& hook:weaponHooks)if(!Readable(image+hook.fire,sizeof(hook.signature)) ||
            std::memcmp(image+hook.fire,hook.signature,sizeof(hook.signature))!=0 ||
            !Readable(image+hook.vtable+kWeaponSlot*8,8) || At<const void*>(image+hook.vtable,kWeaponSlot*8)!=image+hook.fire)return false;
        unsigned changed=0;
        for(auto& hook:weaponHooks) {
            hook.next=reinterpret_cast<FireFn>(image+hook.fire);
            if(!Replace(hook,reinterpret_cast<const void*>(hook.next),reinterpret_cast<const void*>(hook.wrapper))) {
                for(unsigned i=0;i<=changed;++i)Replace(weaponHooks[i],reinterpret_cast<const void*>(weaponHooks[i].wrapper),reinterpret_cast<const void*>(weaponHooks[i].next));
                return false;
            }
            ++changed;
        }
        installed=true;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
unsigned HumanoidWeaponCount(const NativeLease& lease) noexcept {
    unsigned count=0;return ReadHumanoidWeaponCount(lease,&count) ? count : UINT_MAX;
}
bool ReadHumanoidWeaponCount(const NativeLease& lease,unsigned* out) noexcept {
    if(!out)return false;*out=0;
    __try {
        const unsigned char* rows=nullptr;std::size_t count=0;unsigned live=0;
        if(!installed || !Equipment(lease,&rows,&count))return false;
        for(std::size_t i=0;i<count;++i) {
            const auto row=rows+i*16;
            const void* weapon=At<const void*>(row,0),*control=At<const void*>(row,8);
            if(!weapon && !control)continue;
            if(!weapon || !control || !Readable(control,24) || At<LONG>(control,8)<0)return false;
            if(!At<LONG>(control,8))continue; // a real expired arm is not an unknown live weapon
            if(!Readable(weapon,kWeaponOwner+8) || At<const void*>(control,16)!=weapon ||
                At<const void*>(weapon,0x30)!=control || At<const void*>(weapon,kWeaponOwner)!=lease.object)return false;
            if(const auto hook=HookOf(weapon)) {
                if(!Readable(image+hook->vtable+kWeaponSlot*8,8) ||
                    At<const void*>(image+hook->vtable,kWeaponSlot*8)!=reinterpret_cast<const void*>(hook->wrapper))return false;
                ++live;
            } else if(At<const void*>(weapon,0)!=image+0x17C1918)return false; // native passive shield only
        }
        *out=live;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool HumanoidWeaponIndex(const NativeLease& lease,unsigned visibleIndex,unsigned* nativeIndex) noexcept {
    if(!nativeIndex)return false;
    __try {
        const unsigned char* rows=nullptr;std::size_t count=0;unsigned live=0;
        unsigned validated=0;
        if(!ReadHumanoidWeaponCount(lease,&validated) || !Equipment(lease,&rows,&count))return false;
        for(std::size_t i=0;i<count;++i)if(WeaponAt(lease,rows,i)) {
            if(live++==visibleIndex){*nativeIndex=static_cast<unsigned>(i);return true;}
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}
bool ReadHumanoidWeaponState(const NativeLease& lease,unsigned nativeIndex,enemy_control::Ability* out) noexcept {
    if(!out)return false;
    out->state=enemy_control::AbilityState::unavailable;out->cooldownKnown=false;out->cooldownSeconds=0;
    __try {
        const unsigned char* rows=nullptr;std::size_t count=0;
        unsigned validated=0;
        if(!ReadHumanoidWeaponCount(lease,&validated) || !Equipment(lease,&rows,&count) || nativeIndex>=count)return false;
        const auto weapon=static_cast<const unsigned char*>(WeaponAt(lease,rows,nativeIndex));
        if(!weapon || !Readable(weapon,0x5E4) || !weapon[0x5E2])return true;
        const auto hook=HookOf(weapon);if(!hook)return false;
        const std::size_t kind=static_cast<std::size_t>(hook-weaponHooks);
        const auto table=At<const void* const*>(weapon,0);
        if(!Readable(table,19*8) || table[18]!=image+kBusy[kind])return false;
        // Original fire methods ask this exact pure native busy predicate before
        // admission. It includes firing/reload/charge; it is not an invented
        // wall-clock cooldown and exposes no fabricated remaining seconds.
        const bool busy=reinterpret_cast<bool(__fastcall*)(const void*)>(image+kBusy[kind])(weapon);
        out->state=busy ? enemy_control::AbilityState::cooldown : enemy_control::AbilityState::native;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool RunHumanoidInput(NativeLease& lease,const Input& input,InputFn original,void* dt,
                      std::uint64_t weaponMask,std::uint64_t* result) noexcept {
    const FireScope prior=fireScope;
    bool ok=false;
    __try {
        fireScope=FireScope{&lease,lease.object,lease.control,weaponMask};
        ok=RunInput(lease,input,original,dt,result);
    } __finally { fireScope=prior; }
    return ok;
}
} // namespace crew::enemy_native
