// A flight's aircraft kept apart (the player, 2026-11-27, of the wolf pack: 「不是让他到一个交点。。。他到那个交点意义是
//什么，互相撞吗」): a call's jets were each given the same mark and flew it as if alone -- the marked passes on lines only
// as far apart as they came in abreast, clamped to kMaxOffset (sortie_plan.h LineThrough: the 70 m formation's third and
// fourth slots, +70 and +140 m, both on lines 70 and 80 m out, 10 m apart), the unmarked gun run each at the nearest
// target to it (two pairs on two targets, each pair closing from 70 m to 27 m before the dive). The pure part here
// (tools/flight_spread_check.cpp flies it offline); jet_sortie.cpp SortieBegin lays the lanes, jet_combat.cpp PickTarget
// the claims, jet.cpp FlightSeparation the separation, jet_flight.cpp Away the break.
//   Place     an aircraft's slot in its call's formation (support_entry.h AirFormationSlot: 0 the lead, then alternately
//             right and left, each pair farther out) and how many there are; the host's plan decides it, the owner flies
//             it (the aircraft are the owner's; every other machine sees the game's replication of them: no message).
//   Lanes     a flight's passes side by side across its line, in the order they fly abreast (no two cross on the way
//             to them): lane k of n is (k - (n - 1) / 2) LaneGap across. A gun run's four lanes 90 m apart strafe a band
//             270 m wide centred on the line drawn; a carpet's bands are 120 m apart (parallel carpets); one aircraft
//             flies the line itself.
//   Break     after its pass each turns out by its lane (kBreakStep a lane, the outer ones farthest): a fan, not four
//             abreast out along one heading.
//   Claims    a target a flight-mate is on counts kClaimed m farther: each takes another while there are others, the
//             same one only when there is no other (the interceptors' air targets, a gun run's ground ones alike).
//   Separate  every frame, before the ground guard (jet_flight.cpp Wing: Guard), each wing turns off the closest point
//             of approach of each flight-mate inside kSepLook within kSepHorizon s that would come nearer than kSepWant
//             (harder the nearer and sooner; the higher wing gives way most, so two never dodge into each other), and
//             slows behind one it is closing on: kSepMin is never broken (the check's whole-flight minimum).
#pragma once
#include "sortie_plan.h"
#include <cmath>

