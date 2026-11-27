// The game clock, the scene step and the mission's lifecycle (crew.cpp, mission.cpp), and the local player as
// last seen (plugin.cpp).
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>

namespace crew {
// --- Time ---
// The game clock, game thread only (game_clock.h): wall time, stopped while the game is paused (GamePaused), and a
// gap between two reads longer than 250 ms (loading) counts as one 16 ms frame. Every timer of the plugin's logic,
// the player fix's included, is on this clock; wall time (GetTickCount64) is for log throttles and other threads only.
ULONGLONG GameMs() noexcept;
// The game's own pause (the pause menu: xgs::game::System's pause bits, docs/hud-re.md §10); any thread. False when
// CheckPauseFlag (at load, patches nothing) found the code different.
bool CheckPauseFlag() noexcept;
bool GamePaused() noexcept;
// Another thread, or data handed between hooks: stepfresh::Now() / Fresh (step_fresh.h), the same count mirrored.
// The game frame number, game thread only: it steps when a vehicle's per-frame input comes round again
// (crew.cpp InputHook calls SeeFrame), so "once a frame" work compares frame numbers, not clocks.
ULONGLONG GameFrame() noexcept;
// Wall time (GetTickCount64) for what has to see a stalled frame (memo_epoch.h): a function of its own, like GameMs and
// GameFrame, so a test steps it instead of sleeping.
ULONGLONG WallMs() noexcept;
// The native scene step as it reads now (System +0xCE0, stepped before the scene's input/update traversal), with its
// scene and the mission (ResetCrew): a per-frame key valid from the frame's first input on, where GameFrame still has
// the last frame's until the first vehicle's input is done. scene null: unreadable.
struct SceneStep { const void* scene; std::uint32_t count,mission; };
SceneStep SceneStepNow() noexcept;
void SeeFrame(const void* vehicle) noexcept;

// --- Mission lifecycle (mission.cpp) ---
// The mission's player preload (mission.cpp hooks it): every table of per-object state from the last
// mission is dropped here, before the new mission's objects (which may reuse the old addresses) exist.
void MissionStart() noexcept;
void ResetCrew() noexcept;        // crew.cpp
void SceneOver() noexcept;        // crew.cpp: the scene's end (mission.cpp), every step stamp stale

// The player as last seen (on foot through the prompt visitor, or riding through a vehicle input); `at` is
// GameMs (0: never seen).
struct PlayerFix { float pos[3]; std::int32_t team; ULONGLONG at; };
extern PlayerFix player;
void SeePlayer(const float* pos,std::int32_t team) noexcept;
// A new mission (MissionStart): the last mission's fix and player human are forgotten. GameMs counts the load
// as one frame, so without this they would pass for fresh at the new mission's start.
void ResetPlayer() noexcept;
// Shared game-thread scene tick, called by the local Soldier and by vehicle inputs; de-duplicated by GameFrame.
void FrameTick() noexcept;

// The local player's human (plugin.cpp): the soldier a local camera follows, as its step reports it
// (SeeCameraPlayer, firstperson.cpp CamStepHook); nullptr when no camera reported it for kPlayerHumanMs or it is
// no longer the same live player object.
void SeeCameraPlayer(unsigned char* human) noexcept;
unsigned char* PlayerHuman() noexcept;
}  // namespace crew
