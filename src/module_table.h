// The module registry's tables (module.h, module_logic.h, docs/module-registry.md): every module's row and the four
// passes' entries, in run order. A new feature adds its row and its entries here, in the phase it belongs to; plugin.cpp,
// mission.cpp and crew.cpp list no feature themselves. Included by modules.cpp (the frame's passes, the faults) and
// modules_load.cpp (the load's and the mission start's passes): each runs its own tables, and links only what they call.
#pragma once
#include <Windows.h>
#include <cstdint>

#include "module.h"
#include "plugin_log.h"
#include "config.h"
#include "native_body_math.h"
#include "native_bone_update.h"
#include "target_grid.h"
#include "bullet_expand_cull.h"
#include "scene.h"
#include "boarding.h"
#include "plugin_body.h"
#include "booster.h"
#include "campaign.h"
#include "carrierlaser.h"
#include "debug_spawn_api.h"
#include "dreadnought.h"
#include "drill.h"
#include "edf41.h"
#include "emc.h"
#include "emplacement.h"
#include "ground.h"
#include "helisight.h"
#include "highcam.h"
#include "hud.h"
#include "jet.h"
#include "launchers.h"
#include "map.h"
#include "missile.h"
#include "netprobe.h"
#include "patches.h"
#include "sazabi.h"
#include "shield.h"
#include "sidecar.h"
#include "soldier_factory.h"
#include "soldier_type_patches.h"
#include "sightzoom_api.h"
#include "sound.h"
#include "stab_api.h"
#include "strikes.h"
#include "turretcam_api.h"
#include "vehicleram_api.h"
#include "view.h"
#include "nix.h"
#include "vhud.h"
#include "payload.h"
#include "damagestats.h"
#include "mapcmd.h"
#include "proteus.h"
#include "npcai.h"
#include "qmark.h"
#include "infantryscope.h"
#include "crew_frame.h"
#include "soldier_identity.h"
#include "crash_dump.h"
#include "frame_pacing.h"
#include "perf_probe.h"
#include "sync_load_probe.h"
#include "frame_rate.h"
#include "gravity.h"
#include "gravity_net.h"
#include "havok_mt.h"
#include "object_update_mt.h"
#include "floatgun.h"
#include "firstperson.h"
#include "thermal.h"
#include "native_pad.h"
#include "gpufx/gpufx.h"
#include "pvp.h"
#include "havok_props.h"
#include "aiworld_capacity.h"
#include "step_objects_hook.h"
#include "team_native.h"
#include "room_pvp.h"
#include "edf41bike.h"
#include "mod_room.h"
#include "outfit41.h"
#include "weapon_records.h"
#include "soldier_save.h"
#include "soldier_classes.h"
#include "vehicler_foot.h"
#include "runeblade.h"
#include "runeblade_melee.h"
#include "vehicler_start.h"
#include "weapon_equip.h"
#include "loadout_store.h"
#include "room_ui.h"
#include "worldmap.h"
#include "support_soldier.h"
#include "support_firepower.h"
#include "airdrop.h"
#include "player_vehicle.h"
#include "lockon.h"
#include "turretaim.h"
#include "gunnerrecoil.h"
#include "storerecoil.h"
#include "subcarrier.h"
#include "enemy_control.h"
#include "greyboss_parts.h"
#include "enemy_control_net.h"
#include "enemy_control_legacy.h"
#include "enemy_control_weapon_aim.h"
#include "enemy_control_radar.h"
#include "enemy_control_pvp_native.h"
#include "pvp_controlled_spawn.h"
#include "scope_spread.h"
#include "vulcan_rotor.h"
#include "seat_turret.h"
#include "barrel_recoil.h"
#include "keyconfig.h"
#include "aura.h"
#include "transport.h"
#include "gear.h"
#include "playarea.h"
#include "vehicle_weapons.h"
#include "sazabi_sound.h"
#include "support_aircraft.h"
#include "support_points.h"
#include "support_warning.h"
#include "commander.h"
#include "plugin_net.h"
#include "jet_lever_net.h"
#include "heli.h"
#include "warn.h"

