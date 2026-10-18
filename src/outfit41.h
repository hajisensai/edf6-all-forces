// The EDF4.1 outfit (docs/edf41-outfits.md; outfit41.cpp): a fifth outfit for each class, picked in the HQ's outfit
// popup, stored by the plugin (EDF6VehicleCrew.outfit41.ini), worn only by this machine's first player.
#pragma once
#include "core.h"
#include <cstdint>

namespace crew {
// At load: checks the code it rests on (src/outfit41_signatures.inc), then redirects the model path calls, the popup's
// show call and chains the five stock items. False, logged REFUSED, when the code differs: nothing patched.
bool InstallOutfit41() noexcept;
// mission.cpp's online session preload hook hands the call here: the member it preloads is marked this machine's or
// another's for the model path call inside (0x59DD27), then the call is made.
std::uintptr_t Outfit41SessionPreload(PlayerPreloadFn call,std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d);
}  // namespace crew
