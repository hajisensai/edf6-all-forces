// The game's tick clock (0x11DAE60, docs/frame-pacing-re.md) read the way the game reads it, to know how long the frame
// step has to wait for the next tick (frame_pacing.cpp TickWait). Pure arithmetic, checked offline by
// tools/frame_pacing_check.cpp.
//
// 0x11DAE60: elapsed = QPC - base (base: 0x2139D10, a QPC reading taken at start-up); period = freq*100/5994 (a tick is
// 1/59.94 s); n = (elapsed - last)/period, unsigned 64-bit; when n >= 1, last += n*period (last: 0x2139D18, the QPC
// offset from base of the tick boundary the game last stepped to). The next boundary is therefore base + last + period.
#pragma once
#include <cstdint>
#include <intrin.h>

namespace crew::pacing {
// `imul rcx,rax,0x64` then the division by 5994 the compiler made a multiply by 0xAEF006D5604455C3 >> 76.
constexpr std::uint64_t TickPeriod(std::uint64_t freq) noexcept {return freq*100u/5994u;}

// The whole ticks 0x11DAE60 would return at `now` (unsigned 64-bit like the game: QPC wrapping around stays exact).
constexpr std::uint64_t TicksPassed(std::uint64_t now,std::uint64_t base,std::uint64_t last,std::uint64_t period) noexcept {
    return period ? (now-base-last)/period : 0;
}

// QPC counts from `now` to the next tick boundary; 0 when the boundary is now or passed (0x11DAE60 returns >= 1 then).
constexpr std::uint64_t CountsToBoundary(std::uint64_t now,std::uint64_t base,std::uint64_t last,std::uint64_t period) noexcept {
    const std::uint64_t since=now-base-last;
    return since>=period ? 0 : period-since;
}

// QPC counts as a waitable timer's relative due time, in 100 ns, rounded up so the timer never ends before the boundary.
// The product is 128-bit: no QPC frequency overflows it.
inline std::uint64_t CountsTo100ns(std::uint64_t counts,std::uint64_t freq) noexcept {
    if(!counts || !freq)return 0;
    std::uint64_t high=0;
    const std::uint64_t low=_umul128(counts,10'000'000u,&high);
    if(high>=freq)return UINT64_MAX;   // the quotient would not fit (counts far past a second at a tiny frequency)
    std::uint64_t rem=0;
    const std::uint64_t q=_udiv128(high,low,freq,&rem);
    return q+(rem?1u:0u);
}
}  // namespace crew::pacing
