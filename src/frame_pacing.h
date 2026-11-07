// The game's window-thread spin and frame-step Sleep(1) made real waits (frame_pacing.cpp).
#pragma once

namespace crew {
bool InstallFramePacing() noexcept;
}  // namespace crew
