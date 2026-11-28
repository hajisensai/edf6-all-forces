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
#include "frame_pacing.h"
#include "perf_probe.h"
#include "sync_load_probe.h"
#include "frame_rate.h"
#include "frame_rate_logic.h"
#include "log.h"
#include "mod_file.h"
#include "edf/input_source.h"
#include "objref.h"
#include "edf_layout.h"
#include "plugin_log.h"
#include "scene.h"
#include "ground.h"
#include "heli.h"
#include "patches.h"
#include "view.h"
#include "npcai.h"
#include "transport.h"
#include "proteus_logic.h"
#include "gravity.h"
#include "havok_mt.h"
#include "floatgun.h"
#include "firstperson.h"
#include "thermal.h"
#include "native_pad.h"
#include "gpufx/gpufx.h"
#include "pvp.h"
#include "havok_props.h"
#include "aiworld_capacity.h"
#include "team_native.h"
#include "room_pvp.h"
#include "edf41bike.h"
#include "mod_room.h"
#include "outfit41.h"
#include "weapon_records.h"
#include "weapon_equip.h"
#include "loadout_store.h"
#include "room_ui.h"
#include "worldmap.h"
#include "support_soldier.h"
#include "support_firepower.h"
#include "airdrop.h"
#include "player_vehicle.h"
#include "support_config.h"
#include "lockon.h"
#include "turretaim.h"
#include "hudscale.h"
#include "hudtext.h"
#include "gunnerrecoil.h"
#include "storerecoil.h"
#include "memory.h"
#include "subcarrier.h"
#include "enemy_control.h"
#include "greyboss_parts.h"
#include "enemy_control_net.h"
#include "enemy_control_legacy.h"
#include "enemy_control_weapon_aim.h"
#include "enemy_control_radar.h"
#include "pvp_controlled_spawn.h"
#include "scope_pip.h"
#include "scope_spread.h"
#include "enemy_control_pvp_native.h"
#include "vulcan_rotor.h"
#include "seat_turret.h"
#include "edf/host.h"
#include "version.h"
#include "build_id.h"
#include "pack_notice.h"
#include "profile_sigs.h"
#include "keyconfig.h"
#include "pad_hold.h"
#include "aura.h"
#include "module.h"

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
// A value out of [lo, hi] (or no number) is logged and the default used instead of the nearest end.
void Reset(const char* key,float& v,float lo,float hi,float fallback) noexcept {
    if(!(v>=lo && v<=hi)){Log("CONFIG %s=%g out of range [%g, %g]: %g used",key,v,lo,hi,fallback);v=fallback;}
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
    // The ranges in tools/config_keys.py (tools/gen_config.py): those naming another key's value come last.
#define EDF6_CONFIG_VALIDATE
#include "config.inc"
    // -1 (any negative): unlimited (view.h UnlimitedViewDistance); 0: the mission's own; else from 1000 m, no upper limit.
    if(n.viewDistance<0.0f)n.viewDistance=kUnlimitedView;
    else if(n.viewDistance!=0.0f)Fix("ViewDistance",n.viewDistance,1000.0f,1.0e6f);
}

