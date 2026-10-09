// The Proteus rework's rules, the pure part (src/proteus.cpp runs them each frame; tools/proteus_check.cpp checks them
// offline, including this header alone: no game, no Windows). README 普罗透斯, docs/proteus-re.md.
//  - Two stances. walk: the quick command platform (faster legs, a higher step, the cannons loose and slow, the
//    launcher idle). deployed: the fire base (planted; the cannons tight and quick; the field; the launcher). Between
//    them deploying / stowing, a fixed stagger each way in which it neither walks nor turns and the toggle is not taken
//    (the user, 2026-10-06: "架设和收起各有一两秒硬直，期间不能动").
//  - One shield: the stock Air Raider's electromagnetic barrier (BarrierBullet01, the 電磁トーチカ's wall) standing
//    round the hull. The game stops the hostile rounds on it and takes them off its HP; this file only says whether it
//    stands and keeps its HP share between raisings. Walking it faces the hull's nose and slows the legs; deployed it
//    faces the driver's view and heats with every second it stands (the user: "热量根据时间来"). Overheated, or broken
//    (its HP gone), it stays down until cooled / refilled to the resume share. Down and unhit for a while it refills.
//  - The weapons are the Proteus's own three mounts (two cannons, the missile launcher). A mount whose own seat is
//    empty is borrowed by the first seat of its chain that holds a soldier (Operator); the game does the rest.
// Times in seconds, shares 0..1.
#pragma once
#include <cmath>

