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
#include <iterator>
#include <new>
#pragma warning(push)
#pragma warning(disable:4201)
#include "PluginAPI.h"
#pragma warning(pop)
#include "crash_dump.h"
#include "log_buffer.h"
#include "crew.h"
#include "gravity.h"
#include "floatgun.h"
#include "firstperson.h"
#include "thermal.h"
#include "pvp.h"
#include "team_native.h"
#include "room_pvp.h"
#include "edf41bike.h"
#include "mod_room.h"
#include "outfit41.h"
#include "weapon_records.h"
#include "room_ui.h"
#include "support_soldier.h"
#include "airdrop.h"
#include "player_vehicle.h"
#include "support_config.h"
#include "lockon.h"
#include "turretaim.h"
#include "hudscale.h"
#include "hudtext.h"
#include "gunnerrecoil.h"
#include "memory.h"
#include "subcarrier.h"
#include "enemy_control.h"
#include "greyboss_parts.h"
#include "enemy_control_net.h"
#include "enemy_control_legacy.h"
#include "enemy_control_weapon_aim.h"
#include "enemy_control_radar.h"
#include "pvp_controlled_spawn.h"
#include "enemy_control_pvp_native.h"
#include "vulcan_rotor.h"
#include "edf/host.h"
#include "version.h"
#include "build_id.h"
#include "pack_notice.h"
#include "profile_sigs.h"
#include "keyconfig.h"
#include "pad_hold.h"

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

