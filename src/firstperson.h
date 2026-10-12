// First person on foot (firstperson.cpp; the user, 2026-10-12: "a proper first-person game's view", the stock body not
// shown, arms and weapon as a viewmodel, the stock moves kept, online in sync). RE: docs/fp-render-re.md,
// docs/fp-soldier-re.md. Everything here is this machine's picture: the body, its animation, the shots and the aim the
// network carries stay the game's.
#pragma once

namespace crew {
bool InstallFirstPerson() noexcept;   // at load, before InstallMap (the map's camera step chains outside this one)
bool FirstPersonOn() noexcept;        // game thread: the local player's camera is in first person this frame
}  // namespace crew
