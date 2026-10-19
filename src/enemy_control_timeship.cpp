#include "enemy_control_timeship.h"
#include "enemy_control_timeship_pods.h"
#include "enemy_control_timeship_script.h"
#include "enemy_control_action_native.h"
#include "enemy_control.h"
#include "crew.h"
#include "memory.h"
#include <cmath>
#include <cstring>
#include <iterator>

namespace crew::enemy_native {
namespace timeship_control {
constexpr unsigned kWarp=0x3F3040,kWarp2=0x3F3160,kAnchor=0x3ECA80,kVanish=0x3F3280,kVanish2=0x3F3290;
constexpr unsigned kFrames=600; // native pad0 at 0x3EE691, not a guessed wall clock
struct Print {unsigned rva,size;unsigned char bytes[16];};
constexpr Print prints[]={
    {kWarp,15,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x30}},
    {kWarp2,15,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x30}},
    {kAnchor,14,{0x48,0x8B,0x81,0x10,0x11,0x00,0x00,0x80,0xB8,0x20,0x01,0x00,0x00,0x00}},
    {kVanish,11,{0xC7,0x81,0x74,0x14,0x00,0x00,0x02,0x00,0x00,0x00,0xC3}},
    {kVanish2,11,{0xC7,0x81,0x74,0x14,0x00,0x00,0x01,0x00,0x00,0x00,0xC3}},
    {0x3EE691,10,{0xC7,0x86,0x70,0x14,0x00,0x00,0x58,0x02,0x00,0x00}},
    {0x3EF651,7,{0x44,0x38,0xB1,0x10,0x14,0x00,0x00}}
};
constexpr NativeAbility definitions[]={
    {"timeship_warp",L"跃迁再现（原版样式一）",0,0,AbilityKind::timeShipCommand,nullptr,0},
    {"timeship_warp2",L"跃迁再现（原版样式二）",0,0,AbilityKind::timeShipCommand,nullptr,1},
    {"timeship_anchor",L"向准星投放锚并撤离",0,0,AbilityKind::timeShipCommand,nullptr,2},
    {"timeship_vanish",L"跃迁撤离（样式一，结束操控）",0,0,AbilityKind::timeShipCommand,nullptr,3},
    {"timeship_vanish2",L"跃迁撤离（样式二，结束操控）",0,0,AbilityKind::timeShipCommand,nullptr,4}
};
struct Binding {const void* object=nullptr;const void* control=nullptr;std::uint64_t sequence=0;bool faulted=false;};
Binding bindings[enemy_control::kMaxControllers]{};
struct alignas(16) Matrix {float values[16];};
using WarpFn=void(__fastcall*)(void*,const Matrix*,int);
using TargetFn=void(__fastcall*)(void*,const float*);
using EndFn=void(__fastcall*)(void*);
bool Identity(const NativeLease& lease) noexcept {
    if(!image || !lease.object || !lease.control || !lease.profile ||
        lease.profile->vtableRva!=0x17B93C0 || lease.profile->inputRva!=0x3EE300 ||
        !Readable(lease.object,0x1478) || !Readable(lease.control,12) ||
        At<LONG>(lease.control,8)<=0 || At<const void*>(lease.object,0x30)!=lease.control ||
        (lease.object[0x18]&4) || lease.object[0x2E8])return false;
    std::size_t count=0;const auto* profiles=Profiles(&count);
    for(std::size_t i=0;i<count;++i)if(lease.profile==profiles+i)return true;
    return false;
}
bool Native() noexcept {
    for(const auto& p:prints)if(!Readable(image+p.rva,p.size) || std::memcmp(image+p.rva,p.bytes,p.size))return false;
    return true;
}
const NativeAbility* Definition(const NativeAbility& ability) noexcept {
    if(ability.kind!=AbilityKind::timeShipCommand || ability.held || ability.pressed || ability.action || !ability.id)return nullptr;
    for(const auto& d:definitions)if(d.argument==ability.argument && std::strcmp(d.id,ability.id)==0)return &d;
    return nullptr;
}
Binding* Find(const NativeLease& lease,bool create) noexcept {
    Binding* empty=nullptr;
    for(auto& b:bindings){
        if(b.object==lease.object && b.control==lease.control)return &b;
        if(!b.object && !empty)empty=&b;
    }
    if(create && empty){empty->object=lease.object;empty->control=lease.control;return empty;}
    return nullptr;
}
bool CurrentState(const NativeLease& lease,unsigned rva) noexcept {
    return At<const void*>(lease.object,0x10C8)==image+rva;
}
bool Ready(const NativeLease& lease) noexcept {
    return !(At<unsigned>(lease.object,0x1168)&4u) && !lease.object[0x1410] &&
        At<unsigned>(lease.object,0x1474)==0 &&
        (CurrentState(lease,0x3F1170) || CurrentState(lease,0x3F0C00));
}
bool AnchorReady(const NativeLease& lease) noexcept {
    const auto* controller=At<const unsigned char*>(lease.object,0x1110);
    if(!controller || !Readable(controller,0x130) || controller[0x120] || controller[0x121])return false;
    const auto* link=At<const unsigned char*>(controller,0x28);
    if(!link || !Readable(link,0x78) || link[0x70] || At<int>(link,0x58)>=0)return false;
    const auto* child=At<const unsigned char*>(link,0x48);
    const void* weak=At<const void*>(link,0x50);
    return child && weak && Readable(weak,12) && At<LONG>(weak,8)>0 &&
        Readable(child,0x2E9) && At<const void*>(child,0x30)==weak && !(child[0x18]&4) && !child[0x2E8];
}
bool ReadMatrix(const NativeLease& lease,Matrix* out) noexcept {
    std::memcpy(out->values,lease.object+0x60,sizeof(out->values));
    for(const float value:out->values)if(!std::isfinite(value) || std::fabs(value)>100000000.0f)return false;
    // A degenerate/reused transform cannot be supplied to unchecked MOVAPS.
    for(unsigned row=0;row<3;++row){
        const auto* v=out->values+row*4;const double norm=static_cast<double>(v[0])*v[0]+static_cast<double>(v[1])*v[1]+static_cast<double>(v[2])*v[2];
        if(norm<0.000001 || norm>1000000.0 || std::fabs(v[3])>0.0001f)return false;
    }
    return std::fabs(out->values[15]-1.0f)<0.0001f;
}
bool Role(const NativeLease& lease) noexcept {
    bool authority=false;
    if(NativeActionRole(lease.object,&authority))return authority && !NativeActionQuiescing(lease.object);
    bool online=false;return EnemyControlNetworkContext(lease.object,&online) && !online;
}
bool Fresh(const NativeLease& lease,std::uint64_t sequence) noexcept {
    enemy_control::Input input{};return sequence && EnemyControlCurrentInput(lease.object,&input) && input.sequence==sequence;
}
bool IsActive(const NativeLease& lease,int argument) noexcept {
    switch(static_cast<TimeShipCommand>(argument)){
    case TimeShipCommand::warp:return CurrentState(lease,0x3F1250) || CurrentState(lease,0x3F1C30);
    case TimeShipCommand::warp2:return CurrentState(lease,0x3F1430) || CurrentState(lease,0x3F1FB0);
    case TimeShipCommand::anchor:return lease.object[0x1410] || CurrentState(lease,0x3F04D0) || CurrentState(lease,0x3F0300) || CurrentState(lease,0x3F0820);
    case TimeShipCommand::vanish:return At<unsigned>(lease.object,0x1474)==2 || CurrentState(lease,0x3F2690);
    case TimeShipCommand::vanish2:return At<unsigned>(lease.object,0x1474)==1 || CurrentState(lease,0x3F2920);
    default:return false;
    }
}
}
bool InstallTimeShipControl() noexcept {
    __try {
        if(!image || !timeship_control::Native() || !InstallTimeShipScriptGates())return false;
        if(InstallTimeShipPodControl())return true;
        RestoreTimeShipScriptGates();return false;
    } __except(EXCEPTION_EXECUTE_HANDLER){RestoreTimeShipScriptGates();return false;}
}
unsigned ListTimeShipAbilities(const NativeLease& lease,NativeAbility* out,unsigned capacity) noexcept {
    using namespace timeship_control;
    __try {
        if(!Identity(lease) || !Native())return 0;
        unsigned count=0;
        for(const auto& definition:definitions){if(out && count<capacity)out[count]=definition;++count;}
        const unsigned extra=ListTimeShipPodAbilities(lease,out && capacity>count ? out+count : nullptr,capacity>count ? capacity-count : 0);
        return extra==4 ? count+extra : 0; // incomplete child adapter cannot silently shrink the catalogue
    } __except(EXCEPTION_EXECUTE_HANDLER){return 0;}
}
bool ReadTimeShipAbilityState(const NativeLease& lease,const NativeAbility& ability,enemy_control::Ability* out) noexcept {
    using namespace timeship_control;if(!out)return false;
    out->state=enemy_control::AbilityState::unavailable;out->cooldownKnown=false;out->cooldownSeconds=0;
    __try {
        if(!Identity(lease) || !Native())return false;
        if(!Definition(ability))return ReadTimeShipPodAbilityState(lease,ability,out);
        const auto* binding=Find(lease,false);if(binding && binding->faulted)return true;
        if(IsActive(lease,ability.argument)){out->state=enemy_control::AbilityState::active;return true;}
        if(ability.argument==static_cast<int>(TimeShipCommand::anchor) && !AnchorReady(lease))return true;
        out->state=Ready(lease) ? enemy_control::AbilityState::native : enemy_control::AbilityState::cooldown;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool ExecuteTimeShipAbility(const NativeLease& lease,const NativeAbility& ability,std::uint64_t sequence,std::uint64_t seed) noexcept {
    using namespace timeship_control;
    Binding* binding=nullptr;bool entered=false;
    __try {
        if(!Identity(lease) || !Native() || !Role(lease) || !Fresh(lease,sequence))return false;
        if(!Definition(ability))return ExecuteTimeShipPodAbility(lease,ability,sequence,seed);
        if(!Ready(lease))return false;
        binding=Find(lease,true);if(!binding || binding->faulted || sequence<=binding->sequence)return false;
        const auto kind=static_cast<TimeShipCommand>(ability.argument);
        Matrix matrix{};alignas(16) float target[4]{};
        if(kind==TimeShipCommand::warp || kind==TimeShipCommand::warp2){if(!ReadMatrix(lease,&matrix))return false;}
        else if(kind==TimeShipCommand::anchor){
            if(!AnchorReady(lease) || !EnemyControlWeaponAim(lease.object,target))return false;
            for(unsigned i=0;i<3;++i)if(!std::isfinite(target[i]) || std::fabs(target[i])>100000000.0f)return false;
            target[3]=1.0f;
        }
        if(!SetSeed(lease,seed))return false;
        entered=true;bool accepted=false;
        switch(kind){
        case TimeShipCommand::warp:
        case TimeShipCommand::warp2:{
            const auto rva=kind==TimeShipCommand::warp ? kWarp : kWarp2;
            reinterpret_cast<WarpFn>(image+rva)(lease.object,&matrix,static_cast<int>(kFrames));
            accepted=Identity(lease) && CurrentState(lease,kind==TimeShipCommand::warp ? 0x3F1250 : 0x3F1430) &&
                At<unsigned>(lease.object,0x1470)==kFrames && (At<unsigned>(lease.object,0x1168)&4u) &&
                std::memcmp(lease.object+0x60,matrix.values,sizeof(matrix.values))==0;break;
        }
        case TimeShipCommand::anchor:
            reinterpret_cast<TargetFn>(image+kAnchor)(lease.object,target);
            accepted=Identity(lease) && lease.object[0x1410] && std::memcmp(lease.object+0x1420,target,sizeof(target))==0;break;
        case TimeShipCommand::vanish:
        case TimeShipCommand::vanish2:
            reinterpret_cast<EndFn>(image+(kind==TimeShipCommand::vanish ? kVanish : kVanish2))(lease.object);
            accepted=Identity(lease) && At<unsigned>(lease.object,0x1474)==(kind==TimeShipCommand::vanish ? 2u : 1u);break;
        default:break;
        }
        if(!accepted){binding->faulted=true;return false;}
        binding->sequence=sequence;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){if(binding && entered)binding->faulted=true;return false;}
}
bool RestoreTimeShipControl(const NativeLease& lease) noexcept {
    using namespace timeship_control;
    __try {
        if(!lease.profile || lease.profile->vtableRva!=0x17B93C0)return true;
        if(!RestoreTimeShipPodControl(lease))return false;
        // Native state/visibility/child ownership is never rewound on handoff.
        // The engine's action exits still run after this controller is released.
        if(auto* binding=Find(lease,false))*binding={};
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
