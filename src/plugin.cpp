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
#include "lockon.h"
#include "hudscale.h"
#include "hudtext.h"
#include "gunnerrecoil.h"
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
// HudLanguage: auto / en / zh-CN / zh-TW / ja (hudtext.h ParseSetting); anything else is logged and auto used.
int ReadLanguage(int fallback) noexcept {
    wchar_t text[32]{};
    GetPrivateProfileStringW(L"VehicleCrew",L"HudLanguage",L"",text,32,iniPath);
    if(!text[0])return fallback;
    const int v=hudtext::ParseSetting(text);
    if(v>=0)return v;
    Log("CONFIG HudLanguage=%ls unknown (auto / en / zh-CN / zh-TW / ja): auto used",text);
    return static_cast<int>(hudtext::Setting::automatic);
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
    Fix("PlayerHeliStopSec",n.playerHeliStopSec,0.0f,10.0f);
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
    Fix("HudScale",n.hudScale,hudscale::kUserMin,hudscale::kUserMax);
    n.vehicleHudCount=static_cast<int>(FixInt("VehicleHudCount",n.vehicleHudCount,0,12));
    Fix("PlayerJetRamDamage",n.playerJetRamDamage,0.0f,100.0f);
    Fix("VehicleRamDamage",n.vehicleRamDamage,0.0f,100.0f);
    n.playerJetBoostKey=static_cast<int>(FixInt("PlayerJetBoostKey",n.playerJetBoostKey,0,254));
    n.playerJetBrakeKey=static_cast<int>(FixInt("PlayerJetBrakeKey",n.playerJetBrakeKey,0,254));
    n.playerJetSwitchKey=static_cast<int>(FixInt("PlayerJetSwitchKey",n.playerJetSwitchKey,0,254));
    n.playerJetTargetKey=static_cast<int>(FixInt("PlayerJetTargetKey",n.playerJetTargetKey,0,254));
    n.playerJetFlareKey=static_cast<int>(FixInt("PlayerJetFlareKey",n.playerJetFlareKey,0,254));
    n.playerJetHailKey=static_cast<int>(FixInt("PlayerJetHailKey",n.playerJetHailKey,0,254));
    n.gunshipGunnerKey=static_cast<int>(FixInt("GunshipGunnerKey",n.gunshipGunnerKey,0,254));
    n.playerJetFlares=static_cast<int>(FixInt("PlayerJetFlares",n.playerJetFlares,0,99));
    n.playerJetGearKey=static_cast<int>(FixInt("PlayerJetGearKey",n.playerJetGearKey,0,254));
    n.playerJetGearButton=static_cast<int>(FixInt("PlayerJetGearButton",n.playerJetGearButton,0,255));
    n.playerJetChuteCutKey=static_cast<int>(FixInt("PlayerJetChuteCutKey",n.playerJetChuteCutKey,0,254));
    Fix("PlayerJetMouseSpeed",n.playerJetMouseSpeed,0.1f,10.0f);
    Fix("PlayerJetRollScale",n.playerJetRollScale,0.5f,2.0f);
    Fix("PlayerJetAimGain",n.playerJetAimGain,0.5f,2.0f);
    Fix("PlayerRotorLift",n.playerRotorLift,0.0f,4.0f);
    if(n.bigWorld!=0.0f)Fix("BigWorld",n.bigWorld,3000.0f,20000.0f);
    Fix("AirSoftEdge",n.airSoftEdge,0.0f,3000.0f);
    Fix("AirSoftTurns",n.airSoftTurns,0.0f,3.0f);
    Fix("AirSoftCeil",n.airSoftCeil,0.0f,1000.0f);
    Fix("HeliSoftEdge",n.heliSoftEdge,0.0f,1000.0f);
    if(n.viewDistance!=0.0f)Fix("ViewDistance",n.viewDistance,1000.0f,10000.0f);
    Fix("JetSoundVolume",n.jetSoundVolume,0.0f,4.0f);
    Fix("WarnVolume",n.warnVolume,0.0f,4.0f);
    Fix("VehicleEngineVolume",n.vehicleEngineVolume,0.0f,4.0f);
    Fix("VehicleTurretVolume",n.vehicleTurretVolume,0.0f,4.0f);
    Fix("VehicleReloadVolume",n.vehicleReloadVolume,0.0f,4.0f);
    Fix("VehicleGunVolume",n.vehicleGunVolume,0.0f,4.0f);
    Fix("VehicleMgVolume",n.vehicleMgVolume,0.0f,4.0f);
    Fix("VehicleMissileVolume",n.vehicleMissileVolume,0.0f,4.0f);
    Fix("DrillMaxRpm",n.drillMaxRpm,30.0f,1200.0f);
    Fix("DrillSpinUpSec",n.drillSpinUpSec,0.2f,10.0f);
    Fix("DrillSpinDownSec",n.drillSpinDownSec,0.2f,20.0f);
    Fix("DrillDamage",n.drillDamage,0.0f,1.0e6f);
    Fix("DrillBreak",n.drillBreak,0.0f,1.0e6f);
    Fix("DrillHeatSec",n.drillHeatSec,1.0f,600.0f);
    Fix("DrillCoolSec",n.drillCoolSec,1.0f,600.0f);
    Fix("DrillResumeHeat",n.drillResumeHeat,0.0f,0.95f);
    Fix("EmcChargeSec",n.emcChargeSec,0.5f,10.0f);
    Fix("EmcBeamSec",n.emcBeamSec,0.5f,5.0f);
    Fix("EmcBlastRadius",n.emcBlastRadius,10.0f,1000.0f);
    Fix("EmcBlastShare",n.emcBlastShare,0.0f,10.0f);
    Fix("EmcBreak",n.emcBreak,0.0f,1.0e7f);
    Fix("SidecarNpcRange",n.sidecarNpcRange,0.0f,200.0f);
    n.highCamKey=static_cast<int>(FixInt("HighCamKey",n.highCamKey,0,254));
    n.highCamButton=static_cast<int>(FixInt("HighCamButton",n.highCamButton,0,255));
    Fix("HighCamHeight",n.highCamHeight,10.0f,200.0f);
    Fix("HighCamBack",n.highCamBack,0.0f,200.0f);
    Fix("HighCamPitch",n.highCamPitch,15.0f,85.0f);
    n.seatNextKey=static_cast<int>(FixInt("SeatNextKey",n.seatNextKey,0,254));
    n.seatButton=static_cast<int>(FixInt("SeatButton",n.seatButton,0,255));
    n.highCamClass=static_cast<int>(FixInt("HighCamClass",n.highCamClass,1,3));
    Fix("TurretCamRate",n.turretCamRate,10.0f,720.0f);
    n.freeLookKey=static_cast<int>(FixInt("FreeLookKey",n.freeLookKey,0,254));
    n.freeLookButton=static_cast<int>(FixInt("FreeLookButton",n.freeLookButton,0,255));
    n.mapKey=static_cast<int>(FixInt("MapKey",n.mapKey,0,254));
    n.mapButton=static_cast<int>(FixInt("MapButton",n.mapButton,0,0xFFFF));
    if(n.mapViewDistance!=0.0f)Fix("MapViewDistance",n.mapViewDistance,1000.0f,10000.0f);
    n.proteusModeKey=static_cast<int>(FixInt("ProteusModeKey",n.proteusModeKey,0,254));
    n.proteusModeButton=static_cast<int>(FixInt("ProteusModeButton",n.proteusModeButton,0,255));
    n.proteusShieldKey=static_cast<int>(FixInt("ProteusShieldKey",n.proteusShieldKey,0,254));
    n.proteusShieldButton=static_cast<int>(FixInt("ProteusShieldButton",n.proteusShieldButton,0,255));
    n.proteusMarkKey=static_cast<int>(FixInt("ProteusMarkKey",n.proteusMarkKey,0,254));
    n.proteusMarkButton=static_cast<int>(FixInt("ProteusMarkButton",n.proteusMarkButton,0,255));
    n.proteusSalvoKey=static_cast<int>(FixInt("ProteusSalvoKey",n.proteusSalvoKey,0,254));
    Fix("ProteusWalkSpeed",n.proteusWalkSpeed,0.2f,4.0f);
    Fix("ProteusWalkTurn",n.proteusWalkTurn,0.2f,4.0f);
    Fix("ProteusStepHeight",n.proteusStepHeight,0.0f,4.0f);   // 4 m: the walkable test's floor (StepNormal 0.2 on the 5 m foot)
    Fix("ProteusShieldSlow",n.proteusShieldSlow,0.0f,1.0f);
    Fix("ProteusShieldArc",n.proteusShieldArc,10.0f,360.0f);
    Fix("ProteusShieldBlock",n.proteusShieldBlock,0.0f,1.0f);
    Fix("ProteusWalkGunRate",n.proteusWalkGunRate,0.1f,5.0f);
    Fix("ProteusWalkGunSpread",n.proteusWalkGunSpread,0.0f,10.0f);
    Fix("ProteusDeploySec",n.proteusDeploySec,0.0f,10.0f);
    Fix("ProteusStowSec",n.proteusStowSec,0.0f,10.0f);
    Fix("ProteusDeployTurn",n.proteusDeployTurn,0.0f,2.0f);
    Fix("ProteusDeployGunRate",n.proteusDeployGunRate,0.1f,5.0f);
    Fix("ProteusDeployGunSpread",n.proteusDeployGunSpread,0.0f,10.0f);
    Fix("ProteusViewLift",n.proteusViewLift,0.0f,60.0f);
    Fix("ProteusHeatSec",n.proteusHeatSec,1.0f,600.0f);
    Fix("ProteusCoolSec",n.proteusCoolSec,1.0f,600.0f);
    Fix("ProteusResumeHeat",n.proteusResumeHeat,0.0f,0.95f);
    Fix("ProteusBarrier",n.proteusBarrier,0.0f,5.0f);
    Fix("ProteusBarrierRegenSec",n.proteusBarrierRegenSec,1.0f,3600.0f);
    Fix("ProteusBarrierDelaySec",n.proteusBarrierDelaySec,0.0f,600.0f);
    Fix("ProteusFieldRadius",n.proteusFieldRadius,0.0f,500.0f);
    Fix("ProteusFieldDefense",n.proteusFieldDefense,0.0f,0.95f);
    Fix("ProteusFieldAttack",n.proteusFieldAttack,0.0f,5.0f);
    Fix("ProteusFieldFireRate",n.proteusFieldFireRate,1.0f,5.0f);
    Fix("ProteusFieldEnergy",n.proteusFieldEnergy,0.0f,1.0f);
    Fix("ProteusFieldPower",n.proteusFieldPower,0.0f,10000.0f);
    Fix("ProteusGunRate",n.proteusGunRate,0.0f,20.0f);
    Fix("ProteusGunDamage",n.proteusGunDamage,0.0f,1.0e6f);
    n.proteusSalvoCount=static_cast<int>(FixInt("ProteusSalvoCount",n.proteusSalvoCount,1,60));
    Fix("ProteusSalvoDamage",n.proteusSalvoDamage,0.0f,1.0e6f);
    Fix("ProteusSalvoCooldownSec",n.proteusSalvoCooldownSec,0.0f,3600.0f);
    Fix("ProteusSalvoRange",n.proteusSalvoRange,100.0f,1800.0f);   // the gunship shell's reach (jet_bay.cpp kGunshipReach)
    Fix("ProteusPriority",n.proteusPriority,0.05f,1.0f);
    Fix("ProteusPriorityRadius",n.proteusPriorityRadius,0.0f,1000.0f);
    Fix("PrimerHpScale",n.primerHpScale,0.05f,100.0f);
    n.centipedeLinkMax=static_cast<int>(FixInt("CentipedeLinkMax",n.centipedeLinkMax,2,48));
    Fix("CentipedeLinkRange",n.centipedeLinkRange,10.0f,2000.0f);
    Fix("CentipedeWoundDamage",n.centipedeWoundDamage,1.0f,20.0f);
    Fix("PrimerBlood",n.primerBlood,0.0f,5.0f);
    Fix("NpcLaneWidth",n.npcLaneWidth,0.5f,20.0f);
    Fix("NpcLaneLength",n.npcLaneLength,10.0f,1000.0f);
    Fix("NpcFlankDeg",n.npcFlankDeg,10.0f,80.0f);
    Fix("NpcEngageShare",n.npcEngageShare,0.2f,1.0f);
    Fix("NpcDangerRange",n.npcDangerRange,2.0f,100.0f);
    Fix("NpcGrabRange",n.npcGrabRange,0.5f,n.npcDangerRange);
    Fix("NpcCrowd",n.npcCrowd,0.2f,20.0f);
    Fix("NpcRollSec",n.npcRollSec,0.5f,30.0f);
    Fix("NpcRetreatHp",n.npcRetreatHp,0.0f,0.9f);
    Fix("NpcLeash",n.npcLeash,5.0f,500.0f);
    n.npcSquadMin=static_cast<int>(FixInt("NpcSquadMin",n.npcSquadMin,1,16));
    n.npcSquadMax=static_cast<int>(FixInt("NpcSquadMax",n.npcSquadMax,n.npcSquadMin,32));
    Fix("NpcSquadJoinRange",n.npcSquadJoinRange,0.0f,2000.0f);
    n.npcMarkKey=static_cast<int>(FixInt("NpcMarkKey",n.npcMarkKey,0,254));
    Fix("NpcMarkCone",n.npcMarkCone,1.0f,45.0f);
    Fix("NpcGuardRadius",n.npcGuardRadius,2.0f,500.0f);
    Fix("NpcFreeRange",n.npcFreeRange,10.0f,2000.0f);
    Fix("NpcRecruitCooldownSec",n.npcRecruitCooldownSec,0.0f,3600.0f);
    Fix("ScriptNpcSettleSec",n.scriptNpcSettleSec,0.0f,600.0f);
    Fix("TankPostHold",n.tankPostHold,1.0f,100.0f);
    Fix("TankReverseMax",n.tankReverseMax,0.0f,200.0f);
}

