// The frame's render recording (System::Update's call 0x1198FA4 -> 0x11978D0, rcx the system, edx System+0xCE9, r8b 1):
// one owner of the call site, the features that act right before the render recording chained in a fixed order
// (render_record.cpp, docs/unlimited-fps-re.md §4.3.2). edf::RedirectCall takes a call site only while it still calls
// the stock function, so two modules redirecting it would leave the second out; they register here instead.
#pragma once

namespace crew {
namespace renderrecord {
// Game thread, right before the render recording of every frame (frames with ticks too); runs under nothing: each
// callback keeps its own __try.
using Before=void(*)() noexcept;
// The order they run in (see render_record.cpp): the unlimited frame rate's poses first (frame_rate_view.cpp, on frames
// without ticks), then the camera clip planes, then the first person's Umbra boxes around the eye.
enum class Slot { kFrameRate,kViewDistance,kFirstPerson,kCount };
// At load, by each feature that registers: the call site checked and redirected once (later calls: whether it is in).
// False (logged here once): the site is not as read or could not be redirected; nothing registers.
bool Install() noexcept;
// `before` runs in `slot`'s turn from now on (nullptr: no longer). Call after Install returned true.
void Set(Slot slot,Before before) noexcept;
}  // namespace renderrecord
}  // namespace crew
