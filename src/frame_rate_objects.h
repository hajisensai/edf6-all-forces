// Frames above the game's 59.94 Hz, stage 4 (PR-e): every other object's root between two ticks
// (frame_rate_objects.cpp, docs/unlimited-fps-re.md §4.3.3). Driven by frame_rate_view.cpp only: installed with it
// (the frame gate on), sampled from its camera step hook, posed after its player root, put back with its poses.
#pragma once
#include "frame_rate_logic.h"
#include <cstdint>

namespace crew {
namespace frameobjects {
// framerateview::Install, once it is in: the tables' first sizes. Everything below is free before it.
void Install() noexcept;
// Game thread, the camera step's hook (after every object's phase of the tick): once a tick (System's tick count), the
// root of every object of System's update list that has a model.
void Sample() noexcept;
// Game thread, a frame without ticks, right after the player's root (fps-7) and before System::Update (the render
// thread idle): every sampled object's draw poses times its D = cur^-1 * blend(prev, cur, alpha); riders of a vehicle
// (other crew, the auto turret's gunners: anyone in its seats) with the vehicle's D. `rootHuman` / `rootVehicle`: fps-7's
// (left to it); `rootD`: its D (16 floats) when it posed them, else null (their riders are left as stepped).
void Pose(float alpha,const void* rootHuman,const void* rootVehicle,const float* rootD) noexcept;
// Game thread, with fps-7's poses (framerateview RestoreFrame: after this frame's end fence): what Pose wrote goes back
// where the object is still there and the pose still holds what was written.
void Restore() noexcept;
// fps-7's root refused a blend (InterpolateRigid or the inverse): its reason, for the FRAMERATE summary.
void NoteRootRefusal(framerate::Refusal why) noexcept;

struct Counts {
    unsigned posed=0,riders=0,still=0,left=0,mostPoses=0,objects=0;
    unsigned rootRefused[static_cast<int>(framerate::Refusal::kCount)]{};
    unsigned refused[static_cast<int>(framerate::Refusal::kCount)]{};
};
// Game thread, since the last call: objects posed on frames without ticks (riders among them), still (nothing to
// move), left as stepped; the most poses one frame wrote; the objects sampled at the last tick; refusals by reason.
void TakeCounts(Counts& out) noexcept;
}  // namespace frameobjects
}  // namespace crew
