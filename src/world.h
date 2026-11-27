// The world's size (bigworld.cpp, fieldshare.cpp): the physics world's half, the play area's edge, the
// support arrivals' square.
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include "config.h"

namespace crew {
void ResetBigWorld() noexcept;    // bigworld.cpp
bool InstallTerrainShare() noexcept;   // fieldshare.cpp
void ResetTerrainShare() noexcept;
void BigWorldProbe() noexcept;
// The process's memory and the machine's, one log row (bigworld.cpp): at the mission's start and once its map is in, so
// a map's cost shows in the log (the 3 x 3 big map ran an 8 GB machine out of memory, testhub report #3).
void LogMemory(const char* when) noexcept;
// m: the physics world's half size (3000 stock, ini BigWorld when raised): the plugin's walls stand inside it.
// The edge of the play area every flyer keeps inside (the user, 2026-10-05: "don't let them go out there; a buffer
// before it; past the line, coming back comes first"): the stock world's 2400 (600 m inside its +-3000), or the big
// map's ground's own edge, its BigWorld less the margin tools/make_bigmap.py adds past the last block (WORLD_MARGIN;
// selftest holds the two equal). It was the physics world less 600 m on the big map too: 150 m out over no ground,
// where the player slid along the wall below the ground with the heading snapping +-17 deg (2026-10-05 11:47).
constexpr float kBigWorldMargin=750.0f,kStockEdgeIn=600.0f;
// The buffer inside the edge: from here in the flyers are turned in, the more the nearer the edge (EdgeTurn).
constexpr float kEdgeBuffer=800.0f;
inline float WorldHalf() noexcept;
inline float PlayEdge() noexcept { return Cfg().bigWorld>3000.0f ? Cfg().bigWorld-kBigWorldMargin : 3000.0f-kStockEdgeIn; }
inline float WorldHalf() noexcept { return Cfg().bigWorld>3000.0f ? Cfg().bigWorld : 3000.0f; }    // bigworld.cpp: once a mission, the map's ground on a grid (log)
// m: the Havok world's half size in fact (bigworld.cpp): ini BigWorld only once its bounds were patched in, else the
// stock 3000 (WorldHalf takes the ini at its word).
float HavokHalf() noexcept;
// m: the square the support arrivals are made and fly their passes in (support_entry.h Reach::half): kArrivalRoom inside
// both the Havok world (a body past it is no longer simulated) and the jets' deletion (jet.cpp kWorldGoneIn of WorldHalf).
constexpr float kArrivalRoom=300.0f;
inline float ArrivalHalf() noexcept { const float h=HavokHalf(),w=WorldHalf();return (h<w ? h : w)-kArrivalRoom; }
// A bigger physics world and the map pieces' log (bigworld.cpp): at load, before any mission.
bool InstallBigWorld() noexcept;
}  // namespace crew
