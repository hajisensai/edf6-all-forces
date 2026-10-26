// What a support's warning on the ground looks like (the user, 2026-10-26: 「支援要有攻击警告红圈，直升机等没有红圈的可以给出路
// 线预览图之类的」): pure geometry, no game (tests/support_warning_test.cpp; support_warning.cpp puts the outlines on the ground
// and keeps them, hud.cpp draws them on the view and on the map).
//  - a strike (the barrages, the ultimate, a sortie that bombs a place): its outline red, the area its rounds can reach
//    (where they are aimed and their blast round it);
//  - a flown or driven support: its route cyan (where it comes in, the point, and a sortie's pass past it), a guard's ring
//    round the point where it will circle.
#pragma once
#include "barrage_plan.h"
#include "sortie_plan.h"
#include "ultimate_plan.h"
#include <cmath>

namespace crew::warning {

constexpr float kPi=3.14159265358979f;
struct Circle { float centre[3]{}; float radius=0.0f; };

// The area a barrage of `kind` at `target` along `heading` reaches: the circle round every round's point (the planned
// rounds on flat ground at the target's height: barrage_plan.h Plan; the guided rockets with no enemy to aim at spread
// round the mark as they do then, kGuidedReach) widened by each round's blast. Centred on the middle of their points' box
// (a creeping barrage walks ahead of the mark). False: no rounds (an unknown kind).
inline bool BarrageArea(int kind,const float* target,const float* heading,Circle* out) noexcept {
    if(!out || kind<0 || kind>=barrage::kKinds)return false;
    barrage::Setup s;
    for(int i=0;i<3;++i){s.target[i]=target[i];s.heading[i]=heading[i];}
    s.seed=barrage::Seed(target,heading,kind);
    s.worldHalf=1.0e6f;   // the launchers' site does not move the points
    static thread_local barrage::Round rounds[barrage::kMaxRounds];
    const float y=target[1];
    const int n=barrage::Plan(static_cast<barrage::Kind>(kind),s,[y](float,float,float){return y;},rounds,barrage::kMaxRounds);
    if(n<=0)return false;
    float lo[2]={rounds[0].to[0],rounds[0].to[2]},hi[2]={lo[0],lo[1]};
    for(int i=1;i<n;++i) {
        lo[0]=std::fmin(lo[0],rounds[i].to[0]);lo[1]=std::fmin(lo[1],rounds[i].to[2]);
        hi[0]=std::fmax(hi[0],rounds[i].to[0]);hi[1]=std::fmax(hi[1],rounds[i].to[2]);
    }
    Circle c;
    c.centre[0]=(lo[0]+hi[0])*0.5f;c.centre[1]=y;c.centre[2]=(lo[1]+hi[1])*0.5f;
    for(int i=0;i<n;++i) {
        const float dx=rounds[i].to[0]-c.centre[0],dz=rounds[i].to[2]-c.centre[2];
        c.radius=std::fmax(c.radius,std::sqrt(dx*dx+dz*dz)+rounds[i].blast);
    }
    *out=c;
    return true;
}

// The ultimate's: everything inside its storm and the main beam's rings, the outermost ring's blast too (ultimate_plan.h).
constexpr float UltimateRadius() noexcept { return ultimate::kRadius+ultimate::kRingBlast; }

// `n` points round `c` on its plane (the first at +x, counter-clockwise seen from above).
inline void Ring(const Circle& c,int n,float (*out)[3]) noexcept {
    for(int i=0;i<n;++i) {
        const float a=2.0f*kPi*static_cast<float>(i)/static_cast<float>(n);
        out[i][0]=c.centre[0]+c.radius*std::cos(a);out[i][1]=c.centre[1];out[i][2]=c.centre[2]+c.radius*std::sin(a);
    }
}

// A flown support's route: from where it comes in (`from`, in the air) to the point, and for a sortie on along its pass
// (sortie_plan.h LineThrough: the line through the mark along the heading it comes in on, moved across to its side) to
// where it leaves: `beyond` m past the mark (sortie::kPassBeyond for a jet's pass, kHeliPassBeyond for a helicopter's
// strafe; 0: none). Its points into `out` (at most 4), how many.
inline int Route(const float* from,const float* mark,float beyond,float (*out)[3]) noexcept {
    for(int i=0;i<3;++i)out[0][i]=from[i];
    if(beyond<=0.0f) {
        for(int i=0;i<3;++i)out[1][i]=mark[i];
        return 2;
    }
    const float heading[3]={mark[0]-from[0],0.0f,mark[2]-from[2]};
    const sortie::Line l=sortie::LineThrough(mark,from,heading);
    for(int i=0;i<3;++i){out[1][i]=l.at[i];out[2][i]=l.at[i]+l.dir[i]*beyond;}
    out[1][1]=out[2][1]=mark[1];
    return 3;
}

// The carpet bombers' strip (sortie_plan.h: their bombs land within kCarpetHalf of the mark along the line, three jets
// abreast up to kMaxOffset to a side), widened by `blast` all round: its four corners on the mark's plane, in order.
inline void CarpetStrip(const float* from,const float* mark,float blast,float (*out)[3]) noexcept {
    const float heading[3]={mark[0]-from[0],0.0f,mark[2]-from[2]};
    const sortie::Line l=sortie::LineThrough(mark,mark,heading);   // centred on the mark: the formation spans both sides
    const float along=sortie::kCarpetHalf+blast,across=sortie::kMaxOffset+blast;
    const float sa[4]={-1.0f,1.0f,1.0f,-1.0f},sc[4]={-1.0f,-1.0f,1.0f,1.0f};
    for(int k=0;k<4;++k)for(int i=0;i<3;++i)out[k][i]=mark[i]+l.dir[i]*along*sa[k]+l.side[i]*across*sc[k];
    for(int k=0;k<4;++k)out[k][1]=mark[1];
}

// How long a warning shows (ms): a barrage until its last round is down (barrage_plan.h EndFrame, frames at 60 a second),
// at least a moment.
inline unsigned long long BarrageMs(int endFrame) noexcept {
    const int f=endFrame>60 ? endFrame : 60;
    return static_cast<unsigned long long>(f)*1000ull/static_cast<unsigned long long>(barrage::kFps);
}

}  // namespace crew::warning
