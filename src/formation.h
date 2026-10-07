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
    count
};
constexpr int kShapes=static_cast<int>(Shape::count);
constexpr float kDiag=0.70710678f;
constexpr std::uint64_t kBoundMost=8000;   // ms a bound may take before the halves swap anyway
constexpr std::uint64_t kStuckMs=3000,kRestMs=6000;
constexpr float kArrive=1.5f;              // m from its slot that is there
constexpr float kHeadwayMin=0.5f;          // m nearer its slot within kStuckMs that is headway

// The shapes a key cycles through: on the move all the marching ones; on a guard point the defensive ones (and the
// marching shapes that make sense facing a threat: the line and the wedge).
constexpr Shape kMarch[]={Shape::stock,Shape::column,Shape::staggered,Shape::wedge,Shape::vee,Shape::line,Shape::echelonLeft,
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

static_assert(Next(Shape::stock,false)==Shape::column && Next(Shape::bounding,false)==Shape::stock,"the march cycle");
static_assert(Next(Shape::stock,true)==Shape::perimeter && Next(Shape::wedge,true)==Shape::stock,"the guard cycle");
static_assert(Next(Shape::perimeter,false)==Shape::stock && Next(Shape::column,true)==Shape::stock,"a shape of the other list starts over");
static_assert(GuardShape(Shape::perimeter) && !MarchShape(Shape::perimeter) && !GuardShape(Shape::column),"the lists");
}  // namespace formation
}  // namespace npc
