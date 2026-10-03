// EDF6VehicleCrew: NPC drivers for empty friendly vehicles (helicopters flown by the plugin), and
// the player can board any seat an NPC holds, bumping the NPC to a gunner seat.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46; see docs/re-notes.md.
#include <Windows.h>
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
#include "crew.h"
#include "memory.h"

namespace crew {
unsigned char* image=nullptr;
Config cfg{};
PlayerFix player{};
namespace {
HMODULE module=nullptr;
wchar_t logPath[MAX_PATH]{};
wchar_t iniPath[MAX_PATH]{};
FILETIME iniStamp{};
ULONGLONG iniCheckedAt=0;

struct Signature { std::size_t rva; unsigned char bytes[16]; std::size_t size; };
// The code the plugin calls or relies on; any mismatch and it stays off.
const Signature kSignatures[]={
    {kCanRideSeat,{0x40,0x57,0x48,0x83,0xEC,0x30,0x8B,0x81,0x14,0x03,0x00,0x00,0x48,0x8B,0xFA,0x39},16},
    {kCanRide,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48},16},
    {kSeatRide,{0x40,0x53,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x38,0x45,0x33,0xD2,0x45},16},
    {kSeatClear,{0x48,0x85,0xD2,0x0F,0x84,0x94,0x00,0x00,0x00,0x48,0x89,0x5C,0x24,0x08,0x57,0x48},16},
    {kSeatKick,{0x48,0x85,0xD2,0x0F,0x84,0x98,0x01,0x00,0x00,0x48,0x89,0x5C,0x24,0x08,0x48,0x89},16},
    {kFindSeat,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57},16},
    {kRideAi,{0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x55,0x57,0x41,0x56,0x48,0x8D},16},
    {kPromptVisit,{0x40,0x53,0x48,0x83,0xEC,0x30,0x48,0x8B,0x41,0x08,0x48,0x8B,0xD9,0x4C,0x8B,0xD2},16},
    {0x5E3B50,{0x8B,0xC2,0x2D,0x15,0x00,0x00,0x10,0x74,0x09,0x83,0xF8,0x01,0x0F,0x85,0xCE,0x69},16},   // dummy rider: get-off -> dies
    {0x634710,{0x49,0x8B,0x80,0x68,0x02,0x00,0x00,0x48,0x85,0xC0,0x74,0x0A,0x83,0x78,0x08,0x00},16},   // seat+0x268 occupancy
    {0x62DD32,{0x48,0x8B,0x87,0x38,0x02,0x00,0x00,0x48,0x85,0xC0,0x74,0x06,0x83,0x78,0x08,0x00},16},   // same, prompt check
    {0x572EFF,{0x44,0x38,0xAE,0x54,0x03,0x00,0x00},7},                                                 // human+0x354 player
};

bool IdentifyImage(HMODULE handle) noexcept {
    __try {
        auto base=reinterpret_cast<unsigned char*>(handle);
        if(!Readable(base,0x1000))return false;
        auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<0 || dos->e_lfanew>0x800)return false;
        auto pe=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
        if(pe->Signature!=IMAGE_NT_SIGNATURE || pe->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64
           || pe->FileHeader.TimeDateStamp!=0x678CCB46 || pe->OptionalHeader.SizeOfImage!=0x22CE000)return false;
        image=base;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){image=nullptr;return false;}
}

