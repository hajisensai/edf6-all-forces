// EDF6VehicleCrew: NPC drivers for empty friendly vehicles (helicopters flown by the plugin), and
// the player can board any seat an NPC holds, bumping the NPC to a gunner seat.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46; see docs/re-notes.md.
#include <Windows.h>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <new>
#pragma warning(push)
#pragma warning(disable:4201)
#include "PluginAPI.h"
#pragma warning(pop)
#include "crew.h"
#include "memory.h"
#include "subcarrier.h"
#include "edf/host.h"
#include "version.h"

namespace crew {
unsigned char* image=nullptr;
PlayerFix player{};
namespace {
// The published config. A reload builds a new one and swaps the pointer; the old one is never freed (a
// reader on another thread may still hold it, and a reload is a person saving the ini: a few hundred bytes
// each time), so no reader ever sees a half-written config.
const Config kDefaults{};
std::atomic<const Config*> published{&kDefaults};
thread_local int noBumpDepth=0;
}  // namespace
const Config& Cfg() noexcept { return *published.load(std::memory_order_acquire); }
void SuppressBump(bool on) noexcept { noBumpDepth+=on ? 1 : -1; }
bool BumpSuppressed() noexcept { return noBumpDepth>0; }
namespace {
HMODULE module=nullptr;
wchar_t logPath[MAX_PATH]{},logOldPath[MAX_PATH+2]{};
wchar_t iniPath[MAX_PATH]{};
edf::IniWatch ini;

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
// A whole number, signed: GetPrivateProfileIntW parses "-5" and hands it back as a huge UINT, which as a
// delay meant "never" (CrewDelayMs=-1: no NPC ever came). FixInt clamps it.
int ReadInt(const wchar_t* key,DWORD fallback) noexcept {
    return static_cast<int>(GetPrivateProfileIntW(L"VehicleCrew",key,static_cast<INT>(fallback),iniPath));
}

// Range checks: a value outside [lo, hi] is clamped and the change logged (each reload logs it again, so the
// log always says what is in force).
void Fix(const char* key,float& v,float lo,float hi) noexcept {
    const float c=v<lo ? lo : v>hi ? hi : v;
    if(c!=v){Log("CONFIG %s=%g out of range [%g, %g]: %g used",key,v,lo,hi,c);v=c;}
}
DWORD FixInt(const char* key,int v,int lo,int hi) noexcept {
    const int c=v<lo ? lo : v>hi ? hi : v;
    if(c!=v)Log("CONFIG %s=%d out of range [%d, %d]: %d used",key,v,lo,hi,c);
    return static_cast<DWORD>(c);
}
void Validate(Config& n) noexcept {
    Fix("CrewRange",n.crewRange,0.0f,10000.0f);
    Fix("HeliHeight",n.heliHeight,0.0f,500.0f);
    Fix("HeliFollow",n.heliFollow,0.0f,1000.0f);
    Fix("HeliRange",n.heliRange,0.0f,2000.0f);
    // Engaged while following, it takes on enemies within HeliCombatRange of the player plus its gun's reach,
    // capped at HeliRange: a combat range past HeliRange says something HeliRange overrules anyway.
    Fix("HeliCombatRange",n.heliCombatRange,0.0f,n.heliRange);
    Fix("HeliFireHeight",n.heliFireHeight,1.0f,300.0f);
    Fix("HeliFireCone",n.heliFireCone,0.1f,90.0f);
    // heli.cpp Tune rewrites the speed only with a time constant of at least half a second (30 frames).
    Fix("HeliSpeed",n.heliSpeed,0.0f,200.0f);
    Fix("HeliAgility",n.heliAgility,0.5f,60.0f);
    Fix("HeliYawRate",n.heliYawRate,0.0f,360.0f);
    Fix("HeliGuardRadius",n.heliGuardRadius,0.0f,1000.0f);
    Fix("HeliGuardSpeed",n.heliGuardSpeed,1.0f,100.0f);
    Fix("GroundFollow",n.groundFollow,0.0f,500.0f);
    Fix("GroundRange",n.groundRange,0.0f,2000.0f);
    // A leash shorter than the follow distance keeps the crawler leashed for good: it would never engage.
    Fix("GroundLeash",n.groundLeash,n.groundFollow,5000.0f);
    Fix("RescueBelow",n.rescueBelow,-1000.0f,1000.0f);
    Fix("SubHullHp",n.subHullHp,0.0f,1.0e7f);
    Fix("SubHeavyHit",n.subHeavyHit,0.0f,1.0e7f);
    Fix("CarrierLaserDamage",n.carrierLaserDamage,0.0f,1.0e6f);
    Fix("CarrierLaserBreak",n.carrierLaserBreak,0.0f,1.0f);
    Fix("VehicleHudRange",n.vehicleHudRange,0.0f,10000.0f);
    n.vehicleHudCount=static_cast<int>(FixInt("VehicleHudCount",n.vehicleHudCount,0,12));
    Fix("PlayerJetRamDamage",n.playerJetRamDamage,0.0f,100.0f);
    n.playerJetBoostKey=static_cast<int>(FixInt("PlayerJetBoostKey",n.playerJetBoostKey,0,254));
    n.playerJetBrakeKey=static_cast<int>(FixInt("PlayerJetBrakeKey",n.playerJetBrakeKey,0,254));
    n.playerJetSwitchKey=static_cast<int>(FixInt("PlayerJetSwitchKey",n.playerJetSwitchKey,0,254));
    n.playerJetTargetKey=static_cast<int>(FixInt("PlayerJetTargetKey",n.playerJetTargetKey,0,254));
    Fix("PlayerJetMouseSpeed",n.playerJetMouseSpeed,0.1f,10.0f);
    if(n.bigWorld!=0.0f)Fix("BigWorld",n.bigWorld,3000.0f,20000.0f);
    Fix("JetSoundVolume",n.jetSoundVolume,0.0f,4.0f);
}

// The flight controller's gains became constants (heli.cpp): an old ini that still sets them loads as before,
// the keys ignored (said once).
void IgnoreRetired() noexcept {
    static const wchar_t* const kRetired[]={L"HeliMoveGain",L"HeliBrakeGain",L"HeliClimbGain",L"HeliHoverLearn"};
    static bool said[4]{};
    for(int i=0;i<4;++i) {
        wchar_t text[8]{};
        GetPrivateProfileStringW(L"VehicleCrew",kRetired[i],L"",text,8,iniPath);
        if(!text[0] || said[i])continue;
        said[i]=true;
        Log("CONFIG %ls is no longer read (the flight controller's gains are fixed): ignored",kRetired[i]);
    }
}

void LoadConfig() noexcept {
    Config n{};
    n.enabled=ReadBool(L"Enabled",n.enabled);
    n.debug=ReadBool(L"Debug",n.debug);
    n.autoCrew=ReadBool(L"AutoCrew",n.autoCrew);
    n.crewDelayMs=FixInt("CrewDelayMs",ReadInt(L"CrewDelayMs",n.crewDelayMs),0,600000);
    n.crewRange=ReadFloat(L"CrewRange",n.crewRange);
    n.bump=ReadBool(L"Bump",n.bump);
    n.bumpToGunner=ReadBool(L"BumpToGunner",n.bumpToGunner);
    n.heliPilot=ReadBool(L"HeliPilot",n.heliPilot);
    n.heliHeight=ReadFloat(L"HeliHeight",n.heliHeight);
    n.heliFollow=ReadFloat(L"HeliFollow",n.heliFollow);
    n.heliRange=ReadFloat(L"HeliRange",n.heliRange);
    n.heliCombatRange=ReadFloat(L"HeliCombatRange",n.heliCombatRange);
    n.heliFire=ReadBool(L"HeliFire",n.heliFire);
    n.heliFireCone=ReadFloat(L"HeliFireCone",n.heliFireCone);
    n.heliFireHeight=ReadFloat(L"HeliFireHeight",n.heliFireHeight);
    n.heliAvoid=ReadBool(L"HeliAvoid",n.heliAvoid);
    n.heliMissile=ReadBool(L"HeliMissile",n.heliMissile);
    n.heliMissileMs=FixInt("HeliMissileMs",ReadInt(L"HeliMissileMs",n.heliMissileMs),0,600000);
    n.heliLandMs=FixInt("HeliLandMs",ReadInt(L"HeliLandMs",n.heliLandMs),0,3600000);
    n.heliSpeed=ReadFloat(L"HeliSpeed",n.heliSpeed);
    n.heliAgility=ReadFloat(L"HeliAgility",n.heliAgility);
    n.heliYawRate=ReadFloat(L"HeliYawRate",n.heliYawRate);
    n.heliDoorGuns=ReadBool(L"HeliDoorGuns",n.heliDoorGuns);
    n.heliGuardRadius=ReadFloat(L"HeliGuardRadius",n.heliGuardRadius);
    n.heliGuardSpeed=ReadFloat(L"HeliGuardSpeed",n.heliGuardSpeed);
    n.jetPilot=ReadBool(L"JetPilot",n.jetPilot);
    n.jetFuelSec=FixInt("JetFuelSec",ReadInt(L"JetFuelSec",n.jetFuelSec),0,3600);
    n.jetSortieSec=FixInt("JetSortieSec",ReadInt(L"JetSortieSec",n.jetSortieSec),0,3600);
    n.jetAirRaider=ReadBool(L"JetAirRaider",n.jetAirRaider);
    n.jetMissionStrike=ReadBool(L"JetMissionStrike",n.jetMissionStrike);
    n.groundPilot=ReadBool(L"GroundPilot",n.groundPilot);
    n.groundFollow=ReadFloat(L"GroundFollow",n.groundFollow);
    n.groundRange=ReadFloat(L"GroundRange",n.groundRange);
    n.groundLeash=ReadFloat(L"GroundLeash",n.groundLeash);
    n.groundFire=ReadBool(L"GroundFire",n.groundFire);
    n.callNextKey=FixInt("CallNextKey",ReadInt(L"CallNextKey",n.callNextKey),0,255);   // virtual-key codes
    n.callPrevKey=FixInt("CallPrevKey",ReadInt(L"CallPrevKey",n.callPrevKey),0,255);
    n.seaRescue=ReadBool(L"SeaRescue",n.seaRescue);
    n.rescueBelow=ReadFloat(L"RescueBelow",n.rescueBelow);
    n.rescueAutoBoard=ReadBool(L"RescueAutoBoard",n.rescueAutoBoard);
    n.subHullHp=ReadFloat(L"SubHullHp",n.subHullHp);
    n.subHeavyHit=ReadFloat(L"SubHeavyHit",n.subHeavyHit);
    n.carrierLaser=ReadBool(L"CarrierLaser",n.carrierLaser);
    n.carrierLaserDamage=ReadFloat(L"CarrierLaserDamage",n.carrierLaserDamage);
    n.carrierLaserBreak=ReadFloat(L"CarrierLaserBreak",n.carrierLaserBreak);
    n.vehicleWelding=ReadBool(L"VehicleWelding",n.vehicleWelding);
    n.giantContactCap=ReadBool(L"GiantContactCap",n.giantContactCap);
    n.vehicleHud=ReadBool(L"VehicleHud",n.vehicleHud);
    n.vehicleHudCount=ReadInt(L"VehicleHudCount",static_cast<DWORD>(n.vehicleHudCount));
    n.vehicleHudRange=ReadFloat(L"VehicleHudRange",n.vehicleHudRange);
    n.playerJet=ReadBool(L"PlayerJet",n.playerJet);
    n.playerJetInvertPitch=ReadBool(L"PlayerJetInvertPitch",n.playerJetInvertPitch);
    n.playerJetRamDamage=ReadFloat(L"PlayerJetRamDamage",n.playerJetRamDamage);
    n.playerJetBoostKey=ReadInt(L"PlayerJetBoostKey",static_cast<DWORD>(n.playerJetBoostKey));
    n.playerJetBrakeKey=ReadInt(L"PlayerJetBrakeKey",static_cast<DWORD>(n.playerJetBrakeKey));
    n.playerJetSwitchKey=ReadInt(L"PlayerJetSwitchKey",static_cast<DWORD>(n.playerJetSwitchKey));
    n.playerJetTargetKey=ReadInt(L"PlayerJetTargetKey",static_cast<DWORD>(n.playerJetTargetKey));
    n.playerJetMouseSpeed=ReadFloat(L"PlayerJetMouseSpeed",n.playerJetMouseSpeed);
    n.playerJetMouseFlight=ReadBool(L"PlayerJetMouseFlight",n.playerJetMouseFlight);
    n.jetSound=ReadBool(L"JetSound",n.jetSound);
    n.jetSoundVolume=ReadFloat(L"JetSoundVolume",n.jetSoundVolume);
    n.bigWorld=ReadFloat(L"BigWorld",n.bigWorld);
    Validate(n);
    IgnoreRetired();
    Log("CONFIG enabled=%d debug=%d autoCrew=%d delay=%lums range=%.0f bump=%d toGunner=%d heli=%d height=%.0f follow=%.0f engage=%.0f fire=%d",
        n.enabled,n.debug,n.autoCrew,n.crewDelayMs,n.crewRange,n.bump,n.bumpToGunner,
        n.heliPilot,n.heliHeight,n.heliFollow,n.heliRange,n.heliFire);
    Log("CONFIG heli combatRange=%.0f avoid=%d fireHeight=%.0f cone=%.1f missile=%d/%lums landMs=%lu",
        n.heliCombatRange,n.heliAvoid,n.heliFireHeight,n.heliFireCone,n.heliMissile,n.heliMissileMs,n.heliLandMs);
    Log("CONFIG heli speed=%.1f agility=%.1fs yawRate=%.0f doorGuns=%d guardRadius=%.0f guardSpeed=%.1f",n.heliSpeed,n.heliAgility,n.heliYawRate,n.heliDoorGuns,
        n.heliGuardRadius,n.heliGuardSpeed);
    Log("CONFIG sub hullHp=%.0f heavyHit=%.0f",n.subHullHp,n.subHeavyHit);
    Log("CONFIG hud vehicles=%d count=%d range=%.0f",n.vehicleHud,n.vehicleHudCount,n.vehicleHudRange);
    Log("CONFIG playerJet=%d invertPitch=%d ramDamage=%.2f boostKey=0x%X brakeKey=0x%X switchKey=0x%X mouse=%.2f jetSound=%d volume=%.2f",n.playerJet,
        n.playerJetInvertPitch,n.playerJetRamDamage,n.playerJetBoostKey,n.playerJetBrakeKey,n.playerJetSwitchKey,n.playerJetMouseSpeed,n.jetSound,n.jetSoundVolume);
    Log("CONFIG jet pilot=%d fuel=%lus sortie=%lus airRaider=%d missionStrike=%d",n.jetPilot,n.jetFuelSec,
        n.jetSortieSec,n.jetAirRaider,n.jetMissionStrike);
    Log("CONFIG ground pilot=%d follow=%.0f range=%.0f leash=%.0f fire=%d",n.groundPilot,n.groundFollow,
        n.groundRange,n.groundLeash,n.groundFire);
    Log("CONFIG rescue sea=%d below=%.1f autoBoard=%d",n.seaRescue,n.rescueBelow,n.rescueAutoBoard);
    Log("CONFIG carrierLaser=%d damage=%.0f break=%.2f",n.carrierLaser,n.carrierLaserDamage,n.carrierLaserBreak);
    Log("CONFIG calls next=%#lx prev=%#lx (0: off)",n.callNextKey,n.callPrevKey);
    Log("CONFIG physics vehicleWelding=%d giantContactCap=%d",n.vehicleWelding,n.giantContactCap);
    Config* const fresh=new(std::nothrow) Config(n);
    if(fresh)published.store(fresh,std::memory_order_release);
}

}  // namespace

// The log: lines are copied into a memory buffer on the caller's thread (the game thread, the call picker, the draw
// thread: one memcpy under a lock) and a writer thread of its own writes the buffer to one handle kept open every
// kFlushMs. A WriteFile per line on the game thread was the hitch under a burst of lines (an air strike's jets,
// each logging what it saw: 15000 lines a second, 2026-10-04); opening and closing the file per line before that,
// with the virus scanner looking at each close, was the hitch when a mission started. A crash takes at most the
// last kFlushMs of lines. Two buffers of kLogBuffer: the writer takes the full one and the lines go on into the
// other; a line that does not fit is dropped and counted (said with the next write), never waited for. Debug=1
// writes the flight data every second, so past kLogMax the file is renamed to .log.1 (the one before replaced) and
// a new one begun, by the writer. No FILE_SHARE_DELETE: a log deleted while the game runs would take the rest unseen.
namespace {
constexpr LONGLONG kLogMax=32ll<<20;
constexpr std::size_t kLogBuffer=1u<<20;
constexpr DWORD kFlushMs=50;
SRWLOCK logLock=SRWLOCK_INIT;        // the buffers
char logBuffer[2][kLogBuffer];
std::size_t logUsed=0;               // in logBuffer[logActive]
int logActive=0;
unsigned logDropped=0;
INIT_ONCE logWriterOnce=INIT_ONCE_STATIC_INIT;
HANDLE logFile=nullptr;              // the writer's alone
LONGLONG logSize=0;

HANDLE OpenLog() noexcept {
    const HANDLE h=CreateFileW(logPath,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)return nullptr;
    LARGE_INTEGER size{};
    logSize=GetFileSizeEx(h,&size) ? size.QuadPart : 0;
    return h;
}

void WriteLog(const char* text,std::size_t n) noexcept {
    DWORD wrote=0;
    if(logFile && n && WriteFile(logFile,text,static_cast<DWORD>(n),&wrote,nullptr))logSize+=wrote;
}

// The log renamed to .log.1 and a new one opened. A rename that fails (another program holds .log.1, or the log
// without sharing delete) is said in the log, which then grows another kLogMax first.
void RotateLog() noexcept {
    CloseHandle(logFile);
    const BOOL moved=MoveFileExW(logPath,logOldPath,MOVEFILE_REPLACE_EXISTING);
    const DWORD error=moved ? 0 : GetLastError();
    logFile=OpenLog();
    if(!logFile)return;
    char line[160];
    const int n=moved ? _snprintf_s(line,sizeof(line),_TRUNCATE,"(log continued: the previous %lld MB are in .log.1)\r\n",kLogMax>>20)
                      : _snprintf_s(line,sizeof(line),_TRUNCATE,"(log rotation failed, error %lu: this file grows on)\r\n",error);
    if(!moved)logSize=0;   // the next try another kLogMax on, not on every line
    if(n>0)WriteLog(line,static_cast<std::size_t>(n));
}

DWORD WINAPI LogWriter(void*) {
    for(;;) {
        Sleep(kFlushMs);
        AcquireSRWLockExclusive(&logLock);
        const int full=logActive;
        const std::size_t n=logUsed;
        const unsigned dropped=logDropped;
        logActive^=1;logUsed=0;logDropped=0;
        ReleaseSRWLockExclusive(&logLock);
        if(!n && !dropped)continue;
        if(!logFile)logFile=OpenLog();
        if(!logFile)continue;
        if(logSize+static_cast<LONGLONG>(n)>kLogMax)RotateLog();
        WriteLog(logBuffer[full],n);
        if(dropped) {
            char line[120];
            const int k=_snprintf_s(line,sizeof(line),_TRUNCATE,"(%u log lines dropped: more than %zu KB in %lu ms)\r\n",dropped,
                                    kLogBuffer>>10,kFlushMs);
            if(k>0)WriteLog(line,static_cast<std::size_t>(k));
        }
    }
}

BOOL CALLBACK StartLogWriter(PINIT_ONCE,void*,void**) {
    const HANDLE t=CreateThread(nullptr,0,&LogWriter,nullptr,0,nullptr);
    if(t)CloseHandle(t);
    return TRUE;
}
}  // namespace

void Log(const char* format,...) noexcept {
    if(!logPath[0])return;
    char text[1000]{};va_list args;va_start(args,format);vsnprintf_s(text,sizeof(text),_TRUNCATE,format,args);va_end(args);
    SYSTEMTIME t{};GetLocalTime(&t);
    char line[1100];
    const int n=_snprintf_s(line,sizeof(line),_TRUNCATE,"[%02u:%02u:%02u.%03u] %s\r\n",t.wHour,t.wMinute,t.wSecond,t.wMilliseconds,text);
    if(n<=0)return;
    InitOnceExecuteOnce(&logWriterOnce,&StartLogWriter,nullptr,nullptr);
    AcquireSRWLockExclusive(&logLock);
    if(logUsed+static_cast<std::size_t>(n)<=kLogBuffer) {
        std::memcpy(logBuffer[logActive]+logUsed,line,static_cast<std::size_t>(n));
        logUsed+=static_cast<std::size_t>(n);
    } else ++logDropped;
    ReleaseSRWLockExclusive(&logLock);
}

void ReloadConfigIfChanged() noexcept {
    if(ini.Changed())LoadConfig();
}

// The player's human. SeePlayer gets a position inside an object: the human's own (the on-foot ride prompt,
// crew.cpp PromptHook) or the vehicle's they ride (crew.cpp Crew). A vehicle is told by the player in one
// of its seats (the seat's rider is the human); else the object is the human if it is the player (pad and
// player flag) and its weak-this points at itself. Kept with its weak-this control block, so a new object
// at the same address (the next mission) is not taken for it.
namespace {
constexpr ULONGLONG kPlayerHumanMs=2000;
unsigned char* playerHuman=nullptr;
ObjRef playerHumanRef;
ULONGLONG playerHumanAt=0;

unsigned char* HumanOf(unsigned char* object) noexcept {
    const unsigned n=SeatCount(object);
    for(unsigned i=0;i<n;++i)
        if(SeatRider(SeatAt(object,i))==Rider::player)return At<unsigned char*>(SeatAt(object,i),kSeatRider);
    return IsPlayer(object) && At<const void*>(object,kSelf)==object ? object : nullptr;
}
}  // namespace

void SeePlayer(const float* pos,std::int32_t team) noexcept {
    std::memcpy(player.pos,pos,sizeof(player.pos));player.team=team;player.at=GameMs();
    __try {
        auto object=const_cast<unsigned char*>(reinterpret_cast<const unsigned char*>(pos))-kPosition;
        if(auto human=HumanOf(object)) {
            playerHuman=human;playerHumanRef=ObjRef::Of(human);playerHumanAt=player.at;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

void ResetPlayer() noexcept {
    player=PlayerFix{};
    playerHuman=nullptr;playerHumanRef=ObjRef{};playerHumanAt=0;
}

unsigned char* PlayerHuman() noexcept {
    __try {
        if(!playerHuman || GameMs()-playerHumanAt>kPlayerHumanMs)return nullptr;
        if(!Readable(playerHuman,kHumanVehicleCtrl+8) || !playerHumanRef.Is(playerHuman) || !IsPlayer(playerHuman))return nullptr;
        return playerHuman;
    } __except(EXCEPTION_EXECUTE_HANDLER){return nullptr;}
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
    swprintf_s(logOldPath,L"%ls.1",logPath);
    info->infoVersion=PluginInfo::MaxInfoVer;info->name="EDF6 Vehicle Crew";info->version=PLUG_VER(EDF6VC_VERSION_MAJOR,EDF6VC_VERSION_MINOR,EDF6VC_VERSION_PATCH,0);
    Log("EDF6VehicleCrew %s loading",EDF6VC_VERSION);
    ini.Start(iniPath);
    LoadConfig();
    image=edf::IdentifyImage(GetModuleHandleW(L"EDF.dll"));
    if(!image){Log("REFUSED: unsupported EDF.dll");return false;}
    if(!CheckProfile()){Log("REFUSED: unexpected EDF.dll code");return false;}
    // Checks first, patching nothing: a refusal above, or a module found off here, leaves the game as it was.
    const bool heli=CheckHeliProfile();
    const bool ground=CheckGroundProfile();
    Log("HELI profile=%d",heli);
    Log("GROUND profile=%d",ground);
    // Then the installs, in dependency order. From the first patch on the plugin stays loaded whatever fails
    // after (true below): the loader unloading the DLL would leave patched slots pointing at unloaded code.
    InstallBody506();       // the one 506 physics hook: before the jets, the carrier and the player jets
    if(heli) {
        InstallDoorGuns();  // the 410's door guns are part of the heli pilot
        InstallJets();      // the jets and the carrier are flown from HeliFrame: no heli pilot, none of them
        InstallSub();
    } else Log("JET / SUB off: they are flown from the heli pilot's frame, which is off");
    InstallPlayerJets();    // its frame is the vehicles' own input; it needs only the 506 physics hook
    InstallPhysics();       // vehicle chassis welding and the giants' contact cap (physics.cpp)
    InstallLaser();
    InstallGauge();         // the follower gauge's draw (subcarrier.cpp): the carriers' gauges and the vehicle HUD
    InstallHud();
    InstallJetSound();
    InstallMissiles();
    InstallStores();        // before any mission builds a jet: the 506 builds a weapon for every holder
    InstallBigWorld();
    InstallMission();       // the mission's start (Reset*, the preloads) and a trigger of the per-frame hooks
    InstallLoadout(iniPath);
    Log("AIRSTRIKE takeovers=%d",InstallAirstrikes());
    if(!InstallCrew())Log("HOOK crew: no seat hook, so no per-frame hook either: crews, helis, crawlers and the HUD are off");
    StartCallPicker();
    return true;
}

BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH){crew::module=instance;DisableThreadLibraryCalls(instance);}
    return TRUE;
}
