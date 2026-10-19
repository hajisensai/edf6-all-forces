// The map's commands, their pure part (src/mapcmd.cpp drives it; tools/map_cmd_check.cpp checks it offline): the
// screen and the ground (projection, the ray under a screen point), the selection (a set of units: box, click, cycle)
// and what a key press does, the guard formation. No game, no Windows.
//  - The screen: the map's view-projection as the HUD draws with it (row vectors, hud.cpp Project): a unit's screen
//    point is its icon's, the ray under a screen point comes from the inverse (as hud.cpp CameraRay makes the centre's).
//    The map ray gives where that ray meets the ground or a roof; with no hit (past the world's edge, the sea) its
//    meeting with the level plane through the focus stands in (RayLevel).
//  - The selection (the user, 2026-10-06: "操作 需要一个框选吧"): a set of units. A left drag boxes them in (Ctrl too; Shift
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
// meets on the way, then guard it). withdraw (transport_logic.h, the user 2026-10-09: "卡车之类的运输载具改成断剑那种操作方式"):
// a paired squad's transport leaves the field (a support vehicle: removed once out). dismount / dismountAll (the user,
// 2026-10-09: "下车是除了驾驶员，炮手等操作席都下车。全体下车是全部人"): DISMOUNT takes the squad's soldiers off the
// passenger seats only (its driver and gunners keep the vehicle going: LeavesSeat), DISMOUNT ALL every one of them.
// Appended: the values are the map command wire's (command_protocol.cpp).
// hold (the user, 2026-10-19: 「操作手感看看能不能再往rts进化进化」, RTS's H "hold position"): every unit guards the
// spot it stands on now, each its own (no formation round one point): the authority reads where its unit is.
enum class Order : std::uint8_t { none, guard, follow, engage, focus, board, dismount, dismiss, recruit, move, attackMove, withdraw,
                                  dismountAll, hold };
inline constexpr Order kLastOrder=Order::hold;
// The seats a dismount empties, by the seat's role (npcai.cpp SeatPriority: 0 the driver's, 1 an armed seat, 2 a
// passenger's): DISMOUNT the passengers', DISMOUNT ALL every one.
inline constexpr int kSeatDriver=0,kSeatGunner=1,kSeatPassenger=2;
inline bool LeavesSeat(int seatRole,bool all) noexcept { return all || seatRole>=kSeatPassenger; }
inline bool DismountOrder(Order o) noexcept { return o==Order::dismount || o==Order::dismountAll; }
// The orders that take the ground point under the pointer (and stand round it in a formation).
inline bool PointOrder(Order o) noexcept { return o==Order::guard || o==Order::move || o==Order::attackMove; }
// The orders that put a unit somewhere to stand: the point orders, and HOLD (its own spot).
inline bool PlaceOrder(Order o) noexcept { return PointOrder(o) || o==Order::hold; }
// Whether a vehicle unit (heli, jet, crawler, tank) takes `o`: the point orders, hold, follow and release; the squads take all.
inline bool VehicleOrder(Order o) noexcept { return o==Order::none || o==Order::follow || PlaceOrder(o); }
// A vehicle's modules know one point order (their post / anchor): move, attack-move and hold (its own spot) reach it as guard.
inline Order VehicleCommandOf(Order o) noexcept { return PlaceOrder(o) ? Order::guard : o; }
// Whether an aircraft unit (heli, jet) takes `o`: a vehicle's orders and focus fire on the marked enemy (the user,
// 2026-10-09: "飞机没办法指定攻击目标"; jet.cpp / heli.cpp JetCommand / HeliCommand take it as a squad's focus).
inline bool AirOrder(Order o) noexcept { return VehicleOrder(o) || o==Order::focus; }
struct Command { Order order; float at[3]; };   // at: the point of a point order (on the ground)

