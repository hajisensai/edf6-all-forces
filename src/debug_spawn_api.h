// debug_spawn.cpp's entry points (the logic is debug_spawn.h).
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>

namespace crew {
// debug_spawn.cpp (ini DebugSpawn, off by default; src/debug_spawn.h): spawn vehicles, the plugin's aircraft, enemies and
// friendly soldiers out of thin air where the crosshair points. No hook of its own: PreloadDebugSpawn from the mission's
// start (mission.cpp), DebugSpawnFrame from the player's frame (map.cpp MapHumanFrame, crew.cpp FrameTick; once a game
// frame whichever comes first), the menu drawn from the HUD pass (hud.cpp) through DebugSpawnReadout.
void PreloadDebugSpawn() noexcept;
void DebugSpawnFrame(unsigned char* human) noexcept;
struct DebugSpawnCue {
    bool open;                // the menu is up
    int category;             // debugspawn::Category shown
    int row;                  // kEntries index picked (-1 none)
    int pick,count;           // its place in the category, of how many
    bool ready;               // the row can be spawned now (preloaded / its module up)
    int status;               // the last spawn's result, for a moment: DebugSpawnStatus (none: nothing to say)
    int statusRow;            // ...whose (kEntries index)
    wchar_t note[96];         // ...a support call's own answer (support_dispatch.cpp SupportCallAt), else empty
    int keys[5];              // the ini's keys: toggle, prev, next, category, spawn
};
// kDebugSpawnLoading: the row was not loaded this mission; its load is queued (debug_spawn.cpp RequestLoad) and the
// spawn is made at the same place as soon as the game reports its resources complete.
enum DebugSpawnStatus : int { kDebugSpawnNone, kDebugSpawnDone, kDebugSpawnFailed, kDebugSpawnOnline, kDebugSpawnNoPlace,
                                   kDebugSpawnRequested, kDebugSpawnRefused, kDebugSpawnLoading };
bool DebugSpawnReadout(DebugSpawnCue* out) noexcept;   // any thread; false: nothing to show
}  // namespace crew