bool CheckProfile() noexcept {
    __try {
        for(const auto& s:kSignatures)if(!Matches(s.rva,s.bytes,s.size)){Log("PROFILE mismatch at %#zx",s.rva);return false;}
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

float ReadFloat(const wchar_t* key,float fallback) noexcept {
    wchar_t text[64]{};
    GetPrivateProfileStringW(L"VehicleCrew",key,L"",text,64,iniPath);
    if(!text[0])return fallback;
    wchar_t* end=nullptr;
    const float v=std::wcstof(text,&end);
    return end!=text && std::isfinite(v) ? v : fallback;
}
bool ReadBool(const wchar_t* key,bool fallback) noexcept {
    return GetPrivateProfileIntW(L"VehicleCrew",key,fallback ? 1 : 0,iniPath)!=0;
}

void LoadConfig() noexcept {
    Config n{};
    n.enabled=ReadBool(L"Enabled",n.enabled);
    n.debug=ReadBool(L"Debug",n.debug);
    n.autoCrew=ReadBool(L"AutoCrew",n.autoCrew);
    n.crewDelayMs=GetPrivateProfileIntW(L"VehicleCrew",L"CrewDelayMs",n.crewDelayMs,iniPath);
    n.crewRange=ReadFloat(L"CrewRange",n.crewRange);
    n.bump=ReadBool(L"Bump",n.bump);
    n.bumpToGunner=ReadBool(L"BumpToGunner",n.bumpToGunner);
    n.heliPilot=ReadBool(L"HeliPilot",n.heliPilot);
    n.heliHeight=ReadFloat(L"HeliHeight",n.heliHeight);
    n.heliFollow=ReadFloat(L"HeliFollow",n.heliFollow);
    n.heliRange=ReadFloat(L"HeliRange",n.heliRange);
    n.heliFire=ReadBool(L"HeliFire",n.heliFire);
    n.heliStandoff=ReadFloat(L"HeliStandoff",n.heliStandoff);
    n.heliFireCone=ReadFloat(L"HeliFireCone",n.heliFireCone);
    n.heliFireHeight=ReadFloat(L"HeliFireHeight",n.heliFireHeight);
    n.heliMissile=ReadBool(L"HeliMissile",n.heliMissile);
    n.heliMissileMs=GetPrivateProfileIntW(L"VehicleCrew",L"HeliMissileMs",n.heliMissileMs,iniPath);
    n.heliMoveGain=ReadFloat(L"HeliMoveGain",n.heliMoveGain);
    n.heliBrakeGain=ReadFloat(L"HeliBrakeGain",n.heliBrakeGain);
    n.heliClimbGain=ReadFloat(L"HeliClimbGain",n.heliClimbGain);
    n.heliHoverLearn=ReadFloat(L"HeliHoverLearn",n.heliHoverLearn);
    n.heliLandMs=GetPrivateProfileIntW(L"VehicleCrew",L"HeliLandMs",n.heliLandMs,iniPath);
    cfg=n;
    Log("CONFIG enabled=%d debug=%d autoCrew=%d delay=%lums range=%.0f bump=%d toGunner=%d heli=%d height=%.0f follow=%.0f engage=%.0f fire=%d",
        cfg.enabled,cfg.debug,cfg.autoCrew,cfg.crewDelayMs,cfg.crewRange,cfg.bump,cfg.bumpToGunner,
        cfg.heliPilot,cfg.heliHeight,cfg.heliFollow,cfg.heliRange,cfg.heliFire);
    Log("CONFIG heli fireHeight=%.0f standoff=%.0f cone=%.0f missile=%d/%lums move=%.3f brake=%.3f climb=%.3f learn=%.3f landMs=%lu",
        cfg.heliFireHeight,cfg.heliStandoff,cfg.heliFireCone,cfg.heliMissile,cfg.heliMissileMs,cfg.heliMoveGain,cfg.heliBrakeGain,cfg.heliClimbGain,cfg.heliHoverLearn,cfg.heliLandMs);
}

FILETIME IniStamp() noexcept {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    return GetFileAttributesExW(iniPath,GetFileExInfoStandard,&data) ? data.ftLastWriteTime : FILETIME{};
}
}  // namespace

void Log(const char* format,...) noexcept {
    if(!logPath[0])return;
    char text[1000]{};va_list args;va_start(args,format);vsnprintf_s(text,sizeof(text),_TRUNCATE,format,args);va_end(args);
    FILE* f=nullptr;if(_wfopen_s(&f,logPath,L"ab") || !f)return;
    SYSTEMTIME t{};GetLocalTime(&t);
    fprintf(f,"[%02u:%02u:%02u.%03u] %s\r\n",t.wHour,t.wMinute,t.wSecond,t.wMilliseconds,text);fclose(f);
}

void ReloadConfigIfChanged() noexcept {
    const auto now=GetTickCount64();
    if(now-iniCheckedAt<1000)return;
    iniCheckedAt=now;
    const auto stamp=IniStamp();
    if(CompareFileTime(&stamp,&iniStamp)==0)return;
    iniStamp=stamp;
    LoadConfig();
}

bool Matches(std::size_t rva,const unsigned char* bytes,std::size_t size) noexcept {
    return std::memcmp(image+rva,bytes,size)==0;
}

bool PatchVtableSlot(void** slot,void* expected,void* replacement) noexcept {
    DWORD old=0;
    if(!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old))return false;
    const bool ok=InterlockedCompareExchangePointer(slot,replacement,expected)==expected;
    VirtualProtect(slot,sizeof(void*),old,&old);
    return ok;
}

