// map.cpp: the map view (README 地图, docs/camera-re.md §7): M (ini MapKey) or a pad button (MapButton, XInput bits)
// lifts the player's camera into an overhead view of the real 3D world that the mouse / stick pans and turns and the
// wheel zooms (200 m to 3 km), the player's controls held while it is open; hud.cpp draws its marks over it.
// Included by crew.h.
#pragma once
#include <Windows.h>
#include <cstdint>

namespace crew {
// What a mark on the map is: its side and kind (the pin's icon). `lock` is a weapon's lock point (no unit of its own).
enum class MapKind : std::uint8_t { squad, ally, vehicle, air, carrier, enemy, enemyAir, marker, lock };
// A unit's flags: a large enemy (its HP bar shown), the nearest enemy, a lock still acquiring (kind lock), a flying
// small enemy (its dot hollow).
constexpr std::uint8_t kMapLarge=1,kMapNearest=2,kMapAcquiring=4,kMapFlying=8;
// A unit: where it is, the ground under it (an aircraft's stem goes down to it; else its own height), its level heading
// (0, 0: none), its HP share (<0: not shown).
struct MapUnit { float pos[3],ground,dir[2],hp; MapKind kind; std::uint8_t flags; };
constexpr int kMapUnits=384;
// The enemies (the user, 2026-10-06: "large ones first; the small ones need no 3D"): every large one a pin of its own
// (kMapUnits' kind enemy / enemyAir, flag kMapLarge), at most kMapLargeEnemies, the highest HP max first; every small
// one a flat dot (no pin, no ray of its own), at most kMapDots, the nearest first (map.cpp Enemies).
constexpr int kMapLargeEnemies=64,kMapDots=1024;
struct MapDot { float pos[3]; std::uint8_t flags; };   // kMapFlying, kMapNearest
// The map as the game thread last published it, for the draw: the view (its focus on the ground, heading, pitch down,
// height), the player and what the map marks.
struct MapReadout {
    bool pad;                  // the last input was a pad's (the help line shows its buttons)
    bool follow;               // the focus follows the player (until panned; Space / A again)
    float focus[3],yaw,pitch,height;
    float me[3],meDir[3];      // the player (or the vehicle they ride): where, its level heading
    int count;
    MapUnit unit[kMapUnits];
    int dots;
    MapDot dot[kMapDots];
    int mapKey,mapButton;      // the keys as the ini has them (the help line)
};
bool InstallMap() noexcept;    // at load: the input hold, the camera hook, the objective markers' hooks
void ResetMap() noexcept;      // mission.cpp MissionStart: the map closed
// The map is open now (wall clock fresh): `out` its readout. The draw thread's.
bool PlayerMap(MapReadout* out) noexcept;
// The map holds the player's input (every key the plugin reads gives way: KeyHeld helpers test this).
bool MapHoldsKeys() noexcept;
// The camera is the map's this frame (open, or easing back): the HUD keeps its last game view for the aim (CameraRay).
bool MapOwnsView() noexcept;
}  // namespace crew
