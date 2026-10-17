// UfoRobo's robot-form AI and step choice (src/edf41robo.cpp), the pure part, frame for frame as EDF4.1 does it (EDF41.exe
// RVAs; docs/edf41-uforobo.md section 1 and 2.2 "AI"). No game needed (tests/edf41robo_ai_test.cpp).
//
// Why it is here (2026-10-17, a player: "UfoRobo 是不是少了ai，怎么感觉他光攻击但是不动"): the plugin left the attack state A5
// for A3 the frame the attack clip began, so A3's attack timer (600/s..1800/s frames, 4-13 s at s 2.35) ran out while the
// attack (S9, 28-37 s: attack_fire_straight 1702, _cross 1722, _genocide 1120 + 1118 CAS frames) still played, and A4 started
// the next attack the frame the body came back to S7: S9 back to back, the walk legs (phase 1 / 2) spent during the attacks,
// never a step (real runs rfp_M411 / mg2_M411 / rfp_M416 / rfp_M010: S9 in every 600-frame status line, the position the same
// for minutes). 4.1's A5 (0x27A770) starts the attack on entering (the body straight to S9 with the entry, 0x27AAE2) and
// stays until the body is back in S7 (0x27AB46..0x27ABD3); only then A3 runs again and its timer counts.
#pragma once

