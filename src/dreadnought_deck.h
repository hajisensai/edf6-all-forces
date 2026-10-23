// The rule for a pilot getting out of a Dreadnought fighter (src/dreadnought.cpp DreadnoughtTakesBack, src/playerjet.cpp
// Leave, src/playerjet_board.inc Left): no parachute and no catch only when they are certain to stand on the ship as they
// get out, i.e. its fighter is held on a foredeck spot by the hangar that runs on this machine (parked there, or a
// player's brought home and down on it), on the spot and moving with the ship. Near or over the hull is not enough: the
// hull's box takes in air off its bow, its beams and over its deck, and a fighter flying through it at its speed would
// drop its pilot off the ship with no parachute (review 2026-10-24). Any other case keeps the plugin's ejection and
// catch. Header-only and pure, so tests/dreadnought_deck_test.cpp checks it offline.
#pragma once

namespace dreadnought {

constexpr float kOnSpot=3.0f;        // m from its foredeck spot (the hangar touches down within 1.5 m, its pin holds it there)
constexpr float kOnSpotSpeed=4.0f;   // m/s over the ship's (the touch-down's own limit, kFinalSpeed / 3)

// `authority`: the hangar holding it runs on this machine; `held`: it is that hangar's foredeck fighter (parked, or a
// player's being brought down onto the spot); `dist`: metres from the spot; `relSpeed`: m/s over the ship's. A NaN or
// a negative (not measured) is no.
constexpr bool StandsAboard(bool authority,bool held,float dist,float relSpeed) noexcept {
    return authority && held && dist>=0.0f && dist<=kOnSpot && relSpeed>=0.0f && relSpeed<=kOnSpotSpeed;
}

}  // namespace dreadnought
