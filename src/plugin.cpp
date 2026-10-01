// EDF6VehicleImpact: when a Balam lands a melee hit, detonate an explosion at the fist;
// when a Balam is destroyed, detonate a bigger one where it stands.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46; see docs/re-notes.md.
#include <Windows.h>
#include <intrin.h>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#pragma warning(push)
#pragma warning(disable:4201)
#include "PluginAPI.h"
#pragma warning(pop)
#include "memory.h"

namespace vimpact {
unsigned char* image=nullptr;
HMODULE module=nullptr;
wchar_t logPath[MAX_PATH]{};
wchar_t iniPath[MAX_PATH]{};

struct Config {
    bool enabled=true;
    bool playerOnly=false;        // false: NPC-driven Balams trigger the punch blast too
    float damageScale=0.5f;       // punch blast damage = punch damage * scale
    float explosion=30.0f;        // punch blast radius in metres
    DWORD cooldownMs=250;         // per vehicle
    float deathExplosion=100.0f;  // destruction blast radius; 0 disables it
    float deathDamage=2000.0f;
    float deathLift=10.0f;        // metres above the wreck's origin
};
Config cfg{};
FILETIME iniStamp{};
ULONGLONG iniCheckedAt=0;

// --- EDF.dll layout ---
constexpr unsigned kHitCall=0x60C017,kHitTarget=0x6C7DB0;   // Vehicle501 Update -> AttackColl::GetHitFlags
constexpr unsigned kSystem=0x20B2958,kGetFactory=0x1195C50,kSpawn=0x1194280;
constexpr unsigned kIpCtor=0x1001D0,kIpDtor=0x101510;
constexpr unsigned kSoldierVtables[]={0x17CDF28,0x17D0FF8,0x17CF100,0x17CF5B8};
// Vehicle501_FortressRobo; vtable slot 9 is its message handler (damage arrives there)
constexpr unsigned kBalamVtable=0x17D98C8,kBalamMessage=0x60ACC0;
constexpr std::size_t kMessageSlot=9;
constexpr std::size_t kAttackComp=0x2350,kSeats=0x608,kRider=0x260,kSelfPtr=0x28,kSelfCtrl=0x30;
// GameObjectBase
constexpr std::size_t kPosition=0x90,kObjectId=0x314,kDead=0x2E8;
// AttackColl group node (std::map node, value inline)
constexpr std::size_t kNodeActive=0x138,kNodeFlags=0x63,kNodePos=0x90,kNodeDamage=0xB0;
constexpr std::uint8_t kFlagHit=0x20;
// BulletBase::InitParam (0x1E0 bytes)
constexpr std::size_t kIpSize=0x1E0,kIpOwner=0x38,kIpOwnerCtrl=0x40,kIpFrame=0x70,kIpShooterId=0x90;
constexpr std::size_t kIpSpeed=0x94,kIpAlive=0x98,kIpDamage=0x9C,kIpExplosion=0xB0;
constexpr std::size_t kIpGravity=0xE0,kIpCustom=0xE8,kIpCustomTag=0xF8,kIpSeed=0x1A0;
// SolidExpBullet01: needs no AmmoModel and carries an explosion. Its BulletBase flags live
// at bullet+0xC34; bit 0x20 makes the end-of-life path detonate in place (EDF+0x2363B0)
// instead of vanishing, which SolidExp alone never sets (it only bursts on contact).
const wchar_t kBlastBullet[]=L"SolidExpBullet01";
constexpr unsigned kBlastVtable=0x17A4498;
constexpr std::size_t kBulletFlags=0xC34;
constexpr std::uint32_t kDetonateOnExpiry=0x20;

using HitFlagsFn=std::uint8_t*(__fastcall*)(void*,std::uint8_t*);
using MessageFn=bool(__fastcall*)(void*,std::uintptr_t,std::uintptr_t,std::uintptr_t);
using GetFactoryFn=void*(__fastcall*)(void*,const wchar_t*);
using SpawnFn=void*(__fastcall*)(void*,const float*,void*,void*);
using IpCtorFn=void*(__fastcall*)(void*);
using IpDtorFn=void*(__fastcall*)(void*,unsigned);
HitFlagsFn originalHitFlags=nullptr;
MessageFn originalMessage=nullptr;

// Ammo_CustomParameter as a raw SGO node: {type 0 = array, count 0, children at +12}.
// Every weapon hands its bullet such a node; an absent one (tag 0xFFFF) throws
// bad_variant_access in the bullet ctor.
const std::int32_t kEmptyCustom[3]={0,0,12};

// Follow-frame matrices handed to bullets via ip+0x70; a ring so live bullets keep theirs.
alignas(16) float frames[32][16]{};
unsigned nextFrame=0;

struct Cooldown { const void* vehicle; ULONGLONG at; };
Cooldown cooldowns[8]{};

void Log(const char* format,...) noexcept {
    if(!logPath[0])return;
    char text[1000]{};va_list args;va_start(args,format);vsnprintf_s(text,sizeof(text),_TRUNCATE,format,args);va_end(args);
    FILE* f=nullptr;if(_wfopen_s(&f,logPath,L"ab") || !f)return;
    SYSTEMTIME t{};GetLocalTime(&t);
    fprintf(f,"[%02u:%02u:%02u] %s\r\n",t.wHour,t.wMinute,t.wSecond,text);fclose(f);
}

template<class T> T At(const void* base,std::size_t offset) noexcept {
    T value;std::memcpy(&value,static_cast<const unsigned char*>(base)+offset,sizeof(T));return value;
}
template<class T> void Put(void* base,std::size_t offset,T value) noexcept {
    std::memcpy(static_cast<unsigned char*>(base)+offset,&value,sizeof(T));
}

bool PlayerSoldier(const void* object) noexcept {
    if(!Readable(object,sizeof(void*)))return false;
    const auto vtable=At<const unsigned char*>(object,0);
    for(auto rva:kSoldierVtables)if(vtable==image+rva)return true;
    return false;
}

bool PlayerDriven(const unsigned char* vehicle) noexcept {
    if(!Readable(vehicle,kSeats+8))return false;
    const auto seats=At<const unsigned char*>(vehicle,kSeats);
    if(!Readable(seats,kRider+8))return false;
    return PlayerSoldier(At<const void*>(seats,kRider));
}

bool CoolingDown(const void* vehicle,ULONGLONG now) noexcept {
    Cooldown* slot=&cooldowns[0];
    for(auto& c:cooldowns) {
        if(c.vehicle==vehicle){slot=&c;break;}
        if(c.at<slot->at)slot=&c;
    }
    if(slot->vehicle==vehicle && now-slot->at<cfg.cooldownMs)return true;
    *slot={vehicle,now};return false;
}

void FillParam(unsigned char* ip,const unsigned char* vehicle,float damage,float radius,const float* frame) noexcept {
    Put<std::uint32_t>(ip,kIpShooterId,At<std::uint32_t>(vehicle,kObjectId)); // never hits its own vehicle
    Put<float>(ip,kIpSpeed,0.0f);
    Put<std::int32_t>(ip,kIpAlive,1);     // expires on its first update
    Put<float>(ip,kIpDamage,damage);
    Put<float>(ip,kIpExplosion,radius);
    Put<float>(ip,kIpGravity,0.0f);
    Put<const void*>(ip,kIpCustom,kEmptyCustom);
    Put<std::uint16_t>(ip,kIpCustomTag,1);
    Put<const float*>(ip,kIpFrame,frame);
    Put<std::uint64_t>(ip,kIpSeed,__rdtsc());
    // Owner weak_ptr = the vehicle's own weak self reference; the IP dtor releases it.
    const auto self=At<void*>(vehicle,kSelfPtr);
    const auto ctrl=At<unsigned char*>(vehicle,kSelfCtrl);
    if(self && Readable(ctrl,0x10,true)) {
        _InterlockedIncrement(reinterpret_cast<volatile long*>(ctrl+0xC));
        Put<void*>(ip,kIpOwner,self);Put<void*>(ip,kIpOwnerCtrl,ctrl);
    }
}

// A motionless one-frame explosive round at `position`: on its first update it expires
// and detonates in place through the game's own explosion path, so the blast is immediate.
void* Detonate(const unsigned char* vehicle,const float* position,float damage,float radius) noexcept {
    __try {
        void* system=At<void*>(image,kSystem);
        if(!system)return nullptr;
        void* factory=reinterpret_cast<GetFactoryFn>(image+kGetFactory)(system,kBlastBullet);
        if(!factory){Log("no bullet factory named %ls",kBlastBullet);return nullptr;}
        float* frame=frames[nextFrame++%32];
        const float m[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, position[0],position[1],position[2],1};
        std::memcpy(frame,m,sizeof(m));
        alignas(16) float matrix[16];std::memcpy(matrix,m,sizeof(m));
        alignas(16) unsigned char ip[kIpSize];
        reinterpret_cast<IpCtorFn>(image+kIpCtor)(ip);
        FillParam(ip,vehicle,damage,radius,frame);
        auto bullet=static_cast<unsigned char*>(reinterpret_cast<SpawnFn>(image+kSpawn)(system,matrix,factory,ip));
        reinterpret_cast<IpDtorFn>(image+kIpDtor)(ip,0);
        if(bullet && At<const unsigned char*>(bullet,0)==image+kBlastVtable)
            Put<std::uint32_t>(bullet,kBulletFlags,At<std::uint32_t>(bullet,kBulletFlags)|kDetonateOnExpiry);
        return bullet;
    } __except(EXCEPTION_EXECUTE_HANDLER){Log("SPAWN faulted");return nullptr;}
}

// First active group whose sweep hit something this frame. In-order walk of an MSVC std::map.
const unsigned char* HitGroup(const unsigned char* comp) noexcept {
    const auto head=At<const unsigned char*>(comp,0x10);
    if(!Readable(head,0x20))return nullptr;
    const unsigned char* node=At<const unsigned char*>(head,0);
    for(unsigned guard=0;node!=head && guard<64;++guard) {
        if(!Readable(node,kNodeActive+1))return nullptr;
        if(node[kNodeActive] && (node[kNodeFlags]&kFlagHit))return node;
        auto right=At<const unsigned char*>(node,0x10);
        if(!right[0x19]) {
            for(node=right;!At<const unsigned char*>(node,0)[0x19];)node=At<const unsigned char*>(node,0);
        } else {
            auto parent=At<const unsigned char*>(node,8);
            while(!parent[0x19] && node==At<const unsigned char*>(parent,0x10)){node=parent;parent=At<const unsigned char*>(parent,8);}
            node=parent;
        }
    }
    return nullptr;
}

void OnHit(void* comp) noexcept {
    __try {
        const auto vehicle=static_cast<const unsigned char*>(comp)-kAttackComp;
        const bool player=PlayerDriven(vehicle);
        if(cfg.playerOnly && !player)return;
        const auto group=HitGroup(static_cast<const unsigned char*>(comp));
        if(!group || CoolingDown(vehicle,GetTickCount64()))return;
        float position[4];std::memcpy(position,group+kNodePos,sizeof(position));
        for(float v:position)if(!std::isfinite(v))return;
        const float damage=At<float>(group,kNodeDamage)*cfg.damageScale;
        void* bullet=Detonate(vehicle,position,damage,cfg.explosion);
        Log("BLAST at (%.1f, %.1f, %.1f) player=%d damage=%.0f explosion=%.1f bullet=%p",
            position[0],position[1],position[2],player,damage,cfg.explosion,bullet);
    } __except(EXCEPTION_EXECUTE_HANDLER){Log("OnHit faulted");}
}

void OnDeath(const unsigned char* vehicle) noexcept {
    __try {
        float position[4];std::memcpy(position,vehicle+kPosition,sizeof(position));
        position[1]+=cfg.deathLift;
        for(int i=0;i<3;++i)if(!std::isfinite(position[i]))return;
        void* bullet=Detonate(vehicle,position,cfg.deathDamage,cfg.deathExplosion);
        Log("DEATH BLAST at (%.1f, %.1f, %.1f) damage=%.0f explosion=%.1f bullet=%p",
            position[0],position[1],position[2],cfg.deathDamage,cfg.deathExplosion,bullet);
    } __except(EXCEPTION_EXECUTE_HANDLER){Log("OnDeath faulted");}
}

void ReloadConfigIfChanged() noexcept;

std::uint8_t* __fastcall HookHitFlags(void* comp,std::uint8_t* out) {
    auto result=originalHitFlags(comp,out);
    if(*out&kFlagHit)ReloadConfigIfChanged();
    if(cfg.enabled && (*out&kFlagHit))OnHit(comp);
    return result;
}

// Damage reaches GameObjectBase through the message handler (EDF+0x54A530), which sets the
// dead byte at +0x2E8 when life runs out. A 0 -> 1 change across the call is the kill.
bool __fastcall HookMessage(void* object,std::uintptr_t message,std::uintptr_t param,std::uintptr_t extra) {
    const auto vehicle=static_cast<const unsigned char*>(object);
    const bool wasDead=vehicle[kDead]!=0;
    const bool result=originalMessage(object,message,param,extra);
    if(!wasDead && vehicle[kDead]) {
        ReloadConfigIfChanged();
        if(cfg.enabled && cfg.deathExplosion>0.0f)OnDeath(vehicle);
    }
    return result;
}

bool Matches(std::size_t rva,const unsigned char* bytes,std::size_t size) noexcept {
    return std::memcmp(image+rva,bytes,size)==0;
}

bool CheckProfile(HMODULE handle) noexcept {
    __try {
        auto base=reinterpret_cast<unsigned char*>(handle);
        if(!Readable(base,0x1000))return false;
        auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<0 || dos->e_lfanew>0x800)return false;
        auto pe=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
        if(pe->Signature!=IMAGE_NT_SIGNATURE || pe->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64
           || pe->FileHeader.TimeDateStamp!=0x678CCB46 || pe->OptionalHeader.SizeOfImage!=0x22CE000)return false;
        image=base;
        const unsigned char hitCall[]={0xE8,0x94,0xBD,0x0B,0x00};
        const unsigned char hitFlags[]={0x48,0x8B,0x49,0x10,0x4C,0x8B,0xCA,0xC6};
        const unsigned char getFactory[]={0x48,0x89,0x5C,0x24,0x18,0x55,0x57,0x41,0x57,0x48,0x83,0xEC};
        const unsigned char spawn[]={0x40,0x53,0x55,0x56,0x57,0x41,0x56,0x48,0x83,0xEC,0x40,0x49};
        const unsigned char ipCtor[]={0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x48,0x89};
        const unsigned char ipDtor[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x8B,0xDA};
        const unsigned char message[]={0x48,0x89,0x5C,0x24,0x20,0x57,0x48,0x81,0xEC,0x80,0x00,0x00};
        const unsigned char deadByte[]={0x0F,0xB6,0x9F,0xE8,0x02,0x00,0x00};     // movzx ebx,[rdi+2E8]
        const unsigned char blastFlags[]={0x83,0xA7,0x34,0x0C,0x00,0x00,0xEF};   // and [rdi+C34],~10
        const unsigned char expiryTest[]={0xF6,0xC1,0x20};                      // test cl,20
        const bool ok=Matches(kHitCall,hitCall,sizeof(hitCall)) && Matches(kHitTarget,hitFlags,sizeof(hitFlags))
            && Matches(kGetFactory,getFactory,sizeof(getFactory)) && Matches(kSpawn,spawn,sizeof(spawn))
            && Matches(kIpCtor,ipCtor,sizeof(ipCtor)) && Matches(kIpDtor,ipDtor,sizeof(ipDtor))
            && Matches(kBalamMessage,message,sizeof(message)) && Matches(0x54A579,deadByte,sizeof(deadByte))
            && Matches(0x28DD06,blastFlags,sizeof(blastFlags)) && Matches(0x2363B0,expiryTest,sizeof(expiryTest));
        if(!ok)image=nullptr;
        return ok;
    } __except(EXCEPTION_EXECUTE_HANDLER){image=nullptr;return false;}
}

