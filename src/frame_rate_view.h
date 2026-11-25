// Frames above the game's 59.94 Hz, stage 3: what a frame without a step draws (frame_rate_view.cpp,
// docs/unlimited-fps-re.md §4.3). With the frame gate (frame_rate.cpp).
#pragma once
#include <cstdint>

namespace crew {
namespace framerateview {
// At load, after the gate: the camera step's snapshot (0x11995A6) and the projection's interpolation (0x1197B50, the
// frame rate's turn there: render_record.h SetProject). False (logged by the caller): nothing interpolated.
bool Install() noexcept;
// Game thread, each frame right before System::Update (frame_rate.cpp SystemUpdateHook): anything the last frame's end
// fence did not put back is (the blends are undone at the frame end's fence 0x11A305A, once the render thread has drawn
// the frame, so the next frame's Application::Update and step read the simulation), and this frame's phase is set: `alpha` 1 on a frame that steps (its render comes before its steps), else the part of a
// tick since the last boundary. Nothing is posed here: a frame without ticks poses inside System::Update's render
// recording (its call 0x1198FA4, after the frame's object deletion 0x1195F20) at its first projection (0x1197B50, after
// the recording copied the skeletons' poses), and only there.
void BeginFrame(std::uint32_t ticks,float alpha) noexcept;
// Game thread, the FRAMERATE statistics since the last call: viewport projections on frames without ticks drawn blended
// / left as stepped, and frames without ticks whose player root (fps-7) was moved to the blend / left as stepped because
// it changed since its tick / refused (a jump past kCutMove / kCutTurn, not rigid, no inverse).
void TakeCounts(unsigned& blended,unsigned& unblended,unsigned& roots,unsigned& rootsLeft,unsigned& rootsRefused) noexcept;
}  // namespace framerateview

// fps-8, game thread (the camera step, a plugin camera feature switching): `camera`'s view jumps at this step (first
// person on or off, the map's top view, an enemy or a turret taken or given back, a scope in or out), so the frames
// until the next step draw this step as it is instead of a blend across the jump. nullptr: every camera. Free when the
// interpolation is off.
void FrameRateCameraCut(const void* camera) noexcept;
// Game thread, at the render recording (render_record.h, before 0x11978D0 projects): the eye (the camera's world
// matrix's position row) `camera` is drawn from this frame, the blend BeforeProject will write on a frame without ticks
// (same checks, nothing written). False: drawn as it is (interpolation off, a frame with ticks, a cut, a camera not
// blended); the caller reads the camera itself.
bool FrameRateDrawnEye(const void* camera,float eye[3]) noexcept;
}  // namespace crew
