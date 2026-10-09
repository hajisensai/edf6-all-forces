// Native ground support creation, EDF.dll TimeDateStamp 0x678CCB46. The independent setup path is
// the first half of RideAi(true), already used by playerjet.cpp for an empty catch aircraft.
#include "support_spawn.h"
#include "crew.h"
#include "memory.h"
#include "online_authority.h"
#include <cmath>

namespace crew {
namespace {
constexpr unsigned kPreload=0x7A3780,kCreate=0x11945E0,kDelete=0x118A1B0,kReadSetup=0x62D6E0;
constexpr unsigned kInitVtable=0x1762068,kSetupDtors=0x1765220,kSetLevel=0x54E740;
constexpr std::size_t kPreloadMgr=0x20B29A8,kObjectMgr=0x20B2958,kApplySetupSlot=46;
const unsigned char kPreloadSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48};
const unsigned char kCreateSig[]={0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x8D,0x6C,0x24,0xD9};
const unsigned char kDeleteSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x60,0x48};
const unsigned char kReadSetupSig[]={0x48,0x89,0x5C,0x24,0x18,0x56,0x57,0x41,0x56,0x48,0x83,0xEC,0x40,0x48,0x8B,0xDA};
const unsigned char kSetTeamSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x41};
const unsigned char kSetLevelSig[]={0x48,0x8B,0xC4,0x48,0x89,0x58,0x18,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x8D};
struct alignas(16) InitParam { const void* vtable; unsigned char rest[0x28]; };
using CreateFn=unsigned char*(*)(void*,const float*,const wchar_t*,InitParam*);
using SetupFn=void(__fastcall*)(void*,void*);
bool profile=false,preloaded[kSupportVehicleCount]{},broken[kSupportVehicleCount]{};
// Only support-created objects may be rolled back; ObjRef rejects expired/reused addresses.
ObjRef created[64]{};