// Keys no longer read (tools/config_keys.py RETIRED, with why): an old ini that still sets them loads as before, the
// keys ignored (said once).
void IgnoreRetired() noexcept {
    struct Retired { const wchar_t* key; const char* why; };
    static const Retired kRetired[]={
#define EDF6_CONFIG_RETIRED
#include "config.inc"
    };
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
    // Every key tools/config_keys.py has read='gen' (tools/gen_config.py); the ones read here by hand follow.
#define EDF6_CONFIG_LOAD
#include "config.inc"
    n.verbose=ReadInt(L"Debug",n.debug ? 1 : 0)>=2;
    {
        const int raw=ReadInt(L"FrameRateCap",static_cast<DWORD>(n.frameRateCap));
        n.frameRateCap=framerate::FixCap(raw);
        if(n.frameRateCap!=raw)Log("CONFIG FrameRateCap=%d: %d used (1 the display's refresh rate, 0 the game's own 59.94, -1 no cap, 61..1000)",raw,n.frameRateCap);
    }
    n.hudLanguage=ReadLanguage(n.hudLanguage);
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
    if(n.dreadnoughtStockMass)Log("CONFIG DreadnoughtStockMass=1 (tests only): the Dreadnought's flight body left at the stock 1 kg");
    if(n.airdropTest)Log("CONFIG AirdropTest=%d (tests only): a container airdrop asked once a mission",n.airdropTest);
    if(n.npcCrewTest)Log("CONFIG NpcCrewTest=1 (tests only): empty vehicles are crewed without a player having driven them");
    if(n.testDriveCycle)Log("CONFIG TestDriveCycle=1 (tests only): the player driver's throttle runs a fixed cycle (idle, full, half, brake)");
    GetPrivateProfileStringW(L"VehicleCrew",L"TraceFile",L"",n.traceFile,static_cast<DWORD>(std::size(n.traceFile)),iniPath);
    n.edf5CampaignContent=PackContent("EDF5CampaignContent",ReadInt(L"EDF5CampaignContent",n.edf5CampaignContent),packs::kEdf5);
    n.testRangeContent=PackContent("TestRangeContent",ReadInt(L"TestRangeContent",n.testRangeContent),packs::kRange);
    n.edf41Content=PackContent("EDF41Content",ReadInt(L"EDF41Content",n.edf41Content),packs::kEdf41);
    LoadMissionPacks(n.missionPacks);
    // StockVehicleStores, or the older StockHeliStores (the helicopters alone before 2026-10-07) still set to 1
    n.stockStores=ReadBool(L"StockVehicleStores",n.stockStores) || ReadBool(L"StockHeliStores",false);
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
    Log("CONFIG hud vehicles=%d count=%d range=%.0f stockVehicleHud=%d bargaAimMarker=%d hideStockGauges=%d scale=%.2f language=%d worldText3D=%d",
        n.vehicleHud,n.vehicleHudCount,n.vehicleHudRange,n.stockVehicleHud,n.bargaAimMarker,n.hideStockGauges,n.hudScale,n.hudLanguage,n.worldText3D);
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
    Log("CONFIG lockTone beeps=%d lockedMs=%d",n.lockToneBeeps,n.lockToneLockedMs);
    Log("CONFIG playerJet gearKey=0x%X gearPad=0x%X (XInput bits, held %.1f s)",n.playerJetGearKey,n.playerJetGearPad,kGearPadHoldSec);
    Log("CONFIG vehicleSound=%d engine=%.2f turret=%.2f reload=%.2f gun=%.2f mg=%.2f missile=%.2f",n.vehicleSound,n.vehicleEngineVolume,
        n.vehicleTurretVolume,n.vehicleReloadVolume,n.vehicleGunVolume,n.vehicleMgVolume,n.vehicleMissileVolume);
    Log("CONFIG explosionVolume=%.2f flybyVolume=%.2f",n.explosionVolume,n.flybyVolume);
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
    Log("CONFIG npc wingDash=%d far=%.0f sec=%.1f reserve=%.2f fencerMobility=%d dashJump=%.0f dash=%.0f reserve=%d",n.npcWingDash,
        n.npcWingDashFar,n.npcWingDashSec,n.npcWingDashReserve,n.npcFencerMobility,n.npcFencerDashJumpRange,n.npcFencerDashRange,
        n.npcFencerDashReserve);
    Log("CONFIG npc markKey=0x%X markCone=%.0f boarding=%d gunners=%d rideAlong=%d commandHintShown=%d",n.npcMarkKey,n.npcMarkCone,
        n.npcBoarding,n.npcGunners,n.npcRideAlong,n.commandHintShown);
    Log("CONFIG qmark pointSec=%.0f volume=%.2f vanillaSpot=%d",n.qmarkPointSec,n.qmarkVolume,n.vanillaSpot);
    Log("CONFIG npc formation=%d key=0x%X spacing=%.1f guard=%d",n.npcFormation,n.npcFormationKey,n.npcFormationSpacing,
        n.npcGuardFormation);
    Log("CONFIG npc pickup key=0x%X range=%.0f sec=%.0f health=%d",n.npcPickupKey,n.npcPickupRange,n.npcPickupSec,n.npcPickupHealth);
    Log("CONFIG npc guardRadius=%.0f freeRange=%.0f recruitCooldown=%.0fs vanillaRecruit=%d freeSquadCover=%d commanderMode=%d",
        n.npcGuardRadius,n.npcFreeRange,n.npcRecruitCooldownSec,n.vanillaRecruit,n.npcFreeSquadCover,n.commanderMode);
    Log("CONFIG scriptNpcRecruit=%d settle=%.1fs",n.scriptNpcRecruit,n.scriptNpcSettleSec);
    Log("CONFIG tankReturnToPost=%d hold=%.1f reverseMax=%.0f",n.tankReturnToPost,n.tankPostHold,n.tankReverseMax);
    Log("CONFIG vehicleGears=%d up=0x%X down=0x%X",n.vehicleGears,n.vehicleCruiseUpKey,n.vehicleCruiseDownKey);
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
    Log("CONFIG scopeSpread=%d sniperHip x%.2f min %.4f rad, others hip x%.2f aim x%.2f",n.scopeSpread,n.scopeSniperHipSpread,
        n.scopeSniperHipMinSpread,n.scopeHipSpread,n.scopeAimSpread);
    Log("CONFIG firstPerson=%d key=0x%X start=%d eye up %.2f ahead %.2f ease %.2f grip (%.2f,%.2f,%.2f) hold (%.2f,%.2f,%.2f) scale %.2f near %.3f aimSnap=%d motion=%d",
        n.firstPerson,n.firstPersonKey,n.firstPersonStart,n.fpEyeUp,n.fpEyeAhead,n.fpViewHeightEase,n.fpGripX,n.fpGripY,n.fpGripZ,n.fpHoldX,n.fpHoldY,n.fpHoldZ,
        n.fpScale,n.fpNear,n.fpAimSnap,n.fpMotion);
    Log("CONFIG firstPerson muzzle flash scale %.2f (of the gun as drawn) bright %.2f",n.fpFlashScale,n.fpFlashBright);
    Log("CONFIG nixTorsoTwist=%d",n.nixTorsoTwist);
    Log("CONFIG map=%d key=0x%X button=0x%X viewDistance=%.0f marchPace=%d",n.map,n.mapKey,n.mapButton,n.mapViewDistance,n.mapMarchPace);
    Log("CONFIG vehicle secondary switch key=0x%X button=0x%X (the primary's rounds: PlayerJetSwitchKey / LB)",
        n.vehicleSecondarySwitchKey,n.vehicleSecondarySwitchButton);
    Log("CONFIG vehicle fire keys 0x%X 0x%X 0x%X 0x%X buttons 0x%X 0x%X 0x%X 0x%X; auto turrets %d key=0x%X button=0x%X",
        n.vehicleFire3Key,n.vehicleFire4Key,n.vehicleFire5Key,n.vehicleFire6Key,n.vehicleFire3Button,n.vehicleFire4Button,
        n.vehicleFire5Button,n.vehicleFire6Button,n.autoTurrets,n.vehicleAutoTurretKey,n.vehicleAutoTurretButton);
    Log("CONFIG ViewDistance=%.0f (%s)",n.viewDistance,n.viewDistance<0.0f ? "unlimited" : n.viewDistance==0.0f ? "the mission's own" : "m");
    Log("CONFIG damageStats=%d key=0x%X",n.damageStats,n.damageStatsKey);
    Log("CONFIG damageNumbers colours=%d weak=0x%06X friendly=0x%06X allies=%d ally=0x%06X",n.damageNumberColors,
        n.damageNumberWeakColor,n.damageNumberFriendlyColor,n.damageNumberAllies,n.damageNumberAllyColor);
    Log("CONFIG debugSpawn=%d keys menu=0x%X prev=0x%X next=0x%X category=0x%X spawn=0x%X range=%.0f distance=%.0f",n.debugSpawn,
        n.debugSpawnKey,n.debugSpawnPrevKey,n.debugSpawnNextKey,n.debugSpawnCategoryKey,n.debugSpawnSpawnKey,n.debugSpawnRange,
        n.debugSpawnDistance);
    Log("CONFIG enemyControl=%d",n.enemyControl);
    Log("CONFIG rangePlace=%d all=%d",n.rangePlace,n.rangePlaceAll);
    Log("CONFIG roomLoadout=%d preSync=%d key=0x%X button=0x%X",n.roomLoadout,n.roomPreSync,n.roomLoadoutKey,n.roomLoadoutButton);
    Log("CONFIG hqAutoMissionSelect=%d",n.hqAutoMissionSelect);
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

// The log: common/log.h (the buffer, its writer thread, the rotation to .log.1, shared with EDF6AutoTurret).
void Log(const char* format,...) noexcept {
    va_list args;va_start(args,format);edf::logfile::Line(format,args);va_end(args);
}

// What is buffered written now, best-effort: both locks once without waiting (edf::logfile::Flush(true)); it may open
// the log or rotate it as the writer would, so it is not the crash path. A crash goes through CrashLog below:
// edf::logfile::Crash, after the dump is written, writes the older lines and then the crash line through the log's one
// handle (opened then if it was not yet; no rotation, no CRT formatting), or with a log lock held the crash line alone
// by its own raw append.
void FlushLog() noexcept {
    edf::logfile::Flush();
}

void CrashLog(const char* text,std::size_t size) noexcept {
    edf::logfile::Crash(text,size);
}

void ReloadConfigIfChanged() noexcept {
    if(!ini.Changed())return;
    LoadConfig();
    Log("CONFIG reloaded (the ini's write time changed)");   // tools/perfrun.py's stay legs wait for this row
}

// The in-game loadout menu wrote the ini (loadout_store.cpp): its values now, not at the next poll; the watch takes
// the new write time so the poll does not load it twice.
void ReloadConfigNow() noexcept {
    ini.Start(iniPath);
    LoadConfig();
}

const wchar_t* ConfigPath() noexcept { return iniPath; }

// The player's human: the soldier a local camera follows (firstperson.cpp CamStepHook, every frame of a local
// camera, on foot, riding, dead or with the map open). Until 2026-10-27 it was taken from SeePlayer, whose two
// feeds are the on-foot ride prompt (crew.cpp PromptHook: only while a rideable object is about) and the plugin's
// vehicle classes (crew.cpp Crew): away from every vehicle, or in a vehicle of no class, it went stale in 2 s and
// every caller of PlayerHuman saw nobody (a support call: "找不到本机玩家，无法呼叫"). Split screen's second camera
// does not take it from the first while the first's is fresh. Kept with its weak-this control block, so a new
// object at the same address (the next mission) is not taken for it.
namespace {
constexpr ULONGLONG kPlayerHumanMs=2000;
unsigned char* playerHuman=nullptr;
ObjRef playerHumanRef;
ULONGLONG playerHumanAt=0;
}  // namespace

void SeePlayer(const float* pos,std::int32_t team) noexcept {
    std::memcpy(player.pos,pos,sizeof(player.pos));player.team=team;player.at=GameMs();
}

void SeeCameraPlayer(unsigned char* human) noexcept {
    if(!human)return;
    const ULONGLONG now=GameMs();
    __try {
        if(playerHuman && playerHuman!=human && now-playerHumanAt<=kPlayerHumanMs && playerHumanRef.Is(playerHuman))return;
        playerHuman=human;playerHumanRef=ObjRef::Of(human);playerHumanAt=now;
    } __except(EXCEPTION_EXECUTE_HANDLER){playerHuman=nullptr;playerHumanRef=ObjRef{};playerHumanAt=0;}
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
// Two source files patching one code byte (edf/code_owner.h): the second's patch is refused or undoes the first's.
void LogClaimConflict(const edf::ClaimConflict& c) noexcept {
    const auto at=reinterpret_cast<const unsigned char*>(c.at);
    if(image && at>=image && at<image+0x4000000)
        Log("PATCH conflict: %s and %s both patch EDF.dll+0x%llX (%zu bytes): only one may own a site",c.first,c.second,
            static_cast<unsigned long long>(at-image),c.size);
    else Log("PATCH conflict: %s and %s both patch %p (%zu bytes): only one may own a site",c.first,c.second,
             static_cast<const void*>(at),c.size);
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
    wchar_t logPath[MAX_PATH];
    wcscpy_s(logPath,iniPath);
    dot=wcsrchr(logPath,L'.');wcscpy_s(dot,MAX_PATH-(dot-logPath),L".log");
    edf::logfile::Start(logPath);
    info->infoVersion=PluginInfo::MaxInfoVer;info->name="EDF6 Vehicle Crew";info->version=PLUG_VER(EDF6VC_VERSION_MAJOR,EDF6VC_VERSION_MINOR,EDF6VC_VERSION_PATCH,0);
    char file[96];
    edf::DescribeModuleFile(module,file,sizeof(file));
    Log("EDF6VehicleCrew %s loading (build %s, %s)",EDF6VC_VERSION,EDF6_BUILD_ID,file);
    Log("MODS %zu files listed in Mods/OBJECT and Mods/WEAPON",LoadModFiles());   // mod_file.h: before anything asks ModFileIn
    ini.Start(iniPath);
    NoteSupportKindsInstalled();   // before the ini's SupportPreset_ values are first parsed (support_call.h SupportKindInstalled)
    LoadConfig();
    // The saves in the mod's set before the game reads them (tools/vanilla_launch.py restore_saves): never the mod game on
    // the vanilla saves (whose progress would then be stored as the vanilla set's). Not switched back: this plugin
    // refusing to load is not enough (ModLoader.ini still loads the other plugins and redirects Mods, so the mod game
    // would run on the vanilla or a half-switched set), so the game is ended here, before it reads the saves.
    if(!RestoreModSaves()) {
        Log("REFUSED: the saves are still in the vanilla set (原版启动): the game ended before it reads them");
        FlushLog();
        MessageBoxW(nullptr,
            L"存档还在原版那套（原版启动留下的），没能换回 MOD 存档，详见 EDF6VehicleCrew.log。\n\n"
            L"为了不让 MOD 游戏在原版存档上运行（这一局的进度会存错那套），游戏这次不启动。\n"
            L"先关掉还开着的「原版启动」窗口，或运行安装器选 1（安装 / 更新）换回存档，再启动游戏。",
            L"全军出击：存档还在原版那套",MB_OK|MB_ICONERROR|MB_TOPMOST|MB_SETFOREGROUND);
        TerminateProcess(GetCurrentProcess(),1);
        return false;
    }
    StartSaveBackup();   // the saves copied before the game reads them (tools/save_backup.py), whatever follows
    image=edf::IdentifyImage(GetModuleHandleW(L"EDF.dll"));
    if(!image){Log("REFUSED: unsupported EDF.dll");return false;}
    if(!CheckProfile()){Log("REFUSED: unexpected EDF.dll code");return false;}
    edf::SetClaimConflictSink(&LogClaimConflict);   // before the first patch
    // Checks first, patching nothing: a refusal above, or a module found off here, leaves the game as it was.
    const bool heli=CheckHeliProfile();
    const bool ground=CheckGroundProfile();
    Log("HELI profile=%d",heli);
    Log("GROUND profile=%d",ground);
    CheckPauseFlag();       // the game's pause flag (the game clock and the HUD stop with it): read only
    // Then the modules' installs (modules.cpp kInstalls), in dependency order. From the first patch on the plugin
    // stays loaded whatever fails after (true below): the loader unloading the DLL would leave patched slots pointing at
    // unloaded code.
    modules::InstallAll({heli,ground,iniPath});
    Log("LOAD complete (build %s)",EDF6_BUILD_ID);
    if(const unsigned n=edf::ProtectRestoreFailures())Log("PATCH %u code writes left their pages writable (the protection restore failed)",n);
    const edf::ClaimStats claims=edf::GetClaimStats();
    Log("PATCH owners: %u claims at load, %u conflicts, %u not registered (no room)",claims.claims,claims.conflicts,
        claims.dropped);
    return true;
}

BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH){crew::module=instance;DisableThreadLibraryCalls(instance);}
    else if(reason==DLL_PROCESS_DETACH)edf::logfile::Flush();   // process exit: the last kFlushMs of lines (log.h)
    return TRUE;
}