// --- The squads' moves under an order (npcai.cpp Drive), the player's command over the soldiers' own fight ---
// The user, 2026-10-09: "一个移动攻击是全消灭再走。一个移动攻击是先走次要消灭" -- the two RTS moves:
//  - forced (move, right click): every member walks to its place by the point before anything of its own (dodging,
//    falling back hurt, its combat spot); it still turns on and fires at what is in reach as it goes;
//  - fightFirst (attack-move, Z / G): the squad stops where it meets enemies and fights them with its own moves
//    (dodging, its combat spot) until the ground round it is clear, then walks on. The squad holds as one
//    (AttackMoveHolds), not each soldier on its own target, so it does not string out between those fighting and
//    those walking on;
//  - neither: the order's anchor as before (guard holds its point's radius, engage / focus their reach).
struct Pursuit { bool forced,fightFirst; };
inline Pursuit PursuitOf(Order o) noexcept { return Pursuit{o==Order::move,o==Order::attackMove}; }
// An attack-move's squad holds to fight while any member had a target last frame or this one (`target`: what it looks
// for round itself, its leash plus its weapons' reach) -- the ground round it is clear when none has. A target none of
// them can shoot at (behind a wall, out of every weapon's reach) would hold it for ever: so it holds only while it fights,
// `sinceShot` ms since any member last fired, or for `grace` ms from the hold's start for the first shot. A squad past
// that walks on (firing on the way as a move does) and holds again at its next shot.
inline constexpr unsigned long long kAttackMoveGraceMs=8000;
inline bool AttackMoveHolds(bool target,unsigned long long sinceHold,unsigned long long sinceShot,
                            unsigned long long grace=kAttackMoveGraceMs) noexcept {
    return target && (sinceHold<grace || sinceShot<grace);
}
// Whether a member walks on to the order's point this frame (npcai.cpp Drive); `holds`: its squad's AttackMoveHolds.
inline bool Pursues(Pursuit p,bool holds) noexcept { return p.forced || (p.fightFirst && !holds); }
// A move / attack-move whose squad has reached its point (its top within `radius`, level) is a guard of that point from
// then on: the members hold round it and fight what comes, as a guard order does. Others stay as they are.
inline Order Arrive(Order o,float distance,float radius) noexcept {
    return (o==Order::move || o==Order::attackMove) && distance<=radius ? Order::guard : o;
}
// The right mouse button let go without a drag on the map (RTS, by what is under the pointer): with units selected, an
// enemy is attacked (focus fire on it), the player's own mark is followed (FOLLOW), else the ground is the move's point;
// nothing selected or nothing there: none (Order::none with `issue` false). An armed button's targeting is cancelled by it
// instead (mapcmd.cpp). With Shift the move is queued after the ones before it (Waypoints).
struct RightClick { bool issue; Order order; };
inline RightClick RightClickOrder(int selected,bool enemyUnder,bool pointOk,bool onPlayer=false) noexcept {
    if(selected<=0)return RightClick{false,Order::none};
    if(enemyUnder)return RightClick{true,Order::focus};
    if(onPlayer)return RightClick{true,Order::follow};
    return RightClick{pointOk,Order::move};
}

// --- A squad's recruitment on the map ---
// A squad the map offers to recruit (its RECRUIT order lit, its panel row "free"): on foot and nobody's (no player's,
// no script's), not cooling down from a dismissal. A squad riding a vehicle (the plugin's real crews, a support's
// soldiers) is that vehicle's crew: no recruitment is offered for it (the user, 2026-10-09: "载具上的npc还标着可以招募
// 的标记"), its vehicle is the unit the map commands.
// `joinable`: a script's squad the stock would have let a player walk up to and join (no leader, its +0x540 set). With
// the stock walk-up gone (VanillaRecruit=0, the user 2026-10-11: "删了原版的率领npc机制，和指挥跟随重复了") the map's
// RECRUIT is the only way in, so a squad a mission waits for the player to take stays recruitable from the map.
inline bool OffersRecruit(bool ownedByPlayer,bool scripted,bool dismissed,bool riding,bool joinable=false) noexcept {
    return !ownedByPlayer && (!scripted || joinable) && !dismissed && !riding;
}

