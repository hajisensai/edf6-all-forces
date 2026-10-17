// UfoRobo's robot-form AI (src/edf41robo_ai.h), 4.1's A3 / A4 / A5 and S7 choice, with a body that plays clips of the real
// lengths (UFOROBO401.CAS: steps 400, turns 840 / 1680, attacks 1498..2238 frames) and moves by the steps' root motion.
// The player's report (2026-10-17): "怎么感觉他光攻击但是不动". No game needed.
#include "../src/edf41robo_ai.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

namespace {
namespace rb=crew::edf41robo;

int checks=0;
void Check(bool ok,const char* what) {
    ++checks;
    if(!ok) { std::fprintf(stderr,"FAIL: %s\n",what); std::exit(1); }
}

struct Rng {   // a fixed LCG (4.1's constants), reproducible
    std::uint64_t s=12345;
    std::uint32_t Next() { s=s*0x5D588B656C078965ull+0x269EC3ull; return static_cast<std::uint32_t>(s>>32); }
    int Int(int n) { return n<=0 ? 0 : static_cast<int>((static_cast<std::uint64_t>(Next())*static_cast<std::uint64_t>(n))>>32); }
    float Unit() { return static_cast<float>(Next()>>8)/16777216.0f; }
};

constexpr float kPi=3.14159265f;
constexpr float kSpeed=90.0f;

// A robot on flat ground: pose, the body (S7 / S8 / S9) playing clips by their CAS length, root motion as the plugin does
// it (start pose + start yaw x the clip's curve). Straight-line root motion: forward steps 50 (nl / ln / rn) or 100 (lr / rl),
// backward the same negative, side steps +-80 x, turns 45 / 90 / 180 degrees (the 180 also 150 forward).
struct Sim {
    Rng rng;
    float pos[2]={0.0f,0.0f},yaw=0.0f;   // x z
    float target[2]={0.0f,800.0f};
    bool approach=false;
    int ai=rb::kA3;
    rb::Legs legs;
    rb::Timer attack;
    int body=rb::kStand,bodyFrames=0,clipLength=0,chain=0,footPhase=0;
    float stepPos[2]={},stepYaw=0.0f,stepEnd[3]={};   // the clip's end key (x, z, yaw)
    bool stepMoves=false;
    int steps=0,turns=0,attacks=0,attackFramesInA3=0,attackFramesTotal=0;

