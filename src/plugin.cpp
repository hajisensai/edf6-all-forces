// EDF6VehicleImpact: when a player-driven Balam lands a melee hit, spawn the
// Fencer hammer's charged shockwave bullet (ShockWaveBullet01Exp) at the fist.
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
    float damageScale=0.5f;   // shockwave damage = punch damage * scale
    float radius=12.0f;       // explosion radius in metres
    float speed=2.0f;         // hammer default
    int aliveFrames=60;       // hammer charged knock
    DWORD cooldownMs=250;     // per vehicle
    wchar_t bullet[64]=L"ShockWaveBullet01Exp";
};
Config cfg{};

// --- EDF.dll layout ---
constexpr unsigned kHitCall=0x60C017,kHitTarget=0x6C7DB0;   // Vehicle501 Update -> AttackColl::GetHitFlags
constexpr unsigned kSystem=0x20B2958,kGetFactory=0x1195C50,kSpawn=0x1194280;
constexpr unsigned kIpCtor=0x1001D0,kIpDtor=0x101510;
constexpr unsigned kSoldierVtables[]={0x17CDF28,0x17D0FF8,0x17CF100,0x17CF5B8};
// Vehicle501_FortressRobo
constexpr std::size_t kAttackComp=0x2350,kSeats=0x608,kRider=0x260,kSelfPtr=0x28,kSelfCtrl=0x30;
constexpr std::size_t kMatrix=0x60,kObjectId=0x314;
// AttackColl group node (std::map node, value inline)
constexpr std::size_t kNodeActive=0x138,kNodeFlags=0x63,kNodePos=0x90,kNodeDamage=0xB0;
constexpr std::uint8_t kFlagHit=0x20;
// BulletBase::InitParam (0x1E0 bytes)
constexpr std::size_t kIpSize=0x1E0,kIpOwner=0x38,kIpOwnerCtrl=0x40,kIpFrame=0x70,kIpShooterId=0x90;
constexpr std::size_t kIpSpeed=0x94,kIpAlive=0x98,kIpDamage=0x9C,kIpSize_=0xC0,kIpHitSizeAdj=0xC4;
constexpr std::size_t kIpColor=0xD0,kIpGravity=0xE0,kIpCustom=0xE8,kIpCustomTag=0xF8,kIpSeed=0x1A0;
constexpr float kHitSizeAdjust=1.75f,kExpRadiusMul=1.25f;

using HitFlagsFn=std::uint8_t*(__fastcall*)(void*,std::uint8_t*);
using GetFactoryFn=void*(__fastcall*)(void*,const wchar_t*);
using SpawnFn=void*(__fastcall*)(void*,const float*,void*,void*);
using IpCtorFn=void*(__fastcall*)(void*);
using IpDtorFn=void*(__fastcall*)(void*,unsigned);
HitFlagsFn originalHitFlags=nullptr;

// Ammo_CustomParameter [1.0, 30.0] as a raw SGO node tree: {type,a,b}, type 0 = array
// (count a, children at node+b), type 2 = float. The bullet keeps a pointer to it.
const std::int32_t kCustomParam[9]={0,2,12, 2,0,0x3F800000, 2,0,0x41F00000};
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

// Row-major world matrix: right, up, forward, position. Rotation comes from the vehicle.
void BuildMatrix(const unsigned char* vehicle,const float* position,float* m) noexcept {
    std::memcpy(m,vehicle+kMatrix,sizeof(float)*16);
    for(int row=0;row<3;++row) {
        float* r=m+row*4;
        const float len=std::sqrt(r[0]*r[0]+r[1]*r[1]+r[2]*r[2]);
        if(len>1e-4f){r[0]/=len;r[1]/=len;r[2]/=len;}
        r[3]=0.0f;
    }
    m[12]=position[0];m[13]=position[1];m[14]=position[2];m[15]=1.0f;
}

