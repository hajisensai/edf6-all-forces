// The mission's lifecycle: one place where a new mission starts (the player preload, hooked here), so every
// module drops what it kept about the last mission's objects before the new ones exist. Without it the
// tables keyed by object address carried stale entries (and live-looking timestamps: the game clock barely
// moves while loading) into the next mission, whose objects may sit at the same addresses.
// The preload is hooked here, not in the test-range loadout (loadout.cpp), so the mission's start (and with
// it the jets', the carrier's and the laser's preloads) does not hang on the loadout's own checks: the
// loadout only wraps the call when it is on (LoadoutPreload). Layout: docs/loadout-re.md.
#include "crew.h"
#include "gear.h"
#include "memory.h"
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

std::uintptr_t __fastcall PreloadHook(std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d) {
    const auto result=LoadoutPreload(preloadOrig,a,b,c,d);
    MissionStart();
    return result;
}

std::uintptr_t __fastcall SessionPreloadHook(std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d) {
    const auto result=sessionOrig(a,b,c,d);
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
    ResetPlayer();
    ResetCrew();
    ResetHelis();
    ResetGround();
    ResetAirstrikes();
    ResetJets();
    ResetBoosters();
    ResetShields();
    ResetPlayerJets();
    ResetSubs();
    ResetLaser();
    ResetHud();
    ResetLauncher();
    ResetKatyushas();
    ResetHighCam();
    ResetHeliSight();
    ResetGear();
    ResetJetSound();
    ResetMissiles();
    ResetDrills();
    ResetSidecars();
    ResetBigWorld();
    PreloadJets();   // the airstrike takeovers' jets (jet.cpp), with the mission's own resources
    PreloadPlayerJets();   // ...and the player jets, for the catch after an ejection (playerjet.cpp)
    PreloadSub();    // ...and the submarine carrier (subcarrier.cpp)
    PreloadLaser();  // ...and the teleportation ships' portal laser (carrierlaser.cpp)
    EnsureInputs();  // every plugin has loaded by now: the per-frame hooks chain onto theirs
    Log("MISSION start: per-object state dropped, resources preloaded");
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
    Log("HOOK mission preload=%d/%zu online=%d/%zu%s",offline,std::size(kPreloadCalls),online,std::size(kSessionCalls),
        offline+online ? "" : " (no mission start: per-object state is kept across missions, nothing is preloaded)");
    return offline+online>0;
}
}  // namespace crew
