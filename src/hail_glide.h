// The hail's approach of a wing (playerjet_board.inc Approach / SteerAt): where the ferry leg flies and how steep
// any leg may descend. Pure, so tools/hail_glide_check.cpp flies it offline against the logged crashes.
//
// 2026-10-09, the user: "呼叫npc降落，直接撞地上爆炸了". Both hails in the log crashed before the final:
//  - 10:34 fighter: climbed to 680 m, reached the final's entry heading the wrong way (east, the strip faces west) at
//    250 m, and the reversal it then flew with the aim on the entry point went in at 110 m/s sink, banked;
//  - 11:24 fighter: hailed in an inverted dive at 590 m over the touchdown point, the entry 1.5 km behind it: the
//    reversal toward a goal 480 m lower hit the ground at 50 m/s sink.
// Causes: (1) the ferry aimed straight at the entry (its lead shrank to nothing as it closed), so it arrived there
// misaligned with no room to turn and turned at the final's height; (2) the aim had no descent limit and no ground
// margin but a 30 m floor clamp of the aim's vertical (kCatchFloor), far too late for 50-110 m/s of sink.
// So: the ferry first flies to a point kTurnRoom out behind the entry, then inbound along the final's line; every leg
// descends at most kFerryDescent / kFinalDescent, keeps kFloor over the ground under it while ferrying (sinking no
// faster than its clearance allows within kReact s), and a sink that would breach that floor within kReact s flies
// wings level up first (the turn waits).
#pragma once
#include "pjet_handling.h"
#include <cmath>

