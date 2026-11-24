// Frames above the game's 59.94 Hz, stage 3: what a frame without a step draws (frame_rate_view.cpp,
// docs/unlimited-fps-re.md §4.3). Developer only, with the frame gate (frame_rate.cpp).
#pragma once
#include <cstdint>

namespace crew {
namespace framerateview {
// At load, after the gate: the camera step's snapshot (0x11995A6) and the projection's interpolation (0x1197B50, shared
// with the lens view, scope_lens.h SetBeforeProject). False (logged by the caller): nothing interpolated.
bool Install() noexcept;
// Game thread, each frame right before System::Update (frame_rate.cpp SystemUpdateHook): the cameras the last frame drew
// interpolated get their step's own state back (the step and everything after it read the simulation's camera), and
// this frame's phase is set: `alpha` 1 on a frame that steps (its render comes before its steps), else the part of a
// tick since the last boundary.
void BeginFrame(std::uint32_t ticks,float alpha) noexcept;
// Game thread, the FRAMERATE statistics since the last call: viewport projections on frames without ticks drawn blended
// / left as stepped, and frames without ticks whose player root (fps-7) was moved to the blend / left as stepped.
void TakeCounts(unsigned& blended,unsigned& unblended,unsigned& roots,unsigned& rootsLeft) noexcept;
}  // namespace framerateview

// fps-8, game thread (the camera step, a plugin camera feature switching): `camera`'s view jumps at this step (first
// person on or off, the map's top view, an enemy or a turret taken or given back, a scope in or out), so the frames
// until the next step draw this step as it is instead of a blend across the jump. nullptr: every camera. Free when the
// interpolation is off.
void FrameRateCameraCut(const void* camera) noexcept;
}  // namespace crew