// Swap one vtable entry, only if it still holds the expected function.
bool PatchVtableSlot(void** slot,void* expected,void* replacement) noexcept {
    DWORD old=0;
    if(!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old))return false;
    const bool ok=InterlockedCompareExchangePointer(slot,replacement,expected)==expected;
    VirtualProtect(slot,sizeof(void*),old,&old);
    return ok;
}

// Comma-separated floats; returns how many were parsed (at most max).
int ReadFloats(const wchar_t* key,float* out,int max) noexcept {
    wchar_t text[256]{};
    GetPrivateProfileStringW(L"VehicleImpact",key,L"",text,256,iniPath);
    int count=0;
    for(wchar_t* p=text;*p && count<max;) {
        wchar_t* end=nullptr;
        const float value=std::wcstof(p,&end);
        if(end==p || !std::isfinite(value))break;
        out[count++]=value;
        p=end;
        while(*p==L',' || *p==L' ' || *p==L'\t')++p;
    }
    return count;
}

float ReadFloat(const wchar_t* key,float fallback) noexcept {
    float value=fallback;
    return ReadFloats(key,&value,1)==1 ? value : fallback;
}

void LoadConfig() noexcept {
    Config next{};
    next.enabled=GetPrivateProfileIntW(L"VehicleImpact",L"Enabled",1,iniPath)!=0;
    next.playerOnly=GetPrivateProfileIntW(L"VehicleImpact",L"PlayerOnly",0,iniPath)!=0;
    next.damageScale=ReadFloat(L"DamageScale",next.damageScale);
    next.explosion=ReadFloat(L"Explosion",next.explosion);
    next.cooldownMs=GetPrivateProfileIntW(L"VehicleImpact",L"CooldownMs",next.cooldownMs,iniPath);
    next.deathExplosion=ReadFloat(L"DeathExplosion",next.deathExplosion);
    next.deathDamage=ReadFloat(L"DeathDamage",next.deathDamage);
    next.deathLift=ReadFloat(L"DeathLift",next.deathLift);
    cfg=next;
    Log("CONFIG enabled=%d playerOnly=%d explosion=%.1f damageScale=%.2f death=%.1f deathDamage=%.0f deathLift=%.1f",
        cfg.enabled,cfg.playerOnly,cfg.explosion,cfg.damageScale,cfg.deathExplosion,cfg.deathDamage,cfg.deathLift);
}

