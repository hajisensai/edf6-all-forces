// The EDF4.1 Hector walk probe of the test-only autopilot (tests/autopilot/hector_probe.cpp): every Hector's position,
// facing, walk vector, velocity and main CAS state, sampled each frame from its own update, for comparing its walk with
// EDF4.1's (docs/feedback-2026-10-18-edf41-hector-walk.md).
#pragma once
#include <Windows.h>

namespace autopilot {
using LogFn=void(*)(const char* format,...) noexcept;

// Swaps slot 5 (the frame) of EDF6_Berserker_Large's vtable for a sampling wrapper; false when the image is not the
// build it was read from (nothing patched then).
bool InstallHectorProbe(unsigned char* image,LogFn log) noexcept;
// Once per autopilot loop (its own thread): writes the rows the frames collected.
void HectorProbeTick() noexcept;
}  // namespace autopilot
