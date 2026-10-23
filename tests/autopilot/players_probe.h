// The local players probe of the test-only autopilot (tests/autopilot/players_probe.cpp): which pad each local soldier is
// bound to and which class key record it reads, at run time (src/enemy_control_pad.h).
#pragma once
#include <Windows.h>

namespace autopilot {
using LogFn=void(*)(const char* format,...) noexcept;

// Slot 5 (the frame) of the four soldier classes' vtables: false when the image is not the build they were read from,
// nothing patched then.
bool InstallPlayersProbe(unsigned char* image,LogFn log) noexcept;
// Once per autopilot loop: every second, each class's last framed soldier with a controller.
void PlayersProbeTick() noexcept;
}  // namespace autopilot
