// The swap chain in the flip model and the Present's statistics (swap_flip.cpp), with the frame rate's gate (frame_rate.cpp).
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
// The Present as the display got it, over the last statistics window (about 1 s, render thread; read by the game thread's
// log line): calls of Present and the failed / occluded ones, and from IDXGISwapChain::GetFrameStatistics the frames
// shown and the display's refreshes per second (statsHr: its last result; a windowed blt chain has no statistics).
struct PresentStats {
    double calls=0,shown=0,refreshes=0;   // per second
    unsigned failed=0,occluded=0;         // since the start
    long statsHr=0;
    bool valid=false;                     // shown / refreshes measured over the window
};
PresentStats SwapFlipPresentStats() noexcept;
}  // namespace crew
