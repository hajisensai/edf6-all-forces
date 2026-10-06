// The flyers' soft edge (the user, 2026-10-06: "npc应该遇到边界的前面有一个小边界，过了这个小边界会往回走，并且一般情况下
// 不会穿过小边界"). Pure logic, no game state: jet_flight.cpp (Guard, Hover), heli.cpp (Steer) and jet.cpp (the anchor a
// jet works round) call it with their own edges; tools/airbound_check.cpp and tools/jet_obstacle_sim.cpp --edge-suite
// test it offline.
//
// The hard edge is the square every flyer must stay inside (crew.h PlayEdge for the jets; for the helis the game's own
// move area as well, which clamps them). The soft edge is that square shrunk by a band (Band): at least the ini's width,
// at least AirSoftTurns full-speed turn diameters of the kind, but never so wide that the soft box is narrower than
// kRoomTurns of its turn's radius each side (a stock map's 2400 m does not hold a 245 m/s fighter's 2450 m turn plus a
// band of as much). Inside the soft edge a wing is turned in early enough that its turn back ends on the soft line
// (Excursion: the way it still goes out while turning at its g, plus the roll before the turn; Margin: in a corner, the
// whole circle of its turn either way); once past it, it flies back in first and keeps flying in until kBackDepth inside
// (KeepIn's `back`). A
// rotor's wanted velocity outward is cut to what it can stop from before the line (LimitOut), and its goals are put
// inside it (ClampIn).
#pragma once
#include <cmath>

