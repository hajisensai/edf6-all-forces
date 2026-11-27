// Air strikes, barrages, the ultimate strike and the support dispatch (airstrike.cpp, barrage.cpp,
// ultimate.cpp, support_dispatch.cpp).
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>

namespace crew {
void ResetAirstrikes() noexcept;  // airstrike.cpp
// barrage.cpp: the long-range barrages (docs/plan-2026-10-25-strikes.md §1.1, src/barrage_plan.h): the map's bar
// (airstrike.cpp SupportStrikeAt) starts kind `kind` (barrage::Kind) at `at` along `heading` by `caller` (this machine's
// player) with `damage` a round; it is told to every peer (barrage_net_protocol.h), which plays it on its copy of the caller.
// BarrageTick fires its rounds (SharedFrameTick); the rounds' flight is RocketBullet01's update wrapped (InstallBarrages, at
// load). Anything but started: not started, and why (said on the bar's line; its pt are not taken).
enum class BarrageResult : int { started, noTarget, noHook, full, noCaller, noRounds };
BarrageResult BarrageStart(int kind,const unsigned char* caller,const float* at,const float* heading,float damage) noexcept;
void BarrageTick() noexcept;
void ResetBarrages() noexcept;
bool InstallBarrages() noexcept;
// airstrike.cpp: the caller's HUD line for its strike calls (hud.cpp StrikeBanner, the draw thread): a sortie's second wave
// not called; a moment.
bool PlayerStrikeCue(wchar_t* out,std::size_t capacity) noexcept;

// ultimate.cpp: the ultimate strike (docs/plan-2026-10-19-ultimate.md, src/ultimate_plan.h): the map's bar (airstrike.cpp
// SupportStrikeAt) starts its show at `at` by `caller` with `damage`; UltimateTick plays it a frame at a time
// (SharedFrameTick); ResetUltimate at a new mission forgets it, nothing touched (its objects were the last mission's).
// False: no show (its rounds not preloaded, every show slot taken; its pt are not taken).
// Several play at once; online every machine plays each machine's player's shows (ultimate_net_protocol.h).
bool UltimateStart(const unsigned char* caller,const float* at,float damage,std::uint64_t seed) noexcept;
void UltimateTick() noexcept;
void ResetUltimate() noexcept;
void PreloadUltimate(void* mgr) noexcept;   // the magic circles, with the shells (jet_spawn.cpp)

// airstrike.cpp
bool InstallAirstrikes() noexcept;
void SupportDispatchTick() noexcept;
void ResetSupportDispatch() noexcept;
}  // namespace crew