FILETIME IniStamp() noexcept {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    return GetFileAttributesExW(iniPath,GetFileExInfoStandard,&data) ? data.ftLastWriteTime : FILETIME{};
}

// Edits to the ini apply on the next hit or kill; no game restart needed.
void ReloadConfigIfChanged() noexcept {
    const auto now=GetTickCount64();
    if(now-iniCheckedAt<1000)return;
    iniCheckedAt=now;
    const auto stamp=IniStamp();
    if(CompareFileTime(&stamp,&iniStamp)==0)return;
    iniStamp=stamp;
    LoadConfig();
}
}  // namespace vimpact

extern "C" __declspec(dllexport) bool EDFMLAPI EML6_Load(PluginInfo* info) {
    using namespace vimpact;
    if(!info)return false;
    GetModuleFileNameW(module,iniPath,MAX_PATH);
    auto dot=wcsrchr(iniPath,L'.');if(!dot)return false;
    wcscpy_s(dot,MAX_PATH-(dot-iniPath),L".ini");
    wcscpy_s(logPath,iniPath);
    dot=wcsrchr(logPath,L'.');wcscpy_s(dot,MAX_PATH-(dot-logPath),L".log");
    info->infoVersion=PluginInfo::MaxInfoVer;info->name="EDF6 Vehicle Impact";info->version=PLUG_VER(0,3,0,0);
    Log("EDF6VehicleImpact 0.3.0 loading");
    iniStamp=IniStamp();
    LoadConfig();
    if(!CheckProfile(GetModuleHandleW(L"EDF.dll"))){Log("REFUSED: unsupported EDF.dll or conflicting patch");return false;}
    originalHitFlags=reinterpret_cast<HitFlagsFn>(image+kHitTarget);
    bool changed=false;
    const bool hit=RedirectCall(image+kHitCall,reinterpret_cast<void*>(originalHitFlags),
                                reinterpret_cast<void*>(&HookHitFlags),changed);
    originalMessage=reinterpret_cast<MessageFn>(image+kBalamMessage);
    auto slot=reinterpret_cast<void**>(image+kBalamVtable)+kMessageSlot;
    const bool death=PatchVtableSlot(slot,reinterpret_cast<void*>(originalMessage),reinterpret_cast<void*>(&HookMessage));
    Log("HOOK hit=%d changed=%d site=60C017 death=%d",hit,changed,death);
    return hit || changed || death;  // never unload code a patched call or slot points at
}

BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH){vimpact::module=instance;DisableThreadLibraryCalls(instance);}
    return TRUE;
}
