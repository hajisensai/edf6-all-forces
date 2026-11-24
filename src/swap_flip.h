// The swap chain in the flip model (swap_flip.cpp): developer only, with the frame rate's stage (frame_rate.cpp).
#pragma once

namespace crew {
// At load, before the device's init: the swap chain's creation and the Present's flags redirected. False (logged): the
// game's blt model.
bool InstallSwapFlip() noexcept;
// At load, after InstallSwapFlip and before the device's init: chains are made with a frame latency waitable object
// and a queue of `latency` frames (0: without).
void SwapFlipUseWaitable(unsigned latency) noexcept;
// Game thread, at the start of a frame that runs (the frame gate): wait until the chain's queue has room (at most
// 100 ms). Free without a waitable chain.
void SwapFlipWait() noexcept;
}  // namespace crew