// --- The squad panel (the user, 2026-10-09: "小队太多了吧，怎么处理合适，感觉看不过来"; the log's mission had 37) ---
// Rows in the order a commander wants them (npcai.cpp SquadRows sorts by it, stable): the player's own squads, the free
// ones it may command or recruit, the ones riding a vehicle (their vehicles are the map's units), a script's last.
inline int SquadRank(bool recruited,bool scripted,bool riding) noexcept { return scripted ? 3 : riding ? 2 : recruited ? 0 : 1; }
inline constexpr int kSquadRowsFolded=9;   // the rows folded (the number keys are the RTS groups' since 2026-10-19)
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
// guard (G, pad Y) is an attack-move since 2026-10-09 (the user: "看看还有什么指令能砍一砍"): an attack-move turns
// into a guard of its point once there, so the two buttons were one; the key is kept.
struct Press { bool next,prev,guard,follow,release,engage,focus,board,dismount,dismiss,recruit,move,attackMove,dismountAll,withdraw,hold; };
enum class Refusal : std::uint8_t { none, noUnit, noPoint, online, noMark };
// What a frame's presses come to: a command to the selection (issue), or why not (why). The cycle (next / prev) has
// already moved the selection (Cycle).
struct Step { bool issue; Command cmd; Refusal why; };

