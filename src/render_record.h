// The frame's render recording (System::Update's call 0x1198FA4 -> 0x11978D0, rcx the system, edx System+0xCE9, r8b 1):
// one owner of the call site, the features that act right before the render recording and at its projections chained in
// a fixed order
// (render_record.cpp, docs/unlimited-fps-re.md §4.3.2). edf::RedirectCall takes a call site only while it still calls
// the stock function, so two modules redirecting it would leave the second out; they register here instead.
#pragma once

namespace crew {
namespace renderrecord {
// On the recording owner, before the original routine. Each callback protects its native reads.
using Before=void(*)() noexcept;
// The order they run in (see render_record.cpp): the unlimited frame rate first (frame_rate_view.cpp: at the first
// projection the samples and, on frames without ticks, the poses; each projection's camera blend), then the first person
// (firstperson.cpp: its session before the recording, its Umbra boxes around the eye this frame draws at the first
// projection).
enum class Slot { kFrameRate,kViewDistance,kFirstPerson,kPoseSnapshot,kCount };
// At load, by each feature that registers: the call site checked and redirected once (later calls: whether it is in).
// False (logged here once): the site is not as read or could not be redirected; nothing registers.
bool Install() noexcept;
// Route the entire stock entry and its existing mission callbacks to the recording thread.
bool InstallThread() noexcept;
// `before` runs in `slot`'s turn from now on (nullptr: no longer). Call after Install returned true.
void Set(Slot slot,Before before) noexcept;
// On the recording owner, after the native routine, to close its queued work.
void SetAfter(Slot slot,Before after) noexcept;
// On the projection caller, at every projection 0x1197B50 (scope_lens.cpp's hook, shared with the lens view: this frame's viewports
// and the projections of EDF.dll's four other callers of 0x11978D0), in Slot order, before the native projection.
// `first`: the first projection inside the recording this module hooked, once a recording: after the recording's loop
// 0x1197950 made the skeletons' poses and re-placed the models' Umbra boxes (model+0x110), before any draw. A recording
// that never projects leaves no first for a later projection (it is closed when it returns).
using Project=void(*)(void* camera,const int* rect,bool first) noexcept;
// `project` runs in `slot`'s turn at each projection from now on (nullptr: no longer). Call after Install returned true.
void SetProject(Slot slot,Project project) noexcept;
}  // namespace renderrecord
}  // namespace crew