constexpr const char* kGainsFixed="the flight controller's gains are fixed";
// Keys no longer read: an old ini that still sets them loads as before, the keys ignored (said once). The flight
// controller's gains became constants (heli.cpp); the heli's mouse lever (HeliMousePitch) became the mouse-aim flight
// (HeliMouseAim, heliaim.h).
void IgnoreRetired() noexcept {
    struct Retired { const wchar_t* key; const char* why; };
    static const Retired kRetired[]={{L"HeliMoveGain",kGainsFixed},{L"HeliBrakeGain",kGainsFixed},{L"HeliClimbGain",kGainsFixed},
                                     {L"HeliHoverLearn",kGainsFixed},{L"HeliMousePitch","superseded by HeliMouseAim"}};
    constexpr int kCount=static_cast<int>(sizeof(kRetired)/sizeof(kRetired[0]));
    static bool said[kCount]{};
    for(int i=0;i<kCount;++i) {
        wchar_t text[8]{};
        GetPrivateProfileStringW(L"VehicleCrew",kRetired[i].key,L"",text,8,iniPath);
        if(!text[0] || said[i])continue;
        said[i]=true;
        Log("CONFIG %ls is no longer read (%s): ignored",kRetired[i].key,kRetired[i].why);
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
    n.playerHeliStopSec=ReadFloat(L"PlayerHeliStopSec",n.playerHeliStopSec);
    n.playerHeliGunSight=ReadBool(L"PlayerHeliGunSight",n.playerHeliGunSight);
    n.stockVehicleHud=ReadBool(L"StockVehicleHud",n.stockVehicleHud);
    n.hideStockGauges=ReadBool(L"HideStockGauges",n.hideStockGauges);
    n.heliYawRate=ReadFloat(L"HeliYawRate",n.heliYawRate);
    n.heliDoorGuns=ReadBool(L"HeliDoorGuns",n.heliDoorGuns);
    n.heliGuardRadius=ReadFloat(L"HeliGuardRadius",n.heliGuardRadius);
    n.heliGuardSpeed=ReadFloat(L"HeliGuardSpeed",n.heliGuardSpeed);
    n.jetPilot=ReadBool(L"JetPilot",n.jetPilot);
    n.jetFuelSec=FixInt("JetFuelSec",ReadInt(L"JetFuelSec",n.jetFuelSec),0,3600);
    n.jetSortieSec=FixInt("JetSortieSec",ReadInt(L"JetSortieSec",n.jetSortieSec),0,3600);
    n.jetAirRaider=ReadBool(L"JetAirRaider",n.jetAirRaider);
    n.jetMissionStrike=ReadBool(L"JetMissionStrike",n.jetMissionStrike);
    n.throwDrones=ReadBool(L"ThrowDrones",n.throwDrones);
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
    n.boardingGun=ReadBool(L"BoardingGun",n.boardingGun);
    n.subHullHp=ReadFloat(L"SubHullHp",n.subHullHp);
    n.subHeavyHit=ReadFloat(L"SubHeavyHit",n.subHeavyHit);
    n.carrierLaser=ReadBool(L"CarrierLaser",n.carrierLaser);
    n.carrierLaserDamage=ReadFloat(L"CarrierLaserDamage",n.carrierLaserDamage);
    n.carrierLaserBreak=ReadFloat(L"CarrierLaserBreak",n.carrierLaserBreak);
    n.vehicleWelding=ReadBool(L"VehicleWelding",n.vehicleWelding);
    n.giantContactCap=ReadBool(L"GiantContactCap",n.giantContactCap);
    n.splitMissileSurface=ReadBool(L"SplitMissileSurface",n.splitMissileSurface);
    n.stockMissilePN=ReadBool(L"StockMissilePN",n.stockMissilePN);
    n.stockMissileNav=ReadFloat(L"StockMissileNav",n.stockMissileNav);
    if(!(n.stockMissileNav>=2.0f && n.stockMissileNav<=6.0f))n.stockMissileNav=3.0f;
    n.playerLockByView=ReadBool(L"PlayerLockByView",n.playerLockByView);
    n.tempestTv=ReadBool(L"TempestTv",n.tempestTv);
    n.tempestTvMouseSpeed=ReadFloat(L"TempestTvMouseSpeed",n.tempestTvMouseSpeed);
    if(!(n.tempestTvMouseSpeed>=0.1f && n.tempestTvMouseSpeed<=5.0f))n.tempestTvMouseSpeed=1.0f;
    n.tempestTvBoost=ReadFloat(L"TempestTvBoost",n.tempestTvBoost);
    if(!(n.tempestTvBoost>=1.0f && n.tempestTvBoost<=10.0f))n.tempestTvBoost=3.0f;
    n.vehicleHud=ReadBool(L"VehicleHud",n.vehicleHud);
    n.vehicleHudCount=ReadInt(L"VehicleHudCount",static_cast<DWORD>(n.vehicleHudCount));
    n.vehicleHudRange=ReadFloat(L"VehicleHudRange",n.vehicleHudRange);
    n.hudScale=ReadFloat(L"HudScale",n.hudScale);
    n.hudLanguage=ReadLanguage(n.hudLanguage);
    n.playerJet=ReadBool(L"PlayerJet",n.playerJet);
    n.playerJetInvertPitch=ReadBool(L"PlayerJetInvertPitch",n.playerJetInvertPitch);
    n.playerJetRamDamage=ReadFloat(L"PlayerJetRamDamage",n.playerJetRamDamage);
    n.playerJetBoostKey=ReadInt(L"PlayerJetBoostKey",static_cast<DWORD>(n.playerJetBoostKey));
    n.playerJetBrakeKey=ReadInt(L"PlayerJetBrakeKey",static_cast<DWORD>(n.playerJetBrakeKey));
    n.playerJetSwitchKey=ReadInt(L"PlayerJetSwitchKey",static_cast<DWORD>(n.playerJetSwitchKey));
    n.playerJetTargetKey=ReadInt(L"PlayerJetTargetKey",static_cast<DWORD>(n.playerJetTargetKey));
    n.playerJetFlareKey=ReadInt(L"PlayerJetFlareKey",static_cast<DWORD>(n.playerJetFlareKey));
    n.playerJetAll=ReadBool(L"PlayerJetAll",n.playerJetAll);
    n.playerJetHailKey=ReadInt(L"PlayerJetHailKey",static_cast<DWORD>(n.playerJetHailKey));
    n.gunshipBoardGunner=ReadBool(L"GunshipBoardGunner",n.gunshipBoardGunner);
    n.gunshipGunnerKey=ReadInt(L"GunshipGunnerKey",static_cast<DWORD>(n.gunshipGunnerKey));
    n.playerJetFlares=ReadInt(L"PlayerJetFlares",static_cast<DWORD>(n.playerJetFlares));
    n.playerJetGearKey=ReadInt(L"PlayerJetGearKey",static_cast<DWORD>(n.playerJetGearKey));
    n.playerJetGearButton=ReadInt(L"PlayerJetGearButton",static_cast<DWORD>(n.playerJetGearButton));
    n.playerJetChuteCutKey=ReadInt(L"PlayerJetChuteCutKey",static_cast<DWORD>(n.playerJetChuteCutKey));
    n.playerJetCatch=ReadInt(L"PlayerJetCatch",n.playerJetCatch ? 1u : 0u)!=0;
    n.playerJetMouseSpeed=ReadFloat(L"PlayerJetMouseSpeed",n.playerJetMouseSpeed);
    n.playerJetRollScale=ReadFloat(L"PlayerJetRollScale",n.playerJetRollScale);
    n.playerJetAimGain=ReadFloat(L"PlayerJetAimGain",n.playerJetAimGain);
    n.playerRotorLift=ReadFloat(L"PlayerRotorLift",n.playerRotorLift);
    n.playerJetMouseFlight=ReadBool(L"PlayerJetMouseFlight",n.playerJetMouseFlight);
    n.heliMouseAim=ReadBool(L"HeliMouseAim",n.heliMouseAim);
    n.heliFlightHud=ReadBool(L"HeliFlightHud",n.heliFlightHud);
    n.jetEntrySmoke=ReadBool(L"JetEntrySmoke",n.jetEntrySmoke);
    n.playerJetGunSight=ReadBool(L"PlayerJetGunSight",n.playerJetGunSight);
    n.playerJetFlightHud=ReadBool(L"PlayerJetFlightHud",n.playerJetFlightHud);
    n.playerJetThreatHud=ReadBool(L"PlayerJetThreatHud",n.playerJetThreatHud);
    n.playerJetLockByView=ReadBool(L"PlayerJetLockByView",n.playerJetLockByView);
    n.turretAimHud=ReadBool(L"TurretAimHud",n.turretAimHud);
    n.jetSound=ReadBool(L"JetSound",n.jetSound);
    n.jetSoundVolume=ReadFloat(L"JetSoundVolume",n.jetSoundVolume);
    n.warnAudio=ReadBool(L"WarnAudio",n.warnAudio);
    n.warnVoice=ReadBool(L"WarnVoice",n.warnVoice);
    n.warnVolume=ReadFloat(L"WarnVolume",n.warnVolume);
    n.vehicleSound=ReadBool(L"VehicleSound",n.vehicleSound);
    n.vehicleEngineVolume=ReadFloat(L"VehicleEngineVolume",n.vehicleEngineVolume);
    n.vehicleTurretVolume=ReadFloat(L"VehicleTurretVolume",n.vehicleTurretVolume);
    n.vehicleReloadVolume=ReadFloat(L"VehicleReloadVolume",n.vehicleReloadVolume);
    n.vehicleGunVolume=ReadFloat(L"VehicleGunVolume",n.vehicleGunVolume);
    n.vehicleMgVolume=ReadFloat(L"VehicleMgVolume",n.vehicleMgVolume);
    n.vehicleMissileVolume=ReadFloat(L"VehicleMissileVolume",n.vehicleMissileVolume);
    n.bigWorld=ReadFloat(L"BigWorld",n.bigWorld);
    n.airSoftEdge=ReadFloat(L"AirSoftEdge",n.airSoftEdge);
    n.airSoftTurns=ReadFloat(L"AirSoftTurns",n.airSoftTurns);
    n.airSoftCeil=ReadFloat(L"AirSoftCeil",n.airSoftCeil);
    n.heliSoftEdge=ReadFloat(L"HeliSoftEdge",n.heliSoftEdge);
    n.vehicleRam=ReadBool(L"VehicleRam",n.vehicleRam);
    n.vehicleRamDamage=ReadFloat(L"VehicleRamDamage",n.vehicleRamDamage);
    n.drill=ReadBool(L"Drill",n.drill);
    n.drillMaxRpm=ReadFloat(L"DrillMaxRpm",n.drillMaxRpm);
    n.drillSpinUpSec=ReadFloat(L"DrillSpinUpSec",n.drillSpinUpSec);
    n.drillSpinDownSec=ReadFloat(L"DrillSpinDownSec",n.drillSpinDownSec);
    n.drillDamage=ReadFloat(L"DrillDamage",n.drillDamage);
    n.drillBreak=ReadFloat(L"DrillBreak",n.drillBreak);
    n.drillHeatSec=ReadFloat(L"DrillHeatSec",n.drillHeatSec);
    n.drillCoolSec=ReadFloat(L"DrillCoolSec",n.drillCoolSec);
    n.drillResumeHeat=ReadFloat(L"DrillResumeHeat",n.drillResumeHeat);
    n.emcBeam=ReadBool(L"EmcBeam",n.emcBeam);
    n.emcChargeSec=ReadFloat(L"EmcChargeSec",n.emcChargeSec);
    n.emcBeamSec=ReadFloat(L"EmcBeamSec",n.emcBeamSec);
    n.emcBlastRadius=ReadFloat(L"EmcBlastRadius",n.emcBlastRadius);
    n.emcBlastShare=ReadFloat(L"EmcBlastShare",n.emcBlastShare);
    n.emcBreak=ReadFloat(L"EmcBreak",n.emcBreak);
    n.sidecar=ReadBool(L"Sidecar",n.sidecar);
    n.sidecarNpcGunner=ReadBool(L"SidecarNpcGunner",n.sidecarNpcGunner);
    n.sidecarNpcRange=ReadFloat(L"SidecarNpcRange",n.sidecarNpcRange);
    n.highCam=ReadBool(L"HighCam",n.highCam);
    n.highCamKey=ReadInt(L"HighCamKey",static_cast<DWORD>(n.highCamKey));
    n.highCamButton=ReadInt(L"HighCamButton",static_cast<DWORD>(n.highCamButton));
    n.highCamHeight=ReadFloat(L"HighCamHeight",n.highCamHeight);
    n.highCamBack=ReadFloat(L"HighCamBack",n.highCamBack);
    n.highCamPitch=ReadFloat(L"HighCamPitch",n.highCamPitch);
    n.nixTorsoTwist=ReadBool(L"NixTorsoTwist",n.nixTorsoTwist);
    n.highCamClass=ReadInt(L"HighCamClass",static_cast<DWORD>(n.highCamClass));
    n.decoupledTurretCam=ReadBool(L"DecoupledTurretCam",n.decoupledTurretCam);
    n.turretCamRate=ReadFloat(L"TurretCamRate",n.turretCamRate);
    n.freeLookKey=ReadInt(L"FreeLookKey",static_cast<DWORD>(n.freeLookKey));
    n.freeLookButton=ReadInt(L"FreeLookButton",static_cast<DWORD>(n.freeLookButton));
    n.gunStabilizer=ReadBool(L"GunStabilizer",n.gunStabilizer);
    n.viewDistance=ReadFloat(L"ViewDistance",n.viewDistance);
    n.map=ReadBool(L"Map",n.map);
    n.mapKey=ReadInt(L"MapKey",static_cast<DWORD>(n.mapKey));
    n.mapButton=ReadInt(L"MapButton",static_cast<DWORD>(n.mapButton));
    n.mapViewDistance=ReadFloat(L"MapViewDistance",n.mapViewDistance);
    n.stockHeliStores=ReadBool(L"StockHeliStores",n.stockHeliStores);
    n.seatSwitch=ReadBool(L"SeatSwitch",n.seatSwitch);
    n.seatNextKey=ReadInt(L"SeatNextKey",static_cast<DWORD>(n.seatNextKey));
    n.seatNumberKeys=ReadBool(L"SeatNumberKeys",n.seatNumberKeys);
    n.seatButton=ReadInt(L"SeatButton",static_cast<DWORD>(n.seatButton));
    n.seatPilot=ReadBool(L"SeatPilot",n.seatPilot);
    n.seatSwitchOnline=ReadBool(L"SeatSwitchOnline",n.seatSwitchOnline);
    n.seatList=ReadBool(L"SeatList",n.seatList);
    n.proteus=ReadBool(L"ProteusRework",n.proteus);
    n.proteusModeKey=ReadInt(L"ProteusModeKey",static_cast<DWORD>(n.proteusModeKey));
    n.proteusModeButton=ReadInt(L"ProteusModeButton",static_cast<DWORD>(n.proteusModeButton));
    n.proteusShieldKey=ReadInt(L"ProteusShieldKey",static_cast<DWORD>(n.proteusShieldKey));
    n.proteusShieldButton=ReadInt(L"ProteusShieldButton",static_cast<DWORD>(n.proteusShieldButton));
    n.proteusMarkKey=ReadInt(L"ProteusMarkKey",static_cast<DWORD>(n.proteusMarkKey));
    n.proteusMarkButton=ReadInt(L"ProteusMarkButton",static_cast<DWORD>(n.proteusMarkButton));
    n.proteusSalvoKey=ReadInt(L"ProteusSalvoKey",static_cast<DWORD>(n.proteusSalvoKey));
    n.proteusTwoSeats=ReadBool(L"ProteusTwoSeats",n.proteusTwoSeats);
    n.proteusWalkSpeed=ReadFloat(L"ProteusWalkSpeed",n.proteusWalkSpeed);
    n.proteusWalkTurn=ReadFloat(L"ProteusWalkTurn",n.proteusWalkTurn);
    n.proteusStepHeight=ReadFloat(L"ProteusStepHeight",n.proteusStepHeight);
    n.proteusShieldSlow=ReadFloat(L"ProteusShieldSlow",n.proteusShieldSlow);
    n.proteusShieldArc=ReadFloat(L"ProteusShieldArc",n.proteusShieldArc);
    n.proteusShieldBlock=ReadFloat(L"ProteusShieldBlock",n.proteusShieldBlock);
    n.proteusWalkGunRate=ReadFloat(L"ProteusWalkGunRate",n.proteusWalkGunRate);
    n.proteusWalkGunSpread=ReadFloat(L"ProteusWalkGunSpread",n.proteusWalkGunSpread);
    n.proteusDeploySec=ReadFloat(L"ProteusDeploySec",n.proteusDeploySec);
    n.proteusStowSec=ReadFloat(L"ProteusStowSec",n.proteusStowSec);
    n.proteusDeployTurn=ReadFloat(L"ProteusDeployTurn",n.proteusDeployTurn);
    n.proteusDeployGunRate=ReadFloat(L"ProteusDeployGunRate",n.proteusDeployGunRate);
    n.proteusDeployGunSpread=ReadFloat(L"ProteusDeployGunSpread",n.proteusDeployGunSpread);
    n.proteusViewLift=ReadFloat(L"ProteusViewLift",n.proteusViewLift);
    n.proteusHeatSec=ReadFloat(L"ProteusHeatSec",n.proteusHeatSec);
    n.proteusCoolSec=ReadFloat(L"ProteusCoolSec",n.proteusCoolSec);
    n.proteusResumeHeat=ReadFloat(L"ProteusResumeHeat",n.proteusResumeHeat);
    n.proteusBarrier=ReadFloat(L"ProteusBarrier",n.proteusBarrier);
    n.proteusBarrierRegenSec=ReadFloat(L"ProteusBarrierRegenSec",n.proteusBarrierRegenSec);
    n.proteusBarrierDelaySec=ReadFloat(L"ProteusBarrierDelaySec",n.proteusBarrierDelaySec);
    n.proteusFieldRadius=ReadFloat(L"ProteusFieldRadius",n.proteusFieldRadius);
    n.proteusFieldDefense=ReadFloat(L"ProteusFieldDefense",n.proteusFieldDefense);
    n.proteusFieldAttack=ReadFloat(L"ProteusFieldAttack",n.proteusFieldAttack);
    n.proteusFieldFireRate=ReadFloat(L"ProteusFieldFireRate",n.proteusFieldFireRate);
    n.proteusFieldEnergy=ReadFloat(L"ProteusFieldEnergy",n.proteusFieldEnergy);
    n.proteusFieldPower=ReadFloat(L"ProteusFieldPower",n.proteusFieldPower);
    n.proteusDriverGun=ReadBool(L"ProteusDriverGun",n.proteusDriverGun);
    n.proteusGunRate=ReadFloat(L"ProteusGunRate",n.proteusGunRate);
    n.proteusGunDamage=ReadFloat(L"ProteusGunDamage",n.proteusGunDamage);
    n.proteusSalvoCount=ReadInt(L"ProteusSalvoCount",static_cast<DWORD>(n.proteusSalvoCount));
    n.proteusSalvoDamage=ReadFloat(L"ProteusSalvoDamage",n.proteusSalvoDamage);
    n.proteusSalvoCooldownSec=ReadFloat(L"ProteusSalvoCooldownSec",n.proteusSalvoCooldownSec);
    n.proteusSalvoRange=ReadFloat(L"ProteusSalvoRange",n.proteusSalvoRange);
    n.proteusPriority=ReadFloat(L"ProteusPriority",n.proteusPriority);
    n.proteusPriorityRadius=ReadFloat(L"ProteusPriorityRadius",n.proteusPriorityRadius);
    n.primer=ReadBool(L"Primer",n.primer);
    n.primerHpScale=ReadFloat(L"PrimerHpScale",n.primerHpScale);
    n.primerFire=ReadBool(L"PrimerFire",n.primerFire);
    n.primerTrace=ReadBool(L"PrimerTrace",n.primerTrace);
    n.centipedeLinkMax=ReadInt(L"CentipedeLinkMax",static_cast<DWORD>(n.centipedeLinkMax));
    n.centipedeLinkRange=ReadFloat(L"CentipedeLinkRange",n.centipedeLinkRange);
    n.centipedeWoundDamage=ReadFloat(L"CentipedeWoundDamage",n.centipedeWoundDamage);
    n.primerBlood=ReadFloat(L"PrimerBlood",n.primerBlood);
    n.customNpcAi=ReadBool(L"CustomNpcAi",n.customNpcAi);
    n.npcFireLane=ReadBool(L"NpcFireLane",n.npcFireLane);
    n.npcLaneWidth=ReadFloat(L"NpcLaneWidth",n.npcLaneWidth);
    n.npcLaneLength=ReadFloat(L"NpcLaneLength",n.npcLaneLength);
    n.npcFlankDeg=ReadFloat(L"NpcFlankDeg",n.npcFlankDeg);
    n.npcWeaponSwitch=ReadBool(L"NpcWeaponSwitch",n.npcWeaponSwitch);
    n.npcEngageShare=ReadFloat(L"NpcEngageShare",n.npcEngageShare);
    n.npcEvade=ReadBool(L"NpcEvade",n.npcEvade);
    n.npcDangerRange=ReadFloat(L"NpcDangerRange",n.npcDangerRange);
    n.npcGrabRange=ReadFloat(L"NpcGrabRange",n.npcGrabRange);
    n.npcCrowd=ReadFloat(L"NpcCrowd",n.npcCrowd);
    n.npcRollSec=ReadFloat(L"NpcRollSec",n.npcRollSec);
    n.npcRetreatHp=ReadFloat(L"NpcRetreatHp",n.npcRetreatHp);
    n.npcLeash=ReadFloat(L"NpcLeash",n.npcLeash);
    n.npcSquadSuccession=ReadBool(L"NpcSquadSuccession",n.npcSquadSuccession);
    n.npcSquadMin=ReadInt(L"NpcSquadMin",static_cast<DWORD>(n.npcSquadMin));
    n.npcSquadMax=ReadInt(L"NpcSquadMax",static_cast<DWORD>(n.npcSquadMax));
    n.npcSquadJoinRange=ReadFloat(L"NpcSquadJoinRange",n.npcSquadJoinRange);
    n.npcBoarding=ReadBool(L"NpcBoarding",n.npcBoarding);
    n.npcGunners=ReadBool(L"NpcGunners",n.npcGunners);
    n.npcMarkKey=ReadInt(L"NpcMarkKey",static_cast<DWORD>(n.npcMarkKey));
    n.npcMarkCone=ReadFloat(L"NpcMarkCone",n.npcMarkCone);
    n.npcGuardRadius=ReadFloat(L"NpcGuardRadius",n.npcGuardRadius);
    n.npcFreeRange=ReadFloat(L"NpcFreeRange",n.npcFreeRange);
    n.npcRecruitCooldownSec=ReadFloat(L"NpcRecruitCooldownSec",n.npcRecruitCooldownSec);
    n.scriptNpcRecruit=ReadBool(L"ScriptNpcRecruit",n.scriptNpcRecruit);
    n.scriptNpcSettleSec=ReadFloat(L"ScriptNpcSettleSec",n.scriptNpcSettleSec);
    n.tankReturnToPost=ReadBool(L"TankReturnToPost",n.tankReturnToPost);
    n.tankPostHold=ReadFloat(L"TankPostHold",n.tankPostHold);
    n.tankReverseMax=ReadFloat(L"TankReverseMax",n.tankReverseMax);
    Validate(n);
    IgnoreRetired();
    Log("CONFIG enabled=%d debug=%d autoCrew=%d delay=%lums range=%.0f bump=%d toGunner=%d heli=%d height=%.0f follow=%.0f engage=%.0f fire=%d",
        n.enabled,n.debug,n.autoCrew,n.crewDelayMs,n.crewRange,n.bump,n.bumpToGunner,
        n.heliPilot,n.heliHeight,n.heliFollow,n.heliRange,n.heliFire);
    Log("CONFIG heli combatRange=%.0f avoid=%d fireHeight=%.0f cone=%.1f missile=%d/%lums landMs=%lu",
        n.heliCombatRange,n.heliAvoid,n.heliFireHeight,n.heliFireCone,n.heliMissile,n.heliMissileMs,n.heliLandMs);
    Log("CONFIG playerHeliStopSec=%.2f gunSight=%d mouseAim=%d flightHud=%d",n.playerHeliStopSec,n.playerHeliGunSight,n.heliMouseAim,
        n.heliFlightHud);
    Log("CONFIG heli speed=%.1f agility=%.1fs yawRate=%.0f doorGuns=%d guardRadius=%.0f guardSpeed=%.1f",n.heliSpeed,n.heliAgility,n.heliYawRate,n.heliDoorGuns,
        n.heliGuardRadius,n.heliGuardSpeed);
    Log("CONFIG soft edge: jets %.0f m / %.1f turns, ceiling %.0f m, helis %.0f m",n.airSoftEdge,n.airSoftTurns,n.airSoftCeil,n.heliSoftEdge);
    Log("CONFIG sub hullHp=%.0f heavyHit=%.0f",n.subHullHp,n.subHeavyHit);
    Log("CONFIG hud vehicles=%d count=%d range=%.0f stockVehicleHud=%d hideStockGauges=%d scale=%.2f language=%d",n.vehicleHud,
        n.vehicleHudCount,n.vehicleHudRange,n.stockVehicleHud,n.hideStockGauges,n.hudScale,n.hudLanguage);
    Log("CONFIG playerJet=%d invertPitch=%d ramDamage=%.2f boostKey=0x%X brakeKey=0x%X switchKey=0x%X mouse=%.2f rotorLift=%.2f jetSound=%d volume=%.2f",
        n.playerJet,n.playerJetInvertPitch,n.playerJetRamDamage,n.playerJetBoostKey,n.playerJetBrakeKey,n.playerJetSwitchKey,n.playerJetMouseSpeed,
        n.playerRotorLift,n.jetSound,n.jetSoundVolume);
    Log("CONFIG playerJet hud gunSight=%d flight=%d threats=%d lockByView=%d turretAimHud=%d; warnings audio=%d voice=%d volume=%.2f",
        n.playerJetGunSight,n.playerJetFlightHud,n.playerJetThreatHud,n.playerJetLockByView,n.turretAimHud,n.warnAudio,n.warnVoice,
        n.warnVolume);
    Log("CONFIG playerJet gearKey=0x%X gearButton=0x%X",n.playerJetGearKey,n.playerJetGearButton);
    Log("CONFIG vehicleSound=%d engine=%.2f turret=%.2f reload=%.2f gun=%.2f mg=%.2f missile=%.2f",n.vehicleSound,n.vehicleEngineVolume,
        n.vehicleTurretVolume,n.vehicleReloadVolume,n.vehicleGunVolume,n.vehicleMgVolume,n.vehicleMissileVolume);
    Log("CONFIG playerJetAll=%d hailKey=0x%X gunshipBoardGunner=%d gunnerKey=0x%X",n.playerJetAll,n.playerJetHailKey,n.gunshipBoardGunner,
        n.gunshipGunnerKey);
    Log("CONFIG jet pilot=%d fuel=%lus sortie=%lus airRaider=%d missionStrike=%d throwDrones=%d",n.jetPilot,n.jetFuelSec,
        n.jetSortieSec,n.jetAirRaider,n.jetMissionStrike,n.throwDrones);
    Log("CONFIG primer=%d hpScale=%.2f fire=%d trace=%d centipede linkMax=%d linkRange=%.0f woundDamage=%.1f blood=%.2f",n.primer,
        n.primerHpScale,n.primerFire,n.primerTrace,n.centipedeLinkMax,n.centipedeLinkRange,n.centipedeWoundDamage,n.primerBlood);
    Log("CONFIG customNpcAi=%d lane=%d width=%.1f length=%.0f flank=%.0f switch=%d engage=%.2f evade=%d danger=%.0f grab=%.1f crowd=%.1f roll=%.1fs retreatHp=%.2f leash=%.0f",
        n.customNpcAi,n.npcFireLane,n.npcLaneWidth,n.npcLaneLength,n.npcFlankDeg,n.npcWeaponSwitch,n.npcEngageShare,n.npcEvade,
        n.npcDangerRange,n.npcGrabRange,n.npcCrowd,n.npcRollSec,n.npcRetreatHp,n.npcLeash);
    Log("CONFIG npcSquadSuccession=%d min=%d max=%d joinRange=%.0f",n.npcSquadSuccession,n.npcSquadMin,n.npcSquadMax,n.npcSquadJoinRange);
    Log("CONFIG npc markKey=0x%X markCone=%.0f boarding=%d gunners=%d",n.npcMarkKey,n.npcMarkCone,n.npcBoarding,n.npcGunners);
    Log("CONFIG npc guardRadius=%.0f freeRange=%.0f recruitCooldown=%.0fs",n.npcGuardRadius,n.npcFreeRange,n.npcRecruitCooldownSec);
    Log("CONFIG scriptNpcRecruit=%d settle=%.1fs",n.scriptNpcRecruit,n.scriptNpcSettleSec);
    Log("CONFIG tankReturnToPost=%d hold=%.1f reverseMax=%.0f",n.tankReturnToPost,n.tankPostHold,n.tankReverseMax);
    Log("CONFIG ground pilot=%d follow=%.0f range=%.0f leash=%.0f fire=%d",n.groundPilot,n.groundFollow,
        n.groundRange,n.groundLeash,n.groundFire);
    Log("CONFIG drill=%d maxRpm=%.0f spinUp=%.1fs spinDown=%.1fs damage=%.0f/s break=%.0f/s heat=%.0fs cool=%.0fs resume=%.0f%%",n.drill,
        n.drillMaxRpm,n.drillSpinUpSec,n.drillSpinDownSec,n.drillDamage,n.drillBreak,n.drillHeatSec,n.drillCoolSec,n.drillResumeHeat*100.0f);
    Log("CONFIG vehicleRam=%d damage=%.2f",n.vehicleRam,n.vehicleRamDamage);
    Log("CONFIG emcBeam=%d charge=%.1fs beam=%.1fs blast=%.0fm x%.2f break=%.0f/s",n.emcBeam,n.emcChargeSec,n.emcBeamSec,n.emcBlastRadius,
        n.emcBlastShare,n.emcBreak);
    Log("CONFIG sidecar=%d npcGunner=%d npcRange=%.0f",n.sidecar,n.sidecarNpcGunner,n.sidecarNpcRange);
    Log("CONFIG highCam=%d key=0x%X button=0x%X height=%.0f back=%.0f pitch=%.0f",n.highCam,n.highCamKey,n.highCamButton,n.highCamHeight,
        n.highCamBack,n.highCamPitch);
    Log("CONFIG nixTorsoTwist=%d",n.nixTorsoTwist);
    Log("CONFIG map=%d key=0x%X button=0x%X viewDistance=%.0f",n.map,n.mapKey,n.mapButton,n.mapViewDistance);
    Log("CONFIG stockHeliStores=%d seatSwitch=%d nextKey=0x%X numberKeys=%d button=0x%X pilot=%d online=%d list=%d",n.stockHeliStores,n.seatSwitch,
        n.seatNextKey,n.seatNumberKeys,n.seatButton,n.seatPilot,n.seatSwitchOnline,n.seatList);
    Log("CONFIG proteus=%d keys mode=0x%X/0x%X shield=0x%X/0x%X mark=0x%X/0x%X salvo=0x%X twoSeats=%d walk x%.2f turn x%.2f step %.1fm shieldSlow %.2f arc %.0f block %.2f",
        n.proteus,n.proteusModeKey,n.proteusModeButton,n.proteusShieldKey,n.proteusShieldButton,n.proteusMarkKey,n.proteusMarkButton,n.proteusSalvoKey,
        n.proteusTwoSeats,n.proteusWalkSpeed,n.proteusWalkTurn,n.proteusStepHeight,n.proteusShieldSlow,n.proteusShieldArc,n.proteusShieldBlock);
    Log("CONFIG proteus guns walk x%.2f/x%.2f deployed x%.2f/x%.2f stagger %.1f/%.1fs deployTurn %.2f lift %.0fm heat %.0f/%.0fs resume %.2f barrier %.2f regen %.0fs delay %.0fs",
        n.proteusWalkGunRate,n.proteusWalkGunSpread,n.proteusDeployGunRate,n.proteusDeployGunSpread,n.proteusDeploySec,n.proteusStowSec,n.proteusDeployTurn,
        n.proteusViewLift,n.proteusHeatSec,n.proteusCoolSec,n.proteusResumeHeat,n.proteusBarrier,n.proteusBarrierRegenSec,n.proteusBarrierDelaySec);
    Log("CONFIG proteus field %.0fm defense %.2f attack %.2f fireRate %.2f energy %.2f power %.0f; gun=%d %.1f/s %.0f; salvo %d x %.0f cooldown %.0fs range %.0f; priority %.2f within %.0fm",
        n.proteusFieldRadius,n.proteusFieldDefense,n.proteusFieldAttack,n.proteusFieldFireRate,n.proteusFieldEnergy,n.proteusFieldPower,n.proteusDriverGun,
        n.proteusGunRate,n.proteusGunDamage,n.proteusSalvoCount,n.proteusSalvoDamage,n.proteusSalvoCooldownSec,n.proteusSalvoRange,n.proteusPriority,
        n.proteusPriorityRadius);
    Log("CONFIG rescue sea=%d below=%.1f autoBoard=%d boardingGun=%d",n.seaRescue,n.rescueBelow,n.rescueAutoBoard,n.boardingGun);
    Log("CONFIG carrierLaser=%d damage=%.0f break=%.2f",n.carrierLaser,n.carrierLaserDamage,n.carrierLaserBreak);
    Log("CONFIG calls next=%#lx prev=%#lx (0: off)",n.callNextKey,n.callPrevKey);
    Log("CONFIG physics vehicleWelding=%d giantContactCap=%d splitMissileSurface=%d stockMissilePN=%d nav=%.1f playerLockByView=%d tempestTv=%d (mouse %.1f, boost x%.1f)",
        n.vehicleWelding,n.giantContactCap,n.splitMissileSurface,n.stockMissilePN,n.stockMissileNav,n.playerLockByView,
        n.tempestTv,n.tempestTvMouseSpeed,n.tempestTvBoost);
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
    CheckPauseFlag();       // the game's pause flag (the game clock and the HUD stop with it): read only
    // Then the installs, in dependency order. From the first patch on the plugin stays loaded whatever fails
    // after (true below): the loader unloading the DLL would leave patched slots pointing at unloaded code.
    InstallBody506();       // the one 506 physics hook: before the jets, the carrier and the player jets
    InstallBulletPass();    // the bullets' candidate hook: the jets' wingmen and the sidecar's passengers, whatever the heli profile
    if(heli) {
        InstallDoorGuns();  // the 410's door guns are part of the heli pilot
        InstallJets();      // the jets and the carrier are flown from HeliFrame: no heli pilot, none of them
        InstallSub();
    } else Log("JET / SUB off: they are flown from the heli pilot's frame, which is off");
    InstallBoarding();      // after the heli profile's board button check
    InstallPlayerJets();    // its frame is the vehicles' own input; it needs only the 506 physics hook
    InstallVehicleRam();    // the ground vehicles' ram (its charges are the jets' impact charges: jet_bay.cpp)
    InstallDrill();         // the drill tank (its charges are the jets' shells: jet_bay.cpp, so with the heli profile)
    InstallEmc();           // the EMC's charged beam (its rounds are the jets' shells too: jet_bay.cpp)
    InstallKatyusha();      // the Katyusha's launcher pose: the arc onto the camera's ground point, the telescopic ram
    InstallNix();           // the Nix's torso twist: its own update (slot 4) chained, apart from the crews' input slot
    InstallTurretCam();     // the riding camera of a turret (decoupled from it, free look, the high view's placement)
    InstallStabilizer();    // the gun stabilizer, after the aim steps the turret camera chains (it runs from its hook)
    InstallProteus();       // chain both aims after the turret camera and plain-aim stabilizer hooks
    InstallMap();           // the map view (the player's camera overhead, their input held while it is open)
    InstallPhysics();       // vehicle chassis welding and the giants' contact cap (physics.cpp), the sidecar's level hook
    InstallGunnerRecoil();  // a remote gunner's recoil on the vehicle's authority (gunnerrecoil.cpp)
    InstallSidecar();       // the sidecar motorcycle's gunner (sidecar.cpp)
    InstallLaser();
    InstallGauge();         // the follower gauge's draw (subcarrier.cpp): the carriers' gauges and the vehicle HUD
    InstallHud();
    InstallRounds();        // the stock vehicles' and helis' impact points: the rounds as the game flies them
    InstallStockGauges();   // the stock weapon gauge where our HUD lists the weapons, the fuel tanks it showed
    InstallGlyphLock();     // the game's own text, wrong or missing characters (glyphs.cpp)
    InstallJetSound();
    InstallVehicleSound();  // the ground vehicles' engines, turrets, loaders and main guns (vehsound.cpp)
    InstallMissiles();
    InstallSplitMissiles(); // the stock split missiles' split distance to the target's surface
    InstallGuidance();      // the stock homing rounds by proportional navigation
    InstallStores();        // before any mission builds a jet: the 506 builds a weapon for every holder
    InstallLockon();        // every lock-on weapon's search order: the player's nearest the view first
    InstallSeatSwitch();    // the player moving between seats (the stock board button's steps, checked)
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