namespace crew::modules {
namespace {

// What a module with no reset entry keeps, when it is only its hooks (most installs).
constexpr const char* kLoadOnly="install only: its hooks and the addresses they patch, set once at load";

// The modules: M(id, name, stateless). The name is the fault log's and the HUD notice's; the id the fault id.
#define EDF6_MODULES(M) \
    M(kCrashDump,"crash dump",kLoadOnly) \
    M(kWeaponRecords,"weapon records",kLoadOnly) \
    M(kSoldierSave,"soldier save",kLoadOnly) \
    M(kSoldierClasses,"soldier classes",kLoadOnly) \
    M(kFramePacing,"frame pacing",kLoadOnly) \
    M(kRenderFence,"render fence",kLoadOnly) \
    M(kFrameLatency,"frame latency",kLoadOnly) \
    M(kFrameRate,"frame rate",kLoadOnly) \
    M(kNativePerformance,"native performance",kLoadOnly) \
    M(kTargetGrid,"target grid",kLoadOnly) \
    M(kBulletExpandCull,"bullet expand cull",kLoadOnly) \
    M(kFsBudget,"fs budget",kLoadOnly) \
    M(kTeamCapacity,"team capacity",kLoadOnly) \
    M(kHavokProps,"havok props",kLoadOnly) \
    M(kAiWorldCapacity,"ai world capacity",kLoadOnly) \
    M(kStepObjects,"step objects",kLoadOnly) \
    M(kModRoom,"mod room",kLoadOnly) \
    M(kSupportSoldiers,"support soldiers","its table is dropped by its own preload at each mission (support_soldier.cpp PreloadSupportSoldiers)") \
    M(kSupportFirepower,"support firepower",nullptr) \
    M(kBody506,"body506",kLoadOnly) \
    M(kBodyBox,"body box",kLoadOnly) \
    M(kBulletPass,"bullet pass",kLoadOnly) \
    M(kDoorGuns,"door guns",kLoadOnly) \
    M(kJets,"jets",nullptr) \
    M(kCarrier,"carrier",nullptr) \
    M(kBoarding,"boarding",nullptr) \
    M(kAirdrop,"airdrop",nullptr) \
    M(kPlayerVehicles,"player vehicles",kLoadOnly) \
    M(kPlayerJet,"player jet",nullptr) \
    M(kSazabi,"sazabi",nullptr) \
    M(kDreadnought,"dreadnought",nullptr) \
    M(kRam,"ram",nullptr) \
    M(kGravity,"gravity",nullptr) \
    M(kFloatGuns,"float guns",nullptr) \
    M(kDrill,"drill",nullptr) \
    M(kEmc,"emc",nullptr) \
    M(kKatyusha,"katyusha",nullptr) \
    M(kNix,"nix",nullptr) \
    M(kTurretCam,"turret cam",nullptr) \
    M(kSpotRay,"spot ray",kLoadOnly) \
    M(kStabilizer,"stabilizer",nullptr) \
    M(kProteus,"proteus",nullptr) \
    M(kThermal,"thermal",nullptr) \
    M(kNativePad,"native pad",kLoadOnly) \
    M(kGpuFx,"gpufx",nullptr) \
    M(kViewDepth,"view depth",kLoadOnly) \
    M(kView,"view","viewdiag.cpp's last diagnosis: dropped while the new mission's play area is unmeasured (ResetPlayArea, ViewDiagTick)") \
    M(kFirstPerson,"first person",nullptr) \
    M(kScopeSpread,"scope spread",kLoadOnly) \
    M(kEnemyControl,"enemy control",nullptr) \
    M(kEnemyControlLegacy,"enemy control legacy",kLoadOnly) \
    M(kEnemyControlNetwork,"enemy control network",nullptr) \
    M(kGreyBossParts,"greyboss parts",kLoadOnly) \
    M(kMap,"map",nullptr) \
    M(kSightZoom,"sight zoom",nullptr) \
    M(kInfantryScope,"infantry scope",nullptr) \
    M(kPhysics,"physics",kLoadOnly) \
    M(kHavokMultithread,"havok multithread",kLoadOnly) \
    M(kObjectUpdateMultithread,"object update multithread",kLoadOnly) \
    M(kGunnerRecoil,"gunner recoil",kLoadOnly) \
    M(kStoreRecoil,"store recoil",kLoadOnly) \
    M(kSidecar,"sidecar",nullptr) \
    M(kAura,"aura",nullptr) \
    M(kLaser,"laser",nullptr) \
    M(kGauge,"gauge",kLoadOnly) \
    M(kHud,"hud",nullptr) \
    M(kDamageStats,"damage stats",nullptr) \
    M(kDamageNumbers,"damage numbers",kLoadOnly) \
    M(kKillCounters,"kill counters",kLoadOnly) \
    M(kRounds,"rounds",kLoadOnly) \
    M(kStockGauges,"stock gauges",nullptr) \
    M(kWeaponEquip,"weapon equip",kLoadOnly) \
    M(kPackSwitch,"pack switch",kLoadOnly) \
    M(kRoomUi,"room ui",nullptr) \
    M(kWorldMap,"world map",kLoadOnly) \
    M(kKeyConfig,"key config",kLoadOnly) \
    M(kOutfit41,"outfit41",kLoadOnly) \
    M(kGlyphLock,"glyph lock",kLoadOnly) \
    M(kEdf5Campaign,"edf5 campaign",kLoadOnly) \
    M(kPortedMissions,"ported missions",kLoadOnly) \
    M(kEdf41MenuBg,"edf41 menu bg",kLoadOnly) \
    M(kEdf5Online,"edf5 online",nullptr) \
    M(kJetSound,"jet sound",nullptr) \
    M(kVehicleSound,"vehicle sound",nullptr) \
    M(kBlastSound,"blast sound",nullptr) \
    M(kMissiles,"missiles",nullptr) \
    M(kBarrage,"barrage",nullptr) \
    M(kAndryusha,"andryusha",nullptr) \
    M(kSplitMissiles,"split missiles",kLoadOnly) \
    M(kSupportUnitOnAllies,"support unit on allies",kLoadOnly) \
    M(kRouteGuide,"route guide",kLoadOnly) \
    M(kGuidance,"guidance",kLoadOnly) \
    M(kStores,"stores",kLoadOnly) \
    M(kEdf41Bike,"edf41 bike",kLoadOnly) \
    M(kVulcanRotor,"vulcan rotor",nullptr) \
    M(kSeatTurrets,"seat turrets",nullptr) \
    M(kPayload,"payload",nullptr) \
    M(kPvp,"pvp",nullptr) \
    M(kRoomPvp,"room pvp",kLoadOnly) \
    M(kLockon,"lockon",kLoadOnly) \
    M(kSeatSwitch,"seat switch",nullptr) \
    M(kVehiclerStart,"vehicler start",nullptr) \
    M(kAllClassesRide,"all classes ride",kLoadOnly) \
    M(kBigWorld,"big world",nullptr) \
    M(kLegacyMap,"legacy map",kLoadOnly) \
    M(kEdf41Ai,"edf41 ai",kLoadOnly) \
    M(kEdf41Fortress,"edf41 fortress",kLoadOnly) \
    M(kEdf41Dragon,"edf41 dragon",kLoadOnly) \
    M(kEdf41Insects,"edf41 insects",kLoadOnly) \
    M(kEdf41MechBoost,"edf41 mech boost",kLoadOnly) \
    M(kEdf41SmallDragon,"edf41 small dragon",kLoadOnly) \
    M(kEdf41Natives,"edf41 natives",kLoadOnly) \
    M(kEdf41Before,"edf41 before",kLoadOnly) \
    M(kEdf41Sound,"edf41 sound",kLoadOnly) \
    M(kEdf41Mother,"edf41 mother",kLoadOnly) \
    M(kEdf41Robo,"edf41 robo",kLoadOnly) \
    M(kEdf41Deiroi,"edf41 deiroi",kLoadOnly) \
    M(kEdf41Mother401,"edf41 mother401",kLoadOnly) \
    M(kSoldierFactory,"soldier factory",kLoadOnly) \
    M(kVehiclerFoot,"vehicler foot",kLoadOnly) \
    M(kRuneBlade,"runeblade",kLoadOnly) \
    M(kRuneBladeMelee,"runeblade melee",kLoadOnly) \
    M(kSoldierTypePatches,"soldier type patches",kLoadOnly) \
    M(kEnemyControlRadar,"enemy control radar",kLoadOnly) \
    M(kControlledPvpSpawn,"controlled pvp spawn",kLoadOnly) \
    M(kEnemyControlNativePvp,"enemy control native pvp",nullptr) \
    M(kEnemyControlWeaponAim,"enemy control weapon aim",kLoadOnly) \
    M(kTerrainShare,"terrain share",nullptr) \
    M(kMission,"mission","the mission's start hook itself: what a mission keeps is the modules' (ResetAll)") \
    M(kLoadout,"loadout",kLoadOnly) \
    M(kSoldierIdentity,"soldier identity",kLoadOnly) \
    M(kAirstrikes,"airstrikes",nullptr) \
    M(kCrew,"crew",nullptr) \
    M(kPerfProbe,"perf probe",kLoadOnly) \
    M(kSyncLoad,"sync load",kLoadOnly) \
    M(kBarrelRecoil,"barrel recoil",nullptr) \
    M(kCommander,"commander",nullptr) \
    M(kPlayer,"player",nullptr) \
    M(kHeli,"heli",nullptr) \
    M(kHeliReap,"heli reap","heli.cpp's helis: the heli module's reset (ResetHelis) drops them") \
    M(kRescue,"rescue","heli.cpp's sea rescue: the heli module's reset (ResetHelis) drops it with the player's track") \
    M(kGround,"ground",nullptr) \
    M(kNpcGunners,"npc gunners",nullptr) \
    M(kNpcParallel,"npc parallel think",nullptr) \
    M(kQMarks,"qmarks",nullptr) \
    M(kAutoTurret,"auto turret",nullptr) \
    M(kNpcPost,"npc post",nullptr) \
    M(kUltimate,"ultimate",nullptr) \
    M(kSupportWarnings,"support warnings",nullptr) \
    M(kSupportPoints,"support points",nullptr) \
    M(kSupportDispatch,"support dispatch",nullptr) \
    M(kPluginNet,"plugin net",nullptr) \
    M(kTransports,"transports",nullptr) \
    M(kBoosters,"boosters",nullptr) \
    M(kShield,"shield",nullptr) \
    M(kLauncher,"launcher",nullptr) \
    M(kHighCam,"high cam",nullptr) \
    M(kWastedFire,"wasted fire",nullptr) \
    M(kHeliSight,"heli sight",nullptr) \
    M(kStockHud,"stock hud",nullptr) \
    M(kBargaAim,"barga aim",nullptr) \
    M(kGear,"gear",nullptr) \
    M(kTrace,"trace",nullptr) \
    M(kSazabiSound,"sazabi sound",nullptr) \
    M(kGravityNet,"gravity net",nullptr) \
    M(kGravityGun,"gravity gun",nullptr) \
    M(kPlayArea,"play area",nullptr) \
    M(kVehicleWeapons,"vehicle weapons",nullptr) \
    M(kUnderground,"underground",nullptr) \
    M(kExitGround,"exit ground","crew.cpp's exit watch: the crews' reset (ResetCrew) drops it") \
    M(kWarn,"warn",nullptr) \
    M(kDebugSpawn,"debug spawn","its tables: dropped by its own preload at each mission (mission.cpp PreloadDebugSpawn), its online enemies by the support session's (ResetSupportNet)") \
    M(kMapQueue,"map queue","mapcmd.cpp's queue: the map's reset drops it (ResetMap, ResetMapCommands)") \
    M(kJetLever,"jet lever","its slots: its channel's reset (jet_lever_net.cpp JetLeverNetChannel), run by the plugin net module's") \
    M(kEmplacement,"emplacement","keeps nothing: the driver's stick cleared on the vehicle itself each frame (emplacement.cpp)") \
    M(kNetProbe,"net probe","its debug log's pacing only (per object, per session edge): a stale slot holds back one line at most (netprobe.cpp)") \
    M(kAimLines,"aim lines","its hidden lines are in the crews' vehicle states: the crews' reset (ResetCrew) drops them") \
    M(kLockSound,"lock sound","jetsound.cpp's: the jet sound's reset (ResetJetSound) drops it") \
    M(kTwinBarrel,"twin barrel","payload.cpp's: the payload's reset (ResetPayload) drops it")

}  // namespace

#define EDF6_MODULE_ID(id,name,why) id,
enum class Id : std::uint16_t { EDF6_MODULES(EDF6_MODULE_ID) kCount };
#undef EDF6_MODULE_ID

namespace {
#define EDF6_MODULE_ROW(id,name,why) Module{Id::id,name,why},
constexpr Module kModules[]={EDF6_MODULES(EDF6_MODULE_ROW)};
#undef EDF6_MODULE_ROW
constexpr std::size_t kModuleCount=static_cast<std::size_t>(Id::kCount);
static_assert(sizeof(kModules)/sizeof(kModules[0])==kModuleCount,"a row for every id");
static_assert(RowsInIdOrder(kModules),"rows in their ids' order, each named");

// Adapters: a function of the feature's own signature as an entry.
template<auto F> void Install(const LoadContext&) noexcept { static_cast<void>(F()); }
template<auto F> void Call() noexcept { static_cast<void>(F()); }
template<auto F> void Vehicle(unsigned char* v,int) noexcept { F(v); }
template<auto F> void TickInVehicle(unsigned char*,int) noexcept { F(); }

using IP=InstallPhase;
using RP=ResetPhase;
using TP=TickPhase;
using VP=VehiclePhase;

// The load's installs, in dependency order. From the first patch on the plugin stays loaded whatever fails after: the
// loader unloading the DLL would leave patched slots pointing at unloaded code.
constexpr InstallEntry kInstalls[]={
    // first (its filter and handlers point into this DLL): a crash in any install after it is dumped
    {IP::kCrashDump,Id::kCrashDump,[](const LoadContext&) noexcept {InstallCrashDump();}},
    // the weapon records past 0x800 (weapon_records.cpp): before anything could make the game status, whose
    // allocation it enlarges; before the airstrike's ownership grants, which write through WeaponRecord
    {IP::kBeforeGameStatus,Id::kWeaponRecords,&Install<&InstallWeaponRecords>},
    // the new classes' save (soldier_save.cpp): after the weapon records, whose load completion it listens to
    {IP::kBeforeGameStatus,Id::kSoldierSave,&Install<&InstallSoldierSave>},
    // the new classes' player objects ZwVehicler / ZwRuneBlade (soldier_factory.cpp): in the game's factory table
    // (edf41classes AddFactory: only its own fingerprints and the scene system not yet made, so the game's start-up
    // registration 0x701859 still to come); and the per-type tables made to hold types 4 / 5 (soldier_type_patches.cpp).
    // Both before the classes: the SoldierInit read adds 4 / 5 only when they and the save's stand-in are in.
    {IP::kBeforeGameStatus,Id::kSoldierFactory,&Install<&InstallSoldierFactory>},
    {IP::kBeforeGameStatus,Id::kSoldierTypePatches,&Install<&InstallSoldierTypePatches>},
    // the Vehicler on foot (vehicler_foot.cpp): its dash held back over AssultSoldier's live frame slot (the clones copy it),
    // and the bullet falloff gate widened to AmmoDamageReduce a > 1 (the Gyrojet's damage growing with its flight)
    {IP::kBeforeGameStatus,Id::kVehiclerFoot,&Install<&InstallVehiclerFoot>},
    {IP::kBeforeGameStatus,Id::kRuneBlade,&Install<&InstallRuneBlade>},
    // the RuneBlade's melee and special arms (runeblade_melee.cpp): AssultSoldier's live frame slot chained after the one above
    {IP::kBeforeGameStatus,Id::kRuneBladeMelee,&Install<&InstallRuneBladeMelee>},
    // six player classes (soldier_classes.cpp): before the game status, whose constructor reads the mode data's
    // SoldierInit (SetMode 0xDC460 from 0xD6722)
    {IP::kBeforeGameStatus,Id::kSoldierClasses,&Install<&InstallSoldierClasses>},
    {IP::kBeforeAppInit,Id::kFramePacing,&Install<&InstallFramePacing>},   // the window thread's spin and the frame step's Sleep(1) made real waits (frame_pacing.cpp)
    {IP::kBeforeAppInit,Id::kRenderFence,&Install<&InstallRenderFence>},   // the game thread's spin on the render fence made a wait (frame_pacing.cpp)
    {IP::kBeforeAppInit,Id::kFrameLatency,&Install<&InstallFrameLatency>}, // before the application's init makes the device: DXGI's queue to ini MaxFrameLatency
    // ini FrameRateCap (docs/unlimited-fps-re.md): 0 the game's 59.94 Hz, nothing installed; otherwise frames between the
    // ticks, drawn with the camera and the player's root interpolated (the game's blt swap chain with sync interval 1;
    // the flip model only behind the developer switch FrameRateFlip). Not with EDF6VR (it paces frames itself).
    {IP::kBeforeAppInit,Id::kFrameRate,&Install<&InstallFrameRate>},
    {IP::kBeforeAppInit,Id::kNativePerformance,[](const LoadContext&) noexcept {
        if(!Cfg().enabled)return;
        Log("BONES subtree composition: %u/3 native callers redirected",InstallNativeBoneUpdate());
        Log("PHYSICS attachment and collision: %u/%u native paths optimized",InstallNativeBodyMath(),kNativeBodyMathSites);
    }},
    {IP::kBeforeAppInit,Id::kFsBudget,&Install<&InstallFsLoaderBudget>},   // before the application's init starts CRI File System (fsbudget.cpp)
    {IP::kBeforeAppInit,Id::kTargetGrid,&Install<&InstallTargetGrid>},   // the nearest-enemy pick on team grids (target_grid.cpp)
    {IP::kBeforeAppInit,Id::kBulletExpandCull,&Install<&InstallBulletExpandCull>},   // bullets' group expansion: only the parts their sweep reaches (bullet_expand_cull.cpp)
    {IP::kBeforeAppInit,Id::kTeamCapacity,&Install<&InstallTeamCapacity>}, // before the application's init builds the team manager (team_native.cpp): the PvP faction rows
    {IP::kBeforeAppInit,Id::kHavokProps,&Install<&InstallHavokProps>},     // before the application's init (so before any hknpWorld): the event dispatcher's uint32 indexes
    {IP::kBeforeAppInit,Id::kAiWorldCapacity,&Install<&InstallAiWorldCapacity>},   // before any mission makes an ai::World (aiworld_capacity.cpp): 0xFFFE pathfinding agents, not 1024
    {IP::kBeforeAppInit,Id::kStepObjects,&Install<&InstallStepObjects>},   // before any scene: the System's object lists trusted a frame at a time (step_objects_hook.cpp, edf/step_objects.h)
    {IP::kPhysics,Id::kModRoom,[](const LoadContext&) noexcept {Log("ROOM isolation=%d",InstallModRoom());}},
    {IP::kPhysics,Id::kSupportSoldiers,[](const LoadContext&) noexcept {Log("SUPPORT soldiers=%d",InstallSupportSoldiers());}},
    {IP::kPhysics,Id::kSupportFirepower,[](const LoadContext&) noexcept {Log("SUPPORT firepower=%d",InstallSupportFirepower());}},   // support vehicles' and aircraft's damage by level
    {IP::kPhysics,Id::kBody506,&Install<&InstallBody506>},         // the one 506 physics hook: before the jets, the carrier and the player jets
    {IP::kPhysics,Id::kBodyBox,&Install<&InstallBodyBox>},         // physics bodies' world boxes: the shield, the missiles' warhead safety
    {IP::kPhysics,Id::kBulletPass,&Install<&InstallBulletPass>},   // the bullets' candidate hook: the jets' wingmen and the sidecar's passengers, whatever the heli profile
    {IP::kHeliProfile,Id::kDoorGuns,&Install<&InstallDoorGuns>},   // the 410's door guns are part of the heli pilot
    {IP::kHeliProfile,Id::kJets,&Install<&InstallJets>},           // the jets and the carrier are flown from HeliFrame: no heli pilot, none of them
    {IP::kHeliProfile,Id::kCarrier,&Install<&InstallSub>},
    {IP::kFeatures,Id::kBoarding,&Install<&InstallBoarding>},      // after the heli profile's board button check
    {IP::kFeatures,Id::kAirdrop,[](const LoadContext&) noexcept {Log("AIRDROP container step watched=%d",InstallAirdrop());}},   // the transport planes' container airdrops
    {IP::kFeatures,Id::kPlayerVehicles,[](const LoadContext&) noexcept {Log("PLAYERVEH loaded requested jets=%d",InstallPlayerVehicles());}},   // after the airdrop: its vehicle step swaps the path
    {IP::kFeatures,Id::kPlayerJet,&Install<&InstallPlayerJets>},   // its frame is the vehicles' own input; it needs only the 506 physics hook
    {IP::kFeatures,Id::kSazabi,&Install<&InstallSazabi>},          // the same: the 506 physics hook and the vehicles' own input
    {IP::kFeatures,Id::kDreadnought,&Install<&InstallDreadnought>},   // the same
    {IP::kFeatures,Id::kRam,&Install<&InstallVehicleRam>},         // the ground vehicles' ram (its charges are the jets' impact charges: jet_bay.cpp)
    {IP::kFeatures,Id::kGravity,&Install<&gravity::Install>},      // the gravity operations (their impacts are the jets' impact charges too); the gravity gun's on them
    {IP::kFeatures,Id::kFloatGuns,&Install<&InstallFloatGuns>},    // M099's dropped guns that float round the player and fire (the dropped gun's update hook)
    {IP::kFeatures,Id::kDrill,&Install<&InstallDrill>},            // the drill tank (its charges are the jets' shells: jet_bay.cpp, so with the heli profile)
    {IP::kFeatures,Id::kEmc,&Install<&InstallEmc>},                // the EMC's charged beam (its rounds are the jets' shells too: jet_bay.cpp)
    {IP::kFeatures,Id::kKatyusha,&Install<&InstallKatyusha>},      // the Katyusha's launcher pose: the arc onto the camera's ground point, the telescopic ram
    {IP::kFeatures,Id::kNix,&Install<&InstallNix>},                // the Nix's torso twist: its own update (slot 4) chained, apart from the crews' input slot
    {IP::kFeatures,Id::kTurretCam,&Install<&InstallTurretCam>},    // the riding camera of a turret (decoupled from it, free look, the high view's placement)
    {IP::kFeatures,Id::kSpotRay,&Install<&InstallSpotRay>},        // the stock spot (Q): off for this machine's players (VanillaSpot=0), else along the drawn camera
    {IP::kFeatures,Id::kStabilizer,&Install<&InstallStabilizer>},  // the gun stabilizer, after the aim steps the turret camera chains (it runs from its hook)
    {IP::kFeatures,Id::kProteus,&Install<&InstallProteus>},        // the Proteus rework: its weapon mounts, shield barrier, piles and field
    {IP::kFeatures,Id::kThermal,&Install<&thermal::Install>},      // mesh heat/depth passes, inert in optical mode
    {IP::kFeatures,Id::kNativePad,&Install<&nativepad::Install>},  // the game's pad read: a plugin chord (on foot: the night vision's L3 + R3) kept from the game
    {IP::kFeatures,Id::kGpuFx,&Install<&gpufx::Install>},          // GPU particles at the world's end (thermal.cpp's redirect), the stock particle draw slot
    {IP::kFeatures,Id::kViewDepth,&Install<&InstallViewDepth>},    // shared decal reconstruction follows the actual inverse projection
    {IP::kFeatures,Id::kView,[](const LoadContext&) noexcept {
        Log("VIEWDIAG umbra watch %s",InstallUmbraWatch() ? "on" : "NOT installed (EDF.dll's Umbra import is not as read)");}},
    {IP::kFeatures,Id::kFirstPerson,&Install<&InstallFirstPerson>},   // first person on foot: the camera step chained inside the map's (its view wins while open)
    {IP::kFeatures,Id::kScopeSpread,&Install<&InstallScopeSpread>},   // the player's scoped weapons spread by the scope (the shot entry's hook, the weapon creation's call)
    {IP::kFeatures,Id::kEnemyControl,[](const LoadContext&) noexcept {Log("ENEMY CONTROL native hooks=%d",InstallEnemyControl());}},
    {IP::kFeatures,Id::kEnemyControlLegacy,[](const LoadContext&) noexcept {Log("ENEMY CONTROL legacy adapters=%d",InstallEnemyControlLegacy());}},
    {IP::kFeatures,Id::kEnemyControlNetwork,[](const LoadContext&) noexcept {Log("ENEMY CONTROL network bridge=%d",InstallEnemyControlNetwork());}},
    // E516's part events: a name the boss has no part for acts on none (the stock lookup reads past its map's end)
    {IP::kFeatures,Id::kGreyBossParts,[](const LoadContext&) noexcept {Log("GREYBOSS part events by name checked=%d",InstallGreyBossParts());}},
    {IP::kFeatures,Id::kMap,&Install<&InstallMap>},                // the map view (the player's camera overhead, their input held while it is open)
    {IP::kFeatures,Id::kSightZoom,&Install<&InstallSightZoom>},    // a vehicle gun's sight magnified (the same camera step, chained after the map's)
    {IP::kFeatures,Id::kInfantryScope,&Install<&InstallInfantryScope>},   // a soldier's zoomed scope drawn as a real optic (the stock crosshair's draw chained)
    {IP::kFeatures,Id::kPhysics,&Install<&InstallPhysics>},        // vehicle chassis welding and the giants' contact cap (physics.cpp), the sidecar's level hook
    {IP::kFeatures,Id::kHavokMultithread,&Install<&InstallHavokMultithread>},   // the physics step on the game's Havok worker threads (ini HavokMultithread; havok_mt.cpp)
    {IP::kFeatures,Id::kObjectUpdateMultithread,&Install<&InstallObjectUpdateMultithread>},   // slot5 bones -> collision proxies on worker threads (ini ObjectUpdateMT; object_update_mt.cpp)
    {IP::kFeatures,Id::kGunnerRecoil,&Install<&InstallGunnerRecoil>},   // a remote gunner's recoil on the vehicle's authority (gunnerrecoil.cpp)
    {IP::kFeatures,Id::kStoreRecoil,&Install<&InstallStoreRecoil>},     // the 403's store holders past its seats: the hull push, not a seat's jitter (storerecoil.cpp)
    {IP::kFeatures,Id::kSidecar,&Install<&InstallSidecar>},        // the sidecar motorcycle's gunner (sidecar.cpp)
    {IP::kFeatures,Id::kAura,&Install<&InstallAura>},              // the aura trucks (aura.cpp)
    {IP::kFeatures,Id::kLaser,&Install<&InstallLaser>},
    {IP::kFeatures,Id::kGauge,&Install<&InstallGauge>},            // the follower gauge's draw (subcarrier.cpp): the carriers' gauges and the vehicle HUD
    {IP::kFeatures,Id::kHud,&Install<&InstallHud>},
    {IP::kFeatures,Id::kDamageStats,&Install<&InstallDamageStats>},     // the damage function's head and the rounds' spawn (damagestats.cpp): the statistics' page over the map
    {IP::kFeatures,Id::kDamageNumbers,&Install<&InstallDamageNumbers>}, // the damage notice's colours (dmgnum.cpp): weak spots, friendly fire, the allies' numbers
    {IP::kFeatures,Id::kKillCounters,&Install<&InstallKillCounters>},   // the battle record's rows: the EDF4.1 port's kill counters after the stock ones (killcount.cpp)
    {IP::kFeatures,Id::kRounds,&Install<&InstallRounds>},          // the stock vehicles' and helis' impact points: the rounds as the game flies them
    {IP::kFeatures,Id::kStockGauges,&Install<&InstallStockGauges>},     // the stock weapon gauge where our HUD lists the weapons, the fuel tanks it showed
    {IP::kFeatures,Id::kWeaponEquip,&Install<&wequip::InstallWeaponEquip>},   // the 整备 weapons page's equip (weapon_equip.cpp): signatures only, no patch
    {IP::kFeatures,Id::kPackSwitch,&Install<&InstallPackSwitch>},  // the HQ's Q / E mission pack switch (pack_switch.cpp): before InstallRoomUi, which wraps its HQ hook
    {IP::kFeatures,Id::kRoomUi,&Install<&InstallRoomUi>},          // the support loadout menu in the online room and the offline HQ (chains both OnUpdates)
    {IP::kFeatures,Id::kWorldMap,&Install<&InstallWorldMap>},      // the mission select's list as the world map (worldmap.cpp: the select's update, draw, destructor)
    {IP::kFeatures,Id::kKeyConfig,&Install<&InstallKeyConfig>},    // the plugins' keys as pages of the game's own key config (keyconfig.cpp)
    {IP::kFeatures,Id::kOutfit41,[](const LoadContext&) noexcept {Log("OUTFIT41 installed=%d",InstallOutfit41());}},   // the EDF4.1 outfit: the HQ popup's sixth item, the model paths
    {IP::kFeatures,Id::kGlyphLock,&Install<&InstallGlyphLock>},    // the game's own text, wrong or missing characters (glyphs.cpp)
    {IP::kFeatures,Id::kEdf5Campaign,&Install<&InstallEdf5Campaign>},   // the EDF5 campaign's mission packs owned (edf5campaign.cpp)
    {IP::kFeatures,Id::kPortedMissions,&Install<&InstallPortedMissions>},   // ...and with PortedMissionsUnlocked every mission of the ported packs open (edf5campaign.cpp)
    {IP::kFeatures,Id::kEdf41MenuBg,&Install<&InstallEdf41MenuBg>},     // the EDF4.1 packs' HQ shows 4.1's background when the installer added it (edf5campaign.cpp)
    {IP::kFeatures,Id::kEdf5Online,&Install<&InstallEdf5Online>},  // ...and their missions online: the BVM player natives' online half (edf5online.cpp)
    {IP::kFeatures,Id::kJetSound,&Install<&InstallJetSound>},
    {IP::kFeatures,Id::kVehicleSound,&Install<&InstallVehicleSound>},   // the ground vehicles' engines, turrets, loaders and main guns (vehsound.cpp)
    {IP::kFeatures,Id::kBlastSound,&Install<&InstallBlastSound>},  // the rounds' explosions as War Thunder layers them (blastsound.cpp)
    {IP::kFeatures,Id::kMissiles,&Install<&InstallMissiles>},
    {IP::kFeatures,Id::kBarrage,&Install<&InstallBarrages>},       // the long-range barrages' rounds: RocketBullet01's update (barrage.cpp)
    {IP::kFeatures,Id::kAndryusha,&Install<&InstallAndryusha>},    // the Andryusha's carriers: GrenadeBullet01's update, chained (andryusha.cpp)
    {IP::kFeatures,Id::kSplitMissiles,&Install<&InstallSplitMissiles>},   // the stock split missiles' split distance to the target's surface
    {IP::kFeatures,Id::kSupportUnitOnAllies,&Install<&InstallSupportUnitOnAllies>},   // support units stick to allies (ini SupportUnitOnAllies, live)
    {IP::kFeatures,Id::kRouteGuide,&Install<&InstallRouteGuide>},  // the stock route guide strip along the navmesh route's corners
    {IP::kFeatures,Id::kGuidance,&Install<&InstallGuidance>},      // the stock homing rounds by proportional navigation
    {IP::kFeatures,Id::kStores,&Install<&InstallStores>},          // before any mission builds a jet: the 506 builds a weapon for every holder, the others their extras
    {IP::kFeatures,Id::kEdf41Bike,[](const LoadContext&) noexcept {Log("EDF41 original sidecar hooks=%d",InstallEdf41Bike());}},
    {IP::kFeatures,Id::kVulcanRotor,[](const LoadContext&) noexcept {Log("M163 visual rotor=%d",InstallVulcanRotor());}},
    {IP::kFeatures,Id::kSeatTurrets,&Install<&InstallSeatTurrets>},   // the gunner seats' own turrets, posed at the same world build
    {IP::kFeatures,Id::kPayload,&Install<&InstallPayload>},        // the stock vehicles' stores: the holder pull lands on the one picked
    {IP::kFeatures,Id::kPvp,[](const LoadContext&) noexcept {Log("PVP native hooks=%d",InstallPvp());}},
    {IP::kFeatures,Id::kRoomPvp,&Install<&InstallRoomPvp>},        // the room's start held until every member acknowledged the PvP lock (room_pvp.cpp)
    {IP::kFeatures,Id::kLockon,&Install<&InstallLockon>},          // every lock-on weapon's search order: the player's nearest the view first
    {IP::kFeatures,Id::kSeatSwitch,&Install<&InstallSeatSwitch>},  // the player moving between seats (the stock board button's steps, checked)
    {IP::kFeatures,Id::kVehiclerStart,&Install<&InstallVehiclerStart>},   // the Vehicler's start seat: the stock ride's natives checked (vehicler_start.cpp)
    {IP::kFeatures,Id::kAllClassesRide,&Install<&InstallAllClassesRide>},   // every class in every seat: before any mission makes a vehicle (the seat build is in its constructor)
    {IP::kFeatures,Id::kBigWorld,&Install<&InstallBigWorld>},
    {IP::kFeatures,Id::kLegacyMap,[](const LoadContext&) noexcept {Log("LEGACYMAP collision migration hooked=%d",InstallLegacyMap());}},   // EDF4.1 maps (plan P6)
    {IP::kFeatures,Id::kEdf41Ai,&Install<&InstallEdf41Ai>},        // the converted EDF4.1 Hectors' arm gate and back-off cycle (edf41ai.cpp)
    {IP::kFeatures,Id::kEdf41Fortress,&Install<&InstallEdf41Fortress>},   // the converted EDF4.1 fortress: standing, its drop through the core hatch (edf41fortress.cpp)
    {IP::kFeatures,Id::kEdf41Dragon,&Install<&InstallEdf41Dragon>},   // the converted EDF4.1 giant dragon: its flame stream, ranges and glide pass (edf41dragon.cpp)
    {IP::kFeatures,Id::kEdf41Insects,&Install<&InstallEdf41Insects>},   // the DLC's EDF4.1 queen bee and Nephilas: needle speed, thread scatter (edf41insects.cpp)
    {IP::kFeatures,Id::kEdf41MechBoost,&Install<&InstallEdf41MechBoost>},   // the 406 Begaruta (BM03 Vegalta): the jet slots its old boost parser never builds (edf41mech_boost.cpp)
    {IP::kFeatures,Id::kEdf41SmallDragon,&Install<&InstallEdf41SmallDragon>},   // the DLC's EDF4.1 small dragons: the hit reaction on 4.1's ragdoll bodies (edf41smalldragon.cpp)
    {IP::kFeatures,Id::kEdf41Natives,&Install<&InstallEdf41Natives>},   // EDF4.1 script natives the BVM executor lacks: SetEncountReportRadius (edf41natives.cpp)
    {IP::kFeatures,Id::kEdf41Before,&Install<&InstallEdf41Before>},   // EDF4.1's pre-mission scenes without the menu's black fade over them (edf41before.cpp)
    {IP::kFeatures,Id::kEdf41Sound,&Install<&InstallEdf41Sound>},  // EDF4.1's weapon SE bank and voice banks (edf41sound.cpp)
    {IP::kFeatures,Id::kEdf41Mother,&Install<&InstallEdf41Mother>},   // EDF4.1's UfoMother301 (ini Edf41Mother; edf41mother.cpp)
    {IP::kFeatures,Id::kEdf41Robo,&Install<&InstallEdf41Robo>},    // EDF4.1's UfoRobo (ini Edf41Robo; edf41robo.cpp)
    {IP::kFeatures,Id::kEdf41Deiroi,&Install<&InstallEdf41Deiroi>},   // a stock crash 4.1's M412 reaches (edf41deiroi.cpp)
    {IP::kFeatures,Id::kEdf41Mother401,&Install<&InstallEdf41Mother401>},   // EDF4.1's UfoMother401 (ini Edf41Mother401; edf41mother401.cpp)
    {IP::kFeatures,Id::kEnemyControlRadar,[](const LoadContext&) noexcept {Log("ENEMY CONTROL radar=%d",InstallEnemyControlRadar());}},
    {IP::kFeatures,Id::kControlledPvpSpawn,[](const LoadContext&) noexcept {Log("ENEMY CONTROL spawn ownership=%d",InstallControlledPvpSpawnBridge());}},
    {IP::kFeatures,Id::kEnemyControlNativePvp,[](const LoadContext&) noexcept {Log("ENEMY CONTROL native PVP ownership=%d",EnemyControlNativePvpInstall());}},
    {IP::kFeatures,Id::kEnemyControlWeaponAim,[](const LoadContext&) noexcept {Log("ENEMY CONTROL weapon aiming=%d",InstallEnemyControlWeaponAim());}},
    {IP::kFeatures,Id::kTerrainShare,&Install<&InstallTerrainShare>},   // before any map loads: the pieces of one terrain decoded once
    // before any mission: the online code that names players knows All Forces' classes 4 / 5 (soldier_identity.h)
    {IP::kFeatures,Id::kSoldierIdentity,&Install<&InstallSoldierIdentity>},
    {IP::kMission,Id::kMission,&Install<&InstallMission>},         // the mission's start (ResetAll, the preloads) and a trigger of the per-frame hooks
    {IP::kMission,Id::kLoadout,[](const LoadContext& c) noexcept {InstallLoadout(c.iniPath);}},
    {IP::kMission,Id::kAirstrikes,[](const LoadContext&) noexcept {Log("AIRSTRIKE takeovers=%d",InstallAirstrikes());}},
    {IP::kMission,Id::kCrew,[](const LoadContext&) noexcept {
        if(!InstallCrew())Log("HOOK crew: no seat hook, so no per-frame hook either: crews, helis, crawlers and the HUD are off");}},
    {IP::kLast,Id::kBulletExpandCull,&Install<&InstallBulletExpandCullSettle>},   // after damagestats' settle redirect: chains it, arms the cut
    {IP::kLast,Id::kPerfProbe,&Install<&perfprobe::Install>},      // last: it chains call sites other hooks redirect at load (perf_probe.h)
    {IP::kLast,Id::kSyncLoad,&Install<&syncload::Install>},        // after it: the game thread is the frame anchor's (sync_load_probe.h)
};

// The mission's start: every module's tables of the last mission dropped, before this mission's preloads.
constexpr ResetEntry kResets[]={
    {RP::kReleaseHolds,Id::kVulcanRotor,&Call<&ResetVulcanRotors>},
    {RP::kReleaseHolds,Id::kSeatTurrets,&Call<&ResetSeatTurrets>},
    {RP::kReleaseHolds,Id::kBarrelRecoil,&Call<&ResetBarrelRecoil>},
    {RP::kReleaseHolds,Id::kEnemyControlNetwork,&Call<&ResetEnemyControlNetwork>},   // stop authenticated leases before native input restoration
    {RP::kReleaseHolds,Id::kEnemyControl,&Call<&ResetEnemyControl>},   // release native player-mode leases before scene/team identities are reset
    {RP::kReleaseHolds,Id::kEnemyControlNativePvp,[]() noexcept {
        if(!EnemyControlNativePvpReset())Log("ENEMY CONTROL pending native ownership restore retained across mission reset");}},
    {RP::kReleaseHolds,Id::kPvp,&Call<&ResetPvp>},   // true world boundary: restore teams and release support-owner weak identities
    {RP::kMissionState,Id::kCommander,&Call<&ResetCommander>},   // the commander mode's last mission dropped, its map lock let go (commander.cpp)
    {RP::kMissionState,Id::kPlayer,&Call<&ResetPlayer>},
    {RP::kMissionState,Id::kFirstPerson,&Call<&ResetFirstPerson>},
    {RP::kMissionState,Id::kThermal,&Call<&thermal::Reset>},
    {RP::kMissionState,Id::kGpuFx,&Call<&gpufx::Reset>},
    {RP::kMissionState,Id::kCrew,&Call<&ResetCrew>},
    {RP::kMissionState,Id::kHeli,&Call<&ResetHelis>},
    {RP::kMissionState,Id::kGround,&Call<&ResetGround>},
    {RP::kMissionState,Id::kNpcGunners,&Call<&ResetNpcAi>},
    {RP::kMissionState,Id::kNpcParallel,&Call<&ResetNpcParallel>},   // the soldiers' parallel Decide: guesses, intents, counts (npcai.cpp mt)
    {RP::kMissionState,Id::kQMarks,&Call<&ResetQMarks>},   // the Q marks, ours and the teammates' (qmark.cpp)
    {RP::kMissionState,Id::kAutoTurret,&Call<&AutoTurretMissionReset>},   // EDF6AutoTurret's auto-aim back to its ini's (off), its lock let go (turretaim.cpp)
    {RP::kMissionState,Id::kNpcPost,&Call<&ResetNpcPosts>},
    {RP::kMissionState,Id::kAirstrikes,&Call<&ResetAirstrikes>},
    {RP::kMissionState,Id::kUltimate,&Call<&ResetUltimate>},
    {RP::kMissionState,Id::kBarrage,&Call<&ResetBarrages>},
    {RP::kMissionState,Id::kSupportFirepower,&Call<&ResetSupportFirepower>},
    {RP::kMissionState,Id::kSupportWarnings,&Call<&ResetSupportWarnings>},
    {RP::kMissionState,Id::kSupportPoints,&Call<&SupportPointsReset>},
    {RP::kMissionState,Id::kSupportDispatch,&Call<&ResetSupportDispatch>},
    // the room's channels (plugin_net.h): every Channel.reset, after the support session's own reset (support_net.cpp
    // ResetSupportNet, from the dispatch's) registered them
    {RP::kMissionState,Id::kPluginNet,&Call<&plugin_net::Reset>},
    {RP::kMissionState,Id::kTransports,&Call<&ResetTransports>},
    {RP::kMissionState,Id::kAirdrop,&Call<&ResetAirdrops>},
    {RP::kMissionState,Id::kJets,&Call<&ResetJets>},
    {RP::kMissionState,Id::kBoosters,&Call<&ResetBoosters>},
    {RP::kMissionState,Id::kShield,&Call<&ResetShields>},
    {RP::kMissionState,Id::kPlayerJet,&Call<&ResetPlayerJets>},
    {RP::kMissionState,Id::kSazabi,&Call<&ResetSazabi>},
    {RP::kMissionState,Id::kDreadnought,&Call<&ResetDreadnought>},
    {RP::kMissionState,Id::kBoarding,&Call<&ResetBoarding>},
    {RP::kMissionState,Id::kCarrier,&Call<&ResetSubs>},
    {RP::kMissionState,Id::kLaser,&Call<&ResetLaser>},
    {RP::kMissionState,Id::kHud,&Call<&ResetHud>},
    {RP::kMissionState,Id::kLauncher,&Call<&ResetLauncher>},
    {RP::kMissionState,Id::kKatyusha,&Call<&ResetKatyushas>},
    {RP::kMissionState,Id::kAndryusha,&Call<&ResetAndryusha>},
    {RP::kMissionState,Id::kHighCam,&Call<&ResetHighCam>},
    {RP::kMissionState,Id::kSightZoom,&Call<&ResetSightZoom>},
    {RP::kMissionState,Id::kInfantryScope,&Call<&ResetInfantryScope>},
    {RP::kMissionState,Id::kNix,&Call<&ResetNix>},
    {RP::kMissionState,Id::kTurretCam,&Call<&ResetTurretCam>},
    {RP::kMissionState,Id::kStabilizer,&Call<&ResetStabilizer>},
    {RP::kMissionState,Id::kMap,&Call<&ResetMap>},
    {RP::kMissionState,Id::kDamageStats,&Call<&ResetDamageStats>},
    {RP::kMissionState,Id::kWastedFire,&Call<&ResetWastedFire>},
    {RP::kMissionState,Id::kHeliSight,&Call<&ResetHeliSight>},
    {RP::kMissionState,Id::kStockHud,&Call<&ResetStockHud>},
    {RP::kMissionState,Id::kBargaAim,&Call<&ResetBargaAim>},
    {RP::kMissionState,Id::kStockGauges,&Call<&ResetStockGauges>},
    {RP::kMissionState,Id::kRoomUi,&Call<&ResetRoomUi>},   // the room / HQ screen is gone: its menu and room sync go on the next screen frame
    {RP::kMissionState,Id::kPayload,&Call<&ResetPayload>},
    {RP::kMissionState,Id::kSeatSwitch,&Call<&ResetSeatSwitch>},
    {RP::kMissionState,Id::kVehiclerStart,&Call<&ResetVehiclerStart>},
    {RP::kMissionState,Id::kGear,&Call<&ResetGear>},
    {RP::kMissionState,Id::kJetSound,&Call<&ResetJetSound>},
    {RP::kMissionState,Id::kVehicleSound,&Call<&ResetVehicleSound>},
    {RP::kMissionState,Id::kTrace,&Call<&TraceReset>},   // after it: the replay resets where the game did (trace_rec.cpp)
    {RP::kMissionState,Id::kBlastSound,&Call<&ResetBlastSound>},
    {RP::kMissionState,Id::kSazabiSound,&Call<&ResetSazabiSound>},
    {RP::kMissionState,Id::kMissiles,&Call<&ResetMissiles>},
    {RP::kMissionState,Id::kDrill,&Call<&ResetDrills>},
    {RP::kMissionState,Id::kRam,&Call<&ResetVehicleRams>},
    {RP::kMissionState,Id::kGravity,&Call<&gravity::Reset>},
    {RP::kMissionState,Id::kGravityNet,&Call<&gravity::net::Reset>},
    {RP::kMissionState,Id::kGravityGun,&Call<&ResetGravityGun>},
    {RP::kMissionState,Id::kFloatGuns,&Call<&FloatGunsMissionStart>},   // which mission this is (EDF5's last: the float guns), nothing kept from the last
    {RP::kMissionState,Id::kEmc,&Call<&ResetEmc>},
    {RP::kMissionState,Id::kSidecar,&Call<&ResetSidecars>},
    {RP::kMissionState,Id::kProteus,&Call<&ResetProteus>},
    {RP::kMissionState,Id::kAura,&Call<&ResetAura>},
    {RP::kMissionState,Id::kBigWorld,&Call<&ResetBigWorld>},
    {RP::kMissionState,Id::kTerrainShare,&Call<&ResetTerrainShare>},
    {RP::kMissionState,Id::kPlayArea,&Call<&ResetPlayArea>},
    {RP::kMissionState,Id::kEdf5Online,&Call<&ResetEdf5Online>},   // the EDF5 creation loop's online arguments (edf5online.cpp)
    {RP::kMissionState,Id::kVehicleWeapons,&Call<&ResetVehicleWeapons>},   // nothing of this mission's vehicle weapons queued yet (vehicle_weapons.h)
    {RP::kMissionState,Id::kUnderground,&Call<&frame::UnderReset>},   // the last mission's objects not watched (their addresses may be the new ones)
    {RP::kMissionState,Id::kWarn,&Call<&ResetWarn>},   // nothing lit, no missile counted from the last mission (warn.cpp)
};

inline void GpuFxTick() noexcept { gpufx::Tick(); }
inline void ThermalTick() noexcept { const auto& c=Cfg();thermal::Tick(c.enabled && c.thermal,static_cast<unsigned>(c.thermalKey)); }
inline void DebugSpawnTick() noexcept { DebugSpawnFrame(PlayerHuman()); Edf41Mother401DevTick(); }   // (the 401's dev enemy shooter: edf41mother401.cpp)
inline void GravityTick() noexcept { gravity::Frame(); GravityGunTick(); gravity::net::Frame(); FloatGunTick(); }

// Once a game frame, from the first vehicle input of the frame or the player's own frame: what is no one vehicle's
// (the sea rescue, which needs no heli to exist yet; the HUD's publish of what it gathered last frame).
constexpr TickEntry kTicks[]={
    {TP::kWorld,Id::kUnderground,&Call<&frame::UnderPlayerTick>},
    {TP::kWorld,Id::kExitGround,&Call<&frame::ExitGroundWatchTick>},   // off a vehicle: on the floor, not in it (exit_ground.h)
    {TP::kWorld,Id::kRescue,&Call<&RescueTick>},
    {TP::kBeforeHudPublish,Id::kWarn,&Call<&WarnTick>},   // the HUD's publish carries what this decides
    {TP::kBeforeHudPublish,Id::kThermal,&Call<&ThermalTick>},
    {TP::kBeforeHudPublish,Id::kGpuFx,&Call<&GpuFxTick>},
    {TP::kBeforeHudPublish,Id::kPvp,&Call<&PvpFrame>},
    {TP::kBeforeHudPublish,Id::kEnemyControl,&Call<&EnemyControlFrame>},
    {TP::kBeforeHudPublish,Id::kEnemyControlNetwork,&Call<&EnemyControlNetworkFrame>},
    {TP::kHudPublish,Id::kHud,&Call<&HudPublish>},
    {TP::kAfterHudPublish,Id::kBigWorld,&Call<&BigWorldProbe>},
    {TP::kAfterHudPublish,Id::kPlayArea,&Call<&PlayAreaTick>},   // the walls where the map's ground ends (playarea.cpp)
    {TP::kAfterHudPublish,Id::kSupportDispatch,&Call<&SupportDispatchTick>},
    {TP::kAfterHudPublish,Id::kVehiclerStart,&Call<&VehiclerStartTick>},   // after the dispatch's: its made hulls activated first
    {TP::kAfterHudPublish,Id::kPlayerJet,&Call<&PlayerEjectTick>},
    {TP::kAfterHudPublish,Id::kView,&Call<&ViewDiagTick>},   // the grey-map diagnosis (viewdiag.cpp)
    {TP::kAfterHudPublish,Id::kBoarding,&Call<&BoardingTick>},
    {TP::kAfterHudPublish,Id::kDebugSpawn,&Call<&DebugSpawnTick>},   // riding: the player's own frame may not come (off by default)
    {TP::kAfterHudPublish,Id::kUltimate,&Call<&UltimateTick>},       // the ultimate strike's show (ultimate.cpp), on with the plugin off too
    {TP::kAfterHudPublish,Id::kBarrage,&Call<&BarrageTick>},         // the long-range barrages' rounds (barrage.cpp)
    {TP::kAfterHudPublish,Id::kAndryusha,&Call<&AndryushaTick>},     // the Andryusha's airburst fuses and bomblets (andryusha.cpp)
    {TP::kAfterHudPublish,Id::kSupportPoints,&Call<&SupportPointsTick>},   // the map's support pt from the team score (support_points.cpp)
    {TP::kAfterHudPublish,Id::kSupportWarnings,&Call<&SupportWarningTick>},   // the supports' warnings on the ground (support_warning.cpp)
    {TP::kAfterHudPublish,Id::kGravity,&Call<&GravityTick>},         // what is thrown: its impacts and contacts (gravity.cpp)
    // The listener (once a frame: a vehicle's input may have placed it already), then the explosions heard from it: on
    // foot with no vehicle about too (blastsound.cpp).
    {TP::kAfterHudPublish,Id::kJetSound,&Call<&JetSoundTick>},
    {TP::kAfterHudPublish,Id::kBlastSound,&Call<&BlastSoundTick>},
    {TP::kAfterHudPublish,Id::kMapQueue,&Call<&MapCommandQueueTick>},   // the Shift-queued points of this machine's units (mapcmd.cpp)
    {TP::kAfterHudPublish,Id::kAura,&Call<&AuraTick>},               // the aura trucks' auras applied (aura.cpp)
};

// The vehicles' sounds and their recording at one "now" (SoundNow).
ULONGLONG soundMs=0;
inline void TraceSoundStep(unsigned char* v,int) noexcept { if(Cfg().traceFile[0])TraceVehicleSound(v,soundMs); }   // its inputs, for the offline replay (trace_rec.cpp)
inline void SoundStep(unsigned char* v,int) noexcept { VehicleSoundAt(v,soundMs); }

constexpr std::uint8_t kOff=kRunsWhenDisabled;
// Each vehicle's input, at the hook's seams (VehiclePhase).
constexpr VehicleEntry kVehicleSteps[]={
    // the drill tank's trigger is its drill's: taken off the seat before the stock input reads it (drill.cpp)
    {VP::kBeforeGears,Id::kDrill,&Vehicle<&DrillInput>,kNone},
    {VP::kBeforeGears,Id::kEmc,&Vehicle<&EmcInput>,kNone},   // the EMC's trigger is its charge's (emc.cpp): the same, the stock burst never starts
    // an NPC tank pushed off its post drives back: seat 0's stick written before the stock input reads it (npcpost.cpp)
    {VP::kBeforeGears,Id::kNpcPost,&Vehicle<&NpcPostInput>,kNone},
    // the NPC soldiers in its gunner seats aim and fire, before the stock input reads the seats (npcai.cpp); with the
    // plugin off too: it releases our last inputs then
    {VP::kBeforeGears,Id::kNpcGunners,&Vehicle<&NpcGunnersInput>,kOff},
    // a Dreadnought's local rider's aim stick as the observation reads it, before the stock input (dreadnought_turrets.inc)
    {VP::kBeforeGears,Id::kDreadnought,&Vehicle<&DreadnoughtInput>,kNone},
    {VP::kBeforeGears,Id::kSeatTurrets,&Vehicle<&SeatTurretsInput>,kNone},   // a gunner seat's own turret: its local rider's aim stick the same way (seat_turret.cpp)
    // a fixed emplacement never drives: its drive stick cleared last, over every writer before (emplacement.cpp)
    {VP::kBeforeStockInput,Id::kEmplacement,&Vehicle<&EmplacementInput>,kNone},
    {VP::kAfterStockInput,Id::kJetSound,&TickInVehicle<&JetSoundTick>,kOff},   // once a frame, the plugin off too: it stops the sounds then
    {VP::kAfterStockInput,Id::kTurretCam,&Vehicle<&TurretCamFrame>,kOff},   // the plugin off too: it lets the camera go then
    {VP::kAfterStockInput,Id::kProteus,&Vehicle<&ProteusFrame>,kOff},       // the plugin off too: a reworked Proteus gets its stock numbers back
    {VP::kAfterStockInput,Id::kHighCam,&Vehicle<&HighCamFrame>,kOff},       // the plugin off too: the high view goes then
    {VP::kSound,Id::kVehicleSound,&TraceSoundStep,kOff},
    {VP::kSound,Id::kVehicleSound,&SoundStep,kOff},   // the plugin off too: the stock sounds are given back then
    {VP::kAfterSound,Id::kEmc,&Vehicle<&EmcFrame>,kOff},   // the plugin off too: a charge going is let go then (its loop, its glow)
    {VP::kAfterSound,Id::kEmc,&TickInVehicle<&EmcTick>,kOff},   // the plugin off too: an EMC gone mid-charge has its loop stopped
    {VP::kAfterSound,Id::kSazabiSound,&TickInVehicle<&SazabiSoundTick>,kOff},   // the plugin off too: the Sazabi's loops stop then (once a frame)
    {VP::kFrame,Id::kCrew,&frame::CrewVehicle,kNone},
    {VP::kFrame,Id::kSeatSwitch,&Vehicle<&SeatSwitchFrame>,kNone},   // before the steps that read who sits where this frame
    {VP::kFrame,Id::kStabilizer,&Vehicle<&StabFrame>,kNone},         // its seats' aims, for the stabilizer in the aim step after this input
    {VP::kFrame,Id::kAimLines,&Vehicle<&frame::AimLinesVehicle>,kOff},   // the plugin off too: only our saved native lines given back, empty / NPC seats too
    {VP::kFrame,Id::kSightZoom,&Vehicle<&SightZoomStock>,kOff},      // the player's seat's sight magnification (sightzoom.cpp); the plugin off too
    {VP::kFrame,Id::kJets,&Vehicle<&frame::JetReapVehicle>,kNone},
    {VP::kFrame,Id::kHeliReap,&Vehicle<&frame::HeliReapVehicle>,kNone},
    {VP::kFrame,Id::kPlayerJet,&Vehicle<&PlayerJetFrame>,kNone},
    {VP::kFrame,Id::kSazabi,&Vehicle<&SazabiFrame>,kNone},
    {VP::kFrame,Id::kDreadnought,&Vehicle<&DreadnoughtFrame>,kNone},
    // the submarine carrier is the plugin's own (subcarrier.cpp's registry), driven whoever sits in it: not from
    // HeliFrame, which wants an NPC pilot in seat 0 and the heli profile
    {VP::kFrame,Id::kCarrier,&Vehicle<&frame::SubVehicle>,kNone},
    {VP::kFrame,Id::kHeli,&Vehicle<&frame::HeliVehicle>,kNone},
    {VP::kFrame,Id::kHeli,&Vehicle<&HeliCueStep>,kNone},
    {VP::kFrame,Id::kGround,&Vehicle<&frame::GroundVehicle>,kNone},
    {VP::kFrame,Id::kDrill,&Vehicle<&DrillFrame>,kNone},
    {VP::kFrame,Id::kSidecar,&Vehicle<&SidecarFrame>,kNone},
    {VP::kFrame,Id::kAura,&Vehicle<&AuraVehicleFrame>,kNone},   // an aura truck seen: where, how fast, driven (aura.cpp)
    {VP::kFrame,Id::kRam,&Vehicle<&VehicleRamFrame>,kNone},     // after the drill and the sidecar: what the parts drove into this frame
    {VP::kFrame,Id::kHud,&Vehicle<&HudSee>,kNone},
    {VP::kFrame,Id::kLauncher,&Vehicle<&LauncherFrame>,kNone},
    {VP::kFrame,Id::kAndryusha,&Vehicle<&AndryushaFrame>,kNone},   // after the fuse switch (LauncherFrame): the fuse to the copies
    {VP::kFrame,Id::kPayload,&Vehicle<&PayloadFrame>,kNone},       // before the sight: it marks the store the secondary fires
    {VP::kFrame,Id::kSeatTurrets,&Vehicle<&SeatTurretsFrame>,kNone},   // the gunner seats' own turrets: trained, the unmanned ones fired
    {VP::kFrame,Id::kTwinBarrel,&Vehicle<&TwinBarrelFrame>,kNone},     // after every pull of this frame (stock, store, NPC): twin barrels in turn
    {VP::kFrame,Id::kHeliSight,&Vehicle<&HeliSightFrame>,kNone},   // after AimLines: the sight reads which lines are hidden
    {VP::kFrame,Id::kStockHud,&Vehicle<&StockHudFrame>,kNone},
    {VP::kFrame,Id::kBargaAim,&Vehicle<&BargaAimFrame>,kNone},     // the player's Balam: its reach, hit spheres, cannon landing
    {VP::kFrame,Id::kJetSound,&Vehicle<&JetSound>,kNone},
    {VP::kFrame,Id::kJetLever,&Vehicle<&JetLeverFrame>,kNone},     // after its engine sound: online, a registered jet's lever sent / burnt here
    {VP::kFrame,Id::kLockSound,&Vehicle<&LockSound>,kNone},
    {VP::kFrame,Id::kUnderground,&Vehicle<&frame::UnderVehicleStep>,kNone},
    {VP::kFrame,Id::kShield,&Vehicle<&ShieldVehicle>,kNone},
    {VP::kFrame,Id::kNetProbe,&Vehicle<&NetProbe>,kNone},
};

static_assert(PhasesInOrder(kInstalls) && PhasesInOrder(kResets) && PhasesInOrder(kTicks) && PhasesInOrder(kVehicleSteps),
              "each pass's table in its phases' order");
static_assert(IdsInRange(kInstalls,kModuleCount) && IdsInRange(kResets,kModuleCount) && IdsInRange(kTicks,kModuleCount) &&
              IdsInRange(kVehicleSteps,kModuleCount),"every entry a module's, with a function");
static_assert(FirstUnused(kModules,kInstalls,kResets,kTicks,kVehicleSteps)==kModuleCount,"every module in some pass");
// The reset rule (module_logic.h): a module that keeps anything has its one reset, one that keeps nothing says why.
static_assert(FirstResetBreak(kModules,kResets)==kModuleCount,"every stateful module has its reset (or a stateless reason)");
}  // namespace
}  // namespace crew::modules
