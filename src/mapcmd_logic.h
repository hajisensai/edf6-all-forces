// The map's commands, their pure part (src/mapcmd.cpp drives it; tools/map_cmd_check.cpp checks it offline): the
// point at the screen's centre, the selection and what a key press does. No game, no Windows.
//  - The point: the map's camera looks from its eye at its focus, so the screen's centre is the ray eye -> focus. The
//    map ray gives where it meets the ground or a roof; with no hit (past the world's edge, the sea) the ray's meeting
//    with the level plane through the focus stands in (RayLevel).
//  - The selection: one unit (its vehicle), every unit (All), or none. Tab / Shift+Tab (pad X) step through the units in
//    a stable order (the vehicles' addresses: a moving unit does not reorder the list) and then All, wrapping.
//  - A command goes to the selection: guard (go to the point and fight round it), follow (the player), release (back to
//    what the unit did before any command). One a frame; refused with no selection, with no point (guard) or online.
#pragma once
#include <cmath>
#include <cstdint>

namespace mapcmd {
// none: the unit does what it did before any command (a call's own guard point or escort, a crewed heli's follow...).
enum class Order : std::uint8_t { none, guard, follow };
struct Command { Order order; float at[3]; };   // at: the guard's point (on the ground)

// The ray eye + t dir (t > 0) meeting the level plane at height y: true with `hit`.
inline bool RayLevel(const float* eye,const float* dir,float y,float* hit) noexcept {
    const float dy=dir[1];
    if(!(dy>1e-6f || dy<-1e-6f))return false;   // level (or NaN): it never meets the plane
    const float t=(y-eye[1])/dy;
    if(!(t>0.0f) || !std::isfinite(t))return false;
    for(int i=0;i<3;++i)hit[i]=eye[i]+dir[i]*t;
    return std::isfinite(hit[0]+hit[1]+hit[2]);
}

// The selection standing for every unit.
inline const void* All() noexcept { static const char tag=0; return &tag; }

inline int IndexOf(const void* const* ids,int n,const void* id) noexcept {
    for(int i=0;i<n;++i)if(ids[i]==id)return i;
    return -1;
}

// The selection still standing: a unit no longer listed (gone, withdrawn, taken over by the player) is dropped; All
// stands while any unit does.
inline const void* Keep(const void* const* ids,int n,const void* sel) noexcept {
    if(!sel || n<=0)return nullptr;
    if(sel==All())return sel;
    return IndexOf(ids,n,sel)>=0 ? sel : nullptr;
}

// The selection one `step` (+1 next, -1 previous) on through ids[0..n) and All (after the last), wrapping; from none
// the first going forward, All going back.
inline const void* Cycle(const void* const* ids,int n,const void* sel,int step) noexcept {
    if(n<=0)return nullptr;
    sel=Keep(ids,n,sel);
    // Positions 0..n-1 the units, n All; none sits before 0 going forward and after n going back.
    int at=!sel ? (step>0 ? -1 : n+1) : sel==All() ? n : IndexOf(ids,n,sel);
    at=((at+step)%(n+1)+(n+1))%(n+1);
    return at==n ? All() : ids[at];
}

// The keys' presses this frame (edges).
struct Press { bool next,prev,guard,follow,release; };
enum class Refusal : std::uint8_t { none, noUnit, noPoint, online };
// What a frame's presses come to: the selection after them, and a command to give it (issue), or why not (why).
struct Step { const void* sel; bool issue; Command cmd; Refusal why; };

inline Step Decide(const void* const* ids,int n,const void* sel,const Press& p,bool allowed,const float* point,bool pointOk) noexcept {
    Step s{Keep(ids,n,sel),false,Command{Order::none,{0.0f,0.0f,0.0f}},Refusal::none};
    if(p.next)s.sel=Cycle(ids,n,s.sel,1);
    else if(p.prev)s.sel=Cycle(ids,n,s.sel,-1);
    const Order want=p.guard ? Order::guard : p.follow ? Order::follow : Order::none;
    if(!p.guard && !p.follow && !p.release)return s;
    if(!allowed)s.why=Refusal::online;
    else if(!s.sel)s.why=Refusal::noUnit;
    else if(want==Order::guard && !pointOk)s.why=Refusal::noPoint;
    if(s.why!=Refusal::none)return s;
    s.issue=true;s.cmd.order=want;
    if(want==Order::guard)for(int i=0;i<3;++i)s.cmd.at[i]=point[i];
    return s;
}

// Whether `id` gets the command a step issues (its selection one unit, or All).
inline bool Targets(const void* sel,const void* id) noexcept { return sel && id && (sel==All() || sel==id); }
}  // namespace mapcmd
