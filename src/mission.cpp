// The mission's lifecycle: one place where a new mission starts (the player preload, hooked here), so every
// module drops what it kept about the last mission's objects before the new ones exist. Without it the
// tables keyed by object address carried stale entries (and live-looking timestamps: the game clock barely
// moves while loading) into the next mission, whose objects may sit at the same addresses.
// The preload is hooked here, not in the test-range loadout (loadout.cpp), so the mission's start (and with
// it the jets', the carrier's and the laser's preloads) does not hang on the loadout's own checks: the
// loadout only wraps the call when it is on (LoadoutPreload). Layout: docs/loadout-re.md.
#include "crash_dump.h"
#include "crew.h"
#include "gravity.h"
#include "gravity_net.h"
#include "floatgun.h"
#include "firstperson.h"
#include "frame_rate_objects.h"
#include "mission_watch.h"
#include "transport.h"
#include "turretaim.h"
#include "airdrop.h"
#include "gear.h"
#include "memory.h"
#include "playarea.h"
#include "outfit41.h"
#include "host_settings.h"
#include "preload_set.h"
#include "vehicle_weapons.h"
#include "room_ui.h"
#include "sazabi_sound.h"
#include "support_spawn.h"
#include "support_soldier.h"
#include "npc_arms.h"
#include "thermal.h"
#include "gpufx/gpufx.h"
#include "pvp.h"
#include "enemy_control.h"
#include "enemy_control_net.h"
#include "enemy_control_pvp_native.h"
#include "vulcan_rotor.h"
#include "seat_turret.h"
#include "barrel_recoil.h"
#include "support_variants.h"
#include "support_aircraft.h"   // ResetSupportFollowUps
#include "support_firepower.h"
#include "support_points.h"
#include "support_warning.h"
#include "player_vehicle.h"
#include "commander.h"
#include <iterator>