namespace crew {
namespace spread {

// offset: m from the lead's planned place to its own (level): the flight's centre, where its lead comes in, is where it is
// less this.
struct Place { int slot=0,count=1; float offset[3]={0.0f,0.0f,0.0f}; };

// m across the heading of formation slot `slot` (support_entry.h AirFormationSlot's order, in its units).
inline float SlotSide(int slot) noexcept {
    if(slot<=0)return 0.0f;
    const int pair=(slot+1)/2;
    return (slot%2 ? 1.0f : -1.0f)*static_cast<float>(pair);
}
// Its place abreast counted from the left (the side's most negative): 0..count-1.
inline int Rank(const Place& p) noexcept {
    if(p.count<=1 || p.slot<0 || p.slot>=p.count)return 0;
    const float mine=SlotSide(p.slot);
    int rank=0;
    for(int i=0;i<p.count;++i)if(i!=p.slot && SlotSide(i)<mine)++rank;
    return rank;
}
// The lane's offset in lanes from the flight's centre (n of them: -(n-1)/2 .. (n-1)/2).
inline float LaneUnits(const Place& p) noexcept {
    if(p.count<=1)return 0.0f;
    return static_cast<float>(Rank(p))-0.5f*static_cast<float>(p.count-1);
}

// m between neighbouring lanes, by the pass (its stores' swath: a gun run strafes, a carpet lays bands).
constexpr float kLaneGun=90.0f,kLaneCarpet=120.0f,kLaneMissile=120.0f,kLaneOther=100.0f;
constexpr float kBreakStep=0.26f;   // rad a lane its way out turns from the line (15 deg)
constexpr int kPassGunRun=1,kPassMissilePass=2,kPassCarpet=3,kPassGunshipLap=6;   // jet.h SortiePass's values
inline float LaneGap(int pass) noexcept {
    return pass==kPassGunRun ? kLaneGun : pass==kPassCarpet ? kLaneCarpet : pass==kPassMissilePass ? kLaneMissile : kLaneOther;
}
// Its lane's line: the flight's line (LineThrough from the flight's centre: along `heading`, none from there toward the
// mark, across as far as the centre comes in, kMaxOffset at most) moved across by its lane. One aircraft's is its own
// LineThrough. The gunship's lap is round the mark itself, never a lane.
inline sortie::Line LaneLine(const float* point,const float* from,const float* heading,int pass,const Place& p) noexcept {
    const float centre[3]={from[0]-p.offset[0],from[1],from[2]-p.offset[2]};
    sortie::Line l=sortie::LineThrough(point,centre,heading);
    const float across=pass==kPassGunshipLap ? 0.0f : LaneUnits(p)*LaneGap(pass);
    for(int i=0;i<3;++i)l.at[i]+=l.side[i]*across;
    return l;
}
// Its way out (unit, level): the line's way turned toward its lane's side by kBreakStep a lane.
inline void BreakAway(const sortie::Line& l,const Place& p,float* out) noexcept {
    const float a=LaneUnits(p)*kBreakStep,c=std::cos(a),s=std::sin(a);
    out[0]=l.dir[0]*c+l.side[0]*s;out[1]=0.0f;out[2]=l.dir[2]*c+l.side[2]*s;
    const float n=std::sqrt(out[0]*out[0]+out[2]*out[2]);
    if(n>1e-4f){out[0]/=n;out[2]/=n;}
}

// The claims: what a target counts on top of its distance with `mates` flight-mates on it already.
constexpr float kClaimed=1500.0f;
inline float ClaimScore(int mates) noexcept { return mates>0 ? kClaimed*static_cast<float>(mates) : 0.0f; }

// The separation.
constexpr float kSepMin=50.0f;        // m: no two of a flight nearer (the check's floor; the formation flies 70 m abreast)
constexpr float kSepWant=68.0f;       // m: a closest approach nearer than this is turned off
constexpr float kSepLook=900.0f;      // m: mates farther than this are let be
constexpr float kSepHorizon=4.0f;     // s ahead the closest approach is looked for
constexpr float kSepGain=2.5f;        // its push against the unit `want` at full urgency
constexpr float kSepSlow=0.85f;       // the share of its speed it holds behind a mate it closes on at full urgency
constexpr float kSepVertical=0.5f;    // the push's up / down share (a turn apart first: the ground is below)
struct Mate { float pos[3],vel[3]; int wing; };

// `want` (unit) turned off every mate's closest approach, `speedShare` (1: as it was) its speed's share. True when any
// mate pushed it.
inline bool Separate(const float* pos,const float* vel,int wing,const Mate* mates,int count,float* want,float* speedShare) noexcept {
    float push[3]={0.0f,0.0f,0.0f};
    float slow=1.0f;
    bool any=false;
    const float speed=std::sqrt(vel[0]*vel[0]+vel[1]*vel[1]+vel[2]*vel[2]);
    for(int m=0;m<count;++m) {
        const Mate& o=mates[m];
        const float r[3]={o.pos[0]-pos[0],o.pos[1]-pos[1],o.pos[2]-pos[2]};
        const float d=std::sqrt(r[0]*r[0]+r[1]*r[1]+r[2]*r[2]);
        if(!(d<kSepLook))continue;
        const float rv[3]={o.vel[0]-vel[0],o.vel[1]-vel[1],o.vel[2]-vel[2]};
        const float vv=rv[0]*rv[0]+rv[1]*rv[1]+rv[2]*rv[2];
        float t=vv>1e-3f ? -(r[0]*rv[0]+r[1]*rv[1]+r[2]*rv[2])/vv : 0.0f;
        if(t<0.0f)t=0.0f;
        if(t>kSepHorizon)t=kSepHorizon;
        float c[3]={r[0]+rv[0]*t,r[1]+rv[1]*t,r[2]+rv[2]*t};   // the mate from it at the closest approach
        float dc=std::sqrt(c[0]*c[0]+c[1]*c[1]+c[2]*c[2]);
        if(d<dc){c[0]=r[0];c[1]=r[1];c[2]=r[2];dc=d;}           // nearer now than then (opening): now's
        if(!(dc<kSepWant))continue;
        // Away from it there; met dead on (no side to it), off to the side its wing takes (the lower wing left).
        float away[3]={-c[0],-c[1]*kSepVertical,-c[2]};
        float n=std::sqrt(away[0]*away[0]+away[1]*away[1]+away[2]*away[2]);
        if(n<1.0f) {
            const float side=wing<o.wing ? -1.0f : 1.0f;
            const float vx=speed>1.0f ? vel[0]/speed : 0.0f,vz=speed>1.0f ? vel[2]/speed : 1.0f;
            away[0]=vz*side;away[1]=0.0f;away[2]=-vx*side;n=1.0f;
        }
        for(int i=0;i<3;++i)away[i]/=n;
        const float closeness=(kSepWant-dc)/(kSepWant-kSepMin*0.5f);
        const float soon=1.0f-0.5f*t/kSepHorizon;
        float urgency=(closeness>1.0f ? 1.0f+(closeness-1.0f)*2.0f : closeness)*soon;
        const float share=wing>o.wing ? 1.0f : 0.5f;   // the higher wing gives way most
        urgency*=share;
        for(int i=0;i<3;++i)push[i]+=away[i]*urgency*kSepGain;
        // Behind it and closing (it ahead along its path): ease off.
        const float ahead=speed>1.0f ? (r[0]*vel[0]+r[1]*vel[1]+r[2]*vel[2])/speed : 0.0f;
        const float closing=d>1.0f ? -(r[0]*rv[0]+r[1]*rv[1]+r[2]*rv[2])/d : 0.0f;
        if(ahead>0.0f && closing>0.0f) {
            const float u=urgency>1.0f ? 1.0f : urgency;
            const float s=1.0f-(1.0f-kSepSlow)*u;
            if(s<slow)slow=s;
        }
        any=true;
    }
    if(!any){*speedShare=1.0f;return false;}
    float w[3]={want[0]+push[0],want[1]+push[1],want[2]+push[2]};
    const float n=std::sqrt(w[0]*w[0]+w[1]*w[1]+w[2]*w[2]);
    if(n>1e-4f)for(int i=0;i<3;++i)want[i]=w[i]/n;
    *speedShare=slow;
    return true;
}

}  // namespace spread
}  // namespace crew