    explicit Sim(bool approaching,float s) {
        approach=approaching;
        rb::Reroll(legs,true,approach,kSpeed,rng);
        rb::RerollSide(legs,true,approach,kSpeed,rng);
        attack={rng.Int(static_cast<int>(1800.0f/s)),static_cast<int>(600.0f/s),static_cast<int>(1800.0f/s),0.0f,1000.0f};
    }
    float Horizontal() const { return std::hypot(target[0]-pos[0],target[1]-pos[1]); }
    int Turn() const {   // 4.1 0x2795C0, the robot: by the angle to the target in the yaw frame
        const float dx=target[0]-pos[0],dz=target[1]-pos[1];
        const float c=std::cos(yaw),s=std::sin(yaw);
        const float lx=dx*c-dz*s,lz=dx*s+dz*c;
        const float w=std::atan2(lx,lz);
        return w>2.19911f ? 3 : w<-2.19911f ? -3 : w>1.25664f ? 2 : w<-1.25664f ? -2 : w>0.628319f ? 1 : w<-0.628319f ? -1 : 0;
    }
    void Begin(int state,int length,float ex,float ez,float eyaw) {
        body=state; bodyFrames=0; clipLength=length;
        stepPos[0]=pos[0]; stepPos[1]=pos[1]; stepYaw=yaw;
        stepEnd[0]=ex; stepEnd[1]=ez; stepEnd[2]=eyaw;
        stepMoves=state==rb::kStep;
    }
    void Frame() {
        rb::Order o;
        const rb::AiAsk ask=rb::RobotAiStep(ai,legs,attack,approach,true,Horizontal(),body,kSpeed,o,rng);
        int turn=0;
        if(o.face)turn=Turn();
        if(ask==rb::AiAsk::kChooseAttack) {   // fire_straight fits 0..1500 m all round: the body into its clip at once
            Check(body==rb::kStand && ai==rb::kA5,"A5 starts its attack from S7");
            Begin(rb::kAttack,1702,0.0f,0.0f,0.0f);
            ++attacks;
        }
        if(body==rb::kAttack) {
            ++attackFramesTotal;
            if(ai==rb::kA3)++attackFramesInA3;
        }
        // the body
        ++bodyFrames;
        if(body==rb::kStand) {
            const rb::Choice c=rb::ChooseInStand(rb::MoveOf(o.x,o.z),footPhase,chain,turn,false);
            using rb::Pick;
            const float fwd[5]={50.0f,100.0f,100.0f,50.0f,50.0f};
            const float turnYaw[3]={0.785398f,1.570796f,kPi};
            switch(c.pick) {
            case Pick::kForward: Begin(rb::kStep,400,0.0f,fwd[c.index],0.0f); chain=c.chain; ++steps; break;
            case Pick::kBack: Begin(rb::kStep,400,0.0f,-fwd[c.index],0.0f); chain=c.chain; ++steps; break;
            case Pick::kSideL: Begin(rb::kStep,600,80.0f,0.0f,0.0f); chain=0; ++steps; break;
            case Pick::kSideR: Begin(rb::kStep,600,-80.0f,0.0f,0.0f); chain=0; ++steps; break;
            case Pick::kTurnL: Begin(rb::kStep,c.index==2 ? 1680 : 840,0.0f,c.index==2 ? 150.0f : 0.0f,turnYaw[c.index]); chain=0; ++turns; break;
            case Pick::kTurnR: Begin(rb::kStep,c.index==2 ? 1680 : 840,0.0f,c.index==2 ? 150.0f : 0.0f,-turnYaw[c.index]); chain=0; ++turns; break;
            default: break;
            }
            if(c.pick==Pick::kForward || c.pick==Pick::kBack)
                footPhase=(c.index==0 || c.index==2) ? 1 : (c.index==1 ? 2 : 0);   // nl / rl: left ahead; lr: right ahead; ln / rn: together
        } else if(bodyFrames>2 && bodyFrames>=clipLength) {
            body=rb::kStand; bodyFrames=0;
        }
        if(body==rb::kStep && stepMoves) {   // root motion: the start pose + the start yaw x the curve (linear here)
            const float u=clipLength>0 ? static_cast<float>(bodyFrames>clipLength ? clipLength : bodyFrames)/static_cast<float>(clipLength) : 1.0f;
            const float lx=stepEnd[0]*u,lz=stepEnd[1]*u;
            const float c=std::cos(stepYaw),s=std::sin(stepYaw);
            pos[0]=stepPos[0]+lx*c+lz*s;
            pos[1]=stepPos[1]-lx*s+lz*c;
            yaw=stepYaw+stepEnd[2]*u;
        }
    }
};

}  // namespace