using sigs::kSignatures;   // profile_sigs.h

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
    float v=fallback;
    if(!edf::ParseFloat(text,&v))Log("CONFIG %ls=%ls is not a number: %g used",key,text,fallback);
    return v;
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
// A mission pack run's ini id (EDF5CampaignContent, TestRangeContent, EDF41Content): 0 or the run's fixed id
// (mission_packs.h). An older installer's id (one that depended on the mods installed: 3..9 for most) is logged and
// taken as 0, and the player told once a session. Its packs cannot be owned under the new ids: that installer's
// CONFIG.SGO has them at the old ids (a room would publish an id no newer machine has), its lists left out a missing
// mission's row (a row would name another mission than on a newer machine), and it wrote no EDF6VehicleCrew.packs.ini
// (no identity to publish or check, no save layout). Only the installer can write those; the next install does.
int PackContent(const char* key,int v,int fixed) noexcept {
    if(v==0 || v==fixed)return v;
    Log("CONFIG %s=%d is an older installer's mission pack id (now fixed %d): its packs are not owned until the "
        "installer runs again",key,v,fixed);
    wchar_t text[400]{};
    swprintf_s(text,L"EDF6VehicleCrew.ini 里的 %hs=%d 是旧版安装器写的任务包编号（现在固定为 %d）。\n\n"
        L"新版全军出击的任务包需要新版安装器重新生成（编号、关卡列表和任务包身份都变了），在重新运行安装器之前，"
        L"EDF5 / 测试场 / EDF4.1 任务包会显示为「尚未购买」。\n\n请退出游戏，运行新版全军出击安装器选「安装」。",key,v,fixed);
    notice::Post("ini-old-pack-ids",L"全军出击：请重新运行安装器",text,notice::kOnce);
    return 0;
}
// EDF6VehicleCrew.packs.ini beside the ini (tools/make_edf5_campaign.py PACKS_FILE): [Pack<id>] Tag, Digest, Rows,
// Missing, Layout, StableRows, and the older save layouts' maps (Legacy, Older + From<layout>) for each installed pack.
// A section that does not parse is left out (its pack not owned) and logged.
void LoadMissionPacks(packs::Table& table) noexcept {
    table=packs::Table{};
    wchar_t path[MAX_PATH]{};
    wcscpy_s(path,iniPath);
    wchar_t* dot=wcsrchr(path,L'.');
    if(!dot || wcscpy_s(dot,MAX_PATH-(dot-path),L".packs.ini"))return;
    // ASCII values (the installer writes nothing else): read wide, so a game path in any code page works.
    const auto narrow=[](const wchar_t* w,char* out,std::size_t size){
        std::size_t i=0;
        for(;w[i] && i+1<size;++i)out[i]=w[i]<0x80 ? static_cast<char>(w[i]) : '?';
        out[i]=0;
    };
    static wchar_t longText[packs::kMaxRows*5+16];   // "<n>:" and up to 512 rows of "-1," / "511,"
    static char longValue[sizeof(longText)/sizeof(wchar_t)];
    int found=0;
    for(int id=packs::kBase;id<packs::kBase+packs::kCount;++id) {
        wchar_t section[16]{},value[packs::kMaxRows/4+2]{};
        char tag[32]{},digest[32]{},mask[packs::kMaxRows/4+2]{},layout[32]{},older[packs::kMaxFrom*9+8]{};
        swprintf_s(section,L"Pack%d",id);
        GetPrivateProfileStringW(section,L"Tag",L"",value,static_cast<DWORD>(std::size(value)),path);
        narrow(value,tag,sizeof(tag));
        if(!tag[0])continue;
        GetPrivateProfileStringW(section,L"Digest",L"",value,static_cast<DWORD>(std::size(value)),path);
        narrow(value,digest,sizeof(digest));
        GetPrivateProfileStringW(section,L"Missing",L"",value,static_cast<DWORD>(std::size(value)),path);
        narrow(value,mask,sizeof(mask));
        GetPrivateProfileStringW(section,L"Layout",L"",value,static_cast<DWORD>(std::size(value)),path);
        narrow(value,layout,sizeof(layout));
        GetPrivateProfileStringW(section,L"Older",L"",value,static_cast<DWORD>(std::size(value)),path);
        narrow(value,older,sizeof(older));
        const int rows=static_cast<int>(GetPrivateProfileIntW(section,L"Rows",0,path));
        const int stable=static_cast<int>(GetPrivateProfileIntW(section,L"StableRows",static_cast<UINT>(-1),path));
        packs::Pack& pack=table.packs[id-packs::kBase];
        bool ok=packs::Make(id,tag,digest,rows,mask,pack) && packs::SetLayout(pack,layout,stable);
        GetPrivateProfileStringW(section,L"Legacy",L"",longText,static_cast<DWORD>(std::size(longText)),path);
        narrow(longText,longValue,sizeof(longValue));
        ok=ok && packs::AddFrom(pack,0,longValue);
        // Older=<layout>,<layout>,...: each one's map in From<layout>
        for(char* at=older;ok && *at;) {
            char* comma=std::strchr(at,',');
            if(comma)*comma=0;
            std::uint32_t from=0;
            wchar_t key[16]{};
            ok=packs::ParseLayout(at,from);
            if(ok) {
                swprintf_s(key,L"From%hs",at);
                GetPrivateProfileStringW(section,key,L"",longText,static_cast<DWORD>(std::size(longText)),path);
                narrow(longText,longValue,sizeof(longValue));
                ok=longValue[0] && packs::AddFrom(pack,from,longValue);
            }
            at=comma ? comma+1 : at+std::strlen(at);
        }
        if(ok)++found;
        else {
            pack=packs::Pack{};
            Log("CONFIG EDF6VehicleCrew.packs.ini [Pack%d] malformed (Tag=%s Rows=%d Layout=%s Older=%s): that pack is not owned",
                id,tag,rows,layout,older);
        }
    }
    static int logged=-1;
    if(found!=logged)Log("CONFIG mission packs: %d installed (EDF6VehicleCrew.packs.ini)",found);
    logged=found;
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
    Fix("GravityDamage",n.gravityDamage,0.0f,1000.0f);
    Fix("GravityThrowSpeed",n.gravityThrowSpeed,5.0f,90.0f);
    n.floatGuns=static_cast<int>(FixInt("FloatGuns",n.floatGuns,0,2));
    n.floatGunMost=static_cast<int>(FixInt("FloatGunMost",n.floatGunMost,1,8));
    Fix("FloatGunSec",n.floatGunSec,0.0f,3600.0f);
    Fix("FloatGunReach",n.floatGunReach,1.0f,200.0f);
    n.playerJetBoostKey=static_cast<int>(FixInt("PlayerJetBoostKey",n.playerJetBoostKey,0,254));
    n.playerJetBrakeKey=static_cast<int>(FixInt("PlayerJetBrakeKey",n.playerJetBrakeKey,0,254));
    n.playerJetSwitchKey=static_cast<int>(FixInt("PlayerJetSwitchKey",n.playerJetSwitchKey,0,254));
    n.vehicleSecondarySwitchKey=static_cast<int>(FixInt("VehicleSecondarySwitchKey",n.vehicleSecondarySwitchKey,0,254));
    n.vehicleSecondarySwitchButton=static_cast<int>(FixInt("VehicleSecondarySwitchButton",n.vehicleSecondarySwitchButton,0,0xFFFF));
    n.playerJetTargetKey=static_cast<int>(FixInt("PlayerJetTargetKey",n.playerJetTargetKey,0,254));
    n.playerJetFlareKey=static_cast<int>(FixInt("PlayerJetFlareKey",n.playerJetFlareKey,0,254));
    n.playerJetHailKey=static_cast<int>(FixInt("PlayerJetHailKey",n.playerJetHailKey,0,254));
    n.gunshipGunnerKey=static_cast<int>(FixInt("GunshipGunnerKey",n.gunshipGunnerKey,0,254));
    n.gunshipGatlingKey=static_cast<int>(FixInt("GunshipGatlingKey",n.gunshipGatlingKey,0,254));
    n.playerJetFlares=static_cast<int>(FixInt("PlayerJetFlares",n.playerJetFlares,0,99));
    n.playerJetGearKey=static_cast<int>(FixInt("PlayerJetGearKey",n.playerJetGearKey,0,254));
    n.playerJetGearPad=static_cast<int>(FixInt("PlayerJetGearPadButton",n.playerJetGearPad,0,0xFFFF));
    n.playerJetChuteCutKey=static_cast<int>(FixInt("PlayerJetChuteCutKey",n.playerJetChuteCutKey,0,254));
    Fix("PlayerJetMouseSpeed",n.playerJetMouseSpeed,0.1f,10.0f);
    Fix("SazabiWalk",n.sazabiWalk,1.0f,60.0f);
    Fix("SazabiRun",n.sazabiRun,1.0f,80.0f);
    Fix("SazabiFly",n.sazabiFly,1.0f,150.0f);
    Fix("SazabiDash",n.sazabiDash,1.0f,200.0f);
    Fix("SazabiClimb",n.sazabiClimb,1.0f,100.0f);
    Fix("SazabiGravity",n.sazabiGravity,1.0f,60.0f);
    Fix("SazabiTurn",n.sazabiTurn,10.0f,360.0f);
    Fix("SazabiMouseTurn",n.sazabiMouseTurn,0.05f,10.0f);
    Fix("SazabiThrusterSec",n.sazabiThrusterSec,0.5f,120.0f);
    Fix("SazabiThrusterRegen",n.sazabiThrusterRegen,0.0f,5.0f);
    Fix("SazabiAssistCone",n.sazabiAssistCone,0.5f,30.0f);
    Fix("SazabiAssistRange",n.sazabiAssistRange,50.0f,800.0f);
    Fix("SazabiAssistPull",n.sazabiAssistPull,0.0f,20.0f);
    n.sazabiLockKey=static_cast<int>(FixInt("SazabiLockKey",n.sazabiLockKey,0,254));
    n.sazabiLockButton=static_cast<int>(FixInt("SazabiLockButton",n.sazabiLockButton,0,255));
    n.sazabiDashKey=static_cast<int>(FixInt("SazabiDashKey",n.sazabiDashKey,0,254));
    n.sazabiDescendKey=static_cast<int>(FixInt("SazabiDescendKey",n.sazabiDescendKey,0,254));
    n.sazabiFunnelKey=static_cast<int>(FixInt("SazabiFunnelKey",n.sazabiFunnelKey,0,254));
    n.sazabiFunnelButton=static_cast<int>(FixInt("SazabiFunnelButton",n.sazabiFunnelButton,0,255));
    n.sazabiCannonKey=static_cast<int>(FixInt("SazabiMegaCannonKey",n.sazabiCannonKey,0,254));
    n.sazabiCannonButton=static_cast<int>(FixInt("SazabiCannonButton",n.sazabiCannonButton,0,255));
    n.sazabiMeleeKey=static_cast<int>(FixInt("SazabiMeleeKey",n.sazabiMeleeKey,0,254));
    n.sazabiMeleeButton=static_cast<int>(FixInt("SazabiMeleeButton",n.sazabiMeleeButton,0,255));
    n.sazabiGuardKey=static_cast<int>(FixInt("SazabiGuardKey",n.sazabiGuardKey,0,254));
    n.sazabiGuardButton=static_cast<int>(FixInt("SazabiGuardButton",n.sazabiGuardButton,0,255));
    Fix("SazabiGuardShare",n.sazabiGuardShare,0.0f,1.0f);
    Fix("SazabiAxeDamage",n.sazabiAxeDamage,0.0f,1000000.0f);
    Fix("SazabiCannonDamage",n.sazabiCannonDamage,0.0f,100000.0f);
    Fix("SazabiFunnelDamage",n.sazabiFunnelDamage,0.0f,100000.0f);
    Fix("DreadnoughtSpeed",n.dreadnoughtSpeed,1.0f,200.0f);
    Fix("DreadnoughtBoost",n.dreadnoughtBoost,1.0f,4.0f);
    Fix("DreadnoughtAccel",n.dreadnoughtAccel,0.5f,40.0f);
    Fix("DreadnoughtClimb",n.dreadnoughtClimb,1.0f,100.0f);
    Fix("DreadnoughtTurn",n.dreadnoughtTurn,1.0f,90.0f);
    Fix("DreadnoughtFloor",n.dreadnoughtFloor,0.0f,500.0f);
    n.dreadnoughtBoostKey=static_cast<int>(FixInt("DreadnoughtBoostKey",n.dreadnoughtBoostKey,0,254));
    n.dreadnoughtDescendKey=static_cast<int>(FixInt("DreadnoughtDescendKey",n.dreadnoughtDescendKey,0,254));
    n.dreadnoughtStrafeLeftKey=static_cast<int>(FixInt("DreadnoughtStrafeLeftKey",n.dreadnoughtStrafeLeftKey,0,254));
    n.dreadnoughtStrafeRightKey=static_cast<int>(FixInt("DreadnoughtStrafeRightKey",n.dreadnoughtStrafeRightKey,0,254));
    n.dreadnoughtBoostButton=static_cast<int>(FixInt("DreadnoughtBoostButton",n.dreadnoughtBoostButton,0,255));
    n.dreadnoughtDescendButton=static_cast<int>(FixInt("DreadnoughtDescendButton",n.dreadnoughtDescendButton,0,255));
    n.dreadnoughtSeatButton=static_cast<int>(FixInt("DreadnoughtSeatButton",n.dreadnoughtSeatButton,0,255));
    n.dreadnoughtHangarKey=static_cast<int>(FixInt("DreadnoughtHangarKey",n.dreadnoughtHangarKey,0,254));
    n.dreadnoughtHangarButton=static_cast<int>(FixInt("DreadnoughtHangarButton",n.dreadnoughtHangarButton,0,255));
    n.dreadnoughtAheadKey=static_cast<int>(FixInt("DreadnoughtAheadKey",n.dreadnoughtAheadKey,0,254));
    n.dreadnoughtAheadButton=static_cast<int>(FixInt("DreadnoughtAheadButton",n.dreadnoughtAheadButton,0,255));
    Fix("PlayerJetRollScale",n.playerJetRollScale,0.5f,2.0f);
    Fix("PlayerJetAimGain",n.playerJetAimGain,0.5f,2.0f);
    Fix("PlayerRotorLift",n.playerRotorLift,0.0f,4.0f);
    if(n.bigWorld!=0.0f)Fix("BigWorld",n.bigWorld,3000.0f,20000.0f);
    Fix("AirSoftEdge",n.airSoftEdge,0.0f,3000.0f);
    Fix("AirSoftTurns",n.airSoftTurns,0.0f,3.0f);
    Fix("AirSoftCeil",n.airSoftCeil,0.0f,1000.0f);
    Fix("HeliSoftEdge",n.heliSoftEdge,0.0f,1000.0f);
    // -1 (any negative): unlimited (crew.h UnlimitedViewDistance); 0: the mission's own; else from 1000 m, no upper limit.
    if(n.viewDistance<0.0f)n.viewDistance=kUnlimitedView;
    else if(n.viewDistance!=0.0f)Fix("ViewDistance",n.viewDistance,1000.0f,1.0e6f);
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
    Fix("DrillOverheatSec",n.drillOverheatSec,1.0f,600.0f);
    Fix("DrillKillCool",n.drillKillCool,0.0f,1.0f);
    Fix("DrillLaunchRange",n.drillLaunchRange,5.0f,300.0f);
    Fix("DrillLaunchSpeed",n.drillLaunchSpeed,5.0f,400.0f);
    Fix("DrillLaunchDamage",n.drillLaunchDamage,0.0f,1.0e6f);
    Fix("DrillLaunchHeat",n.drillLaunchHeat,0.0f,1.0f);
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
    n.drillLaunchKey=static_cast<int>(FixInt("DrillLaunchKey",n.drillLaunchKey,0,254));
    n.drillLaunchButton=static_cast<int>(FixInt("DrillLaunchButton",n.drillLaunchButton,0,255));
    n.highCamClass=static_cast<int>(FixInt("HighCamClass",n.highCamClass,1,3));
    Fix("TurretCamRate",n.turretCamRate,10.0f,720.0f);
    n.freeLookKey=static_cast<int>(FixInt("FreeLookKey",n.freeLookKey,0,254));
    n.freeLookButton=static_cast<int>(FixInt("FreeLookButton",n.freeLookButton,0,255));
    n.sightZoomKey=static_cast<int>(FixInt("SightZoomKey",n.sightZoomKey,0,254));
    n.sightZoomButton=static_cast<int>(FixInt("SightZoomButton",n.sightZoomButton,0,255));
    n.sightRangeUpKey=static_cast<int>(FixInt("SightRangeUpKey",n.sightRangeUpKey,0,254));
    n.sightRangeDownKey=static_cast<int>(FixInt("SightRangeDownKey",n.sightRangeDownKey,0,254));
    n.sightRangeStep=static_cast<int>(FixInt("SightRangeStep",n.sightRangeStep,0,1000));
    n.mapKey=static_cast<int>(FixInt("MapKey",n.mapKey,0,254));
    n.thermalKey=static_cast<int>(FixInt("VehicleThermalKey",n.thermalKey,0,254));
    n.thermalButton=static_cast<int>(FixInt("VehicleThermalButton",n.thermalButton,0,255));
    n.firstPersonKey=static_cast<int>(FixInt("FirstPersonKey",n.firstPersonKey,0,254));
    Fix("FirstPersonEyeUp",n.fpEyeUp,-0.5f,0.5f);
    Fix("FirstPersonEyeAhead",n.fpEyeAhead,-0.5f,0.5f);
    Fix("FirstPersonViewHeightEase",n.fpViewHeightEase,0.02f,1.0f);
    Fix("FirstPersonCenter",n.fpCenter,0.0f,0.5f);
    Fix("FirstPersonSway",n.fpSway,0.0f,1.0f);
    Fix("FirstPersonGripX",n.fpGripX,-1.0f,1.0f);
    Fix("FirstPersonGripY",n.fpGripY,-1.0f,1.0f);
    Fix("FirstPersonGripZ",n.fpGripZ,-1.0f,2.0f);
    Fix("FirstPersonDualGripX",n.fpDualGripX,-1.0f,1.0f);
    Fix("FirstPersonDualGripY",n.fpDualGripY,-1.0f,1.0f);
    Fix("FirstPersonDualGripZ",n.fpDualGripZ,-1.0f,2.0f);
    Fix("FirstPersonHoldX",n.fpHoldX,-1.0f,1.0f);
    Fix("FirstPersonHoldY",n.fpHoldY,-1.0f,1.0f);
    Fix("FirstPersonHoldZ",n.fpHoldZ,-1.0f,1.0f);
    Fix("FirstPersonScale",n.fpScale,0.05f,1.0f);
    Fix("FirstPersonHoldArc",n.fpHoldArc,0.0f,180.0f);
    Fix("FirstPersonWristMax",n.fpWristMax,0.0f,90.0f);
    if(n.fpNear!=0.0f)Fix("FirstPersonNear",n.fpNear,0.005f,0.1f);
    n.mapButton=static_cast<int>(FixInt("MapButton",n.mapButton,0,0xFFFF));
    if(n.mapViewDistance!=0.0f)Fix("MapViewDistance",n.mapViewDistance,1000.0f,10000.0f);
    n.damageStatsKey=static_cast<int>(FixInt("DamageStatsKey",n.damageStatsKey,0,254));
    n.debugSpawnKey=static_cast<int>(FixInt("DebugSpawnKey",n.debugSpawnKey,0,254));
    n.debugSpawnPrevKey=static_cast<int>(FixInt("DebugSpawnPrevKey",n.debugSpawnPrevKey,0,254));
    n.debugSpawnNextKey=static_cast<int>(FixInt("DebugSpawnNextKey",n.debugSpawnNextKey,0,254));
    n.debugSpawnCategoryKey=static_cast<int>(FixInt("DebugSpawnCategoryKey",n.debugSpawnCategoryKey,0,254));
    n.debugSpawnSpawnKey=static_cast<int>(FixInt("DebugSpawnSpawnKey",n.debugSpawnSpawnKey,0,254));
    Fix("DebugSpawnRange",n.debugSpawnRange,20.0f,3000.0f);
    Fix("DebugSpawnDistance",n.debugSpawnDistance,10.0f,500.0f);
    n.roomLoadoutKey=static_cast<int>(FixInt("RoomLoadoutKey",n.roomLoadoutKey,0,254));
    n.roomLoadoutButton=static_cast<int>(FixInt("RoomLoadoutButton",n.roomLoadoutButton,0,0xFFFF));
    n.proteusModeKey=static_cast<int>(FixInt("ProteusStanceKey",n.proteusModeKey,0,254));
    n.proteusModeButton=static_cast<int>(FixInt("ProteusModeButton",n.proteusModeButton,0,255));
    n.proteusShieldKey=static_cast<int>(FixInt("ProteusShieldKey",n.proteusShieldKey,0,254));
    n.proteusShieldButton=static_cast<int>(FixInt("ProteusShieldButton",n.proteusShieldButton,0,255));
    n.proteusSalvoKey=static_cast<int>(FixInt("ProteusSalvoKey",n.proteusSalvoKey,0,254));
    Fix("ProteusWalkSpeed",n.proteusWalkSpeed,0.2f,4.0f);
    Fix("ProteusWalkTurn",n.proteusWalkTurn,0.2f,4.0f);
    Fix("ProteusStepHeight",n.proteusStepHeight,0.0f,4.0f);   // 4 m: the walkable test's floor (StepNormal 0.2 on the 5 m foot)
    Fix("ProteusShieldSlow",n.proteusShieldSlow,0.0f,1.0f);
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
    Fix("QMarkPointSec",n.qmarkPointSec,1.0f,600.0f);
    Fix("QMarkVolume",n.qmarkVolume,0.0f,2.0f);
    n.npcFormation=static_cast<int>(FixInt("NpcFormation",n.npcFormation,0,10));
    n.npcFormationKey=static_cast<int>(FixInt("NpcFormationCycleKey",n.npcFormationKey,0,254));
    Fix("NpcFormationSpacing",n.npcFormationSpacing,2.0f,30.0f);
    n.npcGuardFormation=static_cast<int>(FixInt("NpcGuardFormation",n.npcGuardFormation,0,10));
    n.npcPickupKey=static_cast<int>(FixInt("NpcPickupKey",n.npcPickupKey,0,254));
    Fix("NpcPickupRange",n.npcPickupRange,10.0f,300.0f);
    Fix("NpcPickupSec",n.npcPickupSec,10.0f,600.0f);
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
// (HeliMouseAim, heliaim.h). The drill's DrillHeatSec became DrillOverheatSec (a new key, so that an ini that still
// has the old 12 s gets the longer default: the installer only adds keys, it never changes a player's value).
void IgnoreRetired() noexcept {
    struct Retired { const wchar_t* key; const char* why; };
    static const Retired kRetired[]={{L"HeliMoveGain",kGainsFixed},{L"HeliBrakeGain",kGainsFixed},{L"HeliClimbGain",kGainsFixed},
                                     {L"HeliHoverLearn",kGainsFixed},{L"HeliMousePitch","superseded by HeliMouseAim"},
                                     {L"DrillHeatSec","superseded by DrillOverheatSec (30 s by default: 12 s overheated too soon)"},
                                     {L"FirstPersonEyeEase","superseded by FirstPersonViewHeightEase (the eye no longer follows the head's sway)"},
                                     {L"ProteusModeKey","superseded by ProteusStanceKey (G by default since 2026-10-26: the 形态 family with the landing gear)"},
                                     {L"NpcFormationKey","superseded by NpcFormationCycleKey (U by default since 2026-10-26: T is the stock Shotcut)"},
                                     {L"SazabiCannonKey","superseded by SazabiMegaCannonKey (X by default since 2026-10-26: E is the stock get-off)"},
                                     {L"PlayerJetGearButton","superseded by PlayerJetGearPadButton (the d-pad down held, since 2026-10-26: L3 is the free look)"}};
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
    n.medicGunnerAim=ReadBool(L"MedicGunnerAim",n.medicGunnerAim);
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
    n.supportPtStart=static_cast<int>(FixInt("SupportPtStart",ReadInt(L"SupportPtStart",n.supportPtStart),0,1000000));
    n.supportPtMost=static_cast<int>(FixInt("SupportPtMost",ReadInt(L"SupportPtMost",n.supportPtMost),1000,1000000));
    n.supportPtScale=ReadFloat(L"SupportPtScale",n.supportPtScale);
    if(!(n.supportPtScale>=0.0f) || n.supportPtScale>100.0f){Log("CONFIG SupportPtScale=%.2f out of 0..100: 1",n.supportPtScale);n.supportPtScale=1.0f;}
    n.seaRescue=ReadBool(L"SeaRescue",n.seaRescue);
    n.rescueBelow=ReadFloat(L"RescueBelow",n.rescueBelow);
    n.rescueAutoBoard=ReadBool(L"RescueAutoBoard",n.rescueAutoBoard);
    n.seaRescueCooldownSec=FixInt("SeaRescueCooldownSec",ReadInt(L"SeaRescueCooldownSec",static_cast<int>(n.seaRescueCooldownSec)),0,3600);
    n.boardingGun=ReadBool(L"BoardingGun",n.boardingGun);
    n.subHullHp=ReadFloat(L"SubHullHp",n.subHullHp);
    n.subHeavyHit=ReadFloat(L"SubHeavyHit",n.subHeavyHit);
    n.carrierLaser=ReadBool(L"CarrierLaser",n.carrierLaser);
    n.carrierLaserDamage=ReadFloat(L"CarrierLaserDamage",n.carrierLaserDamage);
    n.carrierLaserBreak=ReadFloat(L"CarrierLaserBreak",n.carrierLaserBreak);
    n.vehicleWelding=ReadBool(L"VehicleWelding",n.vehicleWelding);
    n.giantContactCap=ReadBool(L"GiantContactCap",n.giantContactCap);
    n.splitMissileSurface=ReadBool(L"SplitMissileSurface",n.splitMissileSurface);
    n.supportUnitOnAllies=ReadBool(L"SupportUnitOnAllies",n.supportUnitOnAllies);
    n.routeGuidePath=ReadBool(L"RouteGuidePath",n.routeGuidePath);
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
    n.vehicleSecondarySwitchKey=ReadInt(L"VehicleSecondarySwitchKey",static_cast<DWORD>(n.vehicleSecondarySwitchKey));
    n.vehicleSecondarySwitchButton=ReadInt(L"VehicleSecondarySwitchButton",static_cast<DWORD>(n.vehicleSecondarySwitchButton));
    n.playerJetTargetKey=ReadInt(L"PlayerJetTargetKey",static_cast<DWORD>(n.playerJetTargetKey));
    n.playerJetFlareKey=ReadInt(L"PlayerJetFlareKey",static_cast<DWORD>(n.playerJetFlareKey));
    n.playerJetAll=ReadBool(L"PlayerJetAll",n.playerJetAll);
    n.playerJetHailKey=ReadInt(L"PlayerJetHailKey",static_cast<DWORD>(n.playerJetHailKey));
    n.gunshipBoardGunner=ReadBool(L"GunshipBoardGunner",n.gunshipBoardGunner);
    n.gunshipGunnerKey=ReadInt(L"GunshipGunnerKey",static_cast<DWORD>(n.gunshipGunnerKey));
    n.gunshipGatlingKey=ReadInt(L"GunshipGatlingKey",static_cast<DWORD>(n.gunshipGatlingKey));
    n.playerJetFlares=ReadInt(L"PlayerJetFlares",static_cast<DWORD>(n.playerJetFlares));
    n.playerJetGearKey=ReadInt(L"PlayerJetGearKey",static_cast<DWORD>(n.playerJetGearKey));
    n.playerJetGearPad=ReadInt(L"PlayerJetGearPadButton",static_cast<DWORD>(n.playerJetGearPad));
    n.playerJetChuteCutKey=ReadInt(L"PlayerJetChuteCutKey",static_cast<DWORD>(n.playerJetChuteCutKey));
    n.playerJetCatch=ReadInt(L"PlayerJetCatch",n.playerJetCatch ? 1u : 0u)!=0;
    n.playerJetMouseSpeed=ReadFloat(L"PlayerJetMouseSpeed",n.playerJetMouseSpeed);
    n.sazabi=ReadBool(L"Sazabi",n.sazabi);
    n.sazabiWalk=ReadFloat(L"SazabiWalk",n.sazabiWalk);
    n.sazabiRun=ReadFloat(L"SazabiRun",n.sazabiRun);
    n.sazabiFly=ReadFloat(L"SazabiFly",n.sazabiFly);
    n.sazabiDash=ReadFloat(L"SazabiDash",n.sazabiDash);
    n.sazabiClimb=ReadFloat(L"SazabiClimb",n.sazabiClimb);
    n.sazabiGravity=ReadFloat(L"SazabiGravity",n.sazabiGravity);
    n.sazabiTurn=ReadFloat(L"SazabiTurn",n.sazabiTurn);
    n.sazabiMouseTurn=ReadFloat(L"SazabiMouseTurn",n.sazabiMouseTurn);
    n.sazabiThrusterSec=ReadFloat(L"SazabiThrusterSec",n.sazabiThrusterSec);
    n.sazabiThrusterRegen=ReadFloat(L"SazabiThrusterRegen",n.sazabiThrusterRegen);
    n.sazabiInvertAim=ReadBool(L"SazabiInvertAim",n.sazabiInvertAim);
    n.sazabiAimAssist=ReadBool(L"SazabiAimAssist",n.sazabiAimAssist);
    n.sazabiAssistCone=ReadFloat(L"SazabiAssistCone",n.sazabiAssistCone);
    n.sazabiAssistRange=ReadFloat(L"SazabiAssistRange",n.sazabiAssistRange);
    n.sazabiAssistPull=ReadFloat(L"SazabiAssistPull",n.sazabiAssistPull);
    n.sazabiAssistMousePull=ReadBool(L"SazabiAssistMousePull",n.sazabiAssistMousePull);
    n.sazabiLockKey=ReadInt(L"SazabiLockKey",static_cast<DWORD>(n.sazabiLockKey));
    n.sazabiLockButton=ReadInt(L"SazabiLockButton",static_cast<DWORD>(n.sazabiLockButton));
    n.sazabiDashKey=ReadInt(L"SazabiDashKey",static_cast<DWORD>(n.sazabiDashKey));
    n.sazabiDescendKey=ReadInt(L"SazabiDescendKey",static_cast<DWORD>(n.sazabiDescendKey));
    // an ini from before the specials had their own keys: its switch key / button become the funnels'
    n.sazabiFunnelKey=ReadInt(L"SazabiFunnelKey",static_cast<DWORD>(ReadInt(L"SazabiSwitchKey",static_cast<DWORD>(n.sazabiFunnelKey))));
    n.sazabiFunnelButton=ReadInt(L"SazabiFunnelButton",
                                 static_cast<DWORD>(ReadInt(L"SazabiSwitchButton",static_cast<DWORD>(n.sazabiFunnelButton))));
    n.sazabiCannonKey=ReadInt(L"SazabiMegaCannonKey",static_cast<DWORD>(n.sazabiCannonKey));
    n.sazabiCannonButton=ReadInt(L"SazabiCannonButton",static_cast<DWORD>(n.sazabiCannonButton));
    n.sazabiMeleeKey=ReadInt(L"SazabiMeleeKey",static_cast<DWORD>(n.sazabiMeleeKey));
    n.sazabiMeleeButton=ReadInt(L"SazabiMeleeButton",static_cast<DWORD>(n.sazabiMeleeButton));
    n.sazabiGuardKey=ReadInt(L"SazabiGuardKey",static_cast<DWORD>(n.sazabiGuardKey));
    n.sazabiGuardButton=ReadInt(L"SazabiGuardButton",static_cast<DWORD>(n.sazabiGuardButton));
    n.sazabiGuardShare=ReadFloat(L"SazabiGuardShare",n.sazabiGuardShare);
    n.sazabiAxeDamage=ReadFloat(L"SazabiAxeDamage",n.sazabiAxeDamage);
    n.sazabiCannonDamage=ReadFloat(L"SazabiCannonDamage",n.sazabiCannonDamage);
    n.sazabiFunnelDamage=ReadFloat(L"SazabiFunnelDamage",n.sazabiFunnelDamage);
    n.sazabiTestBoard=ReadBool(L"SazabiTestBoard",n.sazabiTestBoard);
    n.dreadnought=ReadBool(L"Dreadnought",n.dreadnought);
    n.dreadnoughtSpeed=ReadFloat(L"DreadnoughtSpeed",n.dreadnoughtSpeed);
    n.dreadnoughtBoost=ReadFloat(L"DreadnoughtBoost",n.dreadnoughtBoost);
    n.dreadnoughtAccel=ReadFloat(L"DreadnoughtAccel",n.dreadnoughtAccel);
    n.dreadnoughtClimb=ReadFloat(L"DreadnoughtClimb",n.dreadnoughtClimb);
    n.dreadnoughtTurn=ReadFloat(L"DreadnoughtTurn",n.dreadnoughtTurn);
    n.dreadnoughtFloor=ReadFloat(L"DreadnoughtFloor",n.dreadnoughtFloor);
    n.dreadnoughtBoostKey=static_cast<int>(ReadInt(L"DreadnoughtBoostKey",static_cast<DWORD>(n.dreadnoughtBoostKey)));
    n.dreadnoughtDescendKey=static_cast<int>(ReadInt(L"DreadnoughtDescendKey",static_cast<DWORD>(n.dreadnoughtDescendKey)));
    n.dreadnoughtStrafeLeftKey=static_cast<int>(ReadInt(L"DreadnoughtStrafeLeftKey",static_cast<DWORD>(n.dreadnoughtStrafeLeftKey)));
    n.dreadnoughtStrafeRightKey=static_cast<int>(ReadInt(L"DreadnoughtStrafeRightKey",static_cast<DWORD>(n.dreadnoughtStrafeRightKey)));
    n.dreadnoughtBoostButton=static_cast<int>(ReadInt(L"DreadnoughtBoostButton",static_cast<DWORD>(n.dreadnoughtBoostButton)));
    n.dreadnoughtDescendButton=static_cast<int>(ReadInt(L"DreadnoughtDescendButton",static_cast<DWORD>(n.dreadnoughtDescendButton)));
    n.dreadnoughtSeatButton=static_cast<int>(ReadInt(L"DreadnoughtSeatButton",static_cast<DWORD>(n.dreadnoughtSeatButton)));
    n.dreadnoughtHangarKey=static_cast<int>(ReadInt(L"DreadnoughtHangarKey",static_cast<DWORD>(n.dreadnoughtHangarKey)));
    n.dreadnoughtHangarButton=static_cast<int>(ReadInt(L"DreadnoughtHangarButton",static_cast<DWORD>(n.dreadnoughtHangarButton)));
    n.dreadnoughtAheadKey=static_cast<int>(ReadInt(L"DreadnoughtAheadKey",static_cast<DWORD>(n.dreadnoughtAheadKey)));
    n.dreadnoughtAheadButton=static_cast<int>(ReadInt(L"DreadnoughtAheadButton",static_cast<DWORD>(n.dreadnoughtAheadButton)));
    {   // its hangar modules (dreadnought_deck.h module m): the installer builds the model by these same keys
        static const wchar_t* const kHangarKeys[]={L"DreadnoughtHangarLeftFore",L"DreadnoughtHangarLeftAft",L"DreadnoughtHangarRightFore",
                                                   L"DreadnoughtHangarRightAft"};
        unsigned fitted=0;
        for(unsigned m=0;m<4;++m)if(ReadBool(kHangarKeys[m],((n.dreadnoughtHangars>>m)&1u)!=0))fitted|=1u<<m;
        n.dreadnoughtHangars=fitted;
    }
    {
        wchar_t text[32]{};
        GetPrivateProfileStringW(L"VehicleCrew",L"TestBoardVehicle",L"",text,32,iniPath);
        for(int i=0;i<31 && text[i];++i)n.testBoardVehicle[i]=text[i]<0x80 ? static_cast<char>(text[i]) : '?';
        if(n.testBoardVehicle[0])Log("CONFIG TestBoardVehicle=%s (tests only): the player put into the first such vehicle",n.testBoardVehicle);
    }
    n.dreadnoughtStockMass=ReadBool(L"DreadnoughtStockMass",n.dreadnoughtStockMass);
    if(n.dreadnoughtStockMass)Log("CONFIG DreadnoughtStockMass=1 (tests only): the Dreadnought's flight body left at the stock 1 kg");
    n.airdropTest=static_cast<int>(FixInt("AirdropTest",ReadInt(L"AirdropTest",static_cast<DWORD>(n.airdropTest)),0,3));
    if(n.airdropTest)Log("CONFIG AirdropTest=%d (tests only): a container airdrop asked once a mission",n.airdropTest);
    n.npcCrewTest=ReadBool(L"NpcCrewTest",n.npcCrewTest);
    if(n.npcCrewTest)Log("CONFIG NpcCrewTest=1 (tests only): empty vehicles are crewed without a player having driven them");
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
    n.terrainShare=ReadBool(L"TerrainShare",n.terrainShare);
    n.edf41Sound=ReadBool(L"Edf41Sound",n.edf41Sound);
    n.edf41VoiceProbe=ReadBool(L"Edf41VoiceProbe",n.edf41VoiceProbe);
    n.edf41Ai=ReadBool(L"Edf41Ai",n.edf41Ai);
    n.edf41Mother=ReadBool(L"Edf41Mother",n.edf41Mother);
    n.edf41Robo=ReadBool(L"Edf41Robo",n.edf41Robo);
    n.edf41RoboLevel=ReadFloat(L"Edf41RoboLevel",n.edf41RoboLevel);
    n.edf41RoboGunEvery=static_cast<int>(FixInt("Edf41RoboGunEvery",ReadInt(L"Edf41RoboGunEvery",n.edf41RoboGunEvery),0,100000));
    n.edf41RoboAttack=static_cast<int>(FixInt("Edf41RoboAttack",ReadInt(L"Edf41RoboAttack",n.edf41RoboAttack),-1,15));
    n.edf41RoboPlace=ReadFloat(L"Edf41RoboPlace",n.edf41RoboPlace);
    n.edf41RoboTransformAfter=ReadFloat(L"Edf41RoboTransformAfter",n.edf41RoboTransformAfter);
    n.edf41DevShoot=static_cast<int>(FixInt("Edf41DevShoot",ReadInt(L"Edf41DevShoot",n.edf41DevShoot),0,100000));
    n.edf41DevShootDamage=ReadFloat(L"Edf41DevShootDamage",n.edf41DevShootDamage);
    n.edf41DevShootBlast=ReadFloat(L"Edf41DevShootBlast",n.edf41DevShootBlast);
    n.edf41MotherForce=static_cast<int>(FixInt("Edf41MotherForce",ReadInt(L"Edf41MotherForce",n.edf41MotherForce),0,4095));
    n.edf41MotherPlace=ReadFloat(L"Edf41MotherPlace",n.edf41MotherPlace);
    n.edf41MotherParts=static_cast<int>(FixInt("Edf41MotherParts",ReadInt(L"Edf41MotherParts",n.edf41MotherParts),0,31));
    n.edf41Mother401=ReadBool(L"Edf41Mother401",n.edf41Mother401);
    n.edf41Mother401Dev=static_cast<int>(FixInt("Edf41Mother401Dev",ReadInt(L"Edf41Mother401Dev",n.edf41Mother401Dev),0,8388607));
    n.edf41Mother401DevKill=ReadFloat(L"Edf41Mother401DevKill",n.edf41Mother401DevKill);
    n.edf41Mother401DevAction=static_cast<int>(FixInt("Edf41Mother401DevAction",ReadInt(L"Edf41Mother401DevAction",n.edf41Mother401DevAction),0,63));
    n.edf41Mother401Place=ReadFloat(L"Edf41Mother401Place",n.edf41Mother401Place);
    n.edf41Net=ReadBool(L"Edf41Net",n.edf41Net);                     // the 4.1 classes online (edf41net.cpp)
    n.edf41NetMirror=ReadBool(L"Edf41NetMirror",n.edf41NetMirror);
    n.edf5CampaignContent=PackContent("EDF5CampaignContent",ReadInt(L"EDF5CampaignContent",n.edf5CampaignContent),packs::kEdf5);
    n.testRangeContent=PackContent("TestRangeContent",ReadInt(L"TestRangeContent",n.testRangeContent),packs::kRange);
    n.edf41Content=PackContent("EDF41Content",ReadInt(L"EDF41Content",n.edf41Content),packs::kEdf41);
    LoadMissionPacks(n.missionPacks);
    n.portedMissionsUnlocked=ReadBool(L"PortedMissionsUnlocked",n.portedMissionsUnlocked);
    n.portedWeaponsUnlocked=ReadBool(L"PortedWeaponsUnlocked",n.portedWeaponsUnlocked);
    n.allWeaponsUnlocked=ReadBool(L"AllWeaponsUnlocked",n.allWeaponsUnlocked);
    n.airSoftEdge=ReadFloat(L"AirSoftEdge",n.airSoftEdge);
    n.airSoftTurns=ReadFloat(L"AirSoftTurns",n.airSoftTurns);
    n.airSoftCeil=ReadFloat(L"AirSoftCeil",n.airSoftCeil);
    n.heliSoftEdge=ReadFloat(L"HeliSoftEdge",n.heliSoftEdge);
    n.vehicleRam=ReadBool(L"VehicleRam",n.vehicleRam);
    n.vehicleRamDamage=ReadFloat(L"VehicleRamDamage",n.vehicleRamDamage);
    n.gravityDamage=ReadFloat(L"GravityDamage",n.gravityDamage);
    n.gravityGun=ReadBool(L"GravityGun",n.gravityGun);
    n.gravityThrowSpeed=ReadFloat(L"GravityThrowSpeed",n.gravityThrowSpeed);
    n.gravityAutoTest=ReadBool(L"GravityAutoTest",n.gravityAutoTest);
    n.gravityGrabPlayers=ReadBool(L"GravityGrabPlayers",n.gravityGrabPlayers);
    n.floatGuns=ReadInt(L"FloatGuns",n.floatGuns);
    n.floatGunMost=ReadInt(L"FloatGunMost",n.floatGunMost);
    n.floatGunSec=ReadFloat(L"FloatGunSec",n.floatGunSec);
    n.floatGunReach=ReadFloat(L"FloatGunReach",n.floatGunReach);
    n.drill=ReadBool(L"Drill",n.drill);
    n.drillMaxRpm=ReadFloat(L"DrillMaxRpm",n.drillMaxRpm);
    n.drillSpinUpSec=ReadFloat(L"DrillSpinUpSec",n.drillSpinUpSec);
    n.drillSpinDownSec=ReadFloat(L"DrillSpinDownSec",n.drillSpinDownSec);
    n.drillDamage=ReadFloat(L"DrillDamage",n.drillDamage);
    n.drillBreak=ReadFloat(L"DrillBreak",n.drillBreak);
    n.drillOverheatSec=ReadFloat(L"DrillOverheatSec",n.drillOverheatSec);
    n.drillKillCool=ReadFloat(L"DrillKillCool",n.drillKillCool);
    n.drillLaunch=ReadBool(L"DrillLaunch",n.drillLaunch);
    n.drillLaunchKey=ReadInt(L"DrillLaunchKey",static_cast<DWORD>(n.drillLaunchKey));
    n.drillLaunchButton=ReadInt(L"DrillLaunchButton",static_cast<DWORD>(n.drillLaunchButton));
    n.drillLaunchRange=ReadFloat(L"DrillLaunchRange",n.drillLaunchRange);
    n.drillLaunchSpeed=ReadFloat(L"DrillLaunchSpeed",n.drillLaunchSpeed);
    n.drillLaunchDamage=ReadFloat(L"DrillLaunchDamage",n.drillLaunchDamage);
    n.drillLaunchHeat=ReadFloat(L"DrillLaunchHeat",n.drillLaunchHeat);
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
    n.sightZoom=ReadBool(L"SightZoom",n.sightZoom);
    n.sightZoomKey=ReadInt(L"SightZoomKey",static_cast<DWORD>(n.sightZoomKey));
    n.sightZoomButton=ReadInt(L"SightZoomButton",static_cast<DWORD>(n.sightZoomButton));
    n.sightRangeUpKey=ReadInt(L"SightRangeUpKey",static_cast<DWORD>(n.sightRangeUpKey));
    n.sightRangeDownKey=ReadInt(L"SightRangeDownKey",static_cast<DWORD>(n.sightRangeDownKey));
    n.sightRangeStep=ReadInt(L"SightRangeStep",static_cast<DWORD>(n.sightRangeStep));
    n.sightRangePad=ReadBool(L"SightRangePad",n.sightRangePad);
    n.infantryScope=ReadBool(L"InfantryScope",n.infantryScope);
    n.firstPerson=ReadBool(L"FirstPerson",n.firstPerson);
    n.fpVehicle=ReadBool(L"FirstPersonVehicle",n.fpVehicle);
    n.fpHideStockHud=ReadBool(L"FirstPersonHideStockHud",n.fpHideStockHud);
    n.enemyControl=ReadBool(L"EnemyControl",n.enemyControl);
    n.thermal=ReadBool(L"VehicleThermal",n.thermal);
    n.thermalKey=ReadInt(L"VehicleThermalKey",static_cast<DWORD>(n.thermalKey));
    n.thermalButton=ReadInt(L"VehicleThermalButton",static_cast<DWORD>(n.thermalButton));
    n.firstPersonKey=ReadInt(L"FirstPersonKey",static_cast<DWORD>(n.firstPersonKey));
    n.firstPersonStart=ReadBool(L"FirstPersonStart",n.firstPersonStart);
    n.fpEyeUp=ReadFloat(L"FirstPersonEyeUp",n.fpEyeUp);
    n.fpEyeAhead=ReadFloat(L"FirstPersonEyeAhead",n.fpEyeAhead);
    n.fpViewHeightEase=ReadFloat(L"FirstPersonViewHeightEase",n.fpViewHeightEase);
    n.fpCenter=ReadFloat(L"FirstPersonCenter",n.fpCenter);
    n.fpSway=ReadFloat(L"FirstPersonSway",n.fpSway);
    n.fpGripX=ReadFloat(L"FirstPersonGripX",n.fpGripX);
    n.fpGripY=ReadFloat(L"FirstPersonGripY",n.fpGripY);
    n.fpGripZ=ReadFloat(L"FirstPersonGripZ",n.fpGripZ);
    n.fpDualGripX=ReadFloat(L"FirstPersonDualGripX",n.fpDualGripX);
    n.fpDualGripY=ReadFloat(L"FirstPersonDualGripY",n.fpDualGripY);
    n.fpDualGripZ=ReadFloat(L"FirstPersonDualGripZ",n.fpDualGripZ);
    n.fpHoldX=ReadFloat(L"FirstPersonHoldX",n.fpHoldX);
    n.fpHoldY=ReadFloat(L"FirstPersonHoldY",n.fpHoldY);
    n.fpHoldZ=ReadFloat(L"FirstPersonHoldZ",n.fpHoldZ);
    n.fpScale=ReadFloat(L"FirstPersonScale",n.fpScale);
    n.fpNear=ReadFloat(L"FirstPersonNear",n.fpNear);
    n.fpAimSnap=ReadBool(L"FirstPersonAimSnap",n.fpAimSnap);
    n.fpMotion=ReadBool(L"FirstPersonMotion",n.fpMotion);
    n.fpCulling=ReadBool(L"FirstPersonCulling",n.fpCulling);
    n.fpFit=ReadBool(L"FirstPersonFit",n.fpFit);
    n.fpNativeElbow=ReadBool(L"FirstPersonNativeElbow",n.fpNativeElbow);
    n.fpArmIk=ReadBool(L"FirstPersonArmIk",n.fpArmIk);
    n.fpHoldArc=ReadFloat(L"FirstPersonHoldArc",n.fpHoldArc);
    n.fpWristMax=ReadFloat(L"FirstPersonWristMax",n.fpWristMax);
    n.gunStabilizer=ReadBool(L"GunStabilizer",n.gunStabilizer);
    n.viewDistance=ReadFloat(L"ViewDistance",n.viewDistance);
    n.map=ReadBool(L"Map",n.map);
    n.mapKey=ReadInt(L"MapKey",static_cast<DWORD>(n.mapKey));
    n.mapButton=ReadInt(L"MapButton",static_cast<DWORD>(n.mapButton));
    n.mapViewDistance=ReadFloat(L"MapViewDistance",n.mapViewDistance);
    n.mapMarchPace=ReadBool(L"MapMarchPace",n.mapMarchPace);
    n.damageStats=ReadBool(L"DamageStats",n.damageStats);
    n.damageStatsKey=ReadInt(L"DamageStatsKey",static_cast<DWORD>(n.damageStatsKey));
    n.debugSpawn=ReadBool(L"DebugSpawn",n.debugSpawn);   // off unless the ini says 1 (an old ini without it: off)
    n.debugSpawnKey=ReadInt(L"DebugSpawnKey",static_cast<DWORD>(n.debugSpawnKey));
    n.debugSpawnPrevKey=ReadInt(L"DebugSpawnPrevKey",static_cast<DWORD>(n.debugSpawnPrevKey));
    n.debugSpawnNextKey=ReadInt(L"DebugSpawnNextKey",static_cast<DWORD>(n.debugSpawnNextKey));
    n.debugSpawnCategoryKey=ReadInt(L"DebugSpawnCategoryKey",static_cast<DWORD>(n.debugSpawnCategoryKey));
    n.debugSpawnSpawnKey=ReadInt(L"DebugSpawnSpawnKey",static_cast<DWORD>(n.debugSpawnSpawnKey));
    n.debugSpawnRange=ReadFloat(L"DebugSpawnRange",n.debugSpawnRange);
    n.debugSpawnDistance=ReadFloat(L"DebugSpawnDistance",n.debugSpawnDistance);
    n.rangePlace=ReadBool(L"RangePlace",n.rangePlace);
    n.rangePlaceAll=ReadBool(L"RangePlaceAll",n.rangePlaceAll);
    n.roomLoadout=ReadBool(L"RoomLoadout",n.roomLoadout);
    n.roomPreSync=ReadBool(L"RoomPreSync",n.roomPreSync);
    n.roomLoadoutKey=ReadInt(L"RoomLoadoutKey",static_cast<DWORD>(n.roomLoadoutKey));
    n.roomLoadoutButton=ReadInt(L"RoomLoadoutButton",static_cast<DWORD>(n.roomLoadoutButton));
    // StockVehicleStores, or the older StockHeliStores (the helicopters alone before 2026-10-07) still set to 1
    n.stockStores=ReadBool(L"StockVehicleStores",n.stockStores) || ReadBool(L"StockHeliStores",false);
    n.allClassesRide=ReadBool(L"AllClassesRide",n.allClassesRide);
    n.seatSwitch=ReadBool(L"SeatSwitch",n.seatSwitch);
    n.seatNextKey=ReadInt(L"SeatNextKey",static_cast<DWORD>(n.seatNextKey));
    n.seatNumberKeys=ReadBool(L"SeatNumberKeys",n.seatNumberKeys);
    n.seatButton=ReadInt(L"SeatButton",static_cast<DWORD>(n.seatButton));
    n.seatPilot=ReadBool(L"SeatPilot",n.seatPilot);
    n.seatSwitchOnline=ReadBool(L"SeatSwitchOnline",n.seatSwitchOnline);
    n.seatList=ReadBool(L"SeatList",n.seatList);
    n.proteus=ReadBool(L"ProteusRework",n.proteus);
    n.proteusModeKey=ReadInt(L"ProteusStanceKey",static_cast<DWORD>(n.proteusModeKey));
    n.proteusModeButton=ReadInt(L"ProteusModeButton",static_cast<DWORD>(n.proteusModeButton));
    n.proteusShieldKey=ReadInt(L"ProteusShieldKey",static_cast<DWORD>(n.proteusShieldKey));
    n.proteusShieldButton=ReadInt(L"ProteusShieldButton",static_cast<DWORD>(n.proteusShieldButton));
    n.proteusSalvoKey=ReadInt(L"ProteusSalvoKey",static_cast<DWORD>(n.proteusSalvoKey));
    n.proteusTwoSeats=ReadBool(L"ProteusTwoSeats",n.proteusTwoSeats);
    n.proteusWalkSpeed=ReadFloat(L"ProteusWalkSpeed",n.proteusWalkSpeed);
    n.proteusWalkTurn=ReadFloat(L"ProteusWalkTurn",n.proteusWalkTurn);
    n.proteusStepHeight=ReadFloat(L"ProteusStepHeight",n.proteusStepHeight);
    n.proteusShieldSlow=ReadFloat(L"ProteusShieldSlow",n.proteusShieldSlow);
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
    n.npcBackpack=ReadBool(L"NpcBackpack",n.npcBackpack);
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
    n.qmarkPointSec=ReadFloat(L"QMarkPointSec",n.qmarkPointSec);
    n.qmarkVolume=ReadFloat(L"QMarkVolume",n.qmarkVolume);
    n.vanillaSpot=ReadBool(L"VanillaSpot",n.vanillaSpot);
    n.npcFormation=ReadInt(L"NpcFormation",static_cast<DWORD>(n.npcFormation));
    n.npcFormationKey=ReadInt(L"NpcFormationCycleKey",static_cast<DWORD>(n.npcFormationKey));
    n.npcFormationSpacing=ReadFloat(L"NpcFormationSpacing",n.npcFormationSpacing);
    n.npcGuardFormation=ReadInt(L"NpcGuardFormation",static_cast<DWORD>(n.npcGuardFormation));
    n.npcPickupKey=ReadInt(L"NpcPickupKey",static_cast<DWORD>(n.npcPickupKey));
    n.npcPickupRange=ReadFloat(L"NpcPickupRange",n.npcPickupRange);
    n.npcPickupSec=ReadFloat(L"NpcPickupSec",n.npcPickupSec);
    n.npcPickupHealth=ReadBool(L"NpcPickupHealth",n.npcPickupHealth);
    n.npcGuardRadius=ReadFloat(L"NpcGuardRadius",n.npcGuardRadius);
    n.npcFreeRange=ReadFloat(L"NpcFreeRange",n.npcFreeRange);
    n.npcRecruitCooldownSec=ReadFloat(L"NpcRecruitCooldownSec",n.npcRecruitCooldownSec);
    n.vanillaRecruit=ReadBool(L"VanillaRecruit",n.vanillaRecruit);
    n.npcFreeSquadCover=ReadBool(L"NpcFreeSquadCover",n.npcFreeSquadCover);
    n.scriptNpcRecruit=ReadBool(L"ScriptNpcRecruit",n.scriptNpcRecruit);
    n.scriptNpcSettleSec=ReadFloat(L"ScriptNpcSettleSec",n.scriptNpcSettleSec);
    n.tankReturnToPost=ReadBool(L"TankReturnToPost",n.tankReturnToPost);
    n.vehicleGears=ReadBool(L"VehicleGears",n.vehicleGears);
    n.tankPostHold=ReadFloat(L"TankPostHold",n.tankPostHold);
    n.tankReverseMax=ReadFloat(L"TankReverseMax",n.tankReverseMax);
    Validate(n);
    IgnoreRetired();
    {
        // The keys the game's key store holds (keyconfig.cpp): the ini's values were their first seed only.
        const Config fromIni=n;
        if(OverlayStockKeys(n,true) && StockKeysDiffer(fromIni,n))
            Log("CONFIG keys: the game's key config's (the 「插件」 pages; the Q mark the stock 「标记」), not the ini's *Key values");
    }
    Log("CONFIG enabled=%d debug=%d autoCrew=%d delay=%lums range=%.0f bump=%d toGunner=%d heli=%d height=%.0f follow=%.0f engage=%.0f fire=%d",
        n.enabled,n.debug,n.autoCrew,n.crewDelayMs,n.crewRange,n.bump,n.bumpToGunner,
        n.heliPilot,n.heliHeight,n.heliFollow,n.heliRange,n.heliFire);
    Log("CONFIG heli combatRange=%.0f avoid=%d fireHeight=%.0f cone=%.1f missile=%d/%lums landMs=%lu",
        n.heliCombatRange,n.heliAvoid,n.heliFireHeight,n.heliFireCone,n.heliMissile,n.heliMissileMs,n.heliLandMs);
    Log("CONFIG playerHeliStopSec=%.2f gunSight=%d mouseAim=%d flightHud=%d",n.playerHeliStopSec,n.playerHeliGunSight,n.heliMouseAim,
        n.heliFlightHud);
    Log("CONFIG heli speed=%.1f agility=%.1fs yawRate=%.0f doorGuns=%d medicGunnerAim=%d guardRadius=%.0f guardSpeed=%.1f",n.heliSpeed,
        n.heliAgility,n.heliYawRate,n.heliDoorGuns,n.medicGunnerAim,n.heliGuardRadius,n.heliGuardSpeed);
    Log("CONFIG soft edge: jets %.0f m / %.1f turns, ceiling %.0f m, helis %.0f m",n.airSoftEdge,n.airSoftTurns,n.airSoftCeil,n.heliSoftEdge);
    Log("CONFIG sub hullHp=%.0f heavyHit=%.0f",n.subHullHp,n.subHeavyHit);
    Log("CONFIG hud vehicles=%d count=%d range=%.0f stockVehicleHud=%d hideStockGauges=%d scale=%.2f language=%d",n.vehicleHud,
        n.vehicleHudCount,n.vehicleHudRange,n.stockVehicleHud,n.hideStockGauges,n.hudScale,n.hudLanguage);
    Log("CONFIG sazabi=%d walk=%.0f run=%.0f fly=%.0f dash=%.0f climb=%.0f gravity=%.1f turn=%.0f mouse=%.2f thrusters=%.1fs regen=%.2f invert=%d dashKey=0x%X descendKey=0x%X funnels=0x%X/%d cannon=0x%X/%d melee=0x%X/%d guard=0x%X/%d share=%.2f axe=%.0f cannon=%.0f funnel=%.0f testBoard=%d",
        n.sazabi,n.sazabiWalk,n.sazabiRun,n.sazabiFly,n.sazabiDash,n.sazabiClimb,n.sazabiGravity,n.sazabiTurn,n.sazabiMouseTurn,
        n.sazabiThrusterSec,n.sazabiThrusterRegen,n.sazabiInvertAim,n.sazabiDashKey,n.sazabiDescendKey,n.sazabiFunnelKey,
        n.sazabiFunnelButton,n.sazabiCannonKey,n.sazabiCannonButton,n.sazabiMeleeKey,n.sazabiMeleeButton,n.sazabiGuardKey,n.sazabiGuardButton,n.sazabiGuardShare,
        n.sazabiAxeDamage,n.sazabiCannonDamage,n.sazabiFunnelDamage,n.sazabiTestBoard);
    Log("CONFIG dreadnought=%d speed=%.0f boost=%.1f accel=%.1f climb=%.0f turn=%.1f floor=%.0f boost=0x%X/0x%X descend=0x%X/0x%X strafe=0x%X/0x%X hangar=0x%X/0x%X ahead=0x%X/0x%X hangars=%c%c%c%c (left fore, left aft, right fore, right aft; the installer's)",
        n.dreadnought,n.dreadnoughtSpeed,n.dreadnoughtBoost,n.dreadnoughtAccel,n.dreadnoughtClimb,n.dreadnoughtTurn,
        n.dreadnoughtFloor,n.dreadnoughtBoostKey,n.dreadnoughtBoostButton,n.dreadnoughtDescendKey,n.dreadnoughtDescendButton,
        n.dreadnoughtStrafeLeftKey,n.dreadnoughtStrafeRightKey,n.dreadnoughtHangarKey,n.dreadnoughtHangarButton,
        n.dreadnoughtAheadKey,n.dreadnoughtAheadButton,
        n.dreadnoughtHangars&1u ? '1' : '0',n.dreadnoughtHangars&2u ? '1' : '0',n.dreadnoughtHangars&4u ? '1' : '0',
        n.dreadnoughtHangars&8u ? '1' : '0');
    Log("CONFIG sazabi assist=%d cone=%.1f range=%.0f pull=%.1f mousePull=%d lock=0x%X/0x%X",n.sazabiAimAssist,n.sazabiAssistCone,
        n.sazabiAssistRange,n.sazabiAssistPull,n.sazabiAssistMousePull,n.sazabiLockKey,n.sazabiLockButton);
    Log("CONFIG playerJet=%d invertPitch=%d ramDamage=%.2f boostKey=0x%X brakeKey=0x%X switchKey=0x%X mouse=%.2f rotorLift=%.2f jetSound=%d volume=%.2f",
        n.playerJet,n.playerJetInvertPitch,n.playerJetRamDamage,n.playerJetBoostKey,n.playerJetBrakeKey,n.playerJetSwitchKey,n.playerJetMouseSpeed,
        n.playerRotorLift,n.jetSound,n.jetSoundVolume);
    Log("CONFIG playerJet hud gunSight=%d flight=%d threats=%d lockByView=%d turretAimHud=%d; warnings audio=%d voice=%d volume=%.2f",
        n.playerJetGunSight,n.playerJetFlightHud,n.playerJetThreatHud,n.playerJetLockByView,n.turretAimHud,n.warnAudio,n.warnVoice,
        n.warnVolume);
    Log("CONFIG playerJet gearKey=0x%X gearPad=0x%X (XInput bits, held %.1f s)",n.playerJetGearKey,n.playerJetGearPad,kGearPadHoldSec);
    Log("CONFIG vehicleSound=%d engine=%.2f turret=%.2f reload=%.2f gun=%.2f mg=%.2f missile=%.2f",n.vehicleSound,n.vehicleEngineVolume,
        n.vehicleTurretVolume,n.vehicleReloadVolume,n.vehicleGunVolume,n.vehicleMgVolume,n.vehicleMissileVolume);
    Log("CONFIG playerJetAll=%d hailKey=0x%X gunshipBoardGunner=%d gunnerKey=0x%X gatlingKey=0x%X",n.playerJetAll,n.playerJetHailKey,
        n.gunshipBoardGunner,n.gunshipGunnerKey,n.gunshipGatlingKey);
    Log("CONFIG jet pilot=%d fuel=%lus sortie=%lus airRaider=%d missionStrike=%d throwDrones=%d",n.jetPilot,n.jetFuelSec,
        n.jetSortieSec,n.jetAirRaider,n.jetMissionStrike,n.throwDrones);
    Log("CONFIG primer=%d hpScale=%.2f fire=%d trace=%d centipede linkMax=%d linkRange=%.0f woundDamage=%.1f blood=%.2f",n.primer,
        n.primerHpScale,n.primerFire,n.primerTrace,n.centipedeLinkMax,n.centipedeLinkRange,n.centipedeWoundDamage,n.primerBlood);
    Log("CONFIG customNpcAi=%d lane=%d width=%.1f length=%.0f flank=%.0f switch=%d engage=%.2f evade=%d danger=%.0f grab=%.1f crowd=%.1f roll=%.1fs retreatHp=%.2f leash=%.0f",
        n.customNpcAi,n.npcFireLane,n.npcLaneWidth,n.npcLaneLength,n.npcFlankDeg,n.npcWeaponSwitch,n.npcEngageShare,n.npcEvade,
        n.npcDangerRange,n.npcGrabRange,n.npcCrowd,n.npcRollSec,n.npcRetreatHp,n.npcLeash);
    Log("CONFIG npcSquadSuccession=%d min=%d max=%d joinRange=%.0f",n.npcSquadSuccession,n.npcSquadMin,n.npcSquadMax,n.npcSquadJoinRange);
    Log("CONFIG npcBackpack=%d",n.npcBackpack);
    Log("CONFIG npc markKey=0x%X markCone=%.0f boarding=%d gunners=%d",n.npcMarkKey,n.npcMarkCone,n.npcBoarding,n.npcGunners);
    Log("CONFIG qmark pointSec=%.0f volume=%.2f vanillaSpot=%d",n.qmarkPointSec,n.qmarkVolume,n.vanillaSpot);
    Log("CONFIG npc formation=%d key=0x%X spacing=%.1f guard=%d",n.npcFormation,n.npcFormationKey,n.npcFormationSpacing,n.npcGuardFormation);
    Log("CONFIG npc pickup key=0x%X range=%.0f sec=%.0f health=%d",n.npcPickupKey,n.npcPickupRange,n.npcPickupSec,n.npcPickupHealth);
    Log("CONFIG npc guardRadius=%.0f freeRange=%.0f recruitCooldown=%.0fs vanillaRecruit=%d freeSquadCover=%d",
        n.npcGuardRadius,n.npcFreeRange,n.npcRecruitCooldownSec,n.vanillaRecruit,n.npcFreeSquadCover);
    Log("CONFIG scriptNpcRecruit=%d settle=%.1fs",n.scriptNpcRecruit,n.scriptNpcSettleSec);
    Log("CONFIG tankReturnToPost=%d hold=%.1f reverseMax=%.0f",n.tankReturnToPost,n.tankPostHold,n.tankReverseMax);
    Log("CONFIG vehicleGears=%d",n.vehicleGears);
    Log("CONFIG ground pilot=%d follow=%.0f range=%.0f leash=%.0f fire=%d",n.groundPilot,n.groundFollow,
        n.groundRange,n.groundLeash,n.groundFire);
    Log("CONFIG drill=%d maxRpm=%.0f spinUp=%.1fs spinDown=%.1fs damage=%.0f/s break=%.0f/s overheat=%.0fs cool=%.0fs resume=%.0f%% "
        "killCool=%.0f%%",n.drill,n.drillMaxRpm,n.drillSpinUpSec,n.drillSpinDownSec,n.drillDamage,n.drillBreak,n.drillOverheatSec,
        n.drillCoolSec,n.drillResumeHeat*100.0f,n.drillKillCool*100.0f);
    Log("CONFIG drillLaunch=%d key=0x%02X button=0x%02X range=%.0fm speed=%.0fm/s damage=%.0f heat=%.0f%%",n.drillLaunch,
        n.drillLaunchKey,n.drillLaunchButton,n.drillLaunchRange,n.drillLaunchSpeed,n.drillLaunchDamage,n.drillLaunchHeat*100.0f);
    Log("CONFIG vehicleRam=%d damage=%.2f",n.vehicleRam,n.vehicleRamDamage);
    Log("CONFIG gravity damage=%.2f gun=%d throw=%.0fm/s autoTest=%d grabPlayers=%d floatGuns=%d most=%d sec=%.0f reach=%.1fm",
        n.gravityDamage,n.gravityGun,n.gravityThrowSpeed,n.gravityAutoTest,n.gravityGrabPlayers,n.floatGuns,n.floatGunMost,
        n.floatGunSec,n.floatGunReach);
    Log("CONFIG emcBeam=%d charge=%.1fs beam=%.1fs blast=%.0fm x%.2f break=%.0f/s",n.emcBeam,n.emcChargeSec,n.emcBeamSec,n.emcBlastRadius,
        n.emcBlastShare,n.emcBreak);
    Log("CONFIG sidecar=%d npcGunner=%d npcRange=%.0f",n.sidecar,n.sidecarNpcGunner,n.sidecarNpcRange);
    Log("CONFIG highCam=%d key=0x%X button=0x%X height=%.0f back=%.0f pitch=%.0f",n.highCam,n.highCamKey,n.highCamButton,n.highCamHeight,
        n.highCamBack,n.highCamPitch);
    Log("CONFIG sightZoom=%d key=0x%X button=0x%X infantryScope=%d",n.sightZoom,n.sightZoomKey,n.sightZoomButton,n.infantryScope);
    Log("CONFIG firstPerson=%d key=0x%X start=%d eye up %.2f ahead %.2f ease %.2f grip (%.2f,%.2f,%.2f) hold (%.2f,%.2f,%.2f) scale %.2f near %.3f aimSnap=%d motion=%d",
        n.firstPerson,n.firstPersonKey,n.firstPersonStart,n.fpEyeUp,n.fpEyeAhead,n.fpViewHeightEase,n.fpGripX,n.fpGripY,n.fpGripZ,n.fpHoldX,n.fpHoldY,n.fpHoldZ,
        n.fpScale,n.fpNear,n.fpAimSnap,n.fpMotion);
    Log("CONFIG nixTorsoTwist=%d",n.nixTorsoTwist);
    Log("CONFIG map=%d key=0x%X button=0x%X viewDistance=%.0f marchPace=%d",n.map,n.mapKey,n.mapButton,n.mapViewDistance,n.mapMarchPace);
    Log("CONFIG vehicle secondary switch key=0x%X button=0x%X (the primary's rounds: PlayerJetSwitchKey / LB)",
        n.vehicleSecondarySwitchKey,n.vehicleSecondarySwitchButton);
    Log("CONFIG ViewDistance=%.0f (%s)",n.viewDistance,n.viewDistance<0.0f ? "unlimited" : n.viewDistance==0.0f ? "the mission's own" : "m");
    Log("CONFIG damageStats=%d key=0x%X",n.damageStats,n.damageStatsKey);
    Log("CONFIG debugSpawn=%d keys menu=0x%X prev=0x%X next=0x%X category=0x%X spawn=0x%X range=%.0f distance=%.0f",n.debugSpawn,
        n.debugSpawnKey,n.debugSpawnPrevKey,n.debugSpawnNextKey,n.debugSpawnCategoryKey,n.debugSpawnSpawnKey,n.debugSpawnRange,
        n.debugSpawnDistance);
    Log("CONFIG enemyControl=%d",n.enemyControl);
    Log("CONFIG rangePlace=%d all=%d",n.rangePlace,n.rangePlaceAll);
    Log("CONFIG roomLoadout=%d preSync=%d key=0x%X button=0x%X",n.roomLoadout,n.roomPreSync,n.roomLoadoutKey,n.roomLoadoutButton);
    Log("CONFIG allClassesRide=%d",n.allClassesRide);
    Log("CONFIG stockStores=%d seatSwitch=%d nextKey=0x%X numberKeys=%d button=0x%X pilot=%d online=%d list=%d",n.stockStores,n.seatSwitch,
        n.seatNextKey,n.seatNumberKeys,n.seatButton,n.seatPilot,n.seatSwitchOnline,n.seatList);
    Log("CONFIG proteus=%d keys mode=0x%X/0x%X shield=0x%X/0x%X launcher=0x%X twoSeats=%d walk x%.2f turn x%.2f step %.1fm shieldSlow %.2f",
        n.proteus,n.proteusModeKey,n.proteusModeButton,n.proteusShieldKey,n.proteusShieldButton,n.proteusSalvoKey,
        n.proteusTwoSeats,n.proteusWalkSpeed,n.proteusWalkTurn,n.proteusStepHeight,n.proteusShieldSlow);
    Log("CONFIG proteus guns walk x%.2f/x%.2f deployed x%.2f/x%.2f stagger %.1f/%.1fs deployTurn %.2f lift %.0fm heat %.0f/%.0fs resume %.2f barrier %.2f regen %.0fs delay %.0fs",
        n.proteusWalkGunRate,n.proteusWalkGunSpread,n.proteusDeployGunRate,n.proteusDeployGunSpread,n.proteusDeploySec,n.proteusStowSec,n.proteusDeployTurn,
        n.proteusViewLift,n.proteusHeatSec,n.proteusCoolSec,n.proteusResumeHeat,n.proteusBarrier,n.proteusBarrierRegenSec,n.proteusBarrierDelaySec);
    Log("CONFIG proteus field %.0fm defense %.2f attack %.2f fireRate %.2f energy %.2f power %.0f; priority %.2f within %.0fm",
        n.proteusFieldRadius,n.proteusFieldDefense,n.proteusFieldAttack,n.proteusFieldFireRate,n.proteusFieldEnergy,n.proteusFieldPower,n.proteusPriority,
        n.proteusPriorityRadius);
    Log("CONFIG rescue sea=%d below=%.1f autoBoard=%d cooldown=%lus boardingGun=%d",n.seaRescue,n.rescueBelow,n.rescueAutoBoard,
        n.seaRescueCooldownSec,n.boardingGun);
    Log("CONFIG carrierLaser=%d damage=%.0f break=%.2f",n.carrierLaser,n.carrierLaserDamage,n.carrierLaserBreak);
    Log("CONFIG support pt: start %d, most %d, prices x%.2f",n.supportPtStart,n.supportPtMost,n.supportPtScale);
    Log("CONFIG physics vehicleWelding=%d giantContactCap=%d splitMissileSurface=%d stockMissilePN=%d nav=%.1f playerLockByView=%d tempestTv=%d (mouse %.1f, boost x%.1f)",
        n.vehicleWelding,n.giantContactCap,n.splitMissileSurface,n.stockMissilePN,n.stockMissileNav,n.playerLockByView,
        n.tempestTv,n.tempestTvMouseSpeed,n.tempestTvBoost);
    Log("CONFIG supportUnitOnAllies=%d",n.supportUnitOnAllies);
    Config* const fresh=new(std::nothrow) Config(n);
    if(fresh)published.store(fresh,std::memory_order_release);
    SetSupportUnitOnAllies(n.supportUnitOnAllies);   // the code cave's byte (supportunit.cpp): live
    LoadSupportConfig(iniPath);   // support_config.h: callable units and their crews' weapons, validated
}

}  // namespace