namespace proteus {
enum class Mode : unsigned char { walk, deploying, deployed, stowing };

struct Tunables {
    float deploySec=1.5f,stowSec=1.5f;   // the staggers
    float heatSec=12.0f;                 // deployed: the shield standing from cold to overheated
    float coolSec=6.0f;                  // ...and down from overheated to cold
    float resume=0.3f;                   // overheated / broken, it may stand again at this share (heat down to / HP up to)
    float shieldRegenSec=40.0f;          // the shield's HP from empty to full while it is down
    float shieldDelaySec=4.0f;           // ...once this long without a hit on it
};

struct State {
    Mode mode=Mode::walk;
    float t=0.0f;           // into the stagger (deploying / stowing)
    bool shieldOn=false;    // the shield key's switch (the rider's wish)
    float heat=0.0f;        // 0..1, deployed
    bool overheated=false;
    float shield=1.0f;      // the shield's HP share left (of its full HP)
    bool broken=false;      // its HP ran out while it stood: down until refilled to the resume share
    float quiet=1.0e9f;     // since the shield last took a hit
};

struct Input {
    float dt=0.0f;
    bool toggle=false;      // the stance key's press this frame
    bool shield=false;      // the shield key's press
};

struct Output { bool modeChanged=false; };

inline float Clamp01(float v) noexcept { return v<0.0f ? 0.0f : v>1.0f ? 1.0f : v; }
inline bool Staggered(Mode m) noexcept { return m==Mode::deploying || m==Mode::stowing; }
// The stagger's share done (1 outside one).
inline float StaggerShare(const State& s,const Tunables& k) noexcept {
    if(s.mode==Mode::deploying)return k.deploySec>0.0f ? Clamp01(s.t/k.deploySec) : 1.0f;
    if(s.mode==Mode::stowing)return k.stowSec>0.0f ? Clamp01(s.t/k.stowSec) : 1.0f;
    return 1.0f;
}

// Whether the shield stands now: switched on, not broken, HP left and, deployed, not overheated.
inline bool ShieldUp(const State& s) noexcept {
    return s.shieldOn && !s.broken && s.shield>0.0f && !(s.mode==Mode::deployed && s.overheated);
}
// Which way the standing shield faces: false the hull's nose (walking, in the stagger), true the driver's view.
inline bool ShieldFollowsView(const State& s) noexcept { return s.mode==Mode::deployed; }

// The shield's HP over `dt`: down (`up` false) and unhit for the delay, it refills; broken, it may stand again once
// refilled to the resume share. Online only the shield's registered owner runs this (proteus_net.inc).
inline void Refill(State& s,float dt,const Tunables& k,bool up) noexcept {
    if(!(dt>0.0f) || !std::isfinite(dt))dt=0.0f;
    s.quiet=s.quiet+dt<1.0e9f ? s.quiet+dt : 1.0e9f;
    if(!up && s.quiet>=k.shieldDelaySec && k.shieldRegenSec>0.0f)s.shield=Clamp01(s.shield+dt/k.shieldRegenSec);
    if(s.broken && s.shield>=k.resume)s.broken=false;
}

// One frame of the rules.
inline Output Step(State& s,const Input& in,const Tunables& k) noexcept {
    Output o{};
    const float dt=in.dt>0.0f && std::isfinite(in.dt) ? in.dt : 0.0f;
    if(in.shield)s.shieldOn=!s.shieldOn;
    switch(s.mode) {
        case Mode::walk: if(in.toggle){s.mode=Mode::deploying;s.t=0.0f;o.modeChanged=true;} break;
        case Mode::deployed: if(in.toggle){s.mode=Mode::stowing;s.t=0.0f;o.modeChanged=true;} break;
        case Mode::deploying:
            s.t+=dt;
            if(s.t>=k.deploySec){s.mode=Mode::deployed;s.t=0.0f;o.modeChanged=true;}
            break;
        case Mode::stowing:
            s.t+=dt;
            if(s.t>=k.stowSec){s.mode=Mode::walk;s.t=0.0f;o.modeChanged=true;}
            break;
    }
    const bool up=ShieldUp(s);
    // Heat: standing deployed it heats, otherwise it cools.
    s.heat=Clamp01(s.heat+(s.mode==Mode::deployed && up ? (k.heatSec>0.0f ? dt/k.heatSec : 1.0f)
                                                         : -(k.coolSec>0.0f ? dt/k.coolSec : 1.0f)));
    if(!s.overheated && s.heat>=1.0f)s.overheated=true;
    else if(s.overheated && s.heat<=k.resume)s.overheated=false;
    Refill(s,dt,k,up);
    return o;
}

// The standing barrier's HP share as the game has it now (`share`, its HP over the HP it was raised with): a drop is
// a hit (the quiet spell starts again); none left is broken. Gone without a drop (the game took it away) is broken too.
inline void Sense(State& s,float share) noexcept {
    share=Clamp01(std::isfinite(share) ? share : 0.0f);
    if(share<s.shield)s.quiet=0.0f;
    s.shield=share;
    if(share<=0.0f)s.broken=true;
}

// The legs' speed factor of the stance, before the ini's own scaling: walking full, with the shield up `shieldSlow`,
// otherwise (the stagger, planted) none.
inline float LegShare(const State& s,float shieldSlow) noexcept {
    if(s.mode!=Mode::walk)return 0.0f;
    return ShieldUp(s) ? shieldSlow : 1.0f;
}
// The legs' turn factor: walking as the speed, deployed `deployTurn` (it turns on the spot to face a threat), in the
// stagger none.
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

// --- the weapons ---
// The engine's four seats (every VehicleBigBegaruta SGO's vehicle_riding_position): 0 the driver (no weapon of its own),
// 1 / 2 the left / right cannon, 3 the missile launcher. The two-seat layout closes 2 and 3 to boarding.
constexpr unsigned kDriver=0,kGunner=1,kRightGunner=2,kLauncherSeat=3,kSeats=4;
enum class Trigger : unsigned char { primary, secondary };
// A stock weapon mount: the seat whose weapon it is, and who may operate it, in order. The mount's own seat comes first,
// so a soldier there always keeps the stock behaviour; the others borrow it only while it stands empty.
struct Mount {
    unsigned seat;
    unsigned chain[3];
    unsigned links;
    Trigger driverTrigger;   // which of the driver's triggers fires it when the driver borrows it
    bool deployedOnly;       // borrowed only deployed (walking the launcher stays idle, the user 2026-10-06)
};
constexpr Mount kMounts[]={
    {kGunner,{kGunner,kDriver,0},2,Trigger::primary,false},                       // left cannon: the gunner, else the driver
    {kRightGunner,{kRightGunner,kGunner,kDriver},3,Trigger::primary,false},       // right cannon: paired with the left
    {kLauncherSeat,{kLauncherSeat,kDriver,0},2,Trigger::secondary,true},           // launcher: the driver's second trigger
};
constexpr unsigned kMountCount=sizeof(kMounts)/sizeof(kMounts[0]);

// The seat operating mount `m`: the first of its chain holding a living soldier (bit i of `occupied`: seat i), the
// borrowers only when the stance allows; -1 none. The mount's own seat is never refused.
inline int Operator(const Mount& m,unsigned occupied,Mode mode) noexcept {
    for(unsigned i=0;i<m.links && i<3;++i) {
        const unsigned seat=m.chain[i];
        if(!(occupied&(1u<<seat)))continue;
        if(seat!=m.seat && m.deployedOnly && mode!=Mode::deployed)return -1;
        return static_cast<int>(seat);
    }
    return -1;
}
// Whether a mount is borrowed (operated from another seat than its own): the rework's to activate, aim and fire.
inline bool Borrowed(const Mount& m,int op) noexcept { return op>=0 && static_cast<unsigned>(op)!=m.seat; }
}  // namespace proteus
