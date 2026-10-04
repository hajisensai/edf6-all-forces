// The mission's lifecycle: one place where a new mission starts (the player preload, loadout.cpp hooks it),
// so every module drops what it kept about the last mission's objects before the new ones exist. Without it
// the tables keyed by object address carried stale entries (and live-looking timestamps: the game clock
// barely moves while loading) into the next mission, whose objects may sit at the same addresses.
#include "crew.h"

namespace crew {
void MissionStart() noexcept {
    ResetCrew();
    ResetHelis();
    ResetGround();
    ResetAirstrikes();
    ResetJets();
    ResetBoosters();
    ResetPlayerJets();
    ResetSubs();
    ResetLaser();
    ResetHud();
    PreloadJets();   // the airstrike takeovers' jets (jet.cpp), with the mission's own resources
    PreloadSub();    // ...and the submarine carrier (subcarrier.cpp)
    PreloadLaser();  // ...and the teleportation ships' portal laser (carrierlaser.cpp)
    Log("MISSION start: per-object state dropped, resources preloaded");
}
}  // namespace crew
