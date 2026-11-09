// A mission AI vehicle's real crew keeps quiet while it sits in that vehicle (测试站 #41, the maintainer's choice A,
// 2026-11-08: docs/feedback-2026-11-08-mission-crew-quiet.md, docs/mission-crew-quiet-re.md). Pure logic, no game, no
// Windows: tests/mission_crew_support_test.cpp drives it through mission_crew_support.cpp.
//  - The soldier's own talk (its random / event lines, the subtitle naming it, a Talk_Add* script pick) is the stock
//    TalkManager's: 0x5DF050 returns at once when the speaker's GameObject +0x380 bit 18 is clear (0x5DF08C), and the
//    script pickers 0x5D9FF0 / 0x5DBFE0 skip a soldier without it. The stock script native
//    SetCharacterRandomTalk(obj, false) (0x1B9F10) is that very bit, nothing else. Online the talk is an event
//    (Event_TalkManager 0x7575D0 -> 0x5DAD50 -> 0x5DF050) that every machine gates on its own copy's bit: a local
//    presentation state, each machine writes its own copies.
//  - The plugin clears the bit when one of a mission deployment's soldiers is aboard that deployment's vehicle, and
//    sets it again (only if it was the one that cleared it) once the soldier is off it: dismounted, dead, the vehicle
//    destroyed or gone, the deployment rolled back, the plugin turned off. A soldier or object that is gone is
//    forgotten without a write (the mission's teardown owns the scene).
#pragma once
#include <cstdint>

namespace crewquiet {
inline constexpr std::uint32_t kRandomTalk=0x40000;   // GameObject +0x380 bit 18 (SetCharacterRandomTalk)

struct Step {
    std::uint32_t flags;   // the flags to hold now
    bool write;            // whether `flags` differs from what is there (write it)
    bool quieted;          // whether the plugin owns a restore of the bit from now on
};

// One soldier, once a frame. `exists`: its object is still the one registered (readable, same identity), dead or alive.
// `aboard`: alive and riding the deployment's own live vehicle, with the plugin on. `flags`: its +0x380 now.
// `quieted`: the plugin cleared the bit and has not given it back.
constexpr Step Next(bool exists,bool aboard,std::uint32_t flags,bool quieted) noexcept {
    if(!exists)return {flags,false,false};
    if(aboard) {
        // Cleared whoever set it again meanwhile; owned from the first clear until the soldier is off the vehicle.
        if(flags&kRandomTalk)return {flags&~kRandomTalk,true,true};
        return {flags,false,quieted};
    }
    if(quieted)return {flags|kRandomTalk,true,false};
    return {flags,false,false};
}
}  // namespace crewquiet
