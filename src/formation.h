// The squads' formations (the user, 2026-10-08: "tactical formations, modern formations for moving / tactics /
// defence"), the pure part: where each soldier of a formation stands, how the formation's heading follows its anchor,
// the bounding overwatch's two teams taking turns, and when a soldier that cannot get to its slot gives up on it for a
// while. No game here (tools/formation_check.cpp runs it offline); src/npcai.cpp FormationMove reads the squads into
// it and walks each soldier to its slot.
//  - On the move (the soldiers that follow the player): the player is the formation's point and its heading is the
//    way the player last walked; the slots are in metres right (x) and forward (z) of the player, k the soldier's
//    place in the roster (0 first), `spacing` the ini's NpcFormationSpacing.
//  - In defence (a squad told to guard a point on the map): the point is the centre and the heading the way the
//    player looked at it from when the order was given (towards the threat): a ring round it, or a line across it.
//  - Bounding overwatch: the roster's two halves (even and odd places) take turns: one moves up to its wedge slot while
//    the other holds where it stands (covering it), and they swap when the movers are all there or after kBoundMost.
//  - A slot a soldier makes no headway to for kStuckMs (a wall: the move is a straight line) is let go for kRestMs:
//    the stock follow (which finds its way) takes it meanwhile.
#pragma once
#include <cmath>
#include <cstdint>