int main() {
    // the timer: 4.1 0x271AB0 fires when the count it had was <= 0, only inside its window
    {
        Rng rng;
        rb::Timer t{2,10,10,0.0f,100.0f};
        Check(!rb::TimerTick(t,50.0f,rng) && !rb::TimerTick(t,50.0f,rng),"timer: counts 2, 1");
        Check(rb::TimerTick(t,50.0f,rng) && t.count==10,"timer: fires at 0, restarts at min + rand(max - min)");
        rb::Timer far{0,10,10,150.0f,800.0f};
        Check(!rb::TimerTick(far,100.0f,rng) && far.count==0,"timer: outside its window it does not count");
    }
    // the move of a command (4.1 raw numbers: cruise +-90, sideways +-22.5, approach +-1)
    static_assert(rb::MoveOf(0.0f,0.0f)==0 && rb::MoveOf(0.4f,-0.4f)==0,"no move under 0.5");
    static_assert(rb::MoveOf(0.0f,90.0f)==3 && rb::MoveOf(0.0f,-90.0f)==4 && rb::MoveOf(0.0f,1.0f)==3,"forward / back");
    static_assert(rb::MoveOf(22.5f,0.0f)==1 && rb::MoveOf(-22.5f,0.0f)==2,"sideways");
    static_assert(rb::MoveOf(22.5f,90.0f)==3 && rb::MoveOf(-22.5f,-90.0f)==4,"the cruise beats the sideways drift");
    // S7's choice
    {
        using rb::Pick;
        Check(rb::ChooseInStand(3,0,0,0,false).pick==Pick::kForward && rb::ChooseInStand(3,0,0,0,false).index==0,"together: walk_f_nl");
        Check(rb::ChooseInStand(4,0,0,0,false).pick==Pick::kBack && rb::ChooseInStand(4,0,0,0,false).chain==1,"together: walk_b_nl, chain 1");
        Check(rb::ChooseInStand(3,0,0,2,false).pick==Pick::kTurnL && rb::ChooseInStand(3,0,0,2,false).index==1,"a turn replaces the step");
        Check(rb::ChooseInStand(0,0,0,-3,false).pick==Pick::kTurnR && rb::ChooseInStand(0,0,0,-3,false).index==2,"turn_r180");
        Check(rb::ChooseInStand(0,0,0,0,true).pick==Pick::kAttack,"no move, no turn: the attack");
        Check(rb::ChooseInStand(0,0,0,0,false).pick==Pick::kNone,"nothing asked: stays");
        Check(rb::ChooseInStand(3,1,0,0,false).index==1 && rb::ChooseInStand(3,2,0,0,false).index==2,"a foot ahead, on: lr / rl");
        Check(rb::ChooseInStand(0,1,0,0,true).pick==Pick::kForward && rb::ChooseInStand(0,1,0,0,true).index==3,"a foot ahead: brought back (ln) before anything");
        Check(rb::ChooseInStand(3,2,1,0,false).pick==Pick::kBack && rb::ChooseInStand(3,2,1,0,false).index==4,"backward chain: walk_b_rn");
    }
    // the legs, approaching: phase 1 walks towards a far target (z +1) and away from a near one (z -1), facing it
    {
        Rng rng;
        rb::Legs l;
        rb::Reroll(l,true,true,kSpeed,rng);
        Check(l.radius>=50.0f && l.radius<=200.0f && l.phase1>=1200 && l.phase1<1800,"approach reroll: radius 50..200, phase 1 1200..1800");
        rb::Order o=rb::RobotLegs(l,true,800.0f,kSpeed,rng);
        Check(o.z==1.0f && o.face && rb::MoveOf(o.x,o.z)==3,"approach: a far target, forward");
        o=rb::RobotLegs(l,true,10.0f,kSpeed,rng);
        Check(o.z==-1.0f && rb::MoveOf(o.x,o.z)==4,"approach: a near target, back");
    }
    // the legs, not approaching: phase 1 faces only, phase 2 cruises
    {
        Rng rng;
        rb::Legs l;
        rb::Reroll(l,true,false,kSpeed,rng);
        l.sideFrames=0; l.sideInterval=0;
        l.phase1=1; l.phase2=5; l.cruise[2]=kSpeed;
        rb::Order o=rb::RobotLegs(l,false,800.0f,kSpeed,rng);
        Check(o.face && o.z==0.0f,"normal phase 1: only faces");
        o=rb::RobotLegs(l,false,800.0f,kSpeed,rng);
        Check(!o.face && o.z==kSpeed,"normal phase 2: the cruise");
    }
    // A5 holds the AI while the attack plays: no A3 (and no attack timer) until the body is back in S7
    {
        Rng rng;
        int state=rb::kA4;
        rb::Legs l;
        rb::Timer t{0,100,200,0.0f,1000.0f};
        rb::Order o;
        Check(rb::RobotAiStep(state,l,t,false,true,800.0f,rb::kStep,kSpeed,o,rng)==rb::AiAsk::kNone && state==rb::kA4,"A4 waits for S7");
        Check(rb::RobotAiStep(state,l,t,false,true,800.0f,rb::kStand,kSpeed,o,rng)==rb::AiAsk::kChooseAttack && state==rb::kA5,"A4 -> A5 in S7");
        for(int i=0;i<3000;++i)rb::RobotAiStep(state,l,t,false,true,800.0f,rb::kAttack,kSpeed,o,rng);
        Check(state==rb::kA5 && t.count==0,"A5 through a whole attack, the timer untouched");
        rb::RobotAiStep(state,l,t,false,true,800.0f,rb::kStand,kSpeed,o,rng);
        Check(state==rb::kA3,"the attack over: A3");
    }
    // the whole robot, 10 minutes, a target 800 m ahead, s 2.35 (M411's level 23.52) and s 2.73 (M010): it walks.
    for(int approaching=0;approaching<2;++approaching)
        for(float s:{2.35f,2.73f}) {
            Sim sim(approaching!=0,s);
            for(int f=0;f<36000;++f)sim.Frame();
            const float moved=std::hypot(sim.pos[0],sim.pos[1]);
            std::printf("edf41robo_ai: %s s %.2f: %d steps, %d turns, %d attacks, %.0f m from the start, %.0f m from the target\n",
                        approaching ? "approaching" : "normal",s,sim.steps,sim.turns,sim.attacks,moved,sim.Horizontal());
            Check(sim.attacks>=5,"it still attacks");
            Check(sim.attackFramesInA3==0 && sim.attackFramesTotal>0,"no attack frame with the AI in A3 (A5 holds it)");
            Check(sim.steps>=4,"it takes steps between the attacks");
            Check(moved>=100.0f,"its steps move it");
            if(approaching)Check(sim.Horizontal()<800.0f-100.0f,"approaching: closer to the target");
        }
    std::printf("edf41robo_ai: %d checks OK\n",checks);
    return 0;
}
