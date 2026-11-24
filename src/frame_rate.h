// Frames above the game's 59.94 Hz, stage 2 (frame_rate.cpp, docs/unlimited-fps-re.md): developer only (ini FrameRateDev).
#pragma once

namespace crew {
// Before the application's init, after InstallFramePacing (the gate waits through its TickWait).
void InstallFrameRate() noexcept;
}  // namespace crew