namespace crew {
namespace {
// The per-player preload (offline: the scripts' PreloadPlayerResource reaches it from these two calls).
constexpr unsigned kPreloadPlayer=0x59DE50;
constexpr unsigned kPreloadCalls[]={0x1B8F52,0x225FB5};
// Online (GameStatus+0x38 != -1) the scripts' PreloadPlayerResource (0x1B8CC0) preloads each session player
// through 0x59DC90 instead and never reaches 0x1B8F52; the mission starts there too, or an airstrike takeover
// finds nothing preloaded online and the stock bombers come.
constexpr unsigned kPreloadSession=0x59DC90;
constexpr unsigned kSessionCalls[]={0x1B8E98,0x225FAC};
const unsigned char kPreloadSig[]={0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57};
const unsigned char kSessionSig[]={0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x56,0x57,0x41,0x56,0x48,0x81,0xEC,0x80,0x00,0x00,0x00};

PlayerPreloadFn preloadOrig=nullptr,sessionOrig=nullptr;

// The script's MissionSync_Begin (AngelScript "int MissionSync_Begin(int)", its binding 0x7920E0: 0x78E9A0(edx)), the
// first thing MAINSCRIPT's PlayMission_Common does online (docs/loadout-re.md: MissionSync("Sync") packs this machine's
// member record from GS, the record the others create this machine's player from). Every machine, host and guests,
// runs it before the load. Here, before the record is packed (2026-10-20 review of #218 / #226 / #222):
//   - the EDF4.1 outfit's colours put into the block the record reads (outfit41.cpp), not left to the room's 0.5 s tick;
//   - the host's settings and every member's features read once for this mission (host_settings.h);
//   - the room menu's dialog closed (room_dialog.h: by its own frame; the room screen may not update again).
constexpr unsigned kSyncBegin=0x7920E0,kSyncCall=0x7920EA,kSyncRecord=0x78E9A0;
const unsigned char kSyncSig[]={0x40,0x53,0x48,0x83,0xEC,0x20,0x8B,0xCA,0x8B,0xDA,0xE8,0xB1,0xC8,0xFF,0xFF,0x8B,0xC3,0x48,
                                0x83,0xC4,0x20,0x5B,0xC3};   // push rbx; sub rsp,20h; mov ecx,edx; mov ebx,edx; call 0x78E9A0; mov eax,ebx ...
using SyncRecordFn=std::uintptr_t(__fastcall*)(std::uintptr_t);
std::uintptr_t __fastcall MissionSyncHook(std::uintptr_t arg) {
    Outfit41MissionSync();
    HostSettingsMissionSync();
    ResetRoomUi();
    return reinterpret_cast<SyncRecordFn>(image+kSyncRecord)(arg);
}

std::uintptr_t __fastcall PreloadHook(std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d) {
    WatchMissionPhase("the game's player preload");
    HostSettingsOfflineMission();   // the offline preload: no room's word decides this mission (host_settings.h)
    const auto result=LoadoutPreload(preloadOrig,a,b,c,d);
    MissionStart();
    return result;
}

std::uintptr_t __fastcall SessionPreloadHook(std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d) {
    WatchMissionPhase("the game's session player preload");
    const auto result=Outfit41SessionPreload(sessionOrig,a,b,c,d);   // marks this machine's member (outfit41.cpp)
    MissionStart();
    return result;
}

// The scene's end (docs/mission-cycle-re.md §1.1): the script's FreeResourceGroup(int) (0x70E330) releases a resource group
// through 0x7A2EE0(resource manager, group) at 0x70E35E, unless the thread context's vt+0x1A8 says no. Group 2 is the
// scene's, released after Mission() returns (clear, withdraw, failure) and by InitializeAll (back to the title); a RETRY
// keeps it and goes on to the preload (MissionStart). After it no scene traversal runs until the next mission's, so the
// step count stands still: crew::SceneOver makes every step stamp stale there (step_fresh.h). The call after it,
// 0x70E38E, is havok_mt's (havok_mt_logic.h); this one is not.
constexpr unsigned kFreeGroup=0x70E330,kFreeGroupCall=0x70E35E,kReleaseGroup=0x7A2EE0;
constexpr unsigned kSceneGroup=2;
const unsigned char kFreeGroupSig[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x8B,0xFA,0xE8,0x4F,0x6F,0xAD,0x00,
                                     0x48,0x8B,0xC8,0x48,0x8B,0xD8,0x4C,0x8B,0x00,0x41,0xFF,0x90,0xA8,0x01,0x00,0x00,
                                     0x85,0xC0,0x75,0x58,0x48,0x8B,0x0D,0x4C,0x46,0x9A,0x01,0x8B,0xD7,0xE8,0x7D,0x4B,0x09,
                                     0x00,0x83,0xFF,0x02};   // ... mov rcx,[0x20B29A8]; mov edx,edi; call 0x7A2EE0; cmp edi,2
using ReleaseGroupFn=std::uintptr_t(__fastcall*)(std::uintptr_t,std::uintptr_t);
std::uintptr_t __fastcall ReleaseGroupHook(std::uintptr_t manager,std::uintptr_t group) {
    if(static_cast<std::uint32_t>(group)==kSceneGroup) {
        SceneOver();
        frameobjects::SceneOver();   // the unlimited frame rate's weak counts on the scene's objects (frame_rate_objects.h)
    }
    return reinterpret_cast<ReleaseGroupFn>(image+kReleaseGroup)(manager,group);
}

int Redirect(const unsigned* sites,std::size_t count,unsigned target,void* hook) noexcept {
    int done=0;
    for(std::size_t i=0;i<count;++i) {
        bool changed=false;
        if(RedirectCall(image+sites[i],image+target,hook,changed))++done;
        else if(changed)Log("MISSION call site %#x half patched",sites[i]);
    }
    return done;
}
}  // namespace

void MissionStart() noexcept {
    LogMemory("mission start entered");
    CheckCrashFilter();   // the crash dump's filter and abort handler still first (crash_dump.h)
    WatchMissionPhase("resets");
    ResetVulcanRotors();
    ResetSeatTurrets();
    ResetBarrelRecoil();
    ResetEnemyControlNetwork(); // stop authenticated leases before native input restoration
    ResetEnemyControl(); // release native player-mode leases before scene/team identities are reset
    if(!EnemyControlNativePvpReset())Log("ENEMY CONTROL pending native ownership restore retained across mission reset");
    ResetPvp();  // true world boundary: restore teams and release support-owner weak identities
    ResetCommander();   // the commander mode's last mission dropped, its map lock let go (commander.cpp)
    ResetPlayer();
    ResetFirstPerson();
    thermal::Reset();
    gpufx::Reset();
    ResetCrew();
    ResetHelis();
    ResetGround();
    ResetNpcAi();
    ResetQMarks();   // the Q marks, ours and the teammates' (qmark.cpp)
    AutoTurretMissionReset();   // EDF6AutoTurret's auto-aim back to its ini's (off), its lock let go (turretaim.cpp)
    ResetNpcPosts();
    ResetAirstrikes();
    ResetUltimate();
    ResetBarrages();
    ResetSupportFollowUps();
    ResetSupportFirepower();
    ResetSupportWarnings();
    SupportPointsReset();
    ResetSupportDispatch();
    ResetTransports();
    ResetAirdrops();
    ResetJets();
    ResetBoosters();
    ResetShields();
    ResetPlayerJets();
    ResetSazabi();
    ResetDreadnought();
    ResetBoarding();
    ResetSubs();
    ResetLaser();
    ResetHud();
    ResetLauncher();
    ResetKatyushas();
    ResetAndryusha();
    ResetHighCam();
    ResetSightZoom();
    ResetInfantryScope();
    ResetNix();
    ResetTurretCam();
    ResetStabilizer();
    ResetMap();
    ResetDamageStats();
    ResetWastedFire();
    ResetHeliSight();
    ResetStockHud();
    ResetStockGauges();
    ResetRoomUi();          // the room / HQ screen is gone: its menu and room sync go on the next screen frame
    ResetPayload();
    ResetSeatSwitch();
    ResetGear();
    ResetJetSound();
    ResetVehicleSound();
    ResetSazabiSound();
    ResetMissiles();
    ResetDrills();
    ResetVehicleRams();
    gravity::Reset();
    gravity::net::Reset();
    ResetGravityGun();
    FloatGunsMissionStart();   // which mission this is (EDF5's last: the float guns), nothing kept from the last
    ResetEmc();
    ResetSidecars();
    ResetProteus();
    ResetBigWorld();
    ResetTerrainShare();
    ResetPlayArea();
    ResetEdf5Online();   // the EDF5 creation loop's online arguments (edf5online.cpp)
    ResetVehicleWeapons();     // nothing of this mission's vehicle weapons queued yet (vehicle_weapons.h)
    preload::BeginMission();   // this mission's set of the plugin's content (preload_set.h), decided before this
    WatchMissionPhase("jets");
    PreloadJets();   // the airstrike takeovers' jets (jet.cpp), with the mission's own resources
    WatchMissionPhase("player jets");
    PreloadPlayerJets();   // ...and the player jets, for the catch after an ejection (playerjet.cpp)
    WatchMissionPhase("submarine carrier");
    PreloadSub();    // ...and the submarine carrier (subcarrier.cpp)
    WatchMissionPhase("portal laser");
    PreloadLaser();  // ...and the teleportation ships' portal laser (carrierlaser.cpp)
    WatchMissionPhase("support vehicles");
    PreloadSupportVehicles(); // stock ground support hulls; no dummy crew created by initialization
    WatchMissionPhase("airdrop container");
    PreloadAirdrop();         // ...and the stock container a transport plane drops one in (airdrop.cpp)
    WatchMissionPhase("support soldiers");
    PreloadSupportSoldiers(); // original real NPC resources, before the game's preload wait
    WatchMissionPhase("support soldier weapons");
    PreloadNpcArms();         // ...and the default crew rifle; other weapons load when their orders use them (npc_arms.h)
    WatchMissionPhase("support variants");
    PreloadSupportVariants(); // ...and the generated coloured soldiers / loaded vehicles there are (support_variants.h)
    WatchMissionPhase("player vehicles");
    PreloadPlayerVehicles();  // ...and the jets this player and the room's members summon, loaded (player_vehicle.h)
    WatchMissionPhase("debug spawn");
    PreloadDebugSpawn();      // the debug spawn tool's stock vehicles and enemies; nothing at all with DebugSpawn=0
    LogVehicleWeapons();      // the weapons the vehicles above queued with them
    WatchMissionPhase("per-frame inputs");
    EnsureInputs();  // every plugin has loaded by now: the per-frame hooks chain onto theirs
    Log("MISSION start: per-object state dropped, resources preloaded");
    LogMemory("mission start");
    WatchMissionPhase(nullptr);
}

bool InstallMission() noexcept {
    int offline=0,online=0;
    if(Matches(kPreloadPlayer,kPreloadSig,sizeof(kPreloadSig))) {
        preloadOrig=reinterpret_cast<PlayerPreloadFn>(image+kPreloadPlayer);
        offline=Redirect(kPreloadCalls,std::size(kPreloadCalls),kPreloadPlayer,reinterpret_cast<void*>(&PreloadHook));
    } else Log("MISSION preload: profile mismatch");
    if(Matches(kPreloadSession,kSessionSig,sizeof(kSessionSig))) {
        sessionOrig=reinterpret_cast<PlayerPreloadFn>(image+kPreloadSession);
        online=Redirect(kSessionCalls,std::size(kSessionCalls),kPreloadSession,reinterpret_cast<void*>(&SessionPreloadHook));
    } else Log("MISSION online preload: profile mismatch");
    int sync=0;
    if(Matches(kSyncBegin,kSyncSig,sizeof(kSyncSig))) {
        bool changed=false;
        sync=RedirectCall(image+kSyncCall,image+kSyncRecord,reinterpret_cast<void*>(&MissionSyncHook),changed) ? 1 : 0;
        if(!sync && changed)Log("MISSION MissionSync_Begin call site half patched");
    } else Log("MISSION MissionSync_Begin: profile mismatch (the room's colours / host settings / dialog wait for the preload)");
    int sceneEnd=0;
    if(Matches(kFreeGroup,kFreeGroupSig,sizeof(kFreeGroupSig))) {
        bool changed=false;
        sceneEnd=RedirectCall(image+kFreeGroupCall,image+kReleaseGroup,reinterpret_cast<void*>(&ReleaseGroupHook),changed) ? 1 : 0;
        if(!sceneEnd && changed)Log("MISSION FreeResourceGroup call site half patched");
    } else Log("MISSION FreeResourceGroup: profile mismatch (the scene's end leaves the step stamps fresh until the next mission)");
    Log("HOOK mission sync=%d scene-end=%d",sync,sceneEnd);
    Log("HOOK mission preload=%d/%zu online=%d/%zu%s",offline,std::size(kPreloadCalls),online,std::size(kSessionCalls),
        offline+online ? "" : " (no mission start: per-object state is kept across missions, nothing is preloaded)");
    return offline+online>0;
}
}  // namespace crew