unsigned SeatCount(const unsigned char* vehicle) noexcept {
    const auto n=At<std::uint64_t>(vehicle,kSeatCount);
    const auto seats=At<const unsigned char*>(vehicle,kSeats);
    return n>0 && n<=16 && Readable(seats,n*kSeatStride) ? static_cast<unsigned>(n) : 0;
}

unsigned char* SeatAt(unsigned char* vehicle,unsigned index) noexcept {
    return At<unsigned char*>(vehicle,kSeats)+index*kSeatStride;
}

bool IsPlayer(const unsigned char* human) noexcept {
    return Readable(human,kHumanPlayer+1) && human[kHumanPlayer] && At<const void*>(human,kHumanPad);
}

Rider SeatRider(const unsigned char* seat) noexcept {
    const auto ctrl=At<const unsigned char*>(seat,kSeatRiderCtrl);
    if(!ctrl || !Readable(ctrl,0x10) || At<std::int32_t>(ctrl,8)==0)return Rider::none;
    const auto rider=At<const unsigned char*>(seat,kSeatRider);
    if(!Readable(rider,kHumanPlayer+1))return Rider::other;
    if(At<const unsigned char*>(rider,0)==image+kDummyRiderVtable)return Rider::dummy;
    return IsPlayer(rider) ? Rider::player : Rider::other;
}

void SeePlayer(const float* pos,std::int32_t team) noexcept {
    std::memcpy(player.pos,pos,sizeof(player.pos));player.team=team;player.at=GetTickCount64();
}
}  // namespace crew

extern "C" __declspec(dllexport) bool EDFMLAPI EML6_Load(PluginInfo* info) {
    using namespace crew;
    if(!info)return false;
    GetModuleFileNameW(module,iniPath,MAX_PATH);
    auto dot=wcsrchr(iniPath,L'.');if(!dot)return false;
    wcscpy_s(dot,MAX_PATH-(dot-iniPath),L".ini");
    wcscpy_s(logPath,iniPath);
    dot=wcsrchr(logPath,L'.');wcscpy_s(dot,MAX_PATH-(dot-logPath),L".log");
    info->infoVersion=PluginInfo::MaxInfoVer;info->name="EDF6 Vehicle Crew";info->version=PLUG_VER(0,1,0,0);
    Log("EDF6VehicleCrew 0.1.0 loading");
    iniStamp=IniStamp();
    LoadConfig();
    if(!IdentifyImage(GetModuleHandleW(L"EDF.dll"))){Log("REFUSED: unsupported EDF.dll");return false;}
    if(!CheckProfile()){Log("REFUSED: unexpected EDF.dll code");return false;}
    Log("HELI profile=%d",CheckHeliProfile());
    InstallLoadout(iniPath);   // independent of the crew hooks
    return InstallCrew();   // never unload code a patched slot points at
}

BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH){crew::module=instance;DisableThreadLibraryCalls(instance);}
    return TRUE;
}
