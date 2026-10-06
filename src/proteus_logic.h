// The Proteus rework's rules, the pure part (src/proteus.cpp runs them each frame; tools/proteus_check.cpp checks them
// offline, including this header alone: no game, no Windows). README 普罗透斯, docs/proteus-re.md.
//  - Two stances. walk: the quick command platform (faster legs, a higher step, the guns loose and slow, no missiles).
//    deployed: the fire base (planted: no walking; the guns tight and quick; the field round it; its own barrier; the
//    missile salvo). Between them deploying / stowing, a fixed stagger each way in which it neither walks nor turns and
//    the toggle is not taken (the user, 2026-10-06: "架设和收起各有一两秒硬直，期间不能动").
//  - One shield key, two shields by stance. walking (and in the stagger): the front shield, over the hull's nose, which
//    slows the legs while up and never heats. deployed: the directional shield, where the view faces, which heats with
//    every second it is up (the user: "热量根据时间来") and cools while down; overheated it drops until cooled to the
//    resume share, then comes back by itself if it is still switched on.
//  - The barrier (deployed only): an HP layer of its own (a share of the hull's max HP) that takes what the shields let
//    through before the hull does, and refills slowly after a quiet spell; walking it neither takes hits nor refills.
//  - The salvo: deployed, a target marked, the cooldown over: the launcher's rounds go at the mark, then the cooldown.
// Times in seconds, angles in radians, shares 0..1.
#pragma once
#include <cmath>

