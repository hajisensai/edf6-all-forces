// The game's window-thread spin and frame-step Sleep(1) made real waits (frame_pacing.cpp).
#pragma once
#include <cstdint>

namespace crew {
bool InstallFramePacing() noexcept;
// The game thread's spin on the render fence 0x111CC20 made a wait on its event.
bool InstallRenderFence() noexcept;
// ini MaxFrameLatency set on the DXGI device once the game has made it (before the application's init).
bool InstallFrameLatency() noexcept;
// Whether the frame step's wait is TickWait (frame_rate.cpp's gate waits through it).
bool FramePacingTickWaitInstalled() noexcept;
// With the frame gate on (frame_rate.cpp), TickWait waits the QPC counts this gives (to the next presentation slot)
// instead of to the next tick boundary.
using GateWaitFn=unsigned long long(*)(unsigned long long now) noexcept;
void SetFramePacingGate(GateWaitFn wait) noexcept;
}  // namespace crew
