// A pad button held long (plan-2026-10-25-keybinds.md §3.3 D: the jets' landing gear on the d-pad down, held, so a tap
// of it is nothing). Pure: tests/keybind_test.cpp runs it.
#pragma once

namespace crew {
constexpr float kGearPadHoldSec=0.4f;   // the gear's pad buttons held this long (s) toggle it
// `down` now, `dt` s since the last frame: true from `need` s held on, for as long as it stays held (one press for a
// toggle's edge); `sec` the time held so far (0 once let go).
inline bool HeldLong(bool down,float dt,float* sec,float need) noexcept {
    *sec=down ? *sec+(dt>0.0f ? dt : 0.0f) : 0.0f;
    return down && *sec>=need;
}
}  // namespace crew