namespace crew::hail {
constexpr float kTurnRoom=1200.0f;      // m: the outer point behind the final's entry (a 116 m/s turn at 2 g is ~700 m across)
constexpr float kInboundAlign=0.7f;     // cosine: a track this close to the final's line is inbound already
constexpr float kInboundLead=450.0f;    // m: the inbound leg aims this far past the entry along the line
constexpr float kFerryDescent=0.14f;    // the steepest slope the ferry descends (~8 deg)
constexpr float kFinalDescent=0.12f;    // the steepest on the final (its glide is 0.07)
constexpr float kFinalSink=0.08f;       // on the final the descent may always be this slope (the glide, landing)
constexpr float kFloor=60.0f;           // m over the ground under it while ferrying
constexpr float kReact=5.0f;            // s: the time to the floor its sink must leave
constexpr float kRecoverClimb=0.20f;    // the slope a recovery asks, wings level
constexpr float kLevelTurn=0.5f;        // cosine: a goal further off the track than this (60 deg) is turned to level

inline float FlatLen(float x,float z) noexcept { return std::sqrt(x*x+z*z); }

// The ferry leg's goal: the outer point behind the entry until near it or already lined up, then the final's line a lead
// ahead of its own place along it (pursuit onto the line), both at the entry's height.
// `d` the strip's unit heading; `radius` its level turn's radius (TurnRadius): the outer point lies kOuterTurns of it
// (at least kTurnRoom) behind the entry and the lead is kLeadTurns of it (at least kInboundLead), so the pursuit
// settles on the line instead of circling it. `inbound` is the leg's state (playerjet_board.inc keeps it as the phase
// kHailInbound): once inbound it stays so until it has passed the entry (then round to the outer point again); without
// it a wide turn left the inbound zone and swapped goals for ever.
constexpr float kOuterTurns=4.0f,kLeadTurns=1.5f,kNearTurns=1.5f,kTurnedOnto=0.3f;

// Whether the approach fits the map (PlanPattern). 2026-10-09 16:53 a gunship hailed on a map whose walls stand at
// +-1600 m flew for its outer point 1500 m (the final) + 4 turn radii (2.7 km) behind the touchdown, far past the wall:
// the wall turned it back, it flew at the point again, 4 minutes along the wall at z -1600 until the hail ran out
// ("一直在降落"). The strip search never asked whether its approach fits the map. Now it must, within `box` (x / z): the
// final's entry kFinalLength out half a turn radius inside, the outer point (as FerryGoal places it, at the ferry's
// turn radius `radius`) a whole turn radius inside (it turns about it). `room`: the furthest the outer point may stand
// behind the entry inside the box (FerryGoal never sets it further back, however fast it flies).
struct Box { float lo[2],hi[2]; };
struct Pattern { float room=0.0f; bool ok=false; };
constexpr float kOuterMargin=2.0f;      // turn radii the outer point stands inside the box: it circles it, overshooting
constexpr float kFinalLength=1500.0f;   // playerjet_board.inc's final (its glide from 105 m)
inline bool InBox(const Box& b,float x,float z,float margin) noexcept {
    return x>=b.lo[0]+margin && x<=b.hi[0]-margin && z>=b.lo[1]+margin && z<=b.hi[1]-margin;
}
inline Pattern PlanPattern(const float* touch,const float* d,float radius,const Box& box) noexcept {
    Pattern p;
    const float entry[2]={touch[0]-d[0]*kFinalLength,touch[2]-d[2]*kFinalLength};
    if(!InBox(box,entry[0],entry[1],0.5f*radius))return p;
    // How far back from the entry along -d the box holds a point `radius` inside it.
    float most=1e9f;
    for(int a=0;a<2;++a) {
        const float back=-(a ? d[2] : d[0]),at=entry[a],lo=box.lo[a]+kOuterMargin*radius,hi=box.hi[a]-kOuterMargin*radius;
        if(back>1e-4f)most=std::fmin(most,(hi-at)/back);
        else if(back< -1e-4f)most=std::fmin(most,(lo-at)/back);
    }
    const float room=std::fmax(kTurnRoom,kOuterTurns*radius);
    if(most<room)return p;
    p.room=most;p.ok=true;return p;
}

// `roomMost`: the pattern's room (PlanPattern): the outer point never stands further back than that.
inline void FerryGoal(const float* pos,const float* track,const float* entry,const float* d,float roomMost,float radius,
                      bool& inbound,float* goal) noexcept {
    const float room=std::fmin(std::fmax(kTurnRoom,kOuterTurns*radius),roomMost),lead=std::fmax(kInboundLead,kLeadTurns*radius);
    const float outer[3]={entry[0]-d[0]*room,entry[1],entry[2]-d[2]*room};
    const float toOuter=FlatLen(pos[0]-outer[0],pos[2]-outer[2]);
    const float along=(pos[0]-entry[0])*d[0]+(pos[2]-entry[2])*d[2];   // < 0: before the entry
    const float tl=FlatLen(track[0],track[2]);
    const float align=tl>1e-3f ? (track[0]*d[0]+track[2]*d[2])/tl : 0.0f;
    // Inbound once heading along the line, or turned at least partly onto it near the outer point (it gets there
    // heading away from the strip and turns about it: pursuing the line before that turn crossed it steeply).
    if(!inbound && along< -radius && (align>kInboundAlign || (toOuter<kNearTurns*radius && align>kTurnedOnto)))inbound=true;
    else if(inbound && along>0.0f)inbound=false;
    const float ahead=along+lead;   // past the entry too: the final takes over within kFinalOn of it, lined up
    if(inbound){goal[0]=entry[0]+d[0]*ahead;goal[1]=entry[1];goal[2]=entry[2]+d[2]*ahead;}
    else{goal[0]=outer[0];goal[1]=outer[1];goal[2]=outer[2];}
}
// Whether a wing at `pos` by the final's entry (its glide point) is well inside the missed approach's bands (`high`
// over, `low` under the slope, `side` off its line, `d` the strip's heading): the final may begin. Outside them the
// final was a missed approach the next frame, three in three frames, and the hail was handed back.
inline bool FinalReady(const float* pos,const float* entry,const float* d,float high,float low,float side) noexcept {
    const float off=std::fabs((pos[0]-entry[0])*d[2]-(pos[2]-entry[2])*d[0]);
    return pos[1]<=entry[1]+0.5f*high && pos[1]>=entry[1]-0.5f*low && off<=0.5f*side;
}
// The radius (m) of a level turn at `speed` m/s with `lateral` m/s^2 of sideways lift (LevelTurnLateral).
inline float TurnRadius(float speed,float lateral) noexcept { return lateral>0.1f ? speed*speed/lateral : 5000.0f; }

// Limits the aim `to` (any length, toward the goal) for a wing at velocity `vel`, `clear` m over the ground under it
// (`known`: false when there is no ground under it). `final`: on the glide (no floor, the touchdown allowed).
// True when it is recovering (wings level, climbing).
inline bool LimitAim(float* to,const float* vel,float clear,bool known,bool final) noexcept {
    const float speed=std::sqrt(vel[0]*vel[0]+vel[1]*vel[1]+vel[2]*vel[2]);
    const float sink=-vel[1];
    float h=FlatLen(to[0],to[2]);
    float hx,hz;
    if(h>1e-3f){hx=to[0]/h;hz=to[2]/h;}
    else {
        const float t=FlatLen(vel[0],vel[2]);
        hx=t>1e-3f ? vel[0]/t : 0.0f;hz=t>1e-3f ? vel[2]/t : 1.0f;h=1.0f;
    }
    float slope=to[1]/h;
    const float floor=final ? 0.0f : kFloor;
    if(known && !final) {
        const float margin=clear-floor;
        if(margin<0.0f || (sink>0.0f && margin<sink*kReact)) {
            // Out of height for its sink: level the wings along its own track and climb; the turn comes after.
            const float t=FlatLen(vel[0],vel[2]);
            if(t>1e-3f){hx=vel[0]/t;hz=vel[2]/t;}
            to[0]=hx;to[1]=kRecoverClimb;to[2]=hz;
            return true;
        }
    }
    // A turn of more than kLevelTurn off its track (a reversal onto the final's line) is flown level: the aim's law banks
    // hard for it, and a banked descent is what sank both logged hails.
    if(!final) {
        const float t=FlatLen(vel[0],vel[2]);
        if(t>1e-3f && (vel[0]*hx+vel[2]*hz)/t<kLevelTurn && slope<0.0f)slope=0.0f;
    }
    float most=final ? kFinalDescent : kFerryDescent;
    if(known && speed>1.0f) {
        const float allowed=(clear-floor)/kReact/speed;   // the slope that reaches the floor in kReact s
        most=std::fmin(most,std::fmax(final ? kFinalSink : 0.0f,allowed));
    }
    if(slope< -most)slope=-most;
    to[0]=hx;to[1]=slope;to[2]=hz;
    return false;
}
// Limits how far off its track (in the horizontal) the aim may point, so the turn the aim's law asks (AimSteer:
// steer x AimShare(off) x off x speed of sideways lift) is one the wing holds level: `lateralMost` m/s^2, what its
// lift gives over its weight (SteerAt: kTurnLift of its most). Aimed at a goal behind, a 2 g gunship otherwise banks
// past 80 deg and dives (the sweep in tools/hail_glide_check.cpp). Keeps the aim's slope.
inline void LimitTurn(float* to,const float* vel,float lateralMost,float steer) noexcept {
    const float t=FlatLen(vel[0],vel[2]),h=FlatLen(to[0],to[2]);
    if(t<1e-3f || h<1e-3f)return;
    const float tx=vel[0]/t,tz=vel[2]/t,gx=to[0]/h,gz=to[2]/h;
    const float off=std::atan2(tx*gz-tz*gx,tx*gx+tz*gz);   // signed horizontal angle from the track to the goal
    const float speed=std::sqrt(vel[0]*vel[0]+vel[1]*vel[1]+vel[2]*vel[2]);
    float lo=0.0f,hi=3.14159265f;
    for(int i=0;i<24;++i){const float m=0.5f*(lo+hi);(steer*handling::AimShare(m)*m*speed<=lateralMost ? lo : hi)=m;}
    if(std::fabs(off)<=lo)return;
    const float a=off>0.0f ? lo : -lo,c=std::cos(a),sn=std::sin(a);
    const float rx=tx*c-tz*sn,rz=tx*sn+tz*c;   // the track turned by a toward the goal's side
    to[0]=rx*h;to[2]=rz*h;
}
// The sideways lift a wing may be asked for in a level turn: kTurnLift of its `most` (m/s^2) over 1 g.
constexpr float kTurnLift=0.8f;
inline float LevelTurnLateral(float most) noexcept {
    const float lift=kTurnLift*most,g=handling::kG;
    return lift>g ? std::sqrt(lift*lift-g*g) : 0.0f;
}
}  // namespace crew::hail
