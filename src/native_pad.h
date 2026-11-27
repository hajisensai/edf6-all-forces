// The game's own pad read (EDF.dll's one XInputGetState call, 0x11883D1 in the input callback 0x1188270; docs/room-ui-re.md
// §7: the buttons feed the pad table's channels, the sticks the look). A plugin chord on the first local player's pad is
// kept out of it while the plugin owns that chord (pad_chord_logic.h), so the game does not also act on the chord's
// buttons as its own presses. Purely this machine's input: online the same as offline.
#pragma once
#include <cstdint>

namespace crew::nativepad {
// Once at plugin setup: the read site checked, then redirected through the filter (a pass-through while no chord is owned).
bool Install() noexcept;
// Whether the read site is the game's XInputGetState call, as shipped or redirected by Install (keyconfig.cpp polls the
// same function through its slot).
bool ReadSiteKnown() noexcept;
// Game thread, every step it applies (thermal.cpp: on foot, the night vision's chord): the first local player's chord
// owned now; 0 none. A claim not renewed within a step or so (stepfresh), or while the game is paused, owns nothing.
void OwnChord(std::uint16_t chord) noexcept;
}  // namespace crew::nativepad
