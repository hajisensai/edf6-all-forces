// A Rune Blade NPC's rune steps (src/npcai.cpp RuneHop; the steps themselves: runeblade_ether_logic.h StepFrame, run by
// runeblade.cpp's frame for every Rune Blade of this machine, NPCs too). Pure, no game: tests/runeblade_npc_test.cpp.
//  - A long walk of the plugin's (MoveTo, the way left past kHopFar) with the way ahead free: a takeoff from the ground
//    (runeblade.h RuneBladeNpcTakeoff: the game's own jump request, free as the player's ground jump; the NPC's body is an
//    AssultSoldier, whose own jump press is the roll, so the press would not lift it), then up to kNpcSteps steps in the air
//    along the walk (the jump press +0xD76 with the stick on the way: StepFrame spends kStepCost each), kNpcStepGapMs apart.
//  - The gauge kept: a takeoff only with kTakeoffEther left, a step only while one more stays (kStepCost after it), never
//    once depleted; after landing it rests kHopRestMs before the next.
#pragma once
#include "runeblade_ether_logic.h"
#include <cmath>
#include <cstdint>

namespace crew::runeblade::npchop {

constexpr float kHopFar=25.0f;            // m of the walk left: shorter walks are walked
constexpr float kStepFar=12.0f;           // m left for another step in the air
constexpr int kNpcSteps=2;                // steps a hop at most (the player has five from full)
constexpr std::uint64_t kNpcStepGapMs=350;   // past kStepGapFrames (8 frames) with room: the hop's crest first
constexpr std::uint64_t kHopRestMs=2500;  // from landing to the next takeoff
constexpr std::uint64_t kLiftMs=800;      // a takeoff that did not leave the ground by then is over
constexpr float kTakeoffEther=kStepCost*3.0f;   // the takeoff's steps and one more left after them
constexpr float kTakeoffUp=10.0f;         // m/s up: the stock ground jump's (0x5669A8 walk+0x30 = (0, 10, 0))

enum class Act : std::uint8_t { none,takeoff,step };
enum class Phase : std::uint8_t { idle,lifting,air };
struct Hop {
    Phase phase=Phase::idle;
    std::uint64_t at=0;       // the takeoff's (lifting / air), the landing's (idle)
    std::uint64_t stepAt=0;   // the last step's
    int steps=0;
};
struct In {
    bool walking=false;   // the plugin's own walk this frame
    bool free=false;      // nothing holds the move (npcai DashFree: no grab, no reload lock, not firing a held trigger)
    float left=0.0f;      // m of the walk left (goal distance less the stop)
    bool ground=false;    // +0x711 == 2
    int airFrames=0;      // +0x1264
    float ether=0.0f;     // the gauge (runeblade_ether.h)
    bool depleted=true;
    bool pressed=false;   // the jump press already set this frame (another of the plugin's)
};

inline const char* ActName(Act a) noexcept { return a==Act::takeoff ? "takeoff" : a==Act::step ? "step" : "none"; }

// One think of the NPC: what to do now, `h` updated.
inline Act Decide(Hop& h,const In& in,std::uint64_t now) noexcept {
    switch(h.phase) {
    case Phase::lifting:
        if(!in.ground && in.airFrames>0){h.phase=Phase::air;break;}
        if(now-h.at>=kLiftMs){h.phase=Phase::idle;h.at=now;}
        return Act::none;
    case Phase::air:
        if(in.ground){h.phase=Phase::idle;h.at=now;return Act::none;}
        break;
    case Phase::idle: break;
    }
    if(in.pressed || !in.walking || !in.free || in.depleted || !std::isfinite(in.left) || !std::isfinite(in.ether))return Act::none;
    if(h.phase==Phase::idle) {
        if(!in.ground || in.left<=kHopFar || now-h.at<kHopRestMs || in.ether<kTakeoffEther)return Act::none;
        h.phase=Phase::lifting;h.at=now;h.stepAt=now;h.steps=0;
        return Act::takeoff;
    }
    // in the air from its own takeoff
    if(h.steps>=kNpcSteps || in.airFrames<=kAirborneFrames || in.left<=kStepFar || now-h.stepAt<kNpcStepGapMs ||
       in.ether<kStepCost*2.0f)
        return Act::none;
    ++h.steps;h.stepAt=now;
    return Act::step;
}

}  // namespace crew::runeblade::npchop