void FillParam(unsigned char* ip,const unsigned char* vehicle,float damage,const float* frame) noexcept {
    Put<std::uint32_t>(ip,kIpShooterId,At<std::uint32_t>(vehicle,kObjectId)); // never hits its own vehicle
    Put<float>(ip,kIpSpeed,cfg.speed);
    Put<std::int32_t>(ip,kIpAlive,cfg.aliveFrames);
    Put<float>(ip,kIpDamage,damage);
    Put<float>(ip,kIpSize_,cfg.radius/(kHitSizeAdjust*kExpRadiusMul));
    Put<float>(ip,kIpHitSizeAdj,kHitSizeAdjust);
    const float color[4]={1.3f,1.5f,1.0f,1.0f};
    std::memcpy(ip+kIpColor,color,sizeof(color));
    Put<float>(ip,kIpGravity,0.0f);
    Put<const void*>(ip,kIpCustom,kCustomParam);
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

void* SpawnWave(const unsigned char* vehicle,const float* position,float damage) noexcept {
    __try {
        void* system=At<void*>(image,kSystem);
        if(!system)return nullptr;
        void* factory=reinterpret_cast<GetFactoryFn>(image+kGetFactory)(system,cfg.bullet);
        if(!factory){Log("no bullet factory named %ls",cfg.bullet);return nullptr;}
        float* frame=frames[nextFrame++%32];
        BuildMatrix(vehicle,position,frame);
        alignas(16) float matrix[16];std::memcpy(matrix,frame,sizeof(matrix));
        alignas(16) unsigned char ip[kIpSize];
        reinterpret_cast<IpCtorFn>(image+kIpCtor)(ip);
        FillParam(ip,vehicle,damage,frame);
        void* bullet=reinterpret_cast<SpawnFn>(image+kSpawn)(system,matrix,factory,ip);
        reinterpret_cast<IpDtorFn>(image+kIpDtor)(ip,0);
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
        static unsigned seen=0;
        if(seen<16){++seen;Log("HIT vehicle=%p playerDriven=%d",vehicle,player);}
        if(!player)return;
        const auto group=HitGroup(static_cast<const unsigned char*>(comp));
        if(!group || CoolingDown(vehicle,GetTickCount64()))return;
        float position[4];std::memcpy(position,group+kNodePos,sizeof(position));
        for(float v:position)if(!std::isfinite(v))return;
        const float damage=At<float>(group,kNodeDamage)*cfg.damageScale;
        void* bullet=SpawnWave(vehicle,position,damage);
        Log("WAVE at (%.1f, %.1f, %.1f) damage=%.0f radius=%.1f bullet=%p",
            position[0],position[1],position[2],damage,cfg.radius,bullet);
    } __except(EXCEPTION_EXECUTE_HANDLER){Log("OnHit faulted");}
}

std::uint8_t* __fastcall HookHitFlags(void* comp,std::uint8_t* out) {
    auto result=originalHitFlags(comp,out);
    if(cfg.enabled && (*out&kFlagHit))OnHit(comp);
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
        const bool ok=Matches(kHitCall,hitCall,sizeof(hitCall)) && Matches(kHitTarget,hitFlags,sizeof(hitFlags))
            && Matches(kGetFactory,getFactory,sizeof(getFactory)) && Matches(kSpawn,spawn,sizeof(spawn))
            && Matches(kIpCtor,ipCtor,sizeof(ipCtor)) && Matches(kIpDtor,ipDtor,sizeof(ipDtor))
            && std::wcscmp(reinterpret_cast<const wchar_t*>(image+0x17A28B8),L"ShockWaveBullet01Exp")==0;
        if(!ok)image=nullptr;
        return ok;
    } __except(EXCEPTION_EXECUTE_HANDLER){image=nullptr;return false;}
}

float ReadFloat(const wchar_t* key,float fallback) noexcept {
    wchar_t text[32]{};
    GetPrivateProfileStringW(L"VehicleImpact",key,L"",text,32,iniPath);
    if(!text[0])return fallback;
    const float value=static_cast<float>(_wtof(text));
    return std::isfinite(value) && value>0.0f ? value : fallback;
}

void LoadConfig() noexcept {
    cfg.enabled=GetPrivateProfileIntW(L"VehicleImpact",L"Enabled",1,iniPath)!=0;
    cfg.damageScale=ReadFloat(L"DamageScale",cfg.damageScale);
    cfg.radius=ReadFloat(L"Radius",cfg.radius);
    cfg.speed=ReadFloat(L"Speed",cfg.speed);
    cfg.aliveFrames=static_cast<int>(GetPrivateProfileIntW(L"VehicleImpact",L"AliveFrames",cfg.aliveFrames,iniPath));
    cfg.cooldownMs=GetPrivateProfileIntW(L"VehicleImpact",L"CooldownMs",cfg.cooldownMs,iniPath);
    GetPrivateProfileStringW(L"VehicleImpact",L"Bullet",L"ShockWaveBullet01Exp",cfg.bullet,64,iniPath);
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
    LoadConfig();
    info->infoVersion=PluginInfo::MaxInfoVer;info->name="EDF6 Vehicle Impact";info->version=PLUG_VER(0,1,0,0);
    Log("EDF6VehicleImpact 0.1.0 loading; enabled=%d bullet=%ls radius=%.1f damageScale=%.2f",
        cfg.enabled,cfg.bullet,cfg.radius,cfg.damageScale);
    if(!CheckProfile(GetModuleHandleW(L"EDF.dll"))){Log("REFUSED: unsupported EDF.dll or conflicting patch");return false;}
    originalHitFlags=reinterpret_cast<HitFlagsFn>(image+kHitTarget);
    bool changed=false;
    const bool ok=RedirectCall(image+kHitCall,reinterpret_cast<void*>(originalHitFlags),
                               reinterpret_cast<void*>(&HookHitFlags),changed);
    Log("HOOK ready=%d changed=%d site=60C017",ok,changed);
    return ok || changed;  // never unload code a patched call points at
}

BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH){vimpact::module=instance;DisableThreadLibraryCalls(instance);}
    return TRUE;
}
