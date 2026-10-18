// The Wing Diver energy probe of the test-only autopilot (tests/autopilot/wdenergy_probe.cpp): her flight energy and
// what each of her weapons' charge takes from it, read at run time (docs/edf5-weapons-plan.md, EnergyChargeRequire).
#pragma once
#include <Windows.h>

namespace autopilot {
using LogFn=void(*)(const char* format,...) noexcept;

// The probe's two slots of the Wing Diver's vtable: false when the image is not the build they were read from,
// nothing patched then.
bool InstallWdEnergyProbe(unsigned char* image,LogFn log) noexcept;
// Once per autopilot loop (every 15 ms, its own thread): her energy and her weapons' charge state, every 250 ms.
void WdEnergyProbeTick() noexcept;
}  // namespace autopilot
