// The stock air delivery probe of the test-only autopilot (tests/autopilot/airdrop_probe.cpp): what the game's own
// vehicle request hands its transport container, read at run time (docs/airdrop-vehicle-re.md).
#pragma once
#include <Windows.h>

namespace autopilot {
using LogFn=void(*)(const char* format,...) noexcept;
using HoldFn=void(*)(int vk,bool down) noexcept;

// The probe's hooks on the stock delivery chain (call sites inside Transporter508 / Transporter_Container): false
// when the image is not the build they were read from, nothing patched then.
bool InstallAirdropProbe(unsigned char* image,LogFn log,HoldFn hold) noexcept;
// The mission has started (the offline mission's coroutine): the vehicle call keys are tried from a while after.
void AirdropProbeMissionStarted() noexcept;
// Once per autopilot loop (every 15 ms, its own thread): the call key trial and the delivered vehicle's readback.
void AirdropProbeTick() noexcept;
}  // namespace autopilot
