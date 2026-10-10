// The paratroop plane's passes along one straight line (2026-10-10, the user: 「空降时外援飞来的路线不对，既然地图外飞来，
// 直线之类的更合理的路线才对吧」). Pure: jet_flight.cpp Ferry flies it, tests/ferry_line_test.cpp checks it offline.
//
// Before: Ferry chased the drop point (straight at it, then on past it until kFerryRoom turns from it, then at it again).
// Chasing a point is no track: at its turn's radius (564 m) larger than its way to the point it went by 288 m abeam, and
// the 2.2 turns of room (1240 m) it waited for before turning back were more than a stock map holds, so it flew 2 minutes
// round the north-east corner into buildings (2026-10-10 13:17:32-13:19:20 log); and the soft edge, 600 m inside the
// area the entry was planned on, turned it back the moment it was made (13:17:21 `past the soft edge ... back in first`).
//
// Now: the line (support_entry.h PassLine) runs from off the map through the drop point to off the map on the other side,
// planned with the entry (AirRoute's `turn`). The plane follows the line, not the point: it aims at the point `lead` m
// ahead of its own place along the line (the cross-track error closes over about that), so it crosses the drop point on
// the line, whatever its turn's radius. Past an end (off the map) it turns round (always to its right, a racetrack
// turn: never a coin toss between two equal ways round) and flies the same line back: a pass each way until its stick
// is out (Pass::done); then past the end it is flying to it is gone (the caller deletes it there, off the map).
#pragma once
#include "support_entry.h"
#include <cmath>

namespace crew::ferry {
constexpr float kLeadTurns=1.5f,kLeadLeast=250.0f;   // the lead: this many of its turn's radii, at least this many m
constexpr float kBehind=-0.5f;   // the aim this far behind its way (cosine): it turns round, to its right
inline float Lead(float turn) noexcept { const float l=turn*kLeadTurns;return l>kLeadLeast ? l : kLeadLeast; }

// dir: +1 along the line's dir, -1 the other way; done: its stick is out, the end it flies to is its last.
struct Pass { int dir=1; bool done=false; };

// m along the line from its point, and to its right of it ((-dir.z, dir.x) of the line's dir).
inline void Along(const support::PassLine& l,const float* pos,float* along,float* across) noexcept {
    const float dx=pos[0]-l.at[0],dz=pos[2]-l.at[2];
    *along=dx*l.dir[0]+dz*l.dir[2];
    *across=-dx*l.dir[2]+dz*l.dir[0];
}

// The way to fly (`way`: x, z unit, y 0) for a plane at `pos` flying `vel`. True: its last pass is over (past the end of
// its pass with its stick out): the caller deletes it.
inline bool Steer(const support::PassLine& l,Pass& p,const float* pos,const float* vel,float lead,float* way) noexcept {
    float s,x;Along(l,pos,&s,&x);
    if(p.dir>0 ? s>=l.hi : s<=l.lo) {
        if(p.done)return true;
        p.dir=-p.dir;
    }
    const float aim=s+static_cast<float>(p.dir)*lead;
    float w[2]={l.at[0]+l.dir[0]*aim-pos[0],l.at[2]+l.dir[2]*aim-pos[2]};
    float n=std::sqrt(w[0]*w[0]+w[1]*w[1]);
    if(n<1e-3f){w[0]=l.dir[0]*static_cast<float>(p.dir);w[1]=l.dir[2]*static_cast<float>(p.dir);n=1.0f;}
    w[0]/=n;w[1]/=n;
    // Turning round: the aim behind it. Round to its right (a racetrack turn), not whichever way the steering's shortest
    // arc happens to say each frame.
    const float sp=std::sqrt(vel[0]*vel[0]+vel[2]*vel[2]);
    if(sp>1.0f && (w[0]*vel[0]+w[1]*vel[2])/sp<kBehind){w[0]=-vel[2]/sp;w[1]=vel[0]/sp;}
    way[0]=w[0];way[1]=0.0f;way[2]=w[1];
    return false;
}
}  // namespace crew::ferry
