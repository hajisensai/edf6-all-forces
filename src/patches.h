// The standalone game patches plugin.cpp installs at load, one .cpp each.
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>

namespace crew {
// allclass.cpp: every class in every seat (ini AllClassesRide): the seat build's class mask widened, the ride pose's fallback.
bool InstallAllClassesRide() noexcept;
// fsbudget.cpp: CRI File System's loader pool (num_loaders: 130 as the game builds it, 0x60 + its caller's 0x22) raised
// by the model archives the spawn catalog can keep open (src/spawn_catalog_archives.h) before the library starts, so the
// test range's place panel catalog does not run it out. The other pools do not grow with open archives (fsbudget.cpp).
bool InstallFsLoaderBudget() noexcept;
// killcount.cpp: the battle record (戦績) also lists the EDF4.1 port's own kill counters (edf41_*KillCount, the
// installer's Mods/ETC/ACHIEVEMENT.SGO; docs/kill-counter-re.md): its row table moved to the plugin's, rows added.
bool InstallKillCounters() noexcept;
// The stock split missiles (MissileBullet02) split short of a big target's surface (splitmissile.cpp).
bool InstallSplitMissiles() noexcept;
// Support units (SupportUnitBullet01) stick to allies (supportunit.cpp): the ctor's pass-through-allies byte cleared
// while the ini's SupportUnitOnAllies is on (set by every config load).
bool InstallSupportUnitOnAllies() noexcept;
void SetSupportUnitOnAllies(bool on) noexcept;
// The stock route guide strip (RouteGuide) drawn along the navmesh route's corners (route_guide.cpp).
bool InstallRouteGuide() noexcept;
bool InstallGuidance() noexcept;   // guidance.cpp: the stock homing rounds' steering calls
// The game's glyph cache under one lock (glyphs.cpp): its threads lost glyphs and showed one character as another.
bool InstallGlyphLock() noexcept;
bool InstallBodyBox() noexcept;                    // body_box.cpp: before InstallMissiles and InstallJets
// body_box.cpp: physics body (wrapper) `body`'s world box, min xyz then max xyz; false without one
bool BodyWorldBox(const void* body,float* box) noexcept;
// physics.cpp: stock EDF6 physics defects, patched at load (needs a game restart to toggle)
bool InstallPhysics() noexcept;
}  // namespace crew
