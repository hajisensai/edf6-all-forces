// Frames above the game's 59.94 Hz (frame_rate.cpp, docs/unlimited-fps-re.md): ini FrameRateCap, 0 = the game's rate.
#pragma once

namespace crew {
// Before the application's init, after InstallFramePacing (the gate waits through its TickWait).
void InstallFrameRate() noexcept;
}  // namespace crew
