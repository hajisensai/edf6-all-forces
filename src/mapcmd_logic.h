// The map's commands, their pure part (src/mapcmd.cpp drives it; tools/map_cmd_check.cpp checks it offline): the
// screen and the ground (projection, the ray under a screen point), the selection (a set of units: box, click, cycle)
// and what a key press does, the guard formation. No game, no Windows.
//  - The screen: the map's view-projection as the HUD draws with it (row vectors, hud.cpp Project): a unit's screen
//    point is its icon's, the ray under a screen point comes from the inverse (as hud.cpp CameraRay makes the centre's).
//    The map ray gives where that ray meets the ground or a roof; with no hit (past the world's edge, the sea) its
//    meeting with the level plane through the focus stands in (RayLevel).
//  - The selection (the user, 2026-10-06: "操作 需要一个框选吧"): a set of units. Ctrl + left drag boxes them in (Shift
//    adds), a left click (no drag) picks the unit under the pointer (Shift adds / takes it out) or, on empty ground,
//    clears; Tab / Shift+Tab (pad X) step through one unit at a time in a stable order and then ALL, wrapping.
//  - A command goes to every selected unit: guard (go to the point and fight round it), move / attack-move (go there,
//    then guard it), follow (the player), release (back to what the unit did before any command). One a frame; refused
//    with no selection, with no point (a point order) or online. Several units sent to one point stand round it in a
//    formation (Formation), not on one spot.
#pragma once
#include <cmath>
#include <cstdint>

