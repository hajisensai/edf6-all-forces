// Cross-thread / cross-hook handoff freshness by the scene step, not the wall clock (docs/step-freshness.md).
//
// A publisher stamps its data with the step it published it in (stepfresh::Now()); a reader takes the data as current
// while that stamp is within `lag` steps of the step now. The step is a scene traversal the plugin saw run (crew.cpp
// SeeFrame: one each time crew::GameFrame(), System +0xCE0 extended, moves), at most 3 a frame (docs/perf-probe-re.md
// §1), mirrored here so every thread reads the same count. Not GameFrame() itself: +CE0 is incremented at the top of
// System::update's step loop (0x1198FD0), before the pause test skips the scene (0x11990C0), so it runs on under the
// pause menu; this count stands still there, and a readout published before the pause is as fresh after it.
//
// Why not a wall-clock window (GetTickCount64 / GameMs <= 200..500 ms): a slow frame (stress:100, 204 ms a frame; a
// load hitch) ages a readout past the window although its publisher ran in the very last step, and the HUD drops it
// for a frame. A step count only moves when the scene steps, so "published in the last step or so" holds at any frame
// rate, and with frame pacing's n = 0 frames (docs/unlimited-fps-re.md §4.3) the draw keeps the last step's data.
//
// What each thread sees:
//  - game thread (vehicle input, Think, the shared frame tick, the camera step): Now() is the step being run. A reader
//    may run before or after its publisher within a step, so an every-step publisher's stamp is Now() or Now()-1;
//    kEveryStep allows one more step for a publisher skipped once (its object's slot not reached that step).
//  - draw thread (the HUD pass, recorded at the start of System::update 0x1198FA4 before the frame's steps, or the
//    render thread executing the last frame's recording): Now() is the last step the game thread ran, the scene the
//    recording shows; data stamped in that step or kEveryStep before it is that scene's.
//
// What the step count cannot tell, and is an explicit state instead:
//  - the pause menu: the scene does not run and the count stands still, so data stays "fresh" by count. A reader that must not show anything under the pause
//    menu checks crew::GamePaused() (the HUD pass does, hud.cpp HudDraw); one that keeps a camera / lens through the
//    pause relies on exactly this.
//  - the scene's end (mission.cpp: FreeResourceGroup(2) releasing the scene's resources, after a clear / withdraw /
//    failure and back to the title; crew.cpp SceneOver) and a new mission / load (crew.cpp ResetCrew): NewMission moves
//    Now() kMissionGap steps on, so nothing stamped in the scene before is fresh. Between the two no traversal runs and
//    the count stands still, so without the first every stamp of the last step would stay fresh in the menus (every
//    module also clears its own state in its Reset*).
//  - a state the publisher leaves (the player got out, the map closed, the scope closed): the publisher clears its
//    data when it sees the transition; the count only covers a publisher that stopped running at all (its vehicle
//    gone, the soldier's frame no longer called).
#pragma once
#include <atomic>
#include <cstdint>

namespace stepfresh {
using Step=std::uint64_t;   // 0: never published

// An every-step publisher's data, read by the game thread (before or after it in the same step) or the draw thread.
constexpr Step kEveryStep=2;
// Data published once a frame (the HUD pass before the frame's steps; the stock gauge update, whose thread static reading
// does not settle: once a frame at worst), read by the game thread in the frame's steps: up to 3 steps a frame
// (System::update) after the step the publisher saw, and a whole frame more (3 steps) for a frame it skipped: 2 x 3.
constexpr Step kFromDraw=6;
// Far more steps than a mission has between a reset and its first step: a new mission's Now() is past every stamp of
// the last one by at least this.
constexpr Step kMissionGap=Step{1}<<32;

namespace detail {
struct Mirror {
    std::atomic<Step> now{1};   // the plugin's first step until SeeFrame's (crew.cpp sceneSteps starts there too)
    Step base=0;   // game thread only
};
inline Mirror mirror{};
}  // namespace detail

// Game thread (crew.cpp SeeFrame): the count of scene traversals that ran, from GameFrame() read in each. One step for
// each time it moved, however far: after the pause menu +CE0 has run on through every skipped traversal, and those are
// not steps any publisher ran in. Moved while paused (a caller outside the skipped traversal): not a step either.
struct Traversals {
    std::uint64_t logical=0;   // GameFrame() last seen
    Step steps=1;              // Now()'s first value (Mirror)
    Step Observe(std::uint64_t frame,bool paused) noexcept {
        if(frame!=logical){logical=frame;if(!paused)++steps;}
        return steps;
    }
};

// Any thread: the step now (never 0: a stamp of 0 is "never published").
inline Step Now() noexcept { return detail::mirror.now.load(std::memory_order_acquire); }
// Game thread (crew.cpp SeeFrame): the traversal count now (Traversals::steps, not GameFrame).
inline void See(Step traversal) noexcept { detail::mirror.now.store(detail::mirror.base+traversal,std::memory_order_release); }
// Game thread (crew.cpp SceneOver at the scene's end, ResetCrew at a new mission): every stamp so far is stale from here on.
inline void NewMission(Step traversal) noexcept {
    detail::mirror.base+=kMissionGap;
    See(traversal);
}

// `at` (a stamp of Now()) is within `lag` steps of `now`. A stamp ahead of `now` (the reader loaded Now() before the
// publisher's store reached it) is the newest data there is.
constexpr bool Fresh(Step at,Step now,Step lag) noexcept { return at!=0 && (at>=now || now-at<=lag); }
inline bool Fresh(Step at,Step lag=kEveryStep) noexcept { return Fresh(at,Now(),lag); }
}  // namespace stepfresh
