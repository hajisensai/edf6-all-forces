// The swap chain in the flip model (swap_flip.cpp): developer only, with the frame rate's stage (frame_rate.cpp).
#pragma once

namespace crew {
// At load, before the device's init: the swap chain's creation and the Present's flags redirected. False (logged): the
// game's blt model.
bool InstallSwapFlip() noexcept;
}  // namespace crew
