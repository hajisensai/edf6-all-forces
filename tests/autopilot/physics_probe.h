// The physics determinism probe of the test-only autopilot (tests/autopilot/physics_probe.cpp): every live non-static
// hknp body's position and linear velocity, per scene tick from the mission's first tick, for comparing runs with
// EDF6VehicleCrew's HavokMultithread on and off (net-10, docs/havok-mt-re.md §13 item 5).
#pragma once
#include <Windows.h>

namespace autopilot {
using LogFn=void(*)(const char* format,...) noexcept;

// Chains Application::OnWorldStepBegin's vtable slot (the scene tick, on the game thread) to a sampling wrapper; false
// when the image is not the build it was read from (nothing patched then). `out` is the file the rows are appended to
// (the autopilot's own log). `ticks`: how many scene ticks from the mission's first are sampled.
bool InstallPhysicsProbe(unsigned char* image,LogFn log,const wchar_t* out,unsigned ticks) noexcept;
// Once per autopilot loop (its own thread): appends the rows the ticks collected, in one write.
void PhysicsProbeTick() noexcept;
}  // namespace autopilot
