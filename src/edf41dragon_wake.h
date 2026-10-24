// EDF6 DragonSmall's knock-down state (EDF.dll 0x339550) and when it lets the dragon get up: pure logic, no game
// (tests/edf41dragon_wake_test.cpp). src/edf41dragon.cpp logs it for EDF4.1's giant dragon (DragonBig401), whose
// 2026-10-17 M166 run lay in this state for 60 s at a time ("大龙好像没ai").
//
// What the class does (read from EDF.dll, TimeDateStamp 0x678CCB46):
//  - a hit enters the state (the hit handler 0x334F00, site 0x334FB6) when its knock value (+0x58 of the hit) is over
//    1e-6, or whatever the hit when the dragon is already knocked down (0x333AF0) or its ragdoll is on (+0x1075): every
//    hit while down enters it again, restarting its clock;
//  - on entry it plays fly_damage_large (CAS index at +0x1540) and puts koshi / mune on the ragdoll (0x4A8570), then
//    plays down (+0x1548) once that clip ends (0x3396DE);
//  - each frame it sums, over the ragdoll's bodies, mass x (|linear velocity| + |angular velocity|) (0x6EB150 on
//    +0xB10), and goes to the wake-up state (0x33AA30, plays wakeup) when the state's clock ([[+0x1CD8]], seconds) is
//    over 1.5 and either that sum is under 2000 or the clock is over 60 (constants at EDF+0x1769100 / 0x1768DCC /
//    0x1765A58).
// EDF4.1's own DragonBig does the same in its knock-down state (EDF41.exe 0x1EF400: clock [+0x10B8] over 1.5 and the
// same sum, 0x336B50, under 2000 at 0xAB66B0 / 0xAB6A38) without the 60 s cap.
#pragma once

namespace crew::edf41dragon {

constexpr float kWakeAfter=1.5f;     // s in the state before it may get up
constexpr float kSettled=2000.0f;    // the ragdoll's sum of mass x speed under which it has come to rest
constexpr float kWakeCap=60.0f;      // s: EDF6 gets it up whatever the ragdoll does (4.1 has no cap)

enum class Wake { TooSoon, Unsettled, Settled, Capped };

// Why the class keeps the dragon down this frame (TooSoon / Unsettled), or why it gets up (Settled / Capped).
constexpr Wake WakeReason(float clock,float settle) noexcept {
    if(!(clock>kWakeAfter))return Wake::TooSoon;
    if(kSettled>settle)return Wake::Settled;
    return clock>kWakeCap ? Wake::Capped : Wake::Unsettled;
}

// Whether 4.1's DragonBig would get up in the same frame (no cap).
constexpr bool Wakes41(float clock,float settle) noexcept { return clock>kWakeAfter && kSettled>settle; }

constexpr const char* WakeName(Wake w) noexcept {
    switch(w) {
    case Wake::TooSoon: return "too soon";
    case Wake::Unsettled: return "ragdoll not at rest";
    case Wake::Settled: return "at rest: gets up";
    case Wake::Capped: return "60 s cap: gets up";
    }
    return "?";
}

}  // namespace crew::edf41dragon