namespace crew {
namespace airbound {
constexpr float kGrav=9.8f;
constexpr float kRoomTurns=2.0f;    // the soft box keeps at least this many turn radii each side of its middle
constexpr float kSlack=30.0f;       // m: a wing starts its turn back this much before the turn's end would meet the line
constexpr float kEase=2.0f;         // from kEase times the turn's reach in, its want outward is eased off toward none
constexpr float kTurnIn=0.5f;       // in its turn's reach: its want points in by this (sine) at least: a hard turn in
constexpr float kBackIn=0.7f;       // past the soft line: in by this at least, nearly level (kBackLevel)
constexpr float kBackLevel=0.2f;
constexpr float kBackDepth=150.0f;  // m: past the line it flies in until this far inside again

// A horizontal box: x in [lo[0], hi[0]], z in [lo[1], hi[1]].
struct Box { float lo[2],hi[2]; };

inline Box Square(float half) noexcept { return Box{{-half,-half},{half,half}}; }
// `a` and `b` overlapped (an empty overlap collapses on its middle).
inline Box Overlap(const Box& a,const Box& b) noexcept {
    Box o{};
    for(int i=0;i<2;++i) {
        o.lo[i]=a.lo[i]>b.lo[i] ? a.lo[i] : b.lo[i];
        o.hi[i]=a.hi[i]<b.hi[i] ? a.hi[i] : b.hi[i];
        if(o.lo[i]>o.hi[i])o.lo[i]=o.hi[i]=(o.lo[i]+o.hi[i])*0.5f;
    }
    return o;
}
// `b` shrunk by `by` on every side (a side that would cross its opposite stops at the middle).
inline Box Inset(const Box& b,float by) noexcept {
    Box o{};
    for(int i=0;i<2;++i) {
        const float mid=(b.lo[i]+b.hi[i])*0.5f;
        o.lo[i]=b.lo[i]+by<mid ? b.lo[i]+by : mid;
        o.hi[i]=b.hi[i]-by>mid ? b.hi[i]-by : mid;
    }
    return o;
}
inline float HalfOf(const Box& b) noexcept {
    const float x=(b.hi[0]-b.lo[0])*0.5f,z=(b.hi[1]-b.lo[1])*0.5f;
    return x<z ? x : z;
}

// m: a turn's radius at `speed` pulling `g` g.
inline float TurnRadius(float speed,float g) noexcept { return speed*speed/((g>0.5f ? g : 0.5f)*kGrav); }
// m: the band's width for a flyer whose full-speed turn has radius `radius` (`turns` diameters of it), at least
// `least`, at most what leaves kRoomTurns radii inside `half` (the hard edge's half size), never below 0.
inline float Band(float radius,float turns,float least,float half) noexcept {
    float b=2.0f*radius*turns;
    if(b<least)b=least;
    // A box too small for that (a stock map's 2400 m against a fighter's 1300 m turn) keeps the ini's width, as long as
    // a radius of room stays each side.
    float most=half-kRoomTurns*radius;
    if(most<least){const float tight=half-radius;most=least<tight ? least : tight;}
    if(b>most)b=most;
    return b>0.0f ? b : 0.0f;
}

// The four sides: unit outward normals (x, z) and how far inside side `i` point `p` (x, y, z) is (negative: past it).
constexpr float kNormal[4][2]={{1.0f,0.0f},{-1.0f,0.0f},{0.0f,1.0f},{0.0f,-1.0f}};
inline float Gap(const Box& b,const float* p,int i) noexcept {
    switch(i) {
    case 0: return b.hi[0]-p[0];
    case 1: return p[0]-b.lo[0];
    case 2: return b.hi[1]-p[2];
    default: return p[2]-b.lo[1];
    }
}
// m inside the nearest side (negative: past it).
inline float Depth(const Box& b,const float* p) noexcept {
    float d=Gap(b,p,0);
    for(int i=1;i<4;++i){const float g=Gap(b,p,i);if(g<d)d=g;}
    return d;
}
inline bool Inside(const Box& b,const float* p) noexcept { return Depth(b,p)>=0.0f; }
// `p` (x, y, z) put inside `b` shrunk by `in` (its y untouched). True when it moved.
inline bool ClampIn(const Box& b,float* p,float in) noexcept {
    const Box s=Inset(b,in);
    const float was[2]={p[0],p[2]};
    p[0]=p[0]<s.lo[0] ? s.lo[0] : p[0]>s.hi[0] ? s.hi[0] : p[0];
    p[2]=p[2]<s.lo[1] ? s.lo[1] : p[2]>s.hi[1] ? s.hi[1] : p[2];
    return p[0]!=was[0] || p[2]!=was[1];
}

// m it still goes out (across a line) turning back at radius `radius`, flying at `speed` (horizontal) with `out` of it
// across the line, after `react` s before the turn bites (the roll into it): the arc's reach r (1 - cos a), a its
// heading's angle off along the line.
inline float Excursion(float speed,float out,float radius,float react) noexcept {
    if(out<=0.0f || speed<=1e-3f)return 0.0f;
    const float u=out<speed ? out/speed : 1.0f;
    return out*react+radius*(1.0f-std::sqrt(1.0f-u*u));
}

// The side whose outward normal is (dx, dz) (one of kNormal's).
inline int SideFacing(float dx,float dz) noexcept { return dx>0.5f ? 0 : dx<-0.5f ? 1 : dz>0.5f ? 2 : 3; }
// The way along side `i`'s line (+1: (n.z, -n.x), -1 the other) a wing capped off it turns: the way `want` goes along
// it (or `vel`, with `want` straight out), unless that way there is less room to the next side than its turn takes
// (2 `radius`) and the other way more: into a corner it turns out of it, not into the next side.
inline float TurnSide(const Box& b,const float* pos,const float* vel,const float* want,int i,float radius) noexcept {
    const float* n=kNormal[i];
    const float tx=n[1],tz=-n[0];
    float own=want[0]*tx+want[2]*tz;
    if(std::fabs(own)<0.05f*std::sqrt(want[0]*want[0]+want[2]*want[2]))own=vel[0]*tx+vel[2]*tz;
    const float side=own>=0.0f ? 1.0f : -1.0f;
    const float room=Gap(b,pos,SideFacing(tx*side,tz*side)),other=Gap(b,pos,SideFacing(-tx*side,-tz*side));
    return room<2.0f*radius+kSlack && other>room ? -side : side;
}

// `want`'s horizontal part turned so that at most `cap` (sine) of it points out along `n` (x, z), the rest along the
// line on `side` (TurnSide); its length and its y kept.
inline void CapOut(float* want,const float* n,float cap,float side) noexcept {
    const float h=std::sqrt(want[0]*want[0]+want[2]*want[2]);
    const float hw=h>1e-4f ? h : 1.0f;
    if(h>1e-4f && (want[0]*n[0]+want[2]*n[1])/h<=cap)return;
    const float tx=n[1]*side,tz=-n[0]*side,across=std::sqrt(1.0f-cap*cap);
    want[0]=(tx*across+n[0]*cap)*hw;want[2]=(tz*across+n[1]*cap)*hw;
}

// m the circle of a turn at `radius` to `side` (+1 left, -1 right), begun `react` s on along `vel` from `pos`, keeps
// inside `b` (negative: it crosses a side by that much).
inline float Margin(const Box& b,const float* pos,const float* vel,float radius,float react,float side) noexcept {
    const float s=std::sqrt(vel[0]*vel[0]+vel[2]*vel[2]);
    if(s<1e-3f)return Depth(b,pos);
    const float vx=vel[0]/s,vz=vel[2]/s;
    const float c[3]={pos[0]+vel[0]*react-vz*side*radius,pos[1],pos[2]+vel[2]*react+vx*side*radius};
    return Depth(b,c)-radius;
}

// A wing at `pos` flying `vel` (x, y, z), turning at radius `radius` once `react` s of roll are over: `want` (unit)
// turned so it ends its turn on `soft`'s line at the most.
//  - From kEase times the turn's reach in, its want outward is eased off (none left at the reach).
//  - Within the reach (or past the line, see `back`) and still heading out, it turns back at its most: its want square
//    to its way, on the side (`turn`, kept until it is round) whose turning circle keeps more inside (Margin), never the
//    shorter-way-round coin toss that, capped by two sides in turn, flipped each frame and flew it on along the line
//    into the next side.
//  - Pointed in (kTurnIn of its way, kBackIn past the line), its want is kept pointing in as much.
// `back`: past the line (set here), flying in, nearly level, until kBackDepth inside again (cleared here); `turn` the
// side it turns back on (+1 left, -1 right, 0 none). Returns how it changed `want`: 0 not, 1 eased, 2 turned in, 3 back.
inline int KeepIn(const Box& soft,const float* pos,const float* vel,float radius,float react,float* want,bool* back,
                  signed char* turn) noexcept {
    const float depth=Depth(soft,pos);
    if(depth<0.0f)*back=true;
    else if(*back && depth>=kBackDepth)*back=false;
    const float s=std::sqrt(vel[0]*vel[0]+vel[2]*vel[2]);
    bool hard[4]{},outward=false,nearSide=false;
    float least[4]{};
    for(int i=0;i<4;++i) {
        const float* n=kNormal[i];
        const float g=Gap(soft,pos,i),out=vel[0]*n[0]+vel[2]*n[1];
        const bool backSide=*back && g<kBackDepth;
        hard[i]=backSide || g<=Excursion(s,out,radius,react)+kSlack;
        least[i]=backSide ? -kBackIn : -kTurnIn;
        // Turning back already, it keeps turning until it points in (kTurnIn) wherever its turn could still take it
        // out: dropping the turn as the reach shrank (heading along the line, it is nothing) and picking it up again
        // the next frame flipped the bank each frame, and the lift between the two banks looped the jet up over the
        // ceiling (the offline flight test, 2026-10-06).
        const bool turning=*turn && g<radius+s*react+kSlack;
        if((hard[i] || turning) && out>least[i]*s)outward=true;
        if(out>0.0f && g<2.0f*radius+s*react+kSlack)nearSide=true;
    }
    // Into a corner (or a box little wider than its turn) the turn back toward one side runs it into the next: once
    // neither way's whole circle keeps kSlack inside, it turns now, the way that keeps more.
    const float margin[2]={Margin(soft,pos,vel,radius,react,1.0f),Margin(soft,pos,vel,radius,react,-1.0f)};
    if(nearSide && (margin[0]>margin[1] ? margin[0] : margin[1])<kSlack)outward=true;
    int how=0;
    const float h=std::sqrt(want[0]*want[0]+want[2]*want[2]),hw=h>1e-4f ? h : 1.0f;
    if(outward && s>1.0f) {
        const float vx=vel[0]/s,vz=vel[2]/s;
        if(!*turn)*turn=margin[0]>=margin[1] ? 1 : -1;
        const float side=static_cast<float>(*turn);
        want[0]=-vz*side*hw;want[2]=vx*side*hw;
        how=2;
    } else {
        *turn=0;
        for(int i=0;i<4;++i) {
            const float* n=kNormal[i];
            if(hard[i]) {
                const float along=vel[0]*n[1]-vel[2]*n[0];   // its way along the line's (n.z, -n.x)
                CapOut(want,n,least[i],along>=0.0f ? 1.0f : -1.0f);
                how=2;
                continue;
            }
            const float g=Gap(soft,pos,i),need=Excursion(s,vel[0]*n[0]+vel[2]*n[1],radius,react)+kSlack;
            if(g<=need*kEase){CapOut(want,n,(g-need)/need,TurnSide(soft,pos,vel,want,i,radius));if(!how)how=1;}
        }
    }
    if(*back) {
        how=3;
        want[1]=want[1]<-kBackLevel ? -kBackLevel : want[1]>kBackLevel ? kBackLevel : want[1];
    }
    if(how) {
        const float l=std::sqrt(want[0]*want[0]+want[1]*want[1]+want[2]*want[2]);
        if(l>1e-6f)for(int i=0;i<3;++i)want[i]/=l;
    }
    return how;
}

// A flyer climbing at `vy` (of `speed`) toward `top` (y): pushing over at radius `radius` after `react` s, the height it
// still gains (Excursion along the vertical). The y component its want may have: down by `down` once that reaches top.
inline bool CapClimb(float y,float vy,float speed,float top,float radius,float react,float down,float* want) noexcept {
    const float need=Excursion(speed,vy,radius,react);
    if(top-y>need || want[1]<=-down)return false;
    want[1]=-down;
    const float h=std::sqrt(want[0]*want[0]+want[2]*want[2]),keep=std::sqrt(1.0f-down*down);
    if(h>1e-4f){want[0]*=keep/h;want[2]*=keep/h;}
    return true;
}

// A rotor (or anything that brakes at `brake` m/s^2 after `react` s) at `pos`, its wanted horizontal velocity `wantV`
// (x, y, z; y untouched): the part out across each side of `soft` cut to what still stops on the line; past it, in at
// `backSpeed` at least. True when it cut anything.
inline bool LimitOut(const Box& soft,const float* pos,float brake,float react,float backSpeed,float* wantV) noexcept {
    bool cut=false;
    for(int i=0;i<4;++i) {
        const float* n=kNormal[i];
        const float g=Gap(soft,pos,i);
        const float out=wantV[0]*n[0]+wantV[2]*n[1];
        float most;
        if(g<=0.0f)most=-backSpeed;
        else {
            // v react + v^2 / (2 brake) = g
            const float b=brake*react;
            most=-b+std::sqrt(b*b+2.0f*brake*g);
        }
        if(out<=most)continue;
        wantV[0]-=n[0]*(out-most);wantV[2]-=n[1]*(out-most);
        cut=true;
    }
    return cut;
}
}  // namespace airbound
}  // namespace crew