// The published config with the key store's keys, when they differ from it (keyconfig.cpp KeyConfigTick: a key changed
// in the game's key config, or the store just seeded / loaded). The old config is never freed (as LoadConfig).
void RepublishStockKeys() noexcept {
    const Config& now=Cfg();
    Config next=now;
    if(!OverlayStockKeys(next,false) || !StockKeysDiffer(now,next))return;
    Config* const fresh=new(std::nothrow) Config(next);
    if(!fresh)return;
    published.store(fresh,std::memory_order_release);
    Log("CONFIG keys from the game's key config: formation=0x%X map=0x%X firstPerson=0x%X seat=0x%X switch=0x%X boost=0x%X mark=0x%X",
        next.npcFormationKey,next.mapKey,next.firstPersonKey,next.seatNextKey,next.playerJetSwitchKey,next.playerJetBoostKey,
        next.npcMarkKey);
}

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
logging::Buffer<kLogBuffer> logBuffer;
INIT_ONCE logWriterOnce=INIT_ONCE_STATIC_INIT;
HANDLE logFile=nullptr;              // under logBuffer.fileLock
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

// Called with the file lock and an immutable buffer snapshot, by the writer or an explicit best-effort flush.
void WritePendingLog(const char* text,std::size_t n,unsigned dropped) noexcept {
    if(!n && !dropped)return;
    if(!logFile)logFile=OpenLog();
    if(!logFile)return;
    if(logSize+static_cast<LONGLONG>(n)>kLogMax)RotateLog();
    WriteLog(text,n);
    if(dropped) {
        char line[120];
        const int k=_snprintf_s(line,sizeof(line),_TRUNCATE,"(%u log lines dropped: more than %zu KB in %lu ms)\r\n",dropped,
                                kLogBuffer>>10,kFlushMs);
        if(k>0)WriteLog(line,static_cast<std::size_t>(k));
    }
}

