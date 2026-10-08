#include "support_soldier.h"
#include "memory.h"
#include "online_authority.h"
#include <cmath>

namespace crew {
namespace support_native {
constexpr unsigned kCreate=0x11945E0,kDelete=0x118A1B0,kPreload=0x7A3780,kTeamCall=0x54EE70,kLevel=0x54E740,kFollow=0x54EC50;
constexpr unsigned kInitVtable=0x1762068;
constexpr unsigned kRegister=0x781950,kDeriveId=0x776790;
constexpr std::size_t kNetworkManager=0x20B2AC8;
constexpr std::size_t kObjectManager=0x20B2958,kPreloadManager=0x20B29A8;
constexpr const wchar_t* kBodies[]={L"app:/object/N601_COMMON_RANGER_AF.sgo",L"app:/object/N601_COMMON_RANGER_AF_LEADER.sgo"};
struct alignas(16) InitParam { const void* vtable; unsigned char rest[0x28]; };
static_assert(sizeof(InitParam)==0x30);
using CreateFn=unsigned char*(__fastcall*)(void*,const float*,const wchar_t*,InitParam*);
using DeleteFn=void(__fastcall*)(void*);
using TeamFn=void(__fastcall*)(void*,int,bool);
using LevelFn=void(__fastcall*)(void*,float);
using FollowFn=void(__fastcall*)(void*,void*,bool);
using PreloadFn=void(__fastcall*)(void*,const wchar_t*,int,int);
using RegisterFn=const unsigned char*(__fastcall*)(void*,ObjRef*,const unsigned char*);
CreateFn create=nullptr;DeleteFn destroy=nullptr;TeamFn team=nullptr;LevelFn level=nullptr;
FollowFn follow=nullptr;PreloadFn preload=nullptr;
RegisterFn registerObject=nullptr;
using DeriveFn=void*(__fastcall*)(void*,const void*,unsigned);
DeriveFn deriveId=nullptr;
bool profile=false,preloaded=false,faulted=false;
void* missionManager=nullptr;
SupportSpawnFailure failure=SupportSpawnFailure::none;
struct Owned { ObjRef ref; unsigned epoch=0; bool held=false; };
constexpr unsigned kOwnedLimit=96;
Owned owned[kOwnedLimit]{};
unsigned epoch=0;

void ReleaseWeak(Owned& entry) noexcept {
    if(entry.ref.ctrl) {
        auto* ctrl=const_cast<unsigned char*>(static_cast<const unsigned char*>(entry.ref.ctrl));
        if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(ctrl+0xC))==0)
            reinterpret_cast<void(__fastcall*)(void*)>(At<void* const*>(ctrl,0)[1])(ctrl);
    }
    entry=Owned{};
}
// The retained weak block remains readable after the object has died; never dereference the object first.
bool Live(const Owned& entry) noexcept {
    return entry.ref.ctrl && At<LONG>(entry.ref.ctrl,8)>0 && entry.ref.Is(entry.ref.obj) &&
        !(At<unsigned char>(entry.ref.obj,0x18)&4);
}
Owned* FreeEntry() noexcept {
    for(auto& entry:owned) {
        if(entry.ref && !Live(entry))ReleaseWeak(entry);
        if(!entry.ref)return &entry;
    }
    return nullptr;
}
bool Matrix(const float* m) noexcept {
    if(!m || !Readable(m,64))return false;
    for(unsigned i=0;i<16;++i)if(!std::isfinite(m[i]))return false;
    if(std::fabs(m[3])>0.001f || std::fabs(m[7])>0.001f || std::fabs(m[11])>0.001f || std::fabs(m[15]-1)>0.001f)return false;
    for(unsigned a=0;a<3;++a)for(unsigned b=a;b<3;++b) {
        float dot=0;for(unsigned i=0;i<3;++i)dot+=m[a*4+i]*m[b*4+i];
        if(std::fabs(dot-(a==b ? 1.0f : 0.0f))>0.01f)return false;
    }
    const float determinant=m[0]*(m[5]*m[10]-m[6]*m[9])-m[1]*(m[4]*m[10]-m[6]*m[8])+m[2]*(m[4]*m[9]-m[5]*m[8]);
    return determinant>0.99f;
}
bool Gate(bool replicated=false) noexcept {
    failure=SupportSpawnFailure::none;
    if(!Cfg().enabled){failure=SupportSpawnFailure::disabled;return false;}
    if(!profile || faulted){failure=SupportSpawnFailure::profile;return false;}
    if(!preloaded || !missionManager || At<void*>(image,kObjectManager)!=missionManager){failure=SupportSpawnFailure::mission;return false;}
    // CreateObject does not replicate creation. Until a reliable, ID-bound spawn event is installed,
    // registering just a host object would create an invisible client NPC and is deliberately rejected.
    if((InSession() && !replicated) || (!replicated && !OnlineHostOnly())){failure=SupportSpawnFailure::onlineReplication;return false;}
    return true;
}
int SpawnFault(const char* stage,const EXCEPTION_POINTERS* error,const float* requested,const float* nativeMatrix,const void* soldier) noexcept {
    const auto record=error->ExceptionRecord;
    const auto address=reinterpret_cast<std::uintptr_t>(record->ExceptionAddress);
    const auto base=reinterpret_cast<std::uintptr_t>(image);
    const auto rva=address>=base ? address-base : 0;
    const auto access=record->NumberParameters>=2 ? record->ExceptionInformation[1] : 0;
    Log("SUPPORT soldier fault stage=%s code=%08lX at=%p EDF+%llX access=%p requestedMatrix=%p mod16=%u nativeMatrix=%p mod16=%u object=%p; spawning disabled for this process",
        stage,record->ExceptionCode,record->ExceptionAddress,static_cast<unsigned long long>(rva),reinterpret_cast<const void*>(access),requested,
        static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(requested)&15u),nativeMatrix,
        static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(nativeMatrix)&15u),soldier);
    return EXCEPTION_EXECUTE_HANDLER;
}
bool Spawn(const float* matrix,bool leader,ObjRef* out,const unsigned char* netId=nullptr) noexcept {
    *out=ObjRef{};
    if(!Gate(netId!=nullptr))return false;
    if(netId && (!InSession() || !Readable(netId,32) || At<unsigned>(netId,0xC)!=5 || !registerObject || !At<void*>(image,kNetworkManager))) {
        failure=SupportSpawnFailure::onlineReplication;return false;
    }
    if(!Matrix(matrix)){failure=SupportSpawnFailure::transform;return false;}
    Owned* entry=FreeEntry();
    if(!entry){failure=SupportSpawnFailure::capacity;return false;}
    unsigned char* soldier=nullptr;
    const char* volatile stage="create";
    const float* volatile nativeInput=nullptr;
    __try {
        // CreateObject stores this pointer verbatim at InitParam+8. SceneObject's constructor
        // 1189CB0..1189CCE uses MOVAPS for all four rows. Wire/Plan Unit.matrix is only float-aligned.
        alignas(16) float nativeMatrix[16];
        std::memcpy(nativeMatrix,matrix,sizeof(nativeMatrix));
        nativeInput=nativeMatrix;
        InitParam init{image+kInitVtable,{}};
        soldier=create(missionManager,nativeMatrix,kBodies[leader ? 1 : 0],&init);
        if(!soldier){failure=SupportSpawnFailure::create;return false;}
        stage="identity";
        const auto ctrl=At<unsigned char*>(soldier,kSelfCtrl);
        if(!IsSoldierClass(soldier) || IsAnyPlayer(soldier) || !Readable(ctrl,16) || At<LONG>(ctrl,8)<=0) {
            failure=SupportSpawnFailure::wrongClass;destroy(soldier);return false;
        }
        // Retain only the weak count. The scene manager owns the actual soldier, exactly as CreateFriend.
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(ctrl+0xC));
        *entry=Owned{ObjRef::Of(soldier),epoch,true};
        stage="team";
        team(soldier,2,true);
        stage="level";
        level(soldier,1.0f); // native difficulty scaling, not direct HP/weapon manipulation
        stage="recruit";
        Put<unsigned char>(soldier,0x540,1); // stock CreateFriend's recruitable flag
        if(At<int>(soldier,edf::kTeam)!=2){failure=SupportSpawnFailure::setup;destroy(soldier);ReleaseWeak(*entry);return false;}
        if(netId) {
            stage="network";
            if(!RegisterSupportObject(soldier,netId)) {
                failure=SupportSpawnFailure::setup;destroy(soldier);ReleaseWeak(*entry);return false;
            }
        }
        *out=entry->ref;
        return true;
    } __except(SpawnFault(stage,GetExceptionInformation(),matrix,nativeInput,soldier)) {
        // A constructor fault may leave a partial object in the engine. Never retry that profile this process.
        faulted=true;failure=soldier ? SupportSpawnFailure::setup : SupportSpawnFailure::create;
    }
    if(soldier) {
        __try { destroy(soldier); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    if(entry->ref)ReleaseWeak(*entry);
    return false;
}
}

