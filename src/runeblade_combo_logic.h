// The Rune Blade's melee combo state machine (docs/newclass-re/runeblade-plan.md §1.2), pure (tests/runeblade_test.cpp).
// RB-CORE owns the machine and one state a soldier (runeblade_combo.h); a melee weapon (RB-MELEE) describes itself with a
// Spec and feeds each frame's input, and does what the Output says (its animation, its damage round). Frames are game
// frames (60 a second).
#pragma once
#include <cstdint>

namespace crew::runeblade::combo {

constexpr std::uint8_t kAirOnly=1,kGroundOnly=2,kNoInsert=4,kLandEnds=8,kStageFlags=15;
constexpr int kMostStages=16;

struct Stage {
    std::uint16_t length=0;      // the stage's frames (the swing and its recovery)
    std::uint16_t hitFrame=0;    // Output.hit on this frame (the weapon's damage round)
    std::uint16_t cancelFrom=0;  // from this frame a buffered press goes on to the next stage
    std::uint16_t window=0;      // frames after the stage still waiting for a press (then the combo ends)
    std::int8_t next=-1;         // the next stage; -1 the combo's last
    std::uint8_t flags=0;        // kAirOnly / kGroundOnly / kNoInsert / kLandEnds
};
struct Spec {
    const Stage* stages=nullptr;
    int count=0;
    std::int8_t airEntry=-1;     // the first press in the air starts here (-1: stage 0, as on the ground)
    std::int8_t airBranch=-1;    // a press in the air during a ground stage goes here (-1: no air branch)
    std::uint16_t insertFrames=0;// a ranged insert holds the combo's clock this long
};
struct Input { bool attack=false,airborne=false,ranged=false; };
struct Output {
    int started=-1;     // the stage started this frame
    bool hit=false;     // this frame is the current stage's hit frame
    bool insert=false;  // this frame's ranged press taken into the combo (the ranged weapon fires; the combo goes on)
    bool ended=false;   // the combo ended this frame
    int stage=-1;       // the current stage (-1 idle)
};
enum class Phase : std::uint8_t { idle,stage,window };
struct State {
    Phase phase=Phase::idle;
    int stage=-1;
    int frame=0;        // frames into the stage (or into the window)
    int frozen=0;       // frames the clock is still held (a ranged insert)
    bool buffered=false;
    bool wasAirborne=false;
    const Spec* spec=nullptr;
};

inline bool Valid(const Spec& s) noexcept {
    if(!s.stages || s.count<=0 || s.count>kMostStages)return false;
    if(s.airEntry>=s.count || s.airBranch>=s.count)return false;
    for(int i=0;i<s.count;++i) {
        const Stage& t=s.stages[i];
        if(!t.length || t.hitFrame>=t.length || t.cancelFrom>t.length || t.next>=s.count || (t.flags&~kStageFlags))return false;
        if((t.flags&kAirOnly) && (t.flags&kGroundOnly))return false;
    }
    return true;
}
inline bool Fits(const Stage& t,bool airborne) noexcept {
    return !((t.flags&kAirOnly) && !airborne) && !((t.flags&kGroundOnly) && airborne);
}

namespace detail {
inline void Begin(State& st,int stage,Output& out) noexcept {
    st.phase=Phase::stage;st.stage=stage;st.frame=0;st.buffered=false;
    out.started=stage;
    if(st.spec->stages[stage].hitFrame==0)out.hit=true;
}
inline void End(State& st,Output& out) noexcept {
    st.phase=Phase::idle;st.stage=-1;st.frame=0;st.frozen=0;st.buffered=false;
    out.ended=true;
}
// The stage a press goes on to from the current one: the air branch (airborne, from a stage not of the air), else next.
inline int Following(const State& st,bool airborne) noexcept {
    const Spec& s=*st.spec;
    const Stage& cur=s.stages[st.stage];
    if(airborne && s.airBranch>=0 && !(cur.flags&kAirOnly) && st.stage!=s.airBranch)return s.airBranch;
    return cur.next;
}
}  // namespace detail

inline void Reset(State& st) noexcept { const Spec* s=st.spec;st=State{};st.spec=s; }
inline bool Active(const State& st) noexcept { return st.phase!=Phase::idle; }

// One frame. A spec other than the bound one (a weapon changed) starts over idle; an invalid one does nothing.
inline Output Step(State& st,const Spec* spec,const Input& in) noexcept {
    Output out;
    if(!spec || !Valid(*spec)){st=State{};return out;}
    if(st.spec!=spec){st=State{};st.spec=spec;}
    const bool landed=st.wasAirborne && !in.airborne;
    st.wasAirborne=in.airborne;
    if(st.phase==Phase::idle) {
        if(in.attack) {
            const int first=in.airborne && spec->airEntry>=0 ? spec->airEntry : 0;
            if(Fits(spec->stages[first],in.airborne))detail::Begin(st,first,out);
        }
        out.stage=st.stage;
        return out;
    }
    const Stage& cur=spec->stages[st.stage];
    if(landed && (cur.flags&kLandEnds)){detail::End(st,out);out.stage=-1;return out;}
    if(in.ranged && !(cur.flags&kNoInsert) && spec->insertFrames){out.insert=true;st.frozen=spec->insertFrames;}
    if(in.attack)st.buffered=true;
    if(st.frozen>0){--st.frozen;out.stage=st.stage;return out;}   // the clock held: nothing moves on
    ++st.frame;
    if(st.phase==Phase::stage) {
        if(st.frame==cur.hitFrame)out.hit=true;
        if(st.buffered && st.frame>=cur.cancelFrom) {
            const int to=detail::Following(st,in.airborne);
            if(to>=0 && Fits(spec->stages[to],in.airborne)){detail::Begin(st,to,out);out.stage=st.stage;return out;}
            if(to<0)st.buffered=false;   // the last stage: a press now is spent
        }
        if(st.frame>=cur.length){st.phase=Phase::window;st.frame=0;}
    } else {   // the window
        if(st.buffered) {
            const int to=detail::Following(st,in.airborne);
            if(to>=0 && Fits(spec->stages[to],in.airborne)){detail::Begin(st,to,out);out.stage=st.stage;return out;}
            st.buffered=false;
        }
        if(st.frame>cur.window){detail::End(st,out);out.stage=-1;return out;}
    }
    out.stage=st.stage;
    return out;
}

}  // namespace crew::runeblade::combo
