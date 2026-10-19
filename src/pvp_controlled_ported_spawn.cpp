// Registry-derived resource proof, checked again against each actual typed newborn before activation.
#include "pvp_controlled_ported_spawn.h"
#include "pvp_controlled_spawn.h"
#include "pvp_controlled_npc.h"
#include "enemy_control_legacy.h"
#include "enemy_control_pvp_native.h"
#include "memory.h"
#include <cstring>
#include <cwctype>

namespace crew {
namespace {
struct Resource {
    const wchar_t* path;const char* className;unsigned members,emitters;
    unsigned maxWeapons=0,maxHeld=0;
};
constexpr Resource resources[]={
#include "enemy_control_ported_spawn_profiles.inc"
};
bool SamePath(const wchar_t* a,const wchar_t* b) noexcept {
    if(!a || !b)return false;
    for(unsigned i=0;i<260;++i) {
        if(std::towlower(a[i])!=std::towlower(b[i]))return false;
        if(!a[i])return true;
    }
    return false;
}
const enemy_native::NativeProfile* Profile(const Resource& resource) noexcept {
    std::size_t count=0;const auto* profiles=enemy_native::Profiles(&count);
    for(std::size_t i=0;i<count;++i)if(!std::strcmp(profiles[i].name,resource.className))return &profiles[i];
    return nullptr;
}
bool NativeClass(const ObjRef& root,const Resource& resource) noexcept {
    const auto* profile=Profile(resource);
    if(!profile || !root.obj || !root.ctrl || !Readable(root.ctrl,16) || At<LONG>(root.ctrl,8)<=0 ||
       !Readable(root.obj,kSelfCtrl+8) || At<const void*>(root.obj,kSelf)!=root.obj ||
       At<const void*>(root.obj,kSelfCtrl)!=root.ctrl)return false;
    const auto* table=At<const unsigned char*>(root.obj,0);
    return image && reinterpret_cast<std::uintptr_t>(table)>=8 && Readable(table-8,8) &&
        Readable(image+profile->vtableRva-8,8) && At<const void*>(table-8,0)==At<const void*>(image+profile->vtableRva-8,0);
}
bool NativeCollect(void* user,const ObjRef& root,ObjRef* members,unsigned memberCapacity,unsigned* memberCount,
                   controlled_pvp::Emitter* emitters,unsigned emitterCapacity,unsigned* emitterCount) noexcept {
    const auto* resource=static_cast<const Resource*>(user);
    return resource && NativeClass(root,*resource) && EnemyControlNativePvpProfileReady(resource->className) &&
        EnemyControlNativePvpCollect(root,members,memberCapacity,memberCount,emitters,emitterCapacity,emitterCount);
}
controlled_pvp::Life NativeRead(void*,const controlled_pvp::Emitter& emitter,int* team) noexcept {return EnemyControlNativePvpRead(emitter,team);}
bool NativeWrite(void*,const controlled_pvp::Emitter& emitter,int team) noexcept {return EnemyControlNativePvpWrite(emitter,team);}
bool NativeMemberBaseline(void*,const ObjRef& root,const ObjRef& member,int team,int last,int* original,int* originalLast) noexcept {
    return EnemyControlNativePvpBaseline(root,member,team,last,original,originalLast);
}
bool NativeEmitterBaseline(void*,const controlled_pvp::Emitter& emitter,int team,int last,int* original) noexcept {
    return EnemyControlNativePvpBaseline(emitter,team,last,original);
}
bool Underlying(const Resource& resource,controlled_pvp::SpawnProof* out) noexcept {
    *out={};
    if(!std::strcmp(resource.className,"UfoRobo")) {
        if(!Edf41RoboPvpSpawnProof(out))return false;
    } else if(resource.maxWeapons) {
        if(!PvpControlledNpcProof(resource.className,resource.maxWeapons,resource.maxHeld,out))return false;
    } else {
        if(resource.maxHeld || !Profile(resource) || !EnemyControlNativePvpProfileReady(resource.className))return false;
        out->hooks={const_cast<Resource*>(&resource),&NativeCollect,&NativeRead,&NativeWrite,&NativeMemberBaseline,&NativeEmitterBaseline};
        out->maxMembers=resource.members;out->maxEmitters=resource.emitters;
    }
    return out->hooks.collect && out->maxMembers && out->maxMembers<=resource.members && out->maxEmitters<=resource.emitters;
}
bool Collect(void* user,const ObjRef& root,ObjRef* members,unsigned memberCapacity,unsigned* memberCount,
             controlled_pvp::Emitter* emitters,unsigned emitterCapacity,unsigned* emitterCount) noexcept {
    if(memberCount)*memberCount=0;if(emitterCount)*emitterCount=0;
    const auto* resource=static_cast<const Resource*>(user);controlled_pvp::SpawnProof proof{};
    return resource && Underlying(*resource,&proof) &&
        proof.hooks.collect(proof.hooks.user,root,members,memberCapacity,memberCount,emitters,emitterCapacity,emitterCount);
}
controlled_pvp::Life Read(void* user,const controlled_pvp::Emitter& emitter,int* team) noexcept {
    const auto* resource=static_cast<const Resource*>(user);controlled_pvp::SpawnProof proof{};
    return resource && Underlying(*resource,&proof) && proof.hooks.readEmitter ?
        proof.hooks.readEmitter(proof.hooks.user,emitter,team) : controlled_pvp::Life::invalid;
}
bool Write(void* user,const controlled_pvp::Emitter& emitter,int team) noexcept {
    const auto* resource=static_cast<const Resource*>(user);controlled_pvp::SpawnProof proof{};
    return resource && Underlying(*resource,&proof) && proof.hooks.writeEmitter && proof.hooks.writeEmitter(proof.hooks.user,emitter,team);
}
bool MemberBaseline(void* user,const ObjRef& root,const ObjRef& member,int team,int last,int* original,int* originalLast) noexcept {
    const auto* resource=static_cast<const Resource*>(user);controlled_pvp::SpawnProof proof{};
    return resource && Underlying(*resource,&proof) && proof.hooks.memberBaseline &&
        proof.hooks.memberBaseline(proof.hooks.user,root,member,team,last,original,originalLast);
}
bool EmitterBaseline(void* user,const controlled_pvp::Emitter& emitter,int team,int last,int* original) noexcept {
    const auto* resource=static_cast<const Resource*>(user);controlled_pvp::SpawnProof proof{};
    return resource && Underlying(*resource,&proof) && proof.hooks.emitterBaseline &&
        proof.hooks.emitterBaseline(proof.hooks.user,emitter,team,last,original);
}
bool PrepareInstance(void* user,const ObjRef& child) noexcept {
    const auto* resource=static_cast<const Resource*>(user);controlled_pvp::SpawnProof proof{};
    return resource && Underlying(*resource,&proof) && proof.prepareInstance && proof.prepareInstance(proof.hooks.user,child);
}
}
bool ControlledPvpPortedSpawnProof(const wchar_t* path,controlled_pvp::SpawnProof* out) noexcept {
    if(out)*out={};if(!out || !path || !path[0])return false;
    __try {
        const Resource* selected=nullptr;
        for(const auto& resource:resources)if(SamePath(path,resource.path)) {
            if(selected)return false;selected=&resource;
        }
        if(!selected || !selected->members || selected->members>controlled_pvp::kMaxMembers ||
           selected->emitters>controlled_pvp::kMaxEmitters)return false;
        controlled_pvp::SpawnProof proof{};if(!Underlying(*selected,&proof))return false;
        out->hooks={const_cast<Resource*>(selected),&Collect,&Read,&Write,&MemberBaseline,&EmitterBaseline};
        out->maxMembers=selected->members;out->maxEmitters=selected->emitters;
        if(proof.prepareInstance)out->prepareInstance=&PrepareInstance;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool InstallControlledPvpPortedSpawnResolver() noexcept {return RegisterControlledPvpSpawnResolver(&ControlledPvpPortedSpawnProof);}
}