// The order a frame's presses give (one at a time, in this order of precedence); false with none pressed.
inline bool Wanted(const Press& p,Order* o) noexcept {
    if(p.move)*o=Order::move;
    else if(p.attackMove || p.guard)*o=Order::attackMove;
    else if(p.hold)*o=Order::hold;
    else if(p.follow)*o=Order::follow;
    else if(p.release)*o=Order::none;
    else if(p.engage)*o=Order::engage;
    else if(p.focus)*o=Order::focus;
    else if(p.board)*o=Order::board;
    else if(p.dismountAll)*o=Order::dismountAll;
    else if(p.dismount)*o=Order::dismount;
    else if(p.dismiss)*o=Order::dismiss;
    else if(p.recruit)*o=Order::recruit;
    else if(p.withdraw)*o=Order::withdraw;
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

// --- RTS groups (the user, 2026-10-19: 「合并分组经常无效，分开也是，感觉你这个分组是不是和rts不太一样」) ---
// A group is what the player selected, nothing in the game: the native squads stay as they are. Ctrl+1..9 keeps the
// selection as group N (in its place), Ctrl+Shift+N adds the selection to group N, N selects group N (Shift: added to
// the selection), N pressed twice within kDoubleTapMs selects it and centres the map on it. The SPLIT / JOIN of the
// native follow trees (P / L) they replace changed nothing a player sees for the squads following them (the march lays
// all of them out as one formation round the player) and took the squad lowest in memory as the one joined.
inline constexpr int kGroups=9;
inline constexpr unsigned long long kDoubleTapMs=350;
enum class GroupAct : std::uint8_t { none, assign, append, recall, add, centre };
// What a number key's press does (`again`: the same key's second press within kDoubleTapMs, DoubleTap).
inline GroupAct GroupKey(bool ctrl,bool shift,bool again) noexcept {
    if(ctrl)return shift ? GroupAct::append : GroupAct::assign;
    if(shift)return GroupAct::add;
    return again ? GroupAct::centre : GroupAct::recall;
}
// The second press of the same key within `window` ms of the first (a third starts over).
struct Tap { int key=-1; unsigned long long at=0; };
inline bool DoubleTap(Tap& t,int key,unsigned long long ms,unsigned long long window=kDoubleTapMs) noexcept {
    const bool again=t.key==key && ms>=t.at && ms-t.at<=window;
    if(again){t.key=-1;t.at=0;}
    else{t.key=key;t.at=ms;}
    return again;
}
// A pad button that does one thing tapped and another held (`holdMs`): tap on its release before then, hold once.
enum class TapHold : std::uint8_t { none, tap, hold };
struct Held { bool down=false,fired=false; unsigned long long since=0; };
inline TapHold StepHeld(Held& h,bool down,unsigned long long ms,unsigned long long holdMs) noexcept {
    if(down && !h.down){h.down=true;h.fired=false;h.since=ms;return TapHold::none;}
    if(down) {
        if(!h.fired && ms-h.since>=holdMs){h.fired=true;return TapHold::hold;}
        return TapHold::none;
    }
    const bool was=h.down && !h.fired;
    h=Held{};
    return was ? TapHold::tap : TapHold::none;
}
inline constexpr unsigned long long kPadHoldMs=600;   // a pad button held this long: its hold
// The pad's groups (no number keys there): the D-pad's up, right, down, left are groups 1-4; a tap selects (twice:
// centres), held kPadHoldMs it keeps the selection as that group. Its group index, -1 none.
inline int PadGroup(int dpad) noexcept { return dpad>=0 && dpad<4 ? dpad : -1; }

// A unit's kind for a double-click (RTS: every unit of the kind on the screen): its name up to " x" (a squad's
// "Ranger x4" is a Ranger squad whatever its count). True when `a` and `b` are one kind.
inline bool SameKind(const char* a,const char* b) noexcept {
    if(!a || !b)return false;
    for(int i=0;i<64;++i) {
        const bool endA=!a[i] || (a[i]==' ' && a[i+1]=='x'),endB=!b[i] || (b[i]==' ' && b[i+1]=='x');
        if(endA || endB)return endA && endB;
        if(a[i]!=b[i])return false;
    }
    return true;
}
// Every unit on the screen of `kind[i]`'s kind as unit `at`'s (and its owner `owner[i]`), into the selection (`add`: added).
inline int SelectKind(Selection& s,const Mark* m,const char* const* kind,const int* owner,int n,int at,bool add) noexcept {
    if(at<0 || at>=n || !m[at].pick)return 0;
    if(!add)s.Clear();
    int k=0;
    for(int i=0;i<n;++i)if(m[i].on && m[i].pick && owner[i]==owner[at] && SameKind(kind[i],kind[at])){s.Add(m[i].id);++k;}
    return k;
}

// --- Shift-queued waypoints (RTS: Shift + right click, Shift + Z) ---
// A unit's points after the one it is on its way to: `issued` the order given it last (from here, or from the map before
// the first Shift), the rest in `next`. Each frame (map open or not) its order now and where it is: once it is within
// `reach` (level) of `issued`'s point the next is given; an order now that is not `issued` (another order given it,
// released, a module's own) drops the queue: the player took it over.
inline constexpr int kWaypointsMost=8;
struct Waypoints { bool active=false; Command issued{}; int n=0; Command next[kWaypointsMost]{}; };
inline float Level(const float* a,const float* b) noexcept {
    const float dx=a[0]-b[0],dz=a[2]-b[2];
    return std::sqrt(dx*dx+dz*dz);
}
inline constexpr float kSamePoint=5.0f;   // m: a module's order at its point within this is the one given
inline bool SameOrder(const Command& now,const Command& issued) noexcept {
    return PlaceOrder(issued.order) && PointOrder(now.order) && Level(now.at,issued.at)<=kSamePoint;
}
// Shift with `c` for a unit whose order now is `now` at `pos`: true when `c` is to be given now (nothing queued and the
// unit not on its way anywhere); else it is queued (false; a full queue keeps its first kWaypointsMost).
inline bool Enqueue(Waypoints& w,const Command& now,const float* pos,float reach,const Command& c) noexcept {
    if(w.active && SameOrder(now,w.issued)) {
        if(w.n<kWaypointsMost)w.next[w.n++]=c;
        return false;
    }
    if(PointOrder(now.order) && Level(pos,now.at)>reach) {   // on its way: that point first, then this one
        w=Waypoints{};w.active=true;w.issued=now;w.next[w.n++]=c;
        return false;
    }
    w=Waypoints{};w.active=true;w.issued=c;
    return true;
}
// A plain order (no Shift): the queue gone, `c` the order (followed while it is a place order).
inline void Replace(Waypoints& w,const Command& c) noexcept {
    w=Waypoints{};
    if(PlaceOrder(c.order)){w.active=true;w.issued=c;}
}
enum class QueueStep : std::uint8_t { idle, wait, give, done, dropped };
inline QueueStep Advance(Waypoints& w,const Command& now,const float* pos,float reach,Command* out) noexcept {
    if(!w.active)return QueueStep::idle;
    if(!SameOrder(now,w.issued)){w=Waypoints{};return QueueStep::dropped;}
    if(Level(pos,w.issued.at)>reach)return QueueStep::wait;
    if(!w.n){w=Waypoints{};return QueueStep::done;}
    *out=w.next[0];
    for(int i=1;i<w.n;++i)w.next[i-1]=w.next[i];
    --w.n;w.issued=*out;
    return QueueStep::give;
}
}  // namespace mapcmd
