// The plugin's game clock (crew.cpp GameMs), apart so tools/pause_clock_check.cpp runs the same rule offline.
// Wall time, except:
//  - while the game says it is paused (the pause menu: crew.cpp GamePaused, xgs::game::System's pause bits) it does not
//    move at all. The pause menu does not stop every caller: the System's update steps the viewport cameras on the paused
//    path too (0x119953B -> 0x118DF40 -> camera slot 4, docs/hud-re.md §10), and the turret camera's placement
//    (turretcam.cpp Camera, from the riding camera's state 2) reads the clock there every frame. Under the old rule
//    (only a gap counts as a pause) those reads, 16 ms apart, kept the clock running through the whole pause: the
//    turret camera's 150 / 200 ms freshness ran out a few frames in and it handed the camera back to the stock seat
//    view, which swung away under the pause menu and swung back on resume (the user, 2026-10-06: "暂停以后hud会飞走").
//  - a gap between two reads longer than kPauseMs (loading, a pause seen by nobody, no pause flag known) counts as one
//    kStepMs frame, as before: nothing is flown or updated in it.
// The first read after the pause steps by the wall time since the last (paused) read: one frame, not the pause.
#pragma once
#include <cstdint>

namespace gameclock {
constexpr std::uint64_t kPauseMs=250,kStepMs=16,kStart=3600000;   // starts an hour in: 0 is "never" for its timestamps

struct Clock { std::uint64_t wall=0,game=kStart; };

// One read at wall time `wall` (ms); `paused`: the game is paused this frame.
inline std::uint64_t Read(Clock& c,std::uint64_t wall,bool paused) noexcept {
    if(c.wall && !paused)c.game+=wall-c.wall>kPauseMs ? kStepMs : wall-c.wall;
    c.wall=wall;
    return c.game;
}
}  // namespace gameclock