namespace crew::edf41robo {

enum Ai : int { kA0=0, kA1, kA2, kA3, kA4, kA5 };
constexpr int kStand=7;    // S7: the robot's chooser (0x278810)
constexpr int kStep=8;     // S8: a step or turn clip
constexpr int kAttack=9;   // S9: an attack clip

// 4.1 0x271AB0: {count, min, max, distance window}; counts down only while the distance is inside the window; true when the
// count it had was <= 0 (then min + rand(max - min) again).
struct Timer { int count=0,min=0,max=0; float dmin=0.0f,dmax=0.0f; };
template<class Rng>
bool TimerTick(Timer& t,float distance,Rng& rng) noexcept {
    if(t.dmin>distance || distance>t.dmax)return false;
    if(t.count-- >0)return false;
    t.count=t.min+rng.Int(t.max-t.min);
    return true;
}

// The legs (4.1 +0x147C..+0x14B4): phase 1 frames, radius, sideways frames / interval / amount, phase 2 frames, the cruise
// command, the ship's cruise turn.
struct Legs {
    int phase1=0;
    float radius=0.0f;
    int sideFrames=0,sideInterval=0;
    float sideAmount=0.0f;
    int phase2=0;
    float cruise[3]={0.0f,0.0f,90.0f};
    int cruiseTurnFrames=0;
    float cruiseTurnRate=0.0f;
};

// 4.1 0x2797F0 (speed = +0x1478, 90): phase 1 1200 + rand(600), radius 300 + 200 r (the robot approaching: 50 + 150 r),
// phase 2 600 + rand(300), cruise (0, 0, speed), the robot backwards (-speed) one time in 3 (approaching: 1 in 2), the cruise
// turn rand(300) frames at rand x 0.015 - 0.0075.
template<class Rng>
void Reroll(Legs& l,bool robot,bool approach,float speed,Rng& rng) noexcept {
    l.phase1=1200+rng.Int(600);
    l.radius=robot && approach ? 50.0f+150.0f*rng.Unit() : 300.0f+200.0f*rng.Unit();
    l.phase2=600+rng.Int(300);
    l.cruise[0]=0.0f; l.cruise[1]=0.0f; l.cruise[2]=speed;
    if(robot && rng.Unit()<(approach ? 0.5f : 1.0f/3.0f))l.cruise[2]=-speed;
    l.cruiseTurnFrames=rng.Int(300);
    l.cruiseTurnRate=rng.Unit()*0.015f-0.0075f;
}
// 4.1 0x279AC0: half the time 600 + rand(600) sideways frames, the amount +-speed / 4, the interval rand(600) (the robot
// approaching: 0).
template<class Rng>
void RerollSide(Legs& l,bool robot,bool approach,float speed,Rng& rng) noexcept {
    l.sideFrames=rng.Unit()<0.5f ? 600+rng.Int(600) : 0;
    l.sideAmount=(rng.Unit()<0.5f ? 0.25f : -0.25f)*speed;
    l.sideInterval=robot && approach ? 0 : rng.Int(600);
}

// The frame's command from the legs (4.1 +0x1600 x / +0x1608 z, raw: cruise +-90, sideways +-22.5, approach +-1) and whether
// the robot faces the target this frame (0x2795C0: a turn clip by the angle).
struct Order { float x=0.0f,z=0.0f; bool face=false; };

// 4.1 0x27A180, the host's branch, the legs part. Approaching (+0x1538): phase 1 forward / back by the horizontal distance
// to the target against the radius and facing it; then phase 2's cruise; then sideways; then, the interval run out, both
// rerolled. Not approaching: phase 1 only faces the target, phase 2 cruises (forward 2/3, back 1/3), then a reroll; the
// sideways part on top of either (a sideways reroll every frame its interval is above 0, literally).
template<class Rng>
Order RobotLegs(Legs& l,bool approach,float horizontal,float speed,Rng& rng) noexcept {
    Order o;
    if(approach) {
        if(l.phase1-- >0) {
            const float d2=horizontal*horizontal,r2=l.radius*l.radius;
            if(r2>d2)o.z=-1.0f;
            else if(d2>r2)o.z=1.0f;
            o.face=true;
        } else if(l.phase2-- >0) {
            o.x=l.cruise[0]; o.z=l.cruise[2];
        } else if(l.sideFrames-- >0) {
            o.x=l.sideAmount;
        } else if(!(l.sideInterval-- >0)) {
            RerollSide(l,true,true,speed,rng);
            Reroll(l,true,true,speed,rng);
        }
        return o;
    }
    if(l.phase1-- >0)o.face=true;
    else if(l.phase2-- >0) { o.x=l.cruise[0]; o.z=l.cruise[2]; }
    else Reroll(l,true,false,speed,rng);
    if(l.sideFrames-- >0)o.x=l.sideAmount;
    else if(l.sideInterval-- >0)RerollSide(l,true,false,speed,rng);
    return o;
}

// What the AI asks of its caller this frame.
enum class AiAsk { kNone, kChooseAttack };

// One frame of the robot's AI states A3..A5 given the body state now (4.1 0x27A180 / 0x27A630 / 0x27A770, the host).
//  - A3 (with a target): the legs, then the attack timer (distance 0, window 0..1000: always counts); run out -> A4.
//  - A4: waits for the body in S7, then A5; the caller chooses the attack (kChooseAttack) and puts the body in S9 at once
//    with it (4.1 0x27AAE2), or, none fitting, sends the AI back to A3 (4.1 hands the body machine A3's function there,
//    0x27AAE2 with 0x27A180: a body that never comes back to S7; not copied).
//  - A5: stays while the attack plays; the body back in S7 -> A3.
// The command is empty unless A3 gave one (4.1 0x279390 clears it every frame).
template<class Rng>
AiAsk RobotAiStep(int& ai,Legs& legs,Timer& attack,bool approach,bool target,float horizontal,int body,float speed,Order& order,
                  Rng& rng) noexcept {
    order=Order{};
    switch(ai) {
    case kA3:
        if(!target)return AiAsk::kNone;
        order=RobotLegs(legs,approach,horizontal,speed,rng);
        if(TimerTick(attack,0.0f,rng))ai=kA4;
        return AiAsk::kNone;
    case kA4:
        if(body!=kStand)return AiAsk::kNone;
        ai=kA5;
        return AiAsk::kChooseAttack;
    case kA5:
        if(body==kStand)ai=kA3;
        return AiAsk::kNone;
    default:
        return AiAsk::kNone;
    }
}

// 4.1 0x278810: the move of a command: 0 none (|x| and |z| under 0.5), 1 / 2 sideways (x >= 0 / x < 0) when |x| > |z|,
// else 3 / 4 forward / back (z >= 0 / z < 0).
constexpr int MoveOf(float x,float z) noexcept {
    const float ax=x<0.0f ? -x : x,az=z<0.0f ? -z : z;
    if(ax<0.5f && az<0.5f)return 0;
    if(ax>az)return x<0.0f ? 2 : 1;
    return z<0.0f ? 4 : 3;
}

// 4.1 0x278810's choice in S7. Walks: 0 nl, 1 lr, 2 rl, 3 ln, 4 rn; turns: 0 45, 1 90, 2 180.
enum class Pick { kNone, kForward, kBack, kSideL, kSideR, kTurnL, kTurnR, kAttack };
struct Choice { Pick pick=Pick::kNone; int index=0; int chain=0; };
// footPhase: 0 feet together, 1 the left ahead, 2 the right (0x277F20); chain +0x328 (1 after a backward step); turn
// +0x1614 (-3..3, by the target's angle); attack +0x161C >= 0.
//  - a foot ahead: carry on (lr / rl) when the move goes on the same way, else bring it back (ln / rn) on the chain's side;
//  - feet together: the move's first step (nl, or a side step), then a turn replaces it, else an attack does.
constexpr Choice ChooseInStand(int move,int footPhase,int chain,int turn,bool attack) noexcept {
    if(footPhase==1 || footPhase==2) {
        const int on=footPhase==1 ? 1 : 2,home=footPhase==1 ? 3 : 4;
        if(move==3 && chain==0)return {Pick::kForward,on,0};
        if(move==4 && chain==1)return {Pick::kBack,on,1};
        if(chain==0)return {Pick::kForward,home,0};
        return {Pick::kBack,home,1};
    }
    Choice c{};
    if(move==3)c={Pick::kForward,0,0};
    else if(move==4)c={Pick::kBack,0,1};
    else if(move==1)c={Pick::kSideL,0,0};
    else if(move==2)c={Pick::kSideR,0,0};
    if(turn!=0) {
        const int k=(turn<0 ? -turn : turn)>3 ? 2 : (turn<0 ? -turn : turn)-1;
        return {turn>0 ? Pick::kTurnL : Pick::kTurnR,k,0};
    }
    if(attack)return {Pick::kAttack,0,c.chain};
    return c;
}

// A step's root motion (edf41_robo_motion: keys of x, z, yaw from the clip's start, evenly over `length` clip frames) at
// clip time t, linear between keys.
inline void SampleMotion(const float* keys,int keyCount,float length,float t,float* out) noexcept {
    out[0]=out[1]=out[2]=0.0f;
    if(!keys || keyCount<=0)return;
    const float u=length>0.0f ? (t<0.0f ? 0.0f : t>length ? 1.0f : t/length) : 0.0f;
    const float f=u*static_cast<float>(keyCount-1);
    int a=static_cast<int>(f);
    if(a>keyCount-1)a=keyCount-1;
    const int b=a+1<keyCount ? a+1 : keyCount-1;
    const float w=f-static_cast<float>(a);
    for(int k=0;k<3;++k)out[k]=keys[a*3+k]*(1.0f-w)+keys[b*3+k]*w;
}

}  // namespace crew::edf41robo
