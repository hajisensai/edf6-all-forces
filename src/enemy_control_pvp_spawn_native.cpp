// Resource preflight for controlled summons. The manifest binds each original Root.cpk SGO to its real native class.
#include "enemy_control_pvp_native.h"
#include "memory.h"
#include <cwchar>
#include <cwctype>

namespace crew {
bool EnemyControlNativePvpProfileReady(const char*) noexcept;
namespace {
struct SpawnResource {const wchar_t* path;std::uint32_t vtable;unsigned members,emitters;};
constexpr SpawnResource resources[]={
#include "enemy_control_pvp_spawn_profiles.inc"
};
const enemy_native::NativeProfile* ResourceProfile(const SpawnResource& resource) noexcept {
    std::size_t count=0;const auto* profiles=enemy_native::Profiles(&count);
    for(std::size_t i=0;i<count;++i)if(profiles[i].vtableRva==resource.vtable)return &profiles[i];
    return nullptr;
}
bool SamePath(const wchar_t* a,const wchar_t* b) noexcept {
    if(!a || !b)return false;
    for(unsigned i=0;i<260;++i) {
        if(std::towlower(a[i])!=std::towlower(b[i]))return false;
        if(!a[i])return true;
    }
    return false;
}
bool ExactClass(const ObjRef& root,const SpawnResource& resource) noexcept {
    if(!root.obj || !root.ctrl || !Readable(root.ctrl,16) || At<LONG>(root.ctrl,8)<=0 ||
       !Readable(root.obj,kSelfCtrl+8) || At<const void*>(root.obj,kSelfCtrl)!=root.ctrl)return false;
    const auto* table=At<const unsigned char*>(root.obj,0);
    return reinterpret_cast<std::uintptr_t>(table)>=8 && Readable(table-8,8) &&
        Readable(image+resource.vtable-8,8) && At<const void*>(table-8,0)==At<const void*>(image+resource.vtable-8,0);
}
bool Collect(void* user,const ObjRef& root,ObjRef* members,unsigned memberCapacity,unsigned* memberCount,
             controlled_pvp::Emitter* emitters,unsigned emitterCapacity,unsigned* emitterCount) noexcept {
    if(memberCount)*memberCount=0;if(emitterCount)*emitterCount=0;
    __try {
        const auto* resource=static_cast<const SpawnResource*>(user);
        if(!resource || !ExactClass(root,*resource))return false;
        const auto* profile=ResourceProfile(*resource);
        return profile && EnemyControlNativePvpProfileReady(profile->name) &&
            EnemyControlNativePvpCollect(root,members,memberCapacity,memberCount,emitters,emitterCapacity,emitterCount);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
controlled_pvp::Life Read(void*,const controlled_pvp::Emitter& emitter,int* team) noexcept {return EnemyControlNativePvpRead(emitter,team);}
bool Write(void*,const controlled_pvp::Emitter& emitter,int team) noexcept {return EnemyControlNativePvpWrite(emitter,team);}
bool MemberBaseline(void*,const ObjRef& root,const ObjRef& member,int team,int last,int* original,int* originalLast) noexcept {
    return EnemyControlNativePvpBaseline(root,member,team,last,original,originalLast);
}
bool EmitterBaseline(void*,const controlled_pvp::Emitter& emitter,int team,int last,int* original) noexcept {
    return EnemyControlNativePvpBaseline(emitter,team,last,original);
}
}
bool EnemyControlNativePvpSpawnProof(const wchar_t* path,controlled_pvp::SpawnProof* out) noexcept {
    if(out)*out={};if(!out || !path || !path[0])return false;
    __try {
        const SpawnResource* selected=nullptr;
        for(const auto& resource:resources)if(SamePath(path,resource.path)) {
            if(selected)return false;
            selected=&resource;
        }
        if(!selected || !selected->members || selected->members>controlled_pvp::kMaxMembers ||
           selected->emitters>controlled_pvp::kMaxEmitters)return false;
        const auto* profile=ResourceProfile(*selected);
        if(!profile || !EnemyControlNativePvpProfileReady(profile->name))return false;
        out->maxMembers=selected->members;out->maxEmitters=selected->emitters;
        out->hooks={const_cast<SpawnResource*>(selected),&Collect,&Read,&Write,&MemberBaseline,&EmitterBaseline};return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