DWORD WINAPI LogWriter(void*) {
    for(;;) {
        Sleep(kFlushMs);
        logBuffer.Drain(&WritePendingLog);
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
    logBuffer.Append(line,static_cast<std::size_t>(n));
}

// A best-effort snapshot: both locks are acquired once, without waiting. Crash diagnostics use their own raw append
// below instead: neither ordinary formatting nor a flush of older lines is a prerequisite for producing the dump.
void FlushLog() noexcept {
    if(logPath[0])logBuffer.Drain(&WritePendingLog,true);
}

void CrashLog(const char* text,std::size_t size) noexcept {
    logging::AppendCrashLine(logPath,text,size);
}

void ReloadConfigIfChanged() noexcept {
    if(ini.Changed())LoadConfig();
}

// The in-game loadout menu wrote the ini (loadout_store.cpp): its values now, not at the next poll; the watch takes
// the new write time so the poll does not load it twice.
void ReloadConfigNow() noexcept {
    ini.Start(iniPath);
    LoadConfig();
}

const wchar_t* ConfigPath() noexcept { return iniPath; }

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

// A background host's keyboard, foreground window and pad for this plugin (common/edf/input_source.h); null: its own.
extern "C" __declspec(dllexport) void __cdecl EDF6VehicleCrew_SetInputSourceV2(edf::AsyncKeyFn key,edf::ForegroundFn window,
                                                                             edf::PadStateFn pad) {
    edf::SetInputSource(key,window,pad);
    crew::Log("INPUT keyboard, foreground window and pad %s",key || window || pad ? "handed over by the host (a background run)" : "its own again");
}
extern "C" __declspec(dllexport) void __cdecl EDF6VehicleCrew_SetInputSourceV1(edf::AsyncKeyFn key,edf::ForegroundFn window) {
    EDF6VehicleCrew_SetInputSourceV2(key,window,nullptr);
}

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
    char file[96];
    edf::DescribeModuleFile(module,file,sizeof(file));
    Log("EDF6VehicleCrew %s loading (build %s, %s)",EDF6VC_VERSION,EDF6_BUILD_ID,file);
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
    // The crash dump first (its filter and handlers point into this DLL): a crash in any install below is dumped.
    InstallCrashDump();
    // The weapon records past 0x800 (weapon_records.cpp): before anything could make the game status, whose
    // allocation it enlarges; before the airstrike's ownership grants, which write through WeaponRecord.
    InstallWeaponRecords();
    InstallFsLoaderBudget();   // before the application's init starts CRI File System (fsbudget.cpp)
    InstallTeamCapacity();     // before the application's init builds the team manager (team_native.cpp): the PvP faction rows
    Log("ROOM isolation=%d",InstallModRoom());
    Log("SUPPORT soldiers=%d",InstallSupportSoldiers());
    InstallBody506();       // the one 506 physics hook: before the jets, the carrier and the player jets
    InstallBodyBox();       // physics bodies' world boxes: the shield, the missiles' warhead safety
    InstallBulletPass();    // the bullets' candidate hook: the jets' wingmen and the sidecar's passengers, whatever the heli profile
    if(heli) {
        InstallDoorGuns();  // the 410's door guns are part of the heli pilot
        InstallJets();      // the jets and the carrier are flown from HeliFrame: no heli pilot, none of them
        InstallSub();
    } else Log("JET / SUB off: they are flown from the heli pilot's frame, which is off");
    InstallBoarding();      // after the heli profile's board button check
    Log("AIRDROP container step watched=%d",InstallAirdrop());   // the transport planes' container airdrops
    Log("PLAYERVEH loaded requested jets=%d",InstallPlayerVehicles());   // after the airdrop: its vehicle step swaps the path
    InstallPlayerJets();    // its frame is the vehicles' own input; it needs only the 506 physics hook
    InstallSazabi();        // the same: the 506 physics hook and the vehicles' own input
    InstallDreadnought();   // the same
    InstallVehicleRam();    // the ground vehicles' ram (its charges are the jets' impact charges: jet_bay.cpp)
    gravity::Install();     // the gravity operations (their impacts are the jets' impact charges too); the gravity gun's on them
    InstallFloatGuns();     // M099's dropped guns that float round the player and fire (the dropped gun's update hook)
    InstallDrill();         // the drill tank (its charges are the jets' shells: jet_bay.cpp, so with the heli profile)
    InstallEmc();           // the EMC's charged beam (its rounds are the jets' shells too: jet_bay.cpp)
    InstallKatyusha();      // the Katyusha's launcher pose: the arc onto the camera's ground point, the telescopic ram
    InstallNix();           // the Nix's torso twist: its own update (slot 4) chained, apart from the crews' input slot
    InstallTurretCam();     // the riding camera of a turret (decoupled from it, free look, the high view's placement)
    InstallSpotRay();       // the stock spot (Q): off for this machine's players (VanillaSpot=0), else along the drawn camera
    InstallStabilizer();    // the gun stabilizer, after the aim steps the turret camera chains (it runs from its hook)
    InstallProteus();       // the Proteus rework: its weapon mounts, shield barrier, piles and field
    thermal::Install();    // mesh heat/depth passes, inert in optical mode
    InstallViewDepth();    // shared decal reconstruction follows the actual inverse projection
    Log("VIEWDIAG umbra watch %s",InstallUmbraWatch() ? "on" : "NOT installed (EDF.dll's Umbra import is not as read)");
    InstallFirstPerson();   // first person on foot: the camera step chained inside the map's (its view wins while open)
    Log("ENEMY CONTROL native hooks=%d",InstallEnemyControl());
    Log("ENEMY CONTROL legacy adapters=%d",InstallEnemyControlLegacy());
    Log("ENEMY CONTROL network bridge=%d",InstallEnemyControlNetwork());
    // E516's part events: a name the boss has no part for acts on none (the stock lookup reads past its map's end)
    Log("GREYBOSS part events by name checked=%d",InstallGreyBossParts());
    InstallMap();          // the map view (the player's camera overhead, their input held while it is open)
    InstallSightZoom();     // a vehicle gun's sight magnified (the same camera step, chained after the map's)
    InstallInfantryScope(); // a soldier's zoomed scope drawn as a real optic (the stock crosshair's draw chained)
    InstallPhysics();       // vehicle chassis welding and the giants' contact cap (physics.cpp), the sidecar's level hook
    InstallGunnerRecoil();  // a remote gunner's recoil on the vehicle's authority (gunnerrecoil.cpp)
    InstallSidecar();       // the sidecar motorcycle's gunner (sidecar.cpp)
    InstallLaser();
    InstallGauge();         // the follower gauge's draw (subcarrier.cpp): the carriers' gauges and the vehicle HUD
    InstallHud();
    InstallDamageStats();   // the damage function's head and the rounds' spawn (damagestats.cpp): the statistics' page over the map
    InstallKillCounters();  // the battle record's rows: the EDF4.1 port's kill counters after the stock ones (killcount.cpp)
    InstallRounds();        // the stock vehicles' and helis' impact points: the rounds as the game flies them
    InstallStockGauges();   // the stock weapon gauge where our HUD lists the weapons, the fuel tanks it showed
    InstallRoomUi();        // the support loadout menu in the online room and the offline HQ (chains both OnUpdates)
    InstallKeyConfig();     // the plugins' keys as pages of the game's own key config (keyconfig.cpp)
    Log("OUTFIT41 installed=%d",InstallOutfit41());   // the EDF4.1 outfit: the HQ popup's sixth item, the model paths
    InstallGlyphLock();     // the game's own text, wrong or missing characters (glyphs.cpp)
    InstallEdf5Campaign();  // the EDF5 campaign's mission packs owned (edf5campaign.cpp)
    InstallPortedMissions(); // ...and with PortedMissionsUnlocked every mission of the ported packs open (edf5campaign.cpp)
    InstallEdf41MenuBg();    // the EDF4.1 packs' HQ shows 4.1's background when the installer added it (edf5campaign.cpp)
    InstallEdf5Online();    // ...and their missions online: the BVM player natives' online half (edf5online.cpp)
    InstallJetSound();
    InstallVehicleSound();  // the ground vehicles' engines, turrets, loaders and main guns (vehsound.cpp)
    InstallMissiles();
    InstallBarrages();      // the long-range barrages' rounds: RocketBullet01's update (barrage.cpp)
    InstallSplitMissiles(); // the stock split missiles' split distance to the target's surface
    InstallSupportUnitOnAllies();   // support units stick to allies (ini SupportUnitOnAllies, live)
    InstallRouteGuide();    // the stock route guide strip along the navmesh route's corners
    InstallGuidance();      // the stock homing rounds by proportional navigation
    InstallStores();        // before any mission builds a jet: the 506 builds a weapon for every holder, the others their extras
    Log("EDF41 original sidecar hooks=%d",InstallEdf41Bike());
    Log("M163 visual rotor=%d",InstallVulcanRotor());
    InstallPayload();       // the stock vehicles' stores: the holder pull lands on the one picked
    Log("PVP native hooks=%d",InstallPvp());
    InstallRoomPvp();       // the room's start held until every member acknowledged the PvP lock (room_pvp.cpp)
    InstallLockon();        // every lock-on weapon's search order: the player's nearest the view first
    InstallSeatSwitch();    // the player moving between seats (the stock board button's steps, checked)
    InstallAllClassesRide();   // every class in every seat: before any mission makes a vehicle (the seat build is in its constructor)
    InstallBigWorld();
    Log("LEGACYMAP collision migration hooked=%d",InstallLegacyMap());   // EDF4.1 maps (plan P6)
    InstallEdf41Ai();        // the converted EDF4.1 Hectors' arm gate and back-off cycle (edf41ai.cpp)
    InstallEdf41Fortress();  // the converted EDF4.1 fortress: standing, its drop through the core hatch (edf41fortress.cpp)
    InstallEdf41Dragon();    // the converted EDF4.1 giant dragon: its flame stream, ranges and glide pass (edf41dragon.cpp)
    InstallEdf41Insects();   // the DLC's EDF4.1 queen bee and Nephilas: needle speed, thread scatter (edf41insects.cpp)
    InstallEdf41MechBoost(); // the 406 Begaruta (BM03 Vegalta): the jet slots its old boost parser never builds (edf41mech_boost.cpp)
    InstallEdf41SmallDragon();  // the DLC's EDF4.1 small dragons: the hit reaction on 4.1's ragdoll bodies (edf41smalldragon.cpp)
    InstallEdf41Natives();   // EDF4.1 script natives the BVM executor lacks: SetEncountReportRadius (edf41natives.cpp)
    InstallEdf41Before();    // EDF4.1's pre-mission scenes without the menu's black fade over them (edf41before.cpp)
    InstallEdf41Sound();     // EDF4.1's weapon SE bank and voice banks (edf41sound.cpp)
    InstallEdf41Mother();    // EDF4.1's UfoMother301 (ini Edf41Mother; edf41mother.cpp)
    InstallEdf41Robo();      // EDF4.1's UfoRobo (ini Edf41Robo; edf41robo.cpp)
    InstallEdf41Deiroi();    // a stock crash 4.1's M412 reaches (edf41deiroi.cpp)
    InstallEdf41Mother401(); // EDF4.1's UfoMother401 (ini Edf41Mother401; edf41mother401.cpp)
    Log("ENEMY CONTROL radar=%d",InstallEnemyControlRadar());
    Log("ENEMY CONTROL spawn ownership=%d",InstallControlledPvpSpawnBridge());
    Log("ENEMY CONTROL native PVP ownership=%d",EnemyControlNativePvpInstall());
    Log("ENEMY CONTROL weapon aiming=%d",InstallEnemyControlWeaponAim());
    InstallTerrainShare();   // before any map loads: the pieces of one terrain decoded once
    InstallMission();       // the mission's start (Reset*, the preloads) and a trigger of the per-frame hooks
    InstallLoadout(iniPath);
    Log("AIRSTRIKE takeovers=%d",InstallAirstrikes());
    if(!InstallCrew())Log("HOOK crew: no seat hook, so no per-frame hook either: crews, helis, crawlers and the HUD are off");
    Log("LOAD complete (build %s)",EDF6_BUILD_ID);
    if(const unsigned n=edf::ProtectRestoreFailures())Log("PATCH %u code writes left their pages writable (the protection restore failed)",n);
    return true;
}

BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH){crew::module=instance;DisableThreadLibraryCalls(instance);}
    return TRUE;
}