bool InstallSupportSoldiers() noexcept {
    using namespace support_native;
    struct Signature { unsigned rva; unsigned char bytes[16]; };
    const Signature signatures[]={
        {kCreate,{0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x8D,0x6C,0x24,0xD9}},
        {kDelete,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x60,0x48}},
        {kPreload,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48}},
        {kTeamCall,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x41}},
        {kLevel,{0x48,0x8B,0xC4,0x48,0x89,0x58,0x18,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x8D}},
        {kFollow,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48}}
    };
    profile=false;
    __try {
        for(const auto& s:signatures)if(!Matches(s.rva,s.bytes,sizeof(s.bytes)))return false;
        create=reinterpret_cast<CreateFn>(image+kCreate);destroy=reinterpret_cast<DeleteFn>(image+kDelete);
        preload=reinterpret_cast<PreloadFn>(image+kPreload);team=reinterpret_cast<TeamFn>(image+kTeamCall);
        level=reinterpret_cast<LevelFn>(image+kLevel);follow=reinterpret_cast<FollowFn>(image+kFollow);
        const unsigned char registerSig[]={0x48,0x89,0x5C,0x24,0x20,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x8D,0x6C,0x24};
        if(!Matches(kRegister,registerSig,sizeof(registerSig)))return false;
        const unsigned char deriveSig[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x33,0xC0,0x0F,0x57,0xC0,0x48};
        if(!Matches(kDeriveId,deriveSig,sizeof(deriveSig)))return false;
        registerObject=reinterpret_cast<RegisterFn>(image+kRegister);
        deriveId=reinterpret_cast<DeriveFn>(image+kDeriveId);
        profile=true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return profile;
}
void ResetSupportSoldiers() noexcept {
    using namespace support_native;
    preloaded=false;missionManager=nullptr;++epoch;
    // Mission teardown owns deletion. Release retained weak blocks only; no call into a previous scene.
    for(auto& entry:owned)ReleaseWeak(entry);
}
void PreloadSupportSoldiers() noexcept {
    using namespace support_native;
    ResetSupportSoldiers();
    if(!profile || faulted || !Cfg().enabled)return;
    __try {
        void* mgr=At<void*>(image,kPreloadManager);
        missionManager=At<void*>(image,kObjectManager);
        if(!mgr || !missionManager)return;
        for(const wchar_t* path:kBodies)preload(mgr,path,2,-1);
        preloaded=true;
    } __except(EXCEPTION_EXECUTE_HANDLER){preloaded=false;faulted=true;}
}
bool SupportSoldiersReady() noexcept { return support_native::Gate(true); }
bool HoldSupportSoldier(const ObjRef& soldier,bool held) noexcept {
    using namespace support_native;
    if(!soldier || !missionManager || At<void*>(image,kObjectManager)!=missionManager)return false;
    __try {
        for(auto& entry:owned)if(entry.epoch==epoch && entry.ref.obj==soldier.obj && entry.ref.ctrl==soldier.ctrl &&
                                Live(entry) && !At<unsigned char>(entry.ref.obj,kDead)) {
            entry.held=held;return true;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}
bool SupportSoldierHeld(const void* soldier) noexcept {
    using namespace support_native;
    if(!soldier || !missionManager || At<void*>(image,kObjectManager)!=missionManager)return false;
    __try {
        for(const auto& entry:owned)if(entry.epoch==epoch && entry.ref.obj==soldier && entry.held && Live(entry) &&
                                      !At<unsigned char>(entry.ref.obj,kDead))return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}
SupportSpawnFailure SupportSoldierLastFailure() noexcept { return support_native::failure; }
const wchar_t* SupportSoldierFailureText() noexcept {
    switch(support_native::failure) {
    case SupportSpawnFailure::none:return L"";
    case SupportSpawnFailure::disabled:return L"支援已关闭";
    case SupportSpawnFailure::profile:return L"士兵创建接口不可用";
    case SupportSpawnFailure::mission:return L"本关尚未预载支援士兵";
    case SupportSpawnFailure::onlineReplication:return L"联机支援需要已验证的兵员生成同步";
    case SupportSpawnFailure::transform:return L"支援入口坐标无效";
    case SupportSpawnFailure::capacity:return L"支援兵员已达上限";
    case SupportSpawnFailure::create:return L"创建支援士兵失败";
    case SupportSpawnFailure::wrongClass:return L"支援资源未生成真实士兵";
    case SupportSpawnFailure::setup:return L"支援士兵配置失败";
    }
    return L"支援不可用";
}
bool SpawnSupportSoldier(const float* matrix,ObjRef* out) noexcept {
    if(!out)return false;
    return support_native::Spawn(matrix,false,out);
}
bool ApplySupportSoldierSpawn(const float* matrix,bool leader,const unsigned char* id,ObjRef* out) noexcept {
    if(!out)return false;
    return support_native::Spawn(matrix,leader,out,id);
}
bool RegisterSupportObject(const void* object,const unsigned char* id) noexcept {
    using namespace support_native;
    if(!profile || !registerObject || !InSession() || !Readable(object,0x138) || !Readable(id,32))return false;
    __try {
        if(At<unsigned>(object,0x128)&3u || At<unsigned>(id,0xC)!=5)return false;
        void* mgr=At<void*>(image,kNetworkManager);
        auto* ctrl=At<unsigned char*>(object,kSelfCtrl);
        if(!mgr || !Readable(ctrl,16) || At<LONG>(ctrl,8)<=0)return false;
        ObjRef consumed=ObjRef::Of(object);
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(ctrl+0xC));
        const auto registered=registerObject(mgr,&consumed,id);
        const unsigned expected=OnlineHostOnly() ? 2u : 1u;
        return registered && Readable(registered,32) && std::memcmp(registered,id,20)==0 && std::memcmp(registered+24,id+24,8)==0 &&
            (At<unsigned>(object,0x128)&3u)==expected && At<void*>(object,0x130);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool DeriveSupportSoldierNetId(const void* anchor,unsigned ordinal,unsigned char* out) noexcept {
    using namespace support_native;
    if(!out || !profile || !deriveId || !InSession() || !OnlineHostOnly() || !Readable(anchor,0x138))return false;
    __try {
        if(!(At<unsigned>(anchor,0x128)&3u))return false;
        const auto ctrl=At<const unsigned char*>(anchor,0x130);
        if(!Readable(ctrl,16) || At<LONG>(ctrl,0)<=0)return false;
        const auto entry=At<const void*>(ctrl,8);
        if(!Readable(entry,0x28))return false;
        std::memset(out,0,32); // Native constructor leaves the four padding bytes at +14h unwritten.
        deriveId(out,entry,ordinal);
        return At<unsigned>(out,0xC)==5 && At<unsigned>(out,4)==ordinal;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool ReadNativeObjectId(const unsigned char* object,unsigned char* out) noexcept {
    if(!out || !Readable(object,0x138))return false;
    __try {
        if(!(At<unsigned>(object,0x128)&3u))return false;
        const auto ctrl=At<const unsigned char*>(object,0x130);
        if(!Readable(ctrl,16) || At<LONG>(ctrl,0)<=0)return false;
        const auto entry=At<const unsigned char*>(ctrl,8);
        if(!Readable(entry,0x28))return false;
        std::memset(out,0,32);
        std::memcpy(out,entry+8,20);std::memcpy(out+24,entry+32,8);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool FollowSupportSoldier(const ObjRef& soldier,const ObjRef& leader) noexcept {
    using namespace support_native;
    Owned *s=nullptr,*l=nullptr;
    for(auto& entry:owned)if(entry.epoch==epoch && Live(entry)) {
        if(entry.ref.obj==soldier.obj && entry.ref.ctrl==soldier.ctrl)s=&entry;
        if(entry.ref.obj==leader.obj && entry.ref.ctrl==leader.ctrl)l=&entry;
    }
    if(!s || s==l)return false;
    __try {
        const auto top=static_cast<const unsigned char*>(leader.obj);
        if(!l && (!Readable(top,0x550) || !IsAnyPlayer(top) || !leader.Is(top) ||
                  !Readable(leader.ctrl,16) || At<LONG>(leader.ctrl,8)<=0 || (At<unsigned char>(top,0x18)&4)))return false;
        const void* ancestor=leader.obj;
        for(unsigned i=0;ancestor;++i) {
            if(i>=kOwnedLimit || ancestor==s->ref.obj || !Readable(ancestor,0x550))return false;
            ancestor=At<const void*>(ancestor,0x548);
        }
        follow(const_cast<void*>(s->ref.obj),const_cast<void*>(leader.obj),false);
        return At<const void*>(s->ref.obj,0x548)==leader.obj;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool DeleteSupportSoldier(const ObjRef& soldier) noexcept {
    using namespace support_native;
    for(auto& entry:owned)if(entry.epoch==epoch && entry.ref.obj==soldier.obj && entry.ref.ctrl==soldier.ctrl && entry.ref) {
        if(Live(entry))destroy(const_cast<void*>(entry.ref.obj));
        ReleaseWeak(entry);return true;
    }
    return false;
}
bool SpawnSupportSquad(SupportSquadKind kind,const float* matrices,unsigned count,SupportSquad* out) noexcept {
    using namespace support_native;
    if(!out)return false;
    *out=SupportSquad{};
    const unsigned expected=kind==SupportSquadKind::rangerSquad ? 4u : kind==SupportSquadKind::rangerPlatoon ? 12u : 0u;
    if(!Gate())return false;
    if(!expected || count!=expected || !matrices || !Readable(matrices,count*64)) {failure=SupportSpawnFailure::transform;return false;}
    for(unsigned i=0;i<count;++i)if(!Matrix(matrices+16*i)){failure=SupportSpawnFailure::transform;return false;}
    for(unsigned i=0;i<count;++i) {
        const bool leader=i%4==0;
        if(!Spawn(matrices+16*i,leader,&out->members[i]))break;
        ++out->count;
        if(leader)out->leaders[out->leaderCount++]=out->members[i];
        else {
            bool followed=false;
            __try {
                void* object=const_cast<void*>(out->members[i].obj);
                void* top=const_cast<void*>(out->leaders[out->leaderCount-1].obj);
                follow(object,top,false);
                followed=At<void*>(object,0x548)==top;
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
            if(!followed){failure=SupportSpawnFailure::setup;break;}
        }
        if(i+1==count)return true;
    }
    const SupportSpawnFailure why=failure;
    for(unsigned i=out->count;i>0;--i)DeleteSupportSoldier(out->members[i-1]);
    *out=SupportSquad{};failure=why;
    return false;
}
}
