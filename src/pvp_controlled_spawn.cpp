// Mother's configured resource is checked before a whole wave reserves native ownership slots.
#include "pvp_controlled_spawn.h"
#include "pvp_controlled_ported_spawn.h"
#include "enemy_control_legacy_mother.h"

namespace crew {
// Implemented by the typed native provider. The actual newborn class is checked again before activation.
bool EnemyControlNativePvpSpawnProof(const wchar_t*,controlled_pvp::SpawnProof*) noexcept;
namespace {
constexpr unsigned kMaxResolvers=8;
ControlledPvpSpawnResolver resolvers[kMaxResolvers]{};
bool Resolve(const wchar_t* path,controlled_pvp::SpawnProof* out) noexcept {
    if(out)*out={};
    if(!path || !path[0] || !out)return false;
    bool found=false;
    for(auto resolver:resolvers)if(resolver) {
        controlled_pvp::SpawnProof candidate{};
        if(!resolver(path,&candidate))continue;
        if(found || !candidate.hooks.collect || !candidate.maxMembers || candidate.maxMembers>controlled_pvp::kMaxMembers ||
           candidate.maxEmitters>controlled_pvp::kMaxEmitters)return false;
        *out=candidate;found=true;
    }
    return found;
}
bool Can(void*,const ObjRef& parent,const wchar_t* path,unsigned count) noexcept {
    controlled_pvp::SpawnProof proof{};
    return Resolve(path,&proof) && controlled_pvp::CanPrepareSpawns(parent,proof,count);
}
bool Prepare(void*,const ObjRef& parent,const wchar_t* path,unsigned count,std::uint64_t* token) noexcept {
    if(token)*token=0;
    controlled_pvp::SpawnProof proof{};
    return Resolve(path,&proof) && controlled_pvp::PrepareSpawns(parent,proof,count,token);
}
bool Adopt(void*,std::uint64_t token,const ObjRef& parent,const ObjRef& child) noexcept {
    return controlled_pvp::AdoptSpawn(token,parent,child);
}
void Finish(void*,std::uint64_t token) noexcept {controlled_pvp::FinishSpawns(token);}
}
bool RegisterControlledPvpSpawnResolver(ControlledPvpSpawnResolver resolver) noexcept {
    if(!resolver)return false;
    for(auto existing:resolvers)if(existing==resolver)return true;
    for(auto& existing:resolvers)if(!existing){existing=resolver;return true;}
    return false;
}
bool InstallControlledPvpSpawnBridge() noexcept {
    return RegisterControlledPvpSpawnResolver(&EnemyControlNativePvpSpawnProof) &&
        InstallControlledPvpPortedSpawnResolver() &&
        ConfigureMotherSpawnOwnership({nullptr,&Can,&Prepare,&Adopt,&Finish});
}
}
