// TV guidance for the Air Raider's Tempest cruise missiles (src/tvguide.cpp, docs/tvguide-re.md): the player's own
// Tempest, once it is out, is flown from its nose; the camera rides it, the soldier stands still (the map's hold),
// mouse / right stick steer it at its own stock turn, fire boosts it (for good), the right button (or Esc / B) hands it back to its laser.
// Offline only: controls are not replicated. Online missiles retain their existing stock / PN laser guidance.
#pragma once
#include <cstdint>

namespace crew {
// The player's input this frame for the TV (read by map.cpp, which owns the window, the pad and the soldier's hold).
struct TvInput {
    bool front;        // the game's window in front (keys and mouse count)
    float dx,dy;       // the game's own mouse delta this frame (mouse units; 0 with none)
    float rx,ry;       // the right stick (-1..1, past its dead zone)
    bool fire,leave;   // left button / right trigger; Esc / B
    // The right button (2026-10-10 feedback: "没办法切换回原本的视角" -- Esc is the game's pause menu, which pauses the
    // soldier's update before the TV sees it, so the view stayed on the missile). Hands back as Esc / B does, once it has
    // been let go since the take (an Air Raider aims the laser zoomed with it held).
    bool back;
};
// The TV's frame for the player `human` (game thread, the soldier pre-update, after the map's): true while it holds
// the soldier (TV on, or draining a key that ended it). `mapOpen`: the map took the view (the TV ends).
bool TvFrame(unsigned char* human,bool mapOpen,const TvInput& in) noexcept;
// The view the TV shows (any thread): the soldier it is for, the eye and a point ahead; false: no TV.
bool TvView(const void** human,float* eye,float* look) noexcept;
// Whether the TV holds the plugin's keys (MapHoldsKeys: theirs and EDF6AutoTurret's).
bool TvHoldsKeys() noexcept;
// MissileBullet01's update, before the stock step (missile.cpp): takes the player's Tempest as it comes out and steers
// the one the TV flies. Must run even with Enabled=0 to release a previously owned round safely inside its update.
// True: round `b` is the TV's (the plugin's own guidance leaves it).
bool TvSteer(unsigned char* b) noexcept;
// MissileBullet01's update, after the stock step (missile.cpp): the TV's view from where the round is now (the stock
// step moved it after TvSteer; a view placed before it was a frame behind and inside the missile's own model).
void TvAfter(unsigned char* b) noexcept;
// A new mission: the TV dropped.
void ResetTv() noexcept;
}  // namespace crew