namespace npc {
namespace formation {
enum class Shape : std::uint8_t {
    stock,          // no formation: the stock follow (and the guard's radius), as before
    column,         // single file behind the point
    staggered,      // two files, offset (the road march)
    wedge,          // a V opening back: the point at its tip
    vee,            // a V opening forward: the point behind its mouth (the fire forward)
    line,           // abreast either side of the point
    echelonLeft,    // a diagonal back to the left
    echelonRight,   // a diagonal back to the right
    diamond,        // left, right, behind, ahead: an all-round march
    bounding,       // bounding overwatch: the halves leapfrog in a wedge
    perimeter,      // defence: a ring round the point, all round
    follow,         // the default march: a loose cluster trailing the point (2026-11-27, appended: the ini values stay)
    count
};
constexpr int kShapes=static_cast<int>(Shape::count);
constexpr float kDiag=0.70710678f;
constexpr std::uint64_t kBoundMost=8000;   // ms a bound may take before the halves swap anyway
constexpr std::uint64_t kStuckMs=3000,kRestMs=6000;
constexpr float kArrive=1.5f;              // m from its slot that is there
constexpr float kHeadwayMin=0.5f;          // m nearer its slot within kStuckMs that is headway

// The shapes a key cycles through: on the move all the marching ones, the default follow first and the stock follow
// next (the user, 2026-11-27: 「新的跟随逻辑设为默认」, the stock one kept as a choice); on a guard point the defensive
// ones (and the marching shapes that make sense facing a threat: the line and the wedge).
constexpr Shape kMarchDefault=Shape::follow;
constexpr Shape kMarch[]={Shape::follow,Shape::stock,Shape::column,Shape::staggered,Shape::wedge,Shape::vee,Shape::line,Shape::echelonLeft,
                          Shape::echelonRight,Shape::diamond,Shape::bounding};
constexpr Shape kGuard[]={Shape::stock,Shape::perimeter,Shape::line,Shape::wedge};
template<int N> constexpr Shape NextOf(const Shape (&list)[N],Shape s) noexcept {
    for(int i=0;i<N;++i)if(list[i]==s)return list[(i+1)%N];
    return list[0];
}
constexpr Shape Next(Shape s,bool guard) noexcept { return guard ? NextOf(kGuard,s) : NextOf(kMarch,s); }
// Whether `s` belongs on a guard point / on the move (an ini value or a key's from the other list is stock there).
constexpr bool GuardShape(Shape s) noexcept { for(Shape g:kGuard)if(g==s)return true; return false; }
constexpr bool MarchShape(Shape s) noexcept { for(Shape g:kMarch)if(g==s)return true; return false; }
constexpr Shape FromInt(int v) noexcept { return v>=0 && v<kShapes ? static_cast<Shape>(v) : Shape::stock; }
// The march an ini value (NpcFormation) starts at: its shape when that is a march, else the default (kMarchDefault).
constexpr Shape MarchFromInt(int v) noexcept { return MarchShape(FromInt(v)) && static_cast<int>(FromInt(v))==v ? FromInt(v) : kMarchDefault; }

inline const char* Name(Shape s) noexcept {
    switch(s) {
    case Shape::column: return "column";
    case Shape::staggered: return "staggered column";
    case Shape::wedge: return "wedge";
    case Shape::vee: return "vee";
    case Shape::line: return "line";
    case Shape::echelonLeft: return "echelon left";
    case Shape::echelonRight: return "echelon right";
    case Shape::diamond: return "diamond";
    case Shape::bounding: return "bounding overwatch";
    case Shape::perimeter: return "perimeter";
    case Shape::follow: return "follow";
    default: return "stock";
    }
}

// Soldier k's slot of `n` in shape `s`, `spacing` m apart: (x right, z forward) of the anchor. False for stock.
inline bool Offset(Shape s,int k,int n,float spacing,float* x,float* z) noexcept {
    if(k<0 || n<1 || !(spacing>0.0f))return false;
    const int rank=k/2+1;
    const float side=(k%2==0) ? -1.0f : 1.0f;   // the first one left, the next right, and so on outwards
    switch(s) {
    case Shape::column: *x=0.0f;*z=-spacing*static_cast<float>(k+1);return true;
    case Shape::staggered: *x=side*spacing*0.5f;*z=-spacing*static_cast<float>(rank);return true;
    case Shape::wedge:
    case Shape::bounding: *x=side*spacing*kDiag*static_cast<float>(rank);*z=-spacing*kDiag*static_cast<float>(rank);return true;
    case Shape::vee: *x=side*spacing*kDiag*static_cast<float>(rank);*z=spacing*kDiag*static_cast<float>(rank);return true;
    case Shape::line: *x=side*spacing*static_cast<float>(rank);*z=0.0f;return true;
    case Shape::echelonLeft: *x=-spacing*kDiag*static_cast<float>(k+1);*z=-spacing*kDiag*static_cast<float>(k+1);return true;
    case Shape::echelonRight: *x=spacing*kDiag*static_cast<float>(k+1);*z=-spacing*kDiag*static_cast<float>(k+1);return true;
    case Shape::diamond: {
        const float r=spacing*static_cast<float>(k/4+1);
        switch(k%4) {
        case 0: *x=-r;*z=0.0f;break;   // left
        case 1: *x=r;*z=0.0f;break;    // right
        case 2: *x=0.0f;*z=-r;break;   // behind
        default: *x=0.0f;*z=r;break;   // ahead
        }
        return true;
    }
    case Shape::follow: {
        // A loose cluster behind the point, not a drill: rows of 1, 2, 3, 2, 3... (the first right behind; a row of
        // three centre, left, right; of two left, right), each 0.8 x spacing farther back, the rows of two between
        // the threes' places; each soldier nudged up to 0.06 x spacing by its place (a fixed pattern, so its slot
        // does not wander), at least 0.7 x spacing from any other. The Keep logic below (lead, ease, re-send, catch
        // up, the rest's scatter) is what makes it a follow.
        int row=0,first=0,size=1;
        while(k>=first+size){first+=size;++row;size=row%2 ? 2 : 3;}
        const int col=k-first;
        const float across=size==1 ? 0.0f : size==2 ? (col==0 ? -0.6f : 0.6f) : (col==0 ? 0.0f : col==1 ? -1.1f : 1.1f);
        const float jx=0.02f*static_cast<float>((k*37)%7-3),jz=0.02f*static_cast<float>((k*53)%7-3);
        *x=spacing*(across+jx);*z=-spacing*(0.9f+0.8f*static_cast<float>(row)+jz);
        return true;
    }
    case Shape::perimeter: {
        // A ring with its soldiers `spacing` apart round it (never tighter than `spacing` from the centre); the first
        // one ahead (towards the threat), the others round from there.
        const float c=static_cast<float>(n)*spacing/(2.0f*3.14159265f);
        const float r=c>spacing ? c : spacing;
        const float a=2.0f*3.14159265f*static_cast<float>(k)/static_cast<float>(n);
        *x=r*std::sin(a);*z=r*std::cos(a);
        return true;
    }
    default: return false;
    }
}

// The slot in the world: the anchor `at` + x along the heading's right + z along the heading `fwd` (level, unit).
inline void World(const float* at,const float* fwd,float x,float z,float* out) noexcept {
    const float right[2]={-fwd[1],fwd[0]};   // the game's level right of a heading (-fz, fx), as its cameras' (hud_view Camera)
    out[0]=at[0]+right[0]*x+fwd[0]*z;
    out[1]=at[1];
    out[2]=at[2]+right[1]*x+fwd[1]*z;
}

// The heading a formation on the move takes from its anchor: the way the anchor walked, sampled once it has gone
// `step` m from the last sample (standing, or turning on the spot, it keeps the last one).
struct Heading { float fwd[2]; float last[3]; bool set; };
inline void Track(Heading& h,const float* at,const float* initial,float step) noexcept {
    if(!h.set) {
        const float l=std::sqrt(initial[0]*initial[0]+initial[1]*initial[1]);
        h.fwd[0]=l>1e-4f ? initial[0]/l : 0.0f;h.fwd[1]=l>1e-4f ? initial[1]/l : 1.0f;
        h.last[0]=at[0];h.last[1]=at[1];h.last[2]=at[2];h.set=true;
        return;
    }
    const float dx=at[0]-h.last[0],dz=at[2]-h.last[2],l=std::sqrt(dx*dx+dz*dz);
    if(!(l>=step) || !std::isfinite(l))return;
    h.fwd[0]=dx/l;h.fwd[1]=dz/l;
    h.last[0]=at[0];h.last[1]=at[1];h.last[2]=at[2];
}

// Bounding overwatch: which half moves (0 the even places, 1 the odd), since when; swapped when every mover was at
// its slot last frame (`pending` 0 of `movers` > 0) or the bound has taken kBoundMost. True when it swapped.
struct Bound { int moving; std::uint64_t since; };
inline bool Step(Bound& b,int movers,int pending,std::uint64_t ms) noexcept {
    if(!b.since){b.since=ms;return false;}
    if((movers>0 && pending==0) || ms-b.since>=kBoundMost){b.moving^=1;b.since=ms;return true;}
    return false;
}
constexpr int TeamOf(int k) noexcept { return k%2; }

// A soldier's headway to its slot: false while it may go on walking at it; true (let the stock follow have it) for
// kRestMs once it has made less than kHeadwayMin of headway in kStuckMs.
struct Progress { float best; std::uint64_t since,restUntil; };
inline bool GiveUp(Progress& p,float dist,std::uint64_t ms) noexcept {
    if(p.restUntil && ms<p.restUntil)return true;
    if(p.restUntil){p=Progress{dist,ms,0};return false;}   // rested: try again
    if(dist<=kArrive || !(p.best>0.0f) || dist<p.best-kHeadwayMin){p.best=dist;p.since=ms;return false;}   // best 0: fresh
    if(ms-p.since>=kStuckMs){p.restUntil=ms+kRestMs;return true;}
    return false;
}

// --- Keeping up with the player (2026-11-26, the player: 「可以抄一下这个跟随的好的地方」, a follower mod's design, not its
// code). What the march adds round its slots (npcai.cpp FormationMove):
//  - the anchor's motion (Observe): its level velocity, sampled every kSampleMs and blended; a jump (a warp, boarding)
//    past kJumpSpeed or a gap of over a second starts it over at rest;
//  - the slots round where the anchor will be (Lead): kLeadSec of its velocity ahead, at most kLeadMost m, a quarter of
//    that at a walk (kWalkSpeed and under), so they do not trail their slots by the walk's own lag;
//  - the slot eased (Ease): it moves at most kGoalRate m/s plus kGoalCatch times the anchor's speed, so a heading that
//    turns about swings the slots round instead of sending every soldier across at once; past kGoalSnap m it jumps;
//  - the route's goal re-sent only once it moved kRegoal m and kRegoalMs went by since the last (at once past
//    kRegoalFar): the route is not asked for again every frame the slot moves a little (the stop-start dashes);
//  - the walk's least stick (EaseLeast): the anchor's speed's share of a full walk, more the farther behind it is, so a
//    soldier keeps pace with a walking player instead of easing in to a stop behind its slot every few metres;
//  - resting (Resting / Scatter): the anchor kept within kStillRadius m for kStillMs, the soldiers spread round it,
//    the front left open (its line of fire); it moves off, they take their slots again;
constexpr float kLeadSec=0.6f,kLeadMost=6.0f,kWalkSpeed=4.0f,kWalkLead=0.25f;
constexpr float kJumpSpeed=60.0f,kVelBlend=0.3f;
constexpr std::uint64_t kSampleMs=50,kSampleGap=1000;
constexpr float kGoalRate=6.0f,kGoalCatch=1.5f,kGoalSnap=30.0f;
constexpr float kRegoal=1.5f,kRegoalFar=6.0f;
constexpr std::uint64_t kRegoalMs=500;
constexpr float kFullWalk=6.0f,kCatchFrom=3.0f,kCatchBand=6.0f,kEaseFloor=0.3f;
constexpr float kStillRadius=2.0f;
constexpr std::uint64_t kStillMs=5000;
constexpr float kOpenFront=1.0471976f;     // rad either side of the heading left open while resting (60 degrees)

inline float Level2(const float* a,const float* b) noexcept {
    const float dx=a[0]-b[0],dz=a[2]-b[2];
    return std::sqrt(dx*dx+dz*dz);
}

struct Motion { float last[3]; float vel[2]; std::uint64_t at; bool set; };
inline void Observe(Motion& m,const float* at,std::uint64_t ms) noexcept {
    if(!m.set || ms<m.at){m=Motion{{at[0],at[1],at[2]},{0.0f,0.0f},ms,true};return;}
    const std::uint64_t dt=ms-m.at;
    if(dt<kSampleMs)return;
    const float s=static_cast<float>(dt)*0.001f;
    const float vx=(at[0]-m.last[0])/s,vz=(at[2]-m.last[2])/s;
    if(dt>kSampleGap || !std::isfinite(vx) || !std::isfinite(vz) || vx*vx+vz*vz>kJumpSpeed*kJumpSpeed){m.vel[0]=m.vel[1]=0.0f;}
    else{m.vel[0]+=(vx-m.vel[0])*kVelBlend;m.vel[1]+=(vz-m.vel[1])*kVelBlend;}
    m.last[0]=at[0];m.last[1]=at[1];m.last[2]=at[2];m.at=ms;
}
inline float SpeedOf(const Motion& m) noexcept { return m.set ? std::sqrt(m.vel[0]*m.vel[0]+m.vel[1]*m.vel[1]) : 0.0f; }
inline void Lead(const Motion& m,const float* at,float* out) noexcept {
    const float v=SpeedOf(m);
    float dx=0.0f,dz=0.0f;
    if(v>1e-3f) {
        const float ahead=v*kLeadSec*(v<=kWalkSpeed ? kWalkLead : 1.0f);
        const float l=ahead<kLeadMost ? ahead : kLeadMost;
        dx=m.vel[0]/v*l;dz=m.vel[1]/v*l;
    }
    out[0]=at[0]+dx;out[1]=at[1];out[2]=at[2]+dz;
}

struct Goal { float at[3]; std::uint64_t ms; bool set; };
inline void Ease(Goal& g,const float* target,float anchorSpeed,std::uint64_t ms) noexcept {
    const float d=g.set ? Level2(g.at,target) : 0.0f;
    if(!g.set || ms<g.ms || ms-g.ms>kSampleGap || !(d<=kGoalSnap)) {
        g.at[0]=target[0];g.at[1]=target[1];g.at[2]=target[2];g.ms=ms;g.set=true;
        return;
    }
    const float step=(kGoalRate+kGoalCatch*(anchorSpeed>0.0f ? anchorSpeed : 0.0f))*static_cast<float>(ms-g.ms)*0.001f;
    const float t=d>step ? step/d : 1.0f;
    g.at[0]+=(target[0]-g.at[0])*t;g.at[2]+=(target[2]-g.at[2])*t;g.at[1]=target[1];
    g.ms=ms;
}

struct Sent { float at[3]; std::uint64_t ms; bool set; };
inline bool Resend(Sent& s,const float* goal,std::uint64_t ms) noexcept {
    const float d=s.set ? Level2(s.at,goal) : 0.0f;
    if(s.set && ms>=s.ms && !(d>=kRegoalFar) && !(d>=kRegoal && ms-s.ms>=kRegoalMs))return false;
    s.at[0]=goal[0];s.at[1]=goal[1];s.at[2]=goal[2];s.ms=ms;s.set=true;
    return true;
}

inline float EaseLeast(float anchorSpeed,float dist) noexcept {
    const float v=kEaseFloor+(anchorSpeed>0.0f ? anchorSpeed/kFullWalk : 0.0f)+(dist>kCatchFrom ? (dist-kCatchFrom)/kCatchBand : 0.0f);
    return v<kEaseFloor ? kEaseFloor : v>1.0f ? 1.0f : v;
}

struct Rest { float at[3]; std::uint64_t since; bool set; };
inline bool Resting(Rest& r,const float* anchor,std::uint64_t ms) noexcept {
    if(!r.set || ms<r.since || !(Level2(anchor,r.at)<=kStillRadius)) {
        r.at[0]=anchor[0];r.at[1]=anchor[1];r.at[2]=anchor[2];r.since=ms;r.set=true;
        return false;
    }
    return ms-r.since>=kStillMs;
}
// Soldier k's place round a resting anchor (x right, z forward of its heading): rings `spacing` apart outwards, each
// filled round the back, kOpenFront either side of the heading left open; at least `spacing` between neighbours.
inline bool Scatter(int k,float spacing,float* x,float* z) noexcept {
    if(k<0 || !(spacing>0.0f))return false;
    const float arc=2.0f*3.14159265f-2.0f*kOpenFront;
    int first=0;
    for(int ring=0;;++ring) {
        const float r=spacing*static_cast<float>(ring+1);
        int room=static_cast<int>(arc*r/spacing);
        if(room<1)room=1;
        if(k<first+room) {
            const float a=kOpenFront+arc*(static_cast<float>(k-first)+0.5f)/static_cast<float>(room);
            *x=r*std::sin(a);*z=r*std::cos(a);
            return true;
        }
        first+=room;
    }
}

// A soldier's own keeping up: its eased slot, the goal its route was last given.
struct Keep { Goal goal; Sent sent; };

static_assert(Next(Shape::follow,false)==Shape::stock && Next(Shape::stock,false)==Shape::column &&
              Next(Shape::bounding,false)==Shape::follow,"the march cycle: the default follow first, the stock follow next");
static_assert(MarchFromInt(static_cast<int>(Shape::follow))==Shape::follow && MarchFromInt(0)==Shape::stock &&
              MarchFromInt(static_cast<int>(Shape::perimeter))==kMarchDefault && MarchFromInt(-1)==kMarchDefault,"the ini's march");
static_assert(Next(Shape::stock,true)==Shape::perimeter && Next(Shape::wedge,true)==Shape::stock,"the guard cycle");
static_assert(Next(Shape::perimeter,false)==Shape::follow && Next(Shape::column,true)==Shape::stock,"a shape of the other list starts over");
static_assert(GuardShape(Shape::perimeter) && !MarchShape(Shape::perimeter) && !GuardShape(Shape::column) &&
              MarchShape(Shape::follow) && !GuardShape(Shape::follow),"the lists");
}  // namespace formation
}  // namespace npc
