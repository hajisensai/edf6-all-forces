#pragma once
#include <cstdint>

namespace memoepoch {
// A memo key for per-frame answers: a new epoch each frame, and again each kStallMs a frame does not move (paused,
// loading, or the frame clock not found) so a stalled frame never keeps an answer for good. Not the wall clock itself:
// a slow frame (33-75 ms at stress) crossing a short wall-clock step re-judged every target 2-3 times a frame.
constexpr std::uint64_t kStallMs=250;
struct Clock {
    std::uint64_t frame=~0ull,since=0,epoch=0;
    std::uint64_t Epoch(std::uint64_t now,std::uint64_t wallMs) noexcept {
        if(now!=frame || wallMs-since>=kStallMs){frame=now;since=wallMs;++epoch;}
        return epoch;
    }
    // Epochs only grow: no key from before the reset comes back.
    void Reset() noexcept { frame=~0ull;since=0; }
};
}
