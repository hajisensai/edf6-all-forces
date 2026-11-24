// Frames above the game's 59.94 Hz, stage 4 (PR-e): every other object's root between two ticks
// (frame_rate_objects.cpp, docs/unlimited-fps-re.md §4.3.3). Driven by frame_rate_view.cpp only: installed with it
// (the frame gate on), sampled from its camera step hook, posed at its render recording hook after its player root, put
// back with its poses; and forgotten when the mission's scene is released (mission.cpp).
#pragma once
#include "frame_rate_logic.h"
#include <cstdint>

namespace crew {
namespace frameobjects {
// framerateview::Install, once it is in: the tables' first sizes (no memory: nothing here turns on). Everything below is
// free before it.
void Install() noexcept;
// Game thread, the camera step's hook (after every object's phase of the tick): once a tick (System's tick count), the
// root of every object of System's update list that has a model. Only while frames without ticks come: kIdleTicks ticks
// after the last one (or before the first, or with the frame gate not in: no frame without ticks ever calls Pose) the
// sample is skipped (stress:30, ticks every frame: 0.6-1.3 ms a step for nothing) and counted (Counts::idleTicks).
constexpr unsigned kIdleTicks=60;   // 1 s of ticks without a frame without ticks
void Sample() noexcept;
// Game thread, a frame without ticks, at the render recording call 0x1198FA4 right after the player's root (fps-7):
// every sampled object's draw poses times its D = cur^-1 * blend(prev, cur, alpha); riders of a vehicle (other crew, the
// auto turret's gunners: anyone in its seats) with the vehicle's D. `rootHuman` / `rootVehicle`: fps-7's (left to it);
// `rootD`: its D (16 floats) when it posed them, else null (their riders are left as stepped). After an idle sample
// (above) this frame poses nothing: every object known is counted left for its samples (Left::kSamples), and the sample
// resumes at the next tick (two ticks to blend again).
void Pose(float alpha,const void* rootHuman,const void* rootVehicle,const float* rootD) noexcept;
// Game thread, with fps-7's poses (framerateview RestoreFrame: after this frame's end fence): what Pose wrote goes back
// where the object is still there, bone by bone where the bone still holds what was written.
void Restore() noexcept;
// Game thread, the mission's scene released (mission.cpp ReleaseGroupHook): what is written goes back, every object's
// weak count is given back and its samples forgotten.
void SceneOver() noexcept;
// fps-7's root refused a blend (InterpolateRigid or the inverse): its reason, for the FRAMERATE summary.
void NoteRootRefusal(framerate::Refusal why) noexcept;

// Why an object (or a rider, with its vehicle's reason) was drawn as stepped on a frame without ticks: gone (its object
// freed or being deleted), vtable (another class at the address), pose (its root written after the sample: see
// frame_rate_objects.cpp, "Why at the camera step"), samples (one tick seen so far), unsampled (a tick ran without the
// camera step's sample), refused (the blend: the refused[] counts say why), full (no room in the frame's tables or the
// sample's), fault (a read faulted).
enum class Left { kGone,kVtable,kPose,kSamples,kUnsampled,kRefused,kFull,kFault,kCount };
constexpr int kLeftReasons=static_cast<int>(Left::kCount);

struct Counts {
    unsigned posed=0,riders=0,still=0,mostPoses=0,objects=0,idleTicks=0;
    unsigned left[kLeftReasons]{};
    unsigned rootRefused[static_cast<int>(framerate::Refusal::kCount)]{};
    unsigned refused[static_cast<int>(framerate::Refusal::kCount)]{};
    unsigned AsStepped() const noexcept {
        unsigned n=0;
        for(unsigned v:left)n+=v;
        return n;
    }
};
// Game thread, since the last call: objects posed on frames without ticks (riders among them), still (nothing to
// move), left as stepped by reason; the most poses one frame wrote; the objects sampled at the last tick; refusals by
// reason; the ticks whose sample was skipped (idle, see Sample).
void TakeCounts(Counts& out) noexcept;
}  // namespace frameobjects
}  // namespace crew
