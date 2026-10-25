// The Umbra input probe of the test-only autopilot (tests/autopilot/umbra_probe.cpp): every matrix EDF.dll hands
// umbra_sandlot.dll (an object's placement, a test model's box, a camera's placement) checked for values that are not
// finite or are out of any map's reach, with the call path that produced it (the grey map, 2026-10-25).
#pragma once
#include <Windows.h>

namespace autopilot {
using LogFn=void(*)(const char* format,...) noexcept;

// Swaps EDF.dll's import entries of the Umbra calls for checking wrappers; false when the image is not the build it
// was read from (nothing patched then).
bool InstallUmbraProbe(unsigned char* image,LogFn log) noexcept;
// Once per autopilot loop (its own thread): writes the rows the calls collected and a count every 10 s.
void UmbraProbeTick() noexcept;
}  // namespace autopilot
