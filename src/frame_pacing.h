// The game's window-thread spin and frame-step Sleep(1) made real waits (frame_pacing.cpp).
#pragma once

namespace crew {
bool InstallFramePacing() noexcept;
// The game thread's spin on the render fence 0x111CC20 made a wait on its event.
bool InstallRenderFence() noexcept;
// ini MaxFrameLatency set on the DXGI device once the game has made it (before the application's init).
bool InstallFrameLatency() noexcept;
}  // namespace crew