namespace mapcmd {
inline constexpr float kFormationSpacing=30.0f;
inline constexpr unsigned kMaxFormationUnits=96;
// none: the unit does what it did before any command (a call's own guard point or escort, a crewed heli's follow...).
// The squads' own (docs/npc-ai-design.md §6.2): engage (fight freely round where it stands, a wider reach), focus (every
// member on the marked enemy), board / dismount (the nearest friendly vehicle with room), dismiss (no longer the
// player's: it stays where it is and may not be recruited again for a while), recruit (the player's now).
// The RTS moves (the user, 2026-10-09: "m里面没办法让npc移动攻击，只有守点，如果npc在打怪，就没办法移动了"): move (go to
// the point whatever it is fighting, firing on the way, and guard it once there), attack-move (go there fighting what it
// meets on the way, then guard it). Appended: the values are the map command wire's (command_protocol.cpp).
enum class Order : std::uint8_t { none, guard, follow, engage, focus, board, dismount, dismiss, recruit, move, attackMove };
inline constexpr Order kLastOrder=Order::attackMove;
// The orders that take the ground point under the pointer (and stand round it in a formation).
inline bool PointOrder(Order o) noexcept { return o==Order::guard || o==Order::move || o==Order::attackMove; }
// Whether a vehicle unit (heli, jet, crawler, tank) takes `o`: the point orders, follow and release; the squads take all.
inline bool VehicleOrder(Order o) noexcept { return o==Order::none || o==Order::follow || PointOrder(o); }
// A vehicle's modules know one point order (their post / anchor): move and attack-move reach it as guard.
inline Order VehicleCommandOf(Order o) noexcept { return PointOrder(o) ? Order::guard : o; }
// Whether an aircraft unit (heli, jet) takes `o`: a vehicle's orders and focus fire on the marked enemy (the user,
// 2026-10-09: "飞机没办法指定攻击目标"; jet.cpp / heli.cpp JetCommand / HeliCommand take it as a squad's focus).
inline bool AirOrder(Order o) noexcept { return VehicleOrder(o) || o==Order::focus; }
struct Command { Order order; float at[3]; };   // at: the point of a point order (on the ground)

// --- The squads' moves under an order (npcai.cpp Drive), the player's command over the soldiers' own fight ---
//  - forced (move): every member walks to its place by the point before anything of its own (dodging, falling back
//    hurt, its combat spot); it still turns on and fires at what is in reach as it goes;
//  - fightFirst (attack-move): walks on to the point firing at what it has in reach, as a move does; only a target
//    within `close` m (the danger range: an enemy pressing on it) stops it there to fight with its own moves (dodging,
//    its combat spot). Before 2026-10-09 any target stopped it, so a squad met by a far enemy stood on its combat spot
//    and never came (the user: "这个移动攻击的优先级好像不对，不能让他边走边打吗");
//  - neither: the order's anchor as before (guard holds its point's radius, engage / focus their reach).
struct Pursuit { bool forced,fightFirst; };
inline Pursuit PursuitOf(Order o) noexcept { return Pursuit{o==Order::move,o==Order::attackMove}; }
// Whether a member walks on to the order's point this frame (npcai.cpp Drive), its target (if any) `targetDistance` m off.
inline bool Pursues(Pursuit p,bool target,float targetDistance,float close) noexcept {
    return p.forced || (p.fightFirst && (!target || !(targetDistance<=close)));
}
// A move / attack-move whose squad has reached its point (its top within `radius`, level) is a guard of that point from
// then on: the members hold round it and fight what comes, as a guard order does. Others stay as they are.
inline Order Arrive(Order o,float distance,float radius) noexcept {
    return (o==Order::move || o==Order::attackMove) && distance<=radius ? Order::guard : o;
}
// The right mouse button let go without a drag on the map (RTS): with units selected, an enemy under the pointer is
// attacked (focus fire on it), else the ground under it is the move's point; nothing selected or nothing there: none
// (Order::none with `issue` false). An armed button's targeting is cancelled by it instead (mapcmd.cpp).
struct RightClick { bool issue; Order order; };
inline RightClick RightClickOrder(int selected,bool enemyUnder,bool pointOk) noexcept {
    if(selected<=0)return RightClick{false,Order::none};
    if(enemyUnder)return RightClick{true,Order::focus};
    return RightClick{pointOk,Order::move};
}

// --- A squad's recruitment on the map ---
// A squad the map offers to recruit (its RECRUIT order lit, its panel row "free"): on foot and nobody's (no player's,
// no script's), not cooling down from a dismissal. A squad riding a vehicle (the plugin's real crews, a support's
// soldiers) is that vehicle's crew: no recruitment is offered for it (the user, 2026-10-09: "载具上的npc还标着可以招募
// 的标记"), its vehicle is the unit the map commands.
inline bool OffersRecruit(bool ownedByPlayer,bool scripted,bool dismissed,bool riding) noexcept {
    return !ownedByPlayer && !scripted && !dismissed && !riding;
}

// --- The squad panel (the user, 2026-10-09: "小队太多了吧，怎么处理合适，感觉看不过来"; the log's mission had 37) ---
// Rows in the order a commander wants them (npcai.cpp SquadRows sorts by it, stable): the player's own squads, the free
// ones it may command or recruit, the ones riding a vehicle (their vehicles are the map's units), a script's last.
inline int SquadRank(bool recruited,bool scripted,bool riding) noexcept { return scripted ? 3 : riding ? 2 : recruited ? 0 : 1; }
inline constexpr int kSquadRowsFolded=9;   // the number keys' reach
// The rows shown (a prefix of the sorted rows `rank`): folded (the default) the ones on foot that take orders (rank
// 0-1), at most kSquadRowsFolded, the rest summed in one row that opens the panel; open, all of them (up to `most`).
inline int SquadRowsShown(const int* rank,int n,bool open,int most) noexcept {
    int k=0;
    while(k<n && k<most && (open || (rank[k]<=1 && k<kSquadRowsFolded)))++k;
    return k;
}

// --- Where a unit's mark is drawn and clicked ---
// On its body (the user, 2026-10-09: "这个图标为什么要在npc的竖直顶上。直接在npc身上不好吗"): a soldier's or a ground
// vehicle's position is at its feet, so the mark is kBodyLift m up from it (a soldier's middle); a flying unit's on it.
inline constexpr float kBodyLift=1.2f;
inline void BodyPoint(const float* pos,bool air,float* out) noexcept {
    out[0]=pos[0];out[1]=pos[1]+(air ? 0.0f : kBodyLift);out[2]=pos[2];
}

// --- The screen and the ground ---
// The ray eye + t dir (t > 0) meeting the level plane at height y: true with `hit`.
inline bool RayLevel(const float* eye,const float* dir,float y,float* hit) noexcept {
    const float dy=dir[1];
    if(!(dy>1e-6f || dy<-1e-6f))return false;   // level (or NaN): it never meets the plane
    const float t=(y-eye[1])/dy;
    if(!(t>0.0f) || !std::isfinite(t))return false;
    for(int i=0;i<3;++i)hit[i]=eye[i]+dir[i]*t;
    return std::isfinite(hit[0]+hit[1]+hit[2]);
}

// A 4x4 inverse (Gauss-Jordan, partial pivoting); false when singular.
inline bool Invert4(const float* m,float* out) noexcept {
    float a[4][8];
    for(int r=0;r<4;++r)for(int c=0;c<8;++c)a[r][c]=c<4 ? m[r*4+c] : (c-4==r ? 1.0f : 0.0f);
    for(int c=0;c<4;++c) {
        int p=c;
        for(int r=c+1;r<4;++r)if(std::fabs(a[r][c])>std::fabs(a[p][c]))p=r;
        if(!(std::fabs(a[p][c])>=1e-12f))return false;
        if(p!=c)for(int k=0;k<8;++k){const float t=a[c][k];a[c][k]=a[p][k];a[p][k]=t;}
        const float d=a[c][c];
        for(int k=0;k<8;++k)a[c][k]/=d;
        for(int r=0;r<4;++r) {
            if(r==c)continue;
            const float f=a[r][c];
            for(int k=0;k<8;++k)a[r][k]-=f*a[c][k];
        }
    }
    for(int r=0;r<4;++r)for(int c=0;c<4;++c)out[r*4+c]=a[r][c+4];
    return true;
}

// The world point `p` on a `w` x `h` screen (pixels, y down) through view-projection `vp` (row vectors): false behind
// the eye. Off the screen still projects (a box test wants it to fail on its own).
inline bool Project(const float* vp,const float* p,float w,float h,float* sx,float* sy) noexcept {
    float c[4];
    for(int k=0;k<4;++k)c[k]=p[0]*vp[k]+p[1]*vp[4+k]+p[2]*vp[8+k]+vp[12+k];
    if(!(c[3]>1e-3f))return false;
    *sx=w*0.5f+w*0.5f*c[0]/c[3];*sy=h*0.5f-h*0.5f*c[1]/c[3];
    return std::isfinite(*sx+*sy);
}

// The ray under screen point (x, y): the eye (where clip w is 0, as hud.cpp CameraRay finds it) and a unit direction.
inline bool ScreenRay(const float* vp,float w,float h,float x,float y,float* eye,float* dir) noexcept {
    float inv[16];
    if(!(w>0.0f && h>0.0f) || !Invert4(vp,inv))return false;
    const float nx=(x-w*0.5f)/(w*0.5f),ny=(h*0.5f-y)/(h*0.5f);
    const float atEye[4]={0.0f,0.0f,1.0f,0.0f},ahead[4]={nx,ny,0.5f,1.0f};
    float e[4],a[4];
    for(int k=0;k<4;++k) {
        e[k]=atEye[0]*inv[k]+atEye[1]*inv[4+k]+atEye[2]*inv[8+k]+atEye[3]*inv[12+k];
        a[k]=ahead[0]*inv[k]+ahead[1]*inv[4+k]+ahead[2]*inv[8+k]+ahead[3]*inv[12+k];
    }
    if(!(std::fabs(e[3])>1e-9f) || !(std::fabs(a[3])>1e-9f))return false;
    for(int i=0;i<3;++i){eye[i]=e[i]/e[3];dir[i]=a[i]/a[3]-eye[i];}
    const float len=std::sqrt(dir[0]*dir[0]+dir[1]*dir[1]+dir[2]*dir[2]);
    if(!(len>1e-6f) || !std::isfinite(len))return false;
    for(int i=0;i<3;++i)dir[i]/=len;
    return std::isfinite(eye[0]+eye[1]+eye[2]);
}

// The pointer awaits the first rendered view: a map opens before that view exists. Resizing keeps its normalized
// position; callers cancel a drag when this returns true, because its starting corner was in the previous viewport.
struct PointerPosition { float x=0.0f,y=0.0f,w=0.0f,h=0.0f; bool placed=false; };
inline bool FitPointer(PointerPosition& p,float w,float h) noexcept {
    if(!(w>0.0f && h>0.0f) || !std::isfinite(w+h))return false;
    const bool changed=!p.placed || p.w!=w || p.h!=h;
    if(!changed)return false;
    p.x=p.placed ? p.x*w/p.w : w*0.5f;
    p.y=p.placed ? p.y*h/p.h : h*0.5f;
    p.x=std::fmax(0.0f,std::fmin(p.x,w-1.0f));p.y=std::fmax(0.0f,std::fmin(p.y,h-1.0f));
    p.w=w;p.h=h;p.placed=true;
    return true;
}
// Actual command input also selects the map's input source. Merely having a controller connected does not.
inline bool UsingPad(bool wasPad,bool mouseOrKey,bool padPress) noexcept {
    return mouseOrKey ? false : padPress ? true : wasPad;
}

// --- The selection ---
constexpr int kMaxSel=96;
struct Selection {
    int n=0;
    const void* id[kMaxSel]{};
    bool Has(const void* v) const noexcept { for(int i=0;i<n;++i)if(id[i]==v)return true;return false; }
    void Add(const void* v) noexcept { if(v && !Has(v) && n<kMaxSel)id[n++]=v; }
    void Remove(const void* v) noexcept { for(int i=0;i<n;++i)if(id[i]==v){id[i]=id[--n];return;} }
    void Clear() noexcept { n=0; }
};
// A unit as the selection sees it: its id and where its icon is on the screen (`on`: in front of the eye). `pick`: the
// map's box, click and cycle take it. A squad a mission script drives and a squad riding a vehicle are shown but not
// picked (the user, 2026-10-09: "总有一些我不能指挥的" -- a box over the field took 30 units of which none took an
// order; "坦克上的应该不能统一框选…怎么我在操作坦克还能操作里面的人" -- the crew's squad sits on its vehicle's icon and
// came along in every box). A riding squad is still picked from the squad panel (its row), for its dismount.
struct Mark { const void* id; float x,y; bool on; bool pick=true; };

inline int IndexOf(const void* const* ids,int n,const void* id) noexcept {
    for(int i=0;i<n;++i)if(ids[i]==id)return i;
    return -1;
}

// The selection still standing: the units no longer listed (gone, withdrawn, taken over by the player) dropped.
inline void Keep(Selection& s,const void* const* ids,int n) noexcept {
    for(int i=s.n-1;i>=0;--i)if(IndexOf(ids,n,s.id[i])<0)s.Remove(s.id[i]);
}
// Every listed unit selected (ALL, as the cycle names it): more than one, all of them.
inline bool IsAll(const Selection& s,int n) noexcept { return n>1 && s.n==n; }

// One `step` (+1 next, -1 previous) through ids[0..n) one unit at a time and then ALL, wrapping; from no selection (or a
// boxed one that is neither a single unit nor all) the first going forward, ALL going back.
inline void Cycle(Selection& s,const void* const* ids,int n,int step) noexcept {
    Keep(s,ids,n);
    if(n<=0){s.Clear();return;}
    const int all=n>1 ? n : -2;   // one unit: no ALL position
    const int span=n>1 ? n+1 : 1;
    int at=s.n==1 ? IndexOf(ids,n,s.id[0]) : IsAll(s,n) ? all : (step>0 ? -1 : span);
    at=((at+step)%span+span)%span;
    s.Clear();
    if(at==all)for(int i=0;i<n;++i)s.Add(ids[i]);
    else s.Add(ids[at]);
}

// A box (screen corners in any order): the units whose icon is inside it, added to the selection (`add`, Shift) or in
// its place.
inline void Box(Selection& s,const Mark* m,int n,float x0,float y0,float x1,float y1,bool add) noexcept {
    const float lx=x0<x1 ? x0 : x1,hx=x0<x1 ? x1 : x0,ly=y0<y1 ? y0 : y1,hy=y0<y1 ? y1 : y0;
    if(!add)s.Clear();
    for(int i=0;i<n;++i)if(m[i].on && m[i].pick && m[i].x>=lx && m[i].x<=hx && m[i].y>=ly && m[i].y<=hy)s.Add(m[i].id);
}

// The mark on the screen nearest (x, y) within `radius` px: its index, or -1. A unit's icon under a click, the enemy under
// the pointer (an enemy may have several marks: its lock points, each at its point and up its pin).
inline int Nearest(const Mark* m,int n,float x,float y,float radius) noexcept {
    int best=-1;
    float bestD2=radius*radius;
    for(int i=0;i<n;++i) {
        if(!m[i].on)continue;
        const float dx=m[i].x-x,dy=m[i].y-y,d2=dx*dx+dy*dy;
        if(d2<=bestD2){bestD2=d2;best=i;}
    }
    return best;
}

// A click at (x, y): the unit whose icon is nearest within `radius` px becomes the selection (`add`, Shift: it is added,
// or taken out when it is in); none there clears it (Shift: left as it is). A unit not picked (`pick` false) leaves the
// selection as it is: the caller says why it takes no order. The unit clicked, or nullptr.
// A picked unit in reach wins over one not picked nearer (a crew's squad sits on its vehicle's icon).
inline const void* Click(Selection& s,const Mark* m,int n,float x,float y,float radius,bool add) noexcept {
    int i=-1;
    float bestD2=radius*radius;
    for(int k=0;k<n;++k) {
        if(!m[k].on || !m[k].pick)continue;
        const float dx=m[k].x-x,dy=m[k].y-y,d2=dx*dx+dy*dy;
        if(d2<=bestD2){bestD2=d2;i=k;}
    }
    if(i<0) {
        const int other=Nearest(m,n,x,y,radius);
        if(other>=0)return m[other].id;
    }
    const void* best=i>=0 ? m[i].id : nullptr;
    if(!add)s.Clear();
    if(best && add && s.Has(best))s.Remove(best);
    else if(best)s.Add(best);
    return best;
}

// --- The keys ---
// The keys' presses this frame (edges).
struct Press { bool next,prev,guard,follow,release,engage,focus,board,dismount,dismiss,recruit,move,attackMove; };
enum class Refusal : std::uint8_t { none, noUnit, noPoint, online, noMark };
// What a frame's presses come to: a command to the selection (issue), or why not (why). The cycle (next / prev) has
// already moved the selection (Cycle).
struct Step { bool issue; Command cmd; Refusal why; };

// The order a frame's presses give (one at a time, in this order of precedence); false with none pressed.
inline bool Wanted(const Press& p,Order* o) noexcept {
    if(p.move)*o=Order::move;
    else if(p.attackMove)*o=Order::attackMove;
    else if(p.guard)*o=Order::guard;
    else if(p.follow)*o=Order::follow;
    else if(p.release)*o=Order::none;
    else if(p.engage)*o=Order::engage;
    else if(p.focus)*o=Order::focus;
    else if(p.board)*o=Order::board;
    else if(p.dismount)*o=Order::dismount;
    else if(p.dismiss)*o=Order::dismiss;
    else if(p.recruit)*o=Order::recruit;
    else return false;
    return true;
}

// `marked`: an enemy is marked now (the focus order's target).
inline Step Decide(int selected,const Press& p,bool allowed,const float* point,bool pointOk,bool marked=true) noexcept {
    Step s{false,Command{Order::none,{0.0f,0.0f,0.0f}},Refusal::none};
    Order want=Order::none;
    if(!Wanted(p,&want))return s;
    if(!allowed)s.why=Refusal::online;
    else if(selected<=0)s.why=Refusal::noUnit;
    else if(PointOrder(want) && !pointOk)s.why=Refusal::noPoint;
    else if(want==Order::focus && !marked)s.why=Refusal::noMark;
    if(s.why!=Refusal::none)return s;
    s.issue=true;s.cmd.order=want;
    if(PointOrder(want))for(int i=0;i<3;++i)s.cmd.at[i]=point[i];
    return s;
}

// --- The formation ---
// Slot `i` of `k` units sent to guard `at`: one alone stands on the point; more stand in rings `spacing` m apart round
// it (6 on the first ring, 12 on the second, ...), the first ring's first slot ahead along +z. Level offsets only (the
// caller puts each slot on the ground).
inline void Formation(int i,int k,const float* at,float spacing,float* out) noexcept {
    out[0]=at[0];out[1]=at[1];out[2]=at[2];
    if(k<=1 || i<=0)return;   // slot 0: the point itself
    int ring=1,first=1;
    while(i>=first+6*ring){first+=6*ring;++ring;}
    const int onRing=6*ring,slot=i-first;
    const float a=6.2831853f*static_cast<float>(slot)/static_cast<float>(onRing);
    const float r=spacing*static_cast<float>(ring);
    out[0]+=std::sin(a)*r;out[2]+=std::cos(a)*r;
}
}  // namespace mapcmd
