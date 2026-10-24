// EDF4.1's ObjectAction on its four-legged fortress, in what EDF6's MovingFortress can do (src/edf41fortress.cpp).
//
// A 4.1 mission script sends ObjectAction(fortress, n) (native 3200; 4.1 message 0x80000009, EDF6 0x1000000D). The two
// classes give the same message different numbers (docs/feedback-2026-10-18-edf41-m16-crash.md):
//   4.1 Alien4LegTank slot 6 (EDF41.exe 0x1CA500, jump table 0x1CA5EC, 7 entries; anything else: handled, nothing):
//     0 / 1 group 0 on / off, 2 / 3 group 1 on / off, 4 / 5 group 2 on / off   (0x1C9B50(this, group, on): every part of
//     that group), 6 destroyed now (0x1C9520: kill once, then the destroy state 0x1CBB00, the same as when its HP runs out).
//   EDF6 MovingFortress slot 9 (EDF.dll 0x4C6240): 0 stand up, 1 spawn rubble at the locator 瓦礫基準点 (0x4C37E0, which
//     derefs the locator lookup unchecked: a 4.1 model has no such locator -> EXCEPTION C0000005 at EDF+0x4C3A30),
//     2 stand at once, 3 / 4 every group on / off, anything else to the base class.
// So every 4.1 action is run here and none reaches the class's own table. Pure, no game access (tools/edf41_fortress_check).
#pragma once

namespace fortress41 {

enum class Kind { Parts, Destroy, Nothing };

struct Action {
    Kind kind=Kind::Nothing;
    unsigned groupMask=0;   // the class's part mask: 1 << parts_setting[2] (EDF.dll 0x4C13F8), what 0x4CA7E0 tests
    bool on=false;
};

constexpr int kActions=7;   // 4.1's table: 0..6

constexpr Action FromEdf41(int action) noexcept {
    if(action>=0 && action<6)return Action{Kind::Parts,1u<<(action/2),action%2==0};
    if(action==6)return Action{Kind::Destroy,0u,false};
    return Action{};
}

constexpr const char* Name(int action) noexcept {
    constexpr const char* kNames[kActions]={"cannons on","cannons off","gatling on","gatling off","bits on","bits off",
                                            "destroyed"};
    return action>=0 && action<kActions ? kNames[action] : "not in 4.1's table";
}

}  // namespace fortress41