bool CheckProfile() noexcept {
    return image && Matches(kPreload,kPreloadSig,sizeof(kPreloadSig)) &&
        Matches(kCreate,kCreateSig,sizeof(kCreateSig)) && Matches(kDelete,kDeleteSig,sizeof(kDeleteSig)) &&
        Matches(kReadSetup,kReadSetupSig,sizeof(kReadSetupSig)) && Matches(kSetTeam,kSetTeamSig,sizeof(kSetTeamSig)) &&
        Matches(kSetLevel,kSetLevelSig,sizeof(kSetLevelSig)) && Readable(image+kInitVtable,8);
}
bool Finite3(const float* p) noexcept {
    return p && std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]);
}
bool NativeDelete(unsigned char* v) noexcept {
    __try { reinterpret_cast<void(*)(void*)>(image+kDelete)(v);return true; }
    __except(EXCEPTION_EXECUTE_HANDLER){Log("SUPPORT ground: native rollback fault for %p",v);return false;}
}
// Applies setup and destroys its temporary variant even if apply fails. An unreadable destructor
// disables this resource; it is never silently treated as a ready unarmed/unconfigured vehicle.
bool ApplySetup(unsigned char* v) noexcept {
    alignas(16) unsigned char setup[0x40]{},scratch[0x40]{};
    Put<std::uint16_t>(setup,0x10,0xFFFF);
    bool applied=false,disposed=false;
    __try {
        reinterpret_cast<SetupFn>(image+kReadSetup)(v,setup);
        const auto vt=At<SetupFn const*>(v,0);
        if(Readable(vt,(kApplySetupSlot+1)*sizeof(void*)) && vt[kApplySetupSlot]) {
            vt[kApplySetupSlot](v,setup);applied=true;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){Log("SUPPORT ground: native mission setup fault for %p",v);}
    __try {
        const auto type=At<std::uint16_t>(setup,0x10);
        if(type==0xFFFF)disposed=true;
        else {
            const auto table=reinterpret_cast<SetupFn const*>(image+kSetupDtors);
            if(Readable(table+type,sizeof(void*)) && table[type]) {
                table[type](setup,scratch);disposed=true;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){Log("SUPPORT ground: setup disposal fault for %p",v);}
    return applied && disposed;
}
}  // namespace

void PreloadSupportVehicles() noexcept {
    for(auto& p:preloaded)p=false;
    for(auto& ref:created)ref=ObjRef{};
    profile=CheckProfile();
    if(!profile){Log("SUPPORT ground: native spawn profile mismatch, disabled");return;}
    __try {
        const auto mgr=At<void*>(image,kPreloadMgr);
        if(!mgr)return;
        for(int i=0;i<kSupportVehicleCount;++i) {
            if(broken[i])continue;
            // Stock Root.cpk resources, not optional mod files. Preload traverses their models,
            // ragdolls and weapon resources just as a script's PreloadVehicleResource does.
            reinterpret_cast<void(*)(void*,const wchar_t*,std::int32_t,std::int32_t)>(image+kPreload)(mgr,kSupportVehicles[i].sgo,2,-1);
            preloaded[i]=true;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        for(auto& p:preloaded)p=false;
        Log("SUPPORT ground: mission preload fault, all ground calls disabled for this mission");
    }
}

bool SupportVehicleReady(SupportVehicleKind kind,SupportCrewMode mode) noexcept {
    const auto row=SupportVehicleInfo(kind);
    return row && profile && SupportVehicleModeAvailable(mode) && preloaded[static_cast<unsigned>(kind)] &&
           !broken[static_cast<unsigned>(kind)];
}

unsigned char* SpawnSupportVehicle(SupportVehicleKind kind,SupportCrewMode mode,const float* entry,
                                   const float* heading,const void* owner) noexcept {
    if(!SupportVehicleReady(kind,mode) || !Finite3(entry) || !Finite3(heading))return nullptr;
    const float length=std::hypot(heading[0],heading[2]);
    if(!std::isfinite(length) || length<0.001f)return nullptr;
    ObjRef* slot=nullptr;
    for(auto& ref:created)if(!ref){slot=&ref;break;}
    if(!slot)return nullptr;
    const auto row=SupportVehicleInfo(kind);
    const auto index=static_cast<unsigned>(kind);
    const float x=heading[0]/length,z=heading[2]/length;
    alignas(16) const float matrix[]={z,0,-x,0, 0,1,0,0, x,0,z,0, entry[0],entry[1],entry[2],1};
    InitParam param{image+kInitVtable,{}};
    unsigned char* v=nullptr;
    bool initialized=false;
    __try {
        const auto mgr=At<void*>(image,kObjectMgr);
        if(!mgr)return nullptr;
        v=reinterpret_cast<CreateFn>(image+kCreate)(mgr,matrix,row->sgo,&param);
        if(!v)return nullptr;
        if(At<const void*>(v,0)==image+row->vtable && SeatCount(v)==row->seats && ApplySetup(v)) {
            reinterpret_cast<void(*)(void*,std::int32_t,bool)>(image+kSetTeam)(v,2,true);
            reinterpret_cast<void(__fastcall*)(void*,float)>(image+kSetLevel)(v,1.0f);
            NoteLocalCopy(v,owner);
            *slot=ObjRef::Of(v);
            initialized=slot->ctrl && slot->Is(v);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("SUPPORT ground: native creation fault for %ls, disabled until restart",row->sgo);
    }
    if(!initialized) {
        broken[index]=true;preloaded[index]=false;
        if(v)NativeDelete(v);
        *slot=ObjRef{};
        return nullptr;
    }
    Log("SUPPORT ground: %ls hull=%p seats=%u at verified entry (%.1f,%.1f,%.1f), awaiting real crew",
        row->sgo,v,row->seats,entry[0],entry[1],entry[2]);
    return v;
}

// airdrop.cpp: a vehicle the game's container made for the plugin's transport plane gets the same setup step.
bool ApplySupportVehicleSetup(unsigned char* vehicle) noexcept {return profile && vehicle && ApplySetup(vehicle);}

bool DeleteSupportVehicle(unsigned char* v) noexcept {
    if(!v || !profile)return false;
    __try {
    for(auto& ref:created)if(ref.Is(v)) {
        // A transaction must remove its real crew first. Never destroy a player's occupied vehicle.
        for(unsigned seat=0;seat<SeatCount(v);++seat)if(SeatRider(SeatAt(v,seat))!=Rider::none)return false;
        if(!NativeDelete(v))return false;
        ref=ObjRef{};return true;
    }
    } __except(EXCEPTION_EXECUTE_HANDLER){Log("SUPPORT ground: expired rollback reference %p",v);}
    return false;
}
}  // namespace crew
