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
#include "mission_watch.h"
#include "transport.h"
#include "airdrop.h"
#include "gear.h"
#include "memory.h"
#include "playarea.h"
#include "outfit41.h"
#include "host_settings.h"
#include "room_ui.h"
#include "sazabi_sound.h"
#include "support_spawn.h"
#include "support_soldier.h"
#include "thermal.h"
#include "pvp.h"
#include "enemy_control.h"
#include "enemy_control_net.h"
#include "enemy_control_pvp_native.h"
#include "vulcan_rotor.h"
#include "support_variants.h"
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
    ResetEnemyControlNetwork(); // stop authenticated leases before native input restoration
    ResetEnemyControl(); // release native player-mode leases before scene/team identities are reset
    if(!EnemyControlNativePvpReset())Log("ENEMY CONTROL pending native ownership restore retained across mission reset");
    ResetPvp();  // true world boundary: restore teams and release support-owner weak identities
    ResetPlayer();
    ResetFirstPerson();
    thermal::Reset();
    ResetCrew();
    ResetHelis();
    ResetGround();
    ResetNpcAi();
    ResetQMarks();   // the Q marks, ours and the teammates' (qmark.cpp)
    ResetNpcPosts();
    ResetAirstrikes();
    ResetUltimate();
    ResetSupportDispatch();
    ResetTransports();
    ResetAirdrops();
    ResetJets();
    ResetBoosters();
    ResetShields();
    ResetPlayerJets();
    ResetSazabi();
    ResetBoarding();
    ResetSubs();
    ResetLaser();
    ResetHud();
    ResetLauncher();
    ResetKatyushas();
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
    WatchMissionPhase("support variants");
    PreloadSupportVariants(); // ...and the generated coloured soldiers / loaded vehicles there are (support_variants.h)
    WatchMissionPhase("debug spawn");
    PreloadDebugSpawn();      // the debug spawn tool's stock vehicles and enemies; nothing at all with DebugSpawn=0
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
    Log("HOOK mission sync=%d",sync);
    Log("HOOK mission preload=%d/%zu online=%d/%zu%s",offline,std::size(kPreloadCalls),online,std::size(kSessionCalls),
        offline+online ? "" : " (no mission start: per-object state is kept across missions, nothing is preloaded)");
    return offline+online>0;
}
}  // namespace crew
