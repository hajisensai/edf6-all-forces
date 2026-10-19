// Semantic commands shared by the real EDF4.1 mother controllers and offline tests.
#pragma once
#include "enemy_control_legacy.h"
#include "pvp_controlled.h"
#include <cstring>
#include <cwchar>
namespace crew {
// Startup-only native ownership bridge. Tokens belong to the authenticated controlled-PVP lease,
// not to a network identity or arbitrary SGO caller. Adopt captures the already initialized real baseline.
struct MotherSpawnOwnership {
    void* user=nullptr;
    bool (*canPrepare)(void*,const ObjRef&,const wchar_t*,unsigned) noexcept=nullptr;
    bool (*prepare)(void*,const ObjRef&,const wchar_t*,unsigned,std::uint64_t*) noexcept=nullptr;
    bool (*adopt)(void*,std::uint64_t,const ObjRef&,const ObjRef&) noexcept=nullptr;
    void (*finish)(void*,std::uint64_t) noexcept=nullptr;
};
}
namespace crew::mother_control {
inline MotherSpawnOwnership spawnOwnership{};
inline bool spawnOwnershipConfigured=false;
inline bool OwnedSpawner(const ObjRef& parent) noexcept {
    int team=0,last=0;return controlled_pvp::ReadBaselineForMember(parent,&team,&last);
}
struct SpawnWave {
    ObjRef parent{};std::uint64_t token=0;unsigned remaining=0;wchar_t path[260]{};bool fault=false;
    static bool CanPrepare(const ObjRef& source,const wchar_t* resource,unsigned count) noexcept {
        if(!count || !OwnedSpawner(source))return true;
        return resource && resource[0] && spawnOwnershipConfigured && spawnOwnership.canPrepare &&
            spawnOwnership.canPrepare(spawnOwnership.user,source,resource,count);
    }
    void Finish() noexcept {
        if(token && spawnOwnership.finish)spawnOwnership.finish(spawnOwnership.user,token);
        token=0;remaining=0;parent={};path[0]=0;
    }
    void Reset() noexcept {Finish();fault=false;}
    bool Prepare(const ObjRef& source,const wchar_t* resource,unsigned count) noexcept {
        if(!count || !OwnedSpawner(source)){Finish();return true;}
        if(token && parent.obj==source.obj && parent.ctrl==source.ctrl && resource &&
            std::wcscmp(path,resource)==0 && count<=remaining)return true;
        Finish();
        if(!CanPrepare(source,resource,count) || std::wcslen(resource)>=260 || !spawnOwnership.prepare ||
            !spawnOwnership.prepare(spawnOwnership.user,source,resource,count,&token) || !token) {
            Finish();fault=true;return false;
        }
        parent=source;remaining=count;std::wmemcpy(path,resource,std::wcslen(resource)+1);return true;
    }
    bool Adopt(const ObjRef& source,const ObjRef& child) noexcept {
        if(!OwnedSpawner(source)){Finish();return true;}
        if(!token || !remaining || parent.obj!=source.obj || parent.ctrl!=source.ctrl || !spawnOwnership.adopt ||
            !spawnOwnership.adopt(spawnOwnership.user,token,source,child)) {fault=true;return false;}
        --remaining;return true;
    }
};
constexpr unsigned kAll=255,kSummon=18,kLast301=21;
constexpr std::uint32_t Id301(unsigned command,unsigned index=kAll) noexcept {
    return command<=kLast301 && index<=kAll ? 0x41300000u|(command<<8)|index : 0;
}
constexpr bool Decode301(std::uint32_t id,unsigned& command,unsigned& index) noexcept {
    command=(id>>8)&255;index=id&255;
    return (id&0xFFFF0000u)==0x41300000u && command<=kLast301;
}
constexpr std::uint32_t Id401(unsigned command,unsigned kind) noexcept {
    return command<3 && kind<12 ? 0x41400000u|(command<<8)|kind : 0;
}
inline void Ability(enemy_control::Ability* out,unsigned capacity,unsigned at,std::uint32_t id,const wchar_t* name,
                    bool available=true) noexcept {
    if(!out || at>=capacity)return;
    auto& a=out[at];a={};a.id=id;a.flags=enemy_control::edge;a.channel=at;
    a.state=available ? enemy_control::AbilityState::native : enemy_control::AbilityState::unavailable;
    const auto n=(std::min)(std::wcslen(name),std::size_t{95});std::wmemcpy(a.name,name,n);a.name[n]=0;
}
inline bool Sample(enemy_legacy::Control& control,const enemy_control::Input& input,bool first) noexcept {
    return control.Apply(input,first);
}
inline std::uint64_t Hash(unsigned family,unsigned kind) noexcept { return 0x4544463431430004ull^(std::uint64_t(family)<<24)^kind; }
struct Command { std::uint32_t action=0,index=0;std::uint64_t seed=0,lease=0; };
static_assert(sizeof(Command)==24,"mother semantic command wire layout");
}
namespace crew {
inline bool ConfigureMotherSpawnOwnership(const MotherSpawnOwnership& hooks) noexcept {
    if(!hooks.canPrepare || !hooks.prepare || !hooks.adopt || !hooks.finish)return false;
    if(mother_control::spawnOwnershipConfigured)return mother_control::spawnOwnership.user==hooks.user &&
        mother_control::spawnOwnership.canPrepare==hooks.canPrepare && mother_control::spawnOwnership.prepare==hooks.prepare &&
        mother_control::spawnOwnership.adopt==hooks.adopt && mother_control::spawnOwnership.finish==hooks.finish;
    mother_control::spawnOwnership=hooks;mother_control::spawnOwnershipConfigured=true;return true;
}
const EnemyControlAdapter& Edf41Mother301ControlAdapter() noexcept;
const EnemyControlAdapter& Edf41Mother401ControlAdapter() noexcept;
}
