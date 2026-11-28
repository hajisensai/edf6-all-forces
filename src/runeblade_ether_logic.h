// The Rune Blade's ether gauge and rune steps (docs/newclass-re/runeblade-plan.md §1.1, §1.3; src/runeblade.cpp), the pure
// part (tests/runeblade_test.cpp). No game, no transport.
//  - The gauge lives in the soldier's own GameObject energy fields (docs/newclass-re/gauge.md §1: every soldier has them,
//    only the Wing Diver's code writes them): max +0x304, level +0x308, depleted +0x2FC. The stock energy supply round
//    (0x5478A0: add to +0x308, min +0x304) refills it too.
//  - A frame (StepFrame): on the ground after kRefillDelay frames it refills; in the air a jump press is a rune step (costs
//    kStepCost, a jump again, a horizontal push along the stick) at most every kStepGapFrames; the jump held while falling
//    glides (the fall held at kGlideSink, kGlideDrain a frame). Too little ether, or depleted: the press does nothing.
//  - Depleted: set as the level reaches 0, cleared once the ground refill brings it back to kRecoverAt.
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace crew::runeblade {

// ---- the soldier's fields ----
constexpr std::size_t kEtherMaxAt=0x304,kEtherLevelAt=0x308,kEtherDepletedAt=0x2FC;
constexpr std::size_t kJumpPress=0xD76,kJumpHold=0xD77,kMoveStick=0xD50,kLookYaw=0x1244,kSupport=0x711;
constexpr std::size_t kCarry=0x6B0,kJumpFlag=0x1290,kJumpSpeed=0x1294,kNetFlags=0x128,kHumanFlags=0x380,kAttach=0x39C,
                      kDeadAt=0x2E8;
constexpr std::uint32_t kRiding=0x80;
constexpr unsigned char kOnGround=2;

// ---- the numbers (first guesses, not tuned in the game yet: runeblade-plan.md §3) ----
constexpr float kEtherMax=100.0f;
constexpr float kRefillPerFrame=1.0f;     // 60 a second: full in under two seconds on the ground
constexpr int kRefillDelay=6;             // frames on the ground before it refills (a step's touch-down is no refill)
constexpr float kRecoverAt=25.0f;         // depleted until the ground refill brings it back here
constexpr float kStepCost=20.0f;          // five steps from full
constexpr float kStepUp=13.0f;            // m/s: a step's jump (the stock ground jump is 10, motion.md §3.2)
constexpr float kStepPush=9.0f;           // m/s level along the stick, at least (a faster carry is kept)
constexpr float kMostAirSpeed=24.0f;      // m/s level at most after a step
constexpr int kStepGapFrames=8;           // short, crisp hops: never two steps closer than this
constexpr int kAirborneFrames=3;          // frames off the ground before a press is a step (the ground jump's own frames)
constexpr float kGlideSink=4.0f;          // m/s down at most while gliding
constexpr float kGlideDrain=0.25f;        // a frame: 15 a second
constexpr float kStickDead=0.3f;          // a stick under this pushes no way

struct Ether { float level=kEtherMax,max=kEtherMax; bool depleted=false; };

// Spend `amount` if there is that much and not depleted; reaching 0 depletes.
inline bool Spend(Ether& e,float amount) noexcept {
    if(!(amount>=0.0f) || e.depleted || e.level<amount)return false;
    e.level-=amount;
    if(e.level<=0.0f){e.level=0.0f;e.depleted=true;}
    return true;
}
// Take everything left (a depleting attack): how much was taken (0: nothing to take); depletes.
inline float Drain(Ether& e) noexcept {
    if(e.depleted || e.level<=0.0f)return 0.0f;
    const float taken=e.level;
    e.level=0.0f;e.depleted=true;
    return taken;
}
// Take up to `amount` (a held drain): how much was taken; reaching 0 depletes.
inline float Bleed(Ether& e,float amount) noexcept {
    if(!(amount>0.0f) || e.depleted || e.level<=0.0f)return 0.0f;
    const float taken=amount<e.level ? amount : e.level;
    e.level-=taken;
    if(e.level<=0.0f){e.level=0.0f;e.depleted=true;}
    return taken;
}
// The ground's refill of one frame.
inline void Refill(Ether& e) noexcept {
    if(!(e.max>0.0f))e.max=kEtherMax;
    e.level+=kRefillPerFrame;
    if(e.level>e.max)e.level=e.max;
    if(e.depleted && e.level>=kRecoverAt)e.depleted=false;
}
// Fields as read from the object: a max not positive or not finite is the class's; the level into [0, max].
inline Ether Sane(float level,float max,bool depleted) noexcept {
    Ether e;
    e.max=std::isfinite(max) && max>0.0f ? max : kEtherMax;
    e.level=std::isfinite(level) ? (level<0.0f ? 0.0f : level>e.max ? e.max : level) : e.max;
    e.depleted=depleted;
    return e;
}

// ---- a frame of the steps ----
struct Motion {           // the per-soldier state the frames carry (the plugin's extension of the object)
    int groundFrames=0;   // frames on the ground in a row
    int airFrames=0;      // frames in the air in a row
    int sinceStep=1000;   // frames since the last step
    int steps=0;          // steps since leaving the ground
    bool gliding=false;
};
struct StepIn {
    bool press=false,hold=false;   // the jump this frame / held
    bool ground=false;             // support == 2
    bool able=true;                // alive, not riding, not attached or ragdolled
    float stick[2]{};              // the move stick as +0xD50 has it: (-LX, -LY)
    float yaw=0.0f;                // the look's yaw (+0x1244)
    float carry[3]{};              // the carried velocity (+0x6B0)
};
enum class Cue : std::uint8_t { none=0,takeoff=1,step=2 };
struct StepOut {
    bool jump=false;               // write the jump request (kStepUp)
    bool push=false;               // write carry x / z = pushX / pushZ
    float pushX=0.0f,pushZ=0.0f;
    bool glide=false;              // after the stock frame: carry y held at -kGlideSink at least
    Cue cue=Cue::none;             // what the rune circle effect shows (and the room is told)
};

// The stick (as +0xD50 holds it) to a world level direction at the look's yaw; false under kStickDead. The look's
// forward is (sin yaw, 0, cos yaw) (enemy_control.cpp); +0xD50 is (-LX, 0, -LY) (sidecar.cpp) [inferred: the signs want
// one look in the game, runeblade-plan.md §3].
inline bool WorldPush(const float stick[2],float yaw,float* x,float* z) noexcept {
    const float right=-stick[0],forward=-stick[1];
    const float m=std::sqrt(right*right+forward*forward);
    if(!(m>=kStickDead))return false;
    const float r=right/m,f=forward/m,s=std::sin(yaw),c=std::cos(yaw);
    // forward (s, c), right (c, -s)
    *x=f*s+r*c;*z=f*c-r*s;
    return true;
}

// One frame: updates `m` and `e`, says what to write.
inline StepOut StepFrame(Motion& m,Ether& e,const StepIn& in) noexcept {
    StepOut out;
    if(m.sinceStep<1000)++m.sinceStep;
    if(!in.able){m.gliding=false;m.groundFrames=m.airFrames=0;return out;}
    if(in.ground) {
        if(m.airFrames>0)m.steps=0;
        m.airFrames=0;m.gliding=false;
        ++m.groundFrames;
        if(m.groundFrames>kRefillDelay)Refill(e);
        if(in.press)out.cue=Cue::takeoff;   // the stock jump takes it from the ground; the circle under the feet
        return out;
    }
    m.groundFrames=0;
    ++m.airFrames;
    if(in.press && m.airFrames>kAirborneFrames && m.sinceStep>=kStepGapFrames && Spend(e,kStepCost)) {
        out.jump=true;out.cue=Cue::step;
        m.sinceStep=0;++m.steps;m.gliding=false;
        float x=0.0f,z=0.0f;
        if(WorldPush(in.stick,in.yaw,&x,&z)) {
            // the push along the stick, at least kStepPush, the carry's speed kept when faster, kMostAirSpeed at most
            const float carried=std::sqrt(in.carry[0]*in.carry[0]+in.carry[2]*in.carry[2]);
            float speed=carried>kStepPush ? carried : kStepPush;
            if(speed>kMostAirSpeed)speed=kMostAirSpeed;
            out.push=true;out.pushX=x*speed;out.pushZ=z*speed;
        }
        return out;
    }
    // glide: held, falling, not just stepped, and ether left
    const bool want=in.hold && in.carry[1]<0.0f && m.sinceStep>=kStepGapFrames && m.airFrames>kAirborneFrames;
    m.gliding=want && Bleed(e,kGlideDrain)>0.0f;
    out.glide=m.gliding;
    return out;
}
// The fall held while gliding (after the stock frame wrote its gravity).
inline float GlideFall(float vy) noexcept { return vy<-kGlideSink ? -kGlideSink : vy; }

}  // namespace crew::runeblade
