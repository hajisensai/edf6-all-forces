// A pad chord the plugin owns, kept out of the game's own pad read (native_pad.cpp; offline: thermal_runtime_test).
//
// The game reads its pad once per input pass (XInputGetState at 0x11883D1) and acts on each button's own press; a
// plugin chord read from the same pad (on foot: ini InfantryNightVisionButton, L3 + R3) is therefore also the stock
// L3 / R3 press (2026-11-26 实机: the chord switched night vision and the Ranger dashed as well). While the chord is
// owned, the read that finds all of its buttons held hides them from the game, and each stays hidden until that button
// is released: letting go of one before the other hands the game neither a fresh press of the one still held nor a
// held button it never saw pressed. Nothing waits on time: every read decides from the buttons it holds.
#pragma once
#include <cstdint>

namespace padchord {
struct Swallow {
    std::uint16_t kept=0;   // the chord's buttons being kept from the game, until each is released
    // `raw`: the pad's buttons this read; `chord`: the owned chord now (0: none owned). The game's buttons back.
    std::uint16_t Filter(std::uint16_t raw,std::uint16_t chord) noexcept {
        if(chord && (raw&chord)==chord)kept=static_cast<std::uint16_t>(kept|chord);
        kept=static_cast<std::uint16_t>(kept&raw);
        return static_cast<std::uint16_t>(raw&~kept);
    }
};
}  // namespace padchord
