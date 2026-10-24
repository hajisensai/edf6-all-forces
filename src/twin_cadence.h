// Twin barrels that fire in turn (the BMPT's two 2A42s: the user, 2026-10-21: 「bmpt应该是双炮管轮流开火的」;
// docs/feedback-2026-10-21-bmpt-fire.md). The V603 pulls both of its gun holders on the one primary trigger (0x6214A4),
// so two loaded barrels with the same FireInterval fire in the same frame, every interval: a pair of rounds at once.
// The real Terminator's two guns are worked one after the other. Here each barrel keeps its own native weapon (its own
// rounds, FireInterval, muzzle, flash, casing, sound and network shot), and the cadence is read from the weapons' own
// state: a barrel's pull this frame is held back while its partner's cooldown (+0xE0C, which the shot sets to the
// partner's FireInterval +0x36C, 0x6981E0..0x6981F7, and the weapon update counts down by its step +0xE10 before it
// fires, 0x693A58..0x693A64) will still be more than half that interval after this frame's countdown: the partner fired
// less than half an interval ago (read before the countdown it would be a frame late each time: 7 + 7 frames, not 6 + 6).
// Steady fire is then a round every half interval, left, right, left: each barrel at its own interval, the pair offset by half of it. When both are ready together (the
// first round of a burst) the barrel with more rounds left goes first, the left on a tie, so the pair alternates from the
// first round on and the belts stay even. A partner that cannot fire (no rounds: reloading or spent) is no partner: the
// other barrel fires at its own rate. Pure: payload.cpp TwinBarrelFrame reads the weapons and clears the latches.
#pragma once
#include <cstdint>

namespace crew {
namespace twin {
// FLT_EPSILON: the weapon update's own test of a cooldown done (0x693A95 against 0x1C36980: above it, still cooling).
constexpr float kReady=1.1920929e-7f;
struct Barrel {
    unsigned pair;      // TwinPairOf: barrels sharing a nonzero pair fire in turn (one pair per belt of a twin gun)
    int holder;         // its vehicle holder index (the tie's last resort, the lower first)
    bool latched;       // its trigger latch (weapon +0x139) is pulled this frame
    float cooldown;     // weapon +0xE0C, frames, as the last update left it
    float step;         // weapon +0xE10: what this frame's update takes off it (1 a frame at the stock rate)
    std::int32_t interval;  // weapon +0x36C, FireInterval in frames
    std::int32_t rounds;    // weapon +0xBE8
};
constexpr bool Live(const Barrel& b) noexcept { return b.latched && b.rounds>0; }
// The cooldown this frame's update leaves (0x693A64: minus the step, not below 0) and what it tests (0x693A95).
constexpr float After(const Barrel& b) noexcept { return b.cooldown-b.step>0.0f ? b.cooldown-b.step : 0.0f; }
constexpr bool Ready(const Barrel& b) noexcept { return !(After(b)>kReady); }
// Fired less than half its own interval ago: this frame's cooldown above half the interval its shot set.
constexpr bool Recent(const Barrel& b) noexcept { return b.interval>0 && After(b)*2.0f>static_cast<float>(b.interval); }
// Whether `a` goes before `b` when both would fire this frame: more rounds left, then the lower holder.
constexpr bool Before(const Barrel& a,const Barrel& b) noexcept {
    return a.rounds!=b.rounds ? a.rounds>b.rounds : a.holder<b.holder;
}
// Bit i set: barrel i's pull is held back this frame (n <= 32). Barrels not latched are never touched.
inline std::uint32_t Withheld(const Barrel* b,int n) noexcept {
    std::uint32_t out=0;
    if(!b || n<=0 || n>32)return 0;
    for(int i=0;i<n;++i) {
        if(!Live(b[i]) || !b[i].pair)continue;
        for(int j=0;j<n;++j)
            if(j!=i && b[j].pair==b[i].pair && Live(b[j]) && Recent(b[j])){out|=1u<<i;break;}
    }
    // Two ready together: only the first of them fires (the other is then the partner that just fired).
    for(int i=0;i<n;++i) {
        if((out>>i)&1u || !Live(b[i]) || !b[i].pair || !Ready(b[i]))continue;
        for(int j=0;j<n;++j)
            if(j!=i && !((out>>j)&1u) && b[j].pair==b[i].pair && Live(b[j]) && Ready(b[j]) && Before(b[j],b[i])){out|=1u<<i;break;}
    }
    return out;
}
}  // namespace twin
}  // namespace crew