namespace proteus {
enum class Mode : unsigned char { walk, deploying, deployed, stowing };

struct Tunables {
    float deploySec=1.5f,stowSec=1.5f;   // the staggers
    float heatSec=12.0f;                 // the directional shield up from cold to overheated
    float coolSec=6.0f;                  // ...and down from overheated to cold
    float resume=0.3f;                   // overheated, it is back once cooled to this share
    float shieldBlock=1.0f;              // the share of a hit inside a shield's arc it stops
    float shieldHalfArc=1.0471976f;      // the arc's half angle (60 deg: a 120 deg shield)
    float barrierRegenSec=40.0f;         // the barrier from empty to full, deployed
    float barrierDelaySec=4.0f;          // ...once this long without a hit on it
    float salvoCooldownSec=30.0f;
};

struct State {
    Mode mode=Mode::walk;
    float t=0.0f;           // into the stagger (deploying / stowing)
    bool shieldOn=false;    // the shield key's switch (the rider's wish)
    float heat=0.0f;        // the directional shield's, 0..1
    bool overheated=false;
    float barrier=1.0f;     // the barrier's share left
    float quiet=1.0e9f;     // since the barrier last took a hit
    float salvoWait=0.0f;   // the salvo's cooldown left
};

struct Input {
    float dt=0.0f;
    bool toggle=false;      // the mode key's press this frame
    bool shield=false;      // the shield key's press
    bool salvo=false;       // the salvo's press
    bool marked=false;      // a target is marked
};

// What the step decided this frame.
struct Output {
    bool salvoFired=false;  // the salvo goes this frame
    bool modeChanged=false;
};

inline float Clamp01(float v) noexcept { return v<0.0f ? 0.0f : v>1.0f ? 1.0f : v; }
inline bool Staggered(Mode m) noexcept { return m==Mode::deploying || m==Mode::stowing; }
// The stagger's share done (1 outside one).
inline float StaggerShare(const State& s,const Tunables& k) noexcept {
    if(s.mode==Mode::deploying)return k.deploySec>0.0f ? Clamp01(s.t/k.deploySec) : 1.0f;
    if(s.mode==Mode::stowing)return k.stowSec>0.0f ? Clamp01(s.t/k.stowSec) : 1.0f;
    return 1.0f;
}

// Whether a shield stands now: switched on and, deployed, not overheated. (In the stagger the front shield stands:
// the hull is at its most exposed there.)
inline bool ShieldUp(const State& s) noexcept {
    if(!s.shieldOn)return false;
    return s.mode!=Mode::deployed || !s.overheated;
}
// Which way the standing shield faces: false the hull's nose (the front shield), true the view (the directional one).
inline bool ShieldFollowsView(const State& s) noexcept { return s.mode==Mode::deployed; }

// One frame of the rules.
inline Output Step(State& s,const Input& in,const Tunables& k) noexcept {
    Output o{};
    const float dt=in.dt>0.0f && std::isfinite(in.dt) ? in.dt : 0.0f;
    if(in.shield)s.shieldOn=!s.shieldOn;
    // The stance.
    switch(s.mode) {
        case Mode::walk:
            if(in.toggle){s.mode=Mode::deploying;s.t=0.0f;o.modeChanged=true;}
            break;
        case Mode::deployed:
            if(in.toggle){s.mode=Mode::stowing;s.t=0.0f;o.modeChanged=true;}
            break;
        case Mode::deploying:
            s.t+=dt;
            if(s.t>=k.deploySec){s.mode=Mode::deployed;s.t=0.0f;o.modeChanged=true;}
            break;
        case Mode::stowing:
            s.t+=dt;
            if(s.t>=k.stowSec){s.mode=Mode::walk;s.t=0.0f;o.modeChanged=true;}
            break;
    }
    // The directional shield's heat: up and deployed it heats, otherwise it cools (overheated it is down, so it cools).
    const bool heating=s.mode==Mode::deployed && ShieldUp(s);
    if(heating)s.heat+=k.heatSec>0.0f ? dt/k.heatSec : 1.0f;
    else s.heat-=k.coolSec>0.0f ? dt/k.coolSec : 1.0f;
    s.heat=Clamp01(s.heat);
    if(!s.overheated && s.heat>=1.0f)s.overheated=true;
    else if(s.overheated && s.heat<=k.resume)s.overheated=false;
    // The barrier refills deployed, after a quiet spell.
    s.quiet+=dt;
    if(s.mode==Mode::deployed && s.quiet>=k.barrierDelaySec && k.barrierRegenSec>0.0f)s.barrier=Clamp01(s.barrier+dt/k.barrierRegenSec);
    // The salvo.
    s.salvoWait=s.salvoWait>dt ? s.salvoWait-dt : 0.0f;
    if(in.salvo && s.mode==Mode::deployed && in.marked && s.salvoWait<=0.0f) {
        o.salvoFired=true;
        s.salvoWait=k.salvoCooldownSec;
    }
    return o;
}

// Whether a hit from horizontal direction (dx, dz) (from the Proteus toward where it came from) is inside an arc of half
// angle `half` round the horizontal facing (fx, fz). No horizontal part (straight over or under): outside.
inline bool InArc(float fx,float fz,float dx,float dz,float half) noexcept {
    const float fl=std::sqrt(fx*fx+fz*fz),dl=std::sqrt(dx*dx+dz*dz);
    if(!(fl>1.0e-6f) || !(dl>1.0e-6f))return false;
    return (fx*dx+fz*dz)/(fl*dl)>=std::cos(half);
}

// What reaches the hull of `damage` (> 0) coming from inside the standing shield's arc (`inArc`) or not: the shield
// stops its share, the barrier (deployed) takes what is left while it lasts (`barrierHp`: its full HP; the hit counts
// against its quiet spell). Writes the state.
inline float Absorb(State& s,const Tunables& k,float damage,bool inArc,float barrierHp) noexcept {
    if(!(damage>0.0f))return damage;
    float left=damage;
    if(inArc && ShieldUp(s))left*=1.0f-Clamp01(k.shieldBlock);
    if(s.mode==Mode::deployed && barrierHp>0.0f && left>0.0f) {
        s.quiet=0.0f;
        const float have=s.barrier*barrierHp;
        const float took=left<have ? left : have;
        s.barrier=Clamp01((have-took)/barrierHp);
        left-=took;
    }
    return left;
}

// The legs' speed (and turn) factor of the stance, before the ini's own scaling: walking full, with the front shield up
// `shieldSlow`, otherwise (the stagger, planted) none.
inline float LegShare(const State& s,float shieldSlow) noexcept {
    if(s.mode!=Mode::walk)return 0.0f;
    return ShieldUp(s) ? shieldSlow : 1.0f;
}

// The legs' turn factor: walking as the speed (the front shield slows both), deployed `deployTurn` (planted, it still
// turns on the spot to face a threat: the shield faces the hull's nose), in the stagger none.
inline float TurnShare(const State& s,float shieldSlow,float deployTurn) noexcept {
    if(s.mode==Mode::deployed)return deployTurn;
    return LegShare(s,shieldSlow);
}

// The ground normal's least height (the character's walkable test, docs/proteus-re.md §3) for a step of `step` m onto
// a ledge under a capsule foot of radius `r`: a ledge's edge `step` m up touches the foot's sphere where its normal's
// height is 1 - step / r; the step is climbed when that is still walkable. Clamped to [0.2, 0.99].
inline float StepNormal(float step,float r) noexcept {
    if(!(r>0.0f))return 0.766f;
    const float n=1.0f-step/r;
    return n<0.2f ? 0.2f : n>0.99f ? 0.99f : n;
}
// The step a normal height allows (the inverse).
inline float StepOf(float normal,float r) noexcept { return r*(1.0f-normal); }
}  // namespace proteus
