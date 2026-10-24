// UfoMother301's rest poses (src/edf41mother.cpp), the pure part: what the main CAS player of the hull and of each part must hold
// before 4.1's class plays its first state. No game needed (tests/edf41mother_pose_test.cpp); docs/feedback-2026-10-17-edf41-mother.md.
//
// 4.1 starts no clip at construction for the hull (ctor 0x24ECE0 only looks the player "main" up; its first state 0x252700 plays
// nothing) nor for a cannon (ctor 0x258070 the same; its first state 0x259CD0 plays nothing until +0x900, then 0x259D20 plays
// "appear" and writes the player's speed 1.0 at +0x110): both show the model's bind pose until the script enables them. EDF6's
// constructors do start one: UfoCarrier508's (0x4F6710) plays "close_state" at speed 1, which tools/make_edf41_objects.py aliases
// to the mothership's "close" clip (987 frames from the opened pose: the hatch swung out, the weak point 300 down, to the closed
// one); EventAnimationObject's plays "default", aliased to the cannon's only clip "appear" (141 keys, ~558 frames: the barrel 360
// down and the four beam hatches open). So every mothership spawned with its belly open (closing over 16 s) and its ten cannons
// unfolding (out for good after 9 s): the look of the battle's second phase, and the cannons reach far below the hull.
// The bind poses, read from the MDBs: the hull's equals the end of "close" (hatch at (0, -364.6, 0) unturned; the weak bone at
// y 0 vs the clip's -100, inside the shell either way), a cannon's equals the first key of "appear" (waist at y 0, hatches unturned).
// The other parts already rest as 4.1: the main cannon's "default" is its still close_state (4.1 0x256950 plays close_state), the
// hatches' and covers' too (4.1 0x25BB00), the panels have no CAS of their own (the hatch's binds nothing on their model).
#pragma once

namespace crew::edf41pose {

enum class Group { kHatch, kCover, kCannon, kBigCannon, kPanel, kHull };

// What to do with the clip the EDF6 constructor started.
enum class Hold {
    kNone,          // already 4.1's rest
    kFirstKeyStill, // the clip back to time 0 and stopped (speed 0): its first key is the bind pose
    kLastKey,       // the clip to its end (a clip that does not loop holds its last key): its last key is the bind pose
};

constexpr Hold RestHold(Group g) noexcept {
    switch(g) {
        case Group::kCannon: return Hold::kFirstKeyStill;
        case Group::kHull: return Hold::kLastKey;
        default: return Hold::kNone;
    }
}

// The player's time for the hold; `length` is the clip's (player +0xF0, frames).
constexpr float HoldTime(Hold h,float length) noexcept { return h==Hold::kLastKey ? (length>0.0f ? length : 0.0f) : 0.0f; }
// The player's speed for the hold; `current` is what the constructor left (1).
constexpr float HoldSpeed(Hold h,float current) noexcept { return h==Hold::kFirstKeyStill ? 0.0f : current; }

// The hold is applied once, on the object's first frame, and only while 4.1's class has not played a state of its own (the hull:
// still in its first state, closed, which M603's creation-frame ObjectAction 3 leaves the very same frame; a cannon: still dormant).
constexpr bool HoldNow(Group g,bool alreadyHeld,bool stillInFirstState) noexcept {
    return RestHold(g)!=Hold::kNone && !alreadyHeld && stillInFirstState;
}

// A cannon's "appear" (4.1 0x259D20): played from its start at speed 1, whatever the hold left.
constexpr float kAppearTime=0.0f;
constexpr float kAppearSpeed=1.0f;

}  // namespace crew::edf41pose
