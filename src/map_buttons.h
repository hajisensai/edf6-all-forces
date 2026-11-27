// The map's command card and support bar (the user, 2026-10-08: "the M map should do all of it, best by clicking the
// HUD"; 2026-10-09: "m里面的支援招募弄成一列带图标的hud并且可以点击操作吧，而且m里面显示太复杂了，让他使用更简单更方便一
// 点，可以参考各大rts游戏"). The pure part: what the buttons are, their layout and which one a click is on.
//  - The command card (hud.cpp MapButtons): one icon button with a short word for each order the selection takes and
//    for the squads' tools, laid out by Flow from the drawn words' widths; with nothing selected only the tools that
//    need no selection. Each button's key (and the full word) is in its tooltip, not on it.
//  - The support bar (hud.cpp MapSupportBar): one row a kind of support, its icon and its name, the catalog's
//    variants of it (守点 / 跟随, 有人 / 空车交付: the names after their "·") as small icon chips at the row's right
//    (GroupSupport, Column). A click on a row or a chip arms that call; the next left click on the map is its point.
// hud.cpp hands the rectangles it drew to mapcmd.cpp (MapCommandButtons, MapCommandSupportButtons), which tests a click
// against exactly what was drawn (Hit) before taking it for a unit. tools/map_buttons_check.cpp runs it offline.
#pragma once
#include "mapcmd_logic.h"
#include "support_call.h"
#include <cstddef>
#include <cwchar>

namespace mapbtn {
enum class Id : int {
    move, attackMove, guard, follow, release, engage, focus, board, dismount, dismiss, recruit,   // the orders
    withdraw,                                                                                    // ...a squad's transport sent off
    dismountAll,                                                                                 // ...every rider off, its crew too
    hold,                                                                                        // ...each unit where it stands
    reverseMove, unloadAt, holdFire, altitude, sprint, decoy,                                    // ...Broken Arrow's (2026-11-27)
    formation,                                                                                   // the squads' shape (a menu)
    sweep, health,                                                                               // the box sweep, its health-box switch
    pace,                                                                                        // the paced march's switch
    count
};
constexpr int kCount=static_cast<int>(Id::count);
// The card (the user, 2026-11-27: 「感觉很多没用的指令。删一点或者整合一下，不然太复杂了」; Broken Arrow's card: the
// moves, one switch a rule of engagement, the vehicles' orders folded into one, a kind's own only with that kind
// selected): one row of at most kCardMostUsual for a usual selection (squads on foot: MOVE, ATTACK-MOVE, HOLD, HOLD FIRE,
// STOP, FOLLOW, BOARD, DISMOUNT, WITHDRAW, SPRINT, FORMATION, SWEEP). Off it, their abilities kept (README 「指挥面板」):
//  - GUARD: an attack-move guards its point once there (G gives one);
//  - FAST MOVE: retired, a move judges its own pace (mapcmd_logic.h MoveRushes);
//  - UNLOAD AT, ALL OUT: rows of DISMOUNT's menu (kDismountMenu below; U + a click unloads there, U twice everyone off
//    where they stand: map_keys.h);
//  - RECRUIT: FOLLOW (an unrecruited squad told to follow the player is recruited: npcai.cpp NpcSquadCommandForRequester);
//  - DISMISS: WITHDRAW (a recruited squad's withdrawal is its dismissal unless its transport is a support vehicle that
//    leaves: npcai.cpp NpcSquadCommandForRequester, transport.cpp TransportWithdrawable; B stays);
//  - SMOKE: retired (no unit of the plugin carries smoke);
//  - the health boxes' and the paced march's switches: rows of FORMATION's menu (kMenuHealth / kMenuPace).
// ENGAGE AT WILL and FOCUS FIRE were never on it (an attack-move on the spot; the right button on an enemy, H).
constexpr Id kCardOrder[]={Id::move,Id::attackMove,Id::reverseMove,Id::hold,Id::holdFire,Id::release,Id::follow,Id::board,
                           Id::dismount,Id::withdraw,Id::altitude,Id::sprint,Id::decoy,Id::formation,Id::sweep};
constexpr int kCardCount=static_cast<int>(sizeof(kCardOrder)/sizeof(kCardOrder[0]));
constexpr int kCardMostUsual=12;   // the most a usual selection shows (tools/map_buttons_check.cpp)
constexpr int kOrders=static_cast<int>(Id::formation);
struct Rect { float x0,y0,x1,y1; };

// The order a button gives (an order button: Id below formation). DISMOUNT's button opens its menu instead.
inline mapcmd::Order OrderOf(Id b) noexcept {
    using mapcmd::Order;
    static const Order kOrder[kOrders]={Order::move,Order::attackMove,Order::guard,Order::follow,Order::none,Order::engage,
                                         Order::focus,Order::board,Order::dismount,Order::dismiss,Order::recruit,Order::withdraw,
                                         Order::dismountAll,Order::hold,Order::reverseMove,Order::unloadAt,
                                         Order::holdFire,Order::altitude,Order::sprint,Order::decoy};
    const int i=static_cast<int>(b);
    return i>=0 && i<kOrders ? kOrder[i] : Order::none;
}
inline bool IsOrder(Id b) noexcept { return static_cast<int>(b)>=0 && static_cast<int>(b)<kOrders; }
// Whether `b` is a button of the card at all.
inline bool OnCard(Id b) noexcept {
    for(const Id c:kCardOrder)if(c==b)return true;
    return false;
}
// A button that opens a menu (its rows the choices: kDismountMenu, the formation menu) instead of acting.
inline bool OpensMenu(Id b) noexcept { return b==Id::dismount || b==Id::formation; }
// An order button arms a click on the map (its point) instead of acting at once.
inline bool Arms(Id b) noexcept { return IsOrder(b) && !OpensMenu(b) && mapcmd::PointOrder(OrderOf(b)); }

// --- Each button's state for the selection (the user, 2026-11-27: 「各种按钮只在能操作的时候点亮。车上都是满人怎么上车」) ---
// The card's summary of the selection (mapcmd.cpp Publish): its counts and the orders any of it takes (`takes`: Order
// bits, mapcmd.cpp Takes). `boardRoom`: the seats its squads on foot could take now (npcai.cpp NpcBoardRoom) -- in the
// selected carriers when any is selected (BOARD goes into those), else in the one BOARD would pick. `carriers`: the
// selected vehicles squads get into at all (mapcmd.cpp Carrier: not a jet; `vehicles` counts every non-squad unit).
// `boardOff`: a squad of this machine's selected while its squads do not board (npcai.h NpcBoardingOn).
struct CardSelection {
    int units=0,squads=0,onFoot=0,riding=0,scripted=0,vehicles=0,aircraft=0,jets=0;
    int boardRoom=0;
    std::uint32_t takes=0;
    int carriers=0;
    bool boardOff=false;
};
// Why a button cannot be used now (none: it can; hidden: not on the card for this selection). Its hover text
// (hudtext.inc whyCannot...).
enum class Why : std::uint8_t { none, hidden, cannot, scripted, allRiding, noneRiding, noRoom, noRoomSelected, noDecoys, noWithdraw,
                                noRoomThat,     // noRoomThat: the vehicle the right button was let go on is full (mapcmd.cpp)
                                boardOff };     // squads do not board here (NpcBoarding=0, or the native ride not as read)
inline bool TakesAny(const CardSelection& c,mapcmd::Order o) noexcept { return (c.takes>>static_cast<unsigned>(o))&1u; }
// A button's state: shown or not by what is selected (a kind's own only with that kind), then whether it works (drawn
// greyed with its reason when not; its click and key do nothing but say why, map_refuse). With nothing selected only
// SWEEP (it sends the recruited squads).
inline Why StateOf(Id b,const CardSelection& c) noexcept {
    using mapcmd::Order;
    if(!OnCard(b))return Why::hidden;
    if(b==Id::sweep)return c.units==0 || c.squads>0 ? Why::none : Why::hidden;
    if(c.units<=0)return Why::hidden;
    const bool allScripted=c.scripted>=c.units;
    const Why refused=allScripted ? Why::scripted : Why::cannot;
    switch(b) {
    // The tanks', the transports' (a ground vehicle selected, or squads riding: their transport's driver). Squads on foot
    // take it too (a move walked backing off) but by R alone: its button would push their usual card past one row.
    case Id::reverseMove:
        return (c.vehicles>c.aircraft || c.riding>0) && TakesAny(c,Order::reverseMove) ? Why::none : Why::hidden;
    case Id::board:
        if(c.squads<=0)return Why::hidden;
        if(allScripted)return Why::scripted;
        if(c.onFoot<=0)return Why::allRiding;
        if(!TakesAny(c,Order::board))return Why::cannot;
        return c.boardRoom>0 ? Why::none : c.boardOff ? Why::boardOff : c.carriers>0 ? Why::noRoomSelected : Why::noRoom;
    case Id::dismount:
        if(c.squads<=0 && c.vehicles<=0)return Why::hidden;
        if(TakesAny(c,Order::dismountAll) || TakesAny(c,Order::unloadAt) || TakesAny(c,Order::dismount))return Why::none;
        return allScripted ? Why::scripted : Why::noneRiding;
    case Id::withdraw:
        if(c.squads<=0 && c.aircraft<=0)return Why::hidden;
        return TakesAny(c,Order::withdraw) || TakesAny(c,Order::dismiss) ? Why::none : allScripted ? Why::scripted : Why::noWithdraw;
    case Id::follow:
        return TakesAny(c,Order::follow) || TakesAny(c,Order::recruit) ? Why::none : refused;
    case Id::altitude: return c.aircraft<=0 ? Why::hidden : TakesAny(c,Order::altitude) ? Why::none : refused;
    case Id::sprint: return c.squads<=0 && c.jets<=0 ? Why::hidden : TakesAny(c,Order::sprint) ? Why::none : refused;
    case Id::decoy: return c.jets<=0 ? Why::hidden : TakesAny(c,Order::decoy) ? Why::none : allScripted ? Why::scripted : Why::noDecoys;
    case Id::formation:
        if(c.squads<=0)return Why::hidden;
        if(TakesAny(c,Order::formation))return Why::none;
        return allScripted ? Why::scripted : c.onFoot<=0 ? Why::allRiding : Why::cannot;
    default: return TakesAny(c,OrderOf(b)) ? Why::none : refused;   // the moves, HOLD, HOLD FIRE, STOP
    }
}
inline bool Shown(Id b,const CardSelection& c) noexcept { return StateOf(b,c)!=Why::hidden; }
inline bool Available(Id b,const CardSelection& c) noexcept { return StateOf(b,c)==Why::none; }
// A key's order (map_keys.h) against the selection: the state of its button when one shows, else whether any of the
// selection takes it (the keys of orders off the card: G, U, H...). Why::hidden never: a key always answers.
inline Why OrderState(mapcmd::Order o,const CardSelection& c) noexcept {
    using mapcmd::Order;
    for(int i=0;i<kOrders;++i) {
        const Id b=static_cast<Id>(i);
        if(OrderOf(b)==o && OnCard(b) && !OpensMenu(b)) {
            const Why w=StateOf(b,c);
            if(w!=Why::hidden)return w;
        }
    }
    if(TakesAny(c,o))return Why::none;
    if(c.units>0 && c.scripted>=c.units)return Why::scripted;
    if(o==Order::dismountAll || o==Order::dismount || o==Order::unloadAt)return c.units>0 ? Why::noneRiding : Why::cannot;
    if(o==Order::board && c.squads>0 && c.onFoot<=0)return Why::allRiding;
    return Why::cannot;
}

// Lays `n` buttons of widths `w[i]` (px, the label's own plus its padding) out in rows `rowH` high, `gap` apart, each
// row centred on the screen (`width`) and no wider than it less `margin` either side; the first row's bottom at
// `bottom`, further rows above it. Returns the rows used (0: no room even for one button).
inline int Flow(const float* w,int n,float width,float bottom,float rowH,float gap,float margin,Rect* out) noexcept {
    const float most=width-2.0f*margin;
    int rows=0,i=0;
    while(i<n) {
        // The buttons of this row: as many as fit (at least one, shrunk to the row when it alone is wider).
        int k=i;
        float used=0.0f;
        while(k<n) {
            const float add=(k>i ? gap : 0.0f)+w[k];
            if(k>i && used+add>most)break;
            used+=add;
            ++k;
            if(used>=most)break;
        }
        if(used>most)used=most;
        const float y1=bottom-static_cast<float>(rows)*(rowH+gap),y0=y1-rowH;
        if(y0<0.0f)return rows;
        float x=(width-used)*0.5f;
        for(int j=i;j<k;++j) {
            const float bw=w[j]<most ? w[j] : most;
            out[j]=Rect{x,y0,x+bw,y1};
            x+=bw+gap;
        }
        ++rows;
        i=k;
    }
    return rows;
}

// Broken Arrow's action panel (the user, 2026-11-27: 「这个操作方式和显示都抄抄断箭吧」): `n` cells alike, `cellW` x `rowH`,
// `gap` apart, in the screen's bottom-right corner (the right column at `width-margin`), as many columns as fit in
// `share` of the width less the margin (at least one, at most `n`); filled left to right from the top row, the bottom
// row's bottom at `bottom`. A cell wider than the screen less its margins is cut to it. Rows past the screen's top are
// left out (their cells zero: x1 == x0). Returns the rows used.
inline int Grid(int n,float cellW,float width,float share,float bottom,float rowH,float gap,float margin,Rect* out) noexcept {
    for(int i=0;i<n;++i)out[i]=Rect{};
    if(n<=0)return 0;
    const float cw=cellW<width-2.0f*margin ? cellW : width-2.0f*margin;
    if(!(cw>0.0f))return 0;
    const float room=width*share-margin;
    int cols=static_cast<int>((room+gap)/(cw+gap));
    if(cols<1)cols=1;
    if(cols>n)cols=n;
    int rows=(n+cols-1)/cols;
    const int most=static_cast<int>((bottom+gap)/(rowH+gap));
    if(rows>most)rows=most<0 ? 0 : most;
    const float x0=width-margin-static_cast<float>(cols)*cw-static_cast<float>(cols-1)*gap;
    for(int r=0;r<rows;++r) {
        const float y1=bottom-static_cast<float>(rows-1-r)*(rowH+gap),y0=y1-rowH;
        for(int c=0;c<cols;++c) {
            const int i=r*cols+c;
            if(i>=n)break;
            const float x=x0+static_cast<float>(c)*(cw+gap);
            out[i]=Rect{x,y0,x+cw,y1};
        }
    }
    return rows;
}

// Broken Arrow's selection panel: `n` cards `cardW` x `cardH`, `gap` apart, from `left` rightwards up to `right`, rows
// upwards from `bottom` (the bottom one first) no higher than `top`. Returns the cards placed (the rest have no room).
inline int Cards(int n,float cardW,float cardH,float left,float right,float top,float bottom,float gap,Rect* out) noexcept {
    const int cols=static_cast<int>((right-left+gap)/(cardW+gap));
    if(cols<1 || n<=0)return 0;
    int placed=0;
    for(int i=0;i<n;++i) {
        const int r=i/cols,c=i%cols;
        const float y1=bottom-static_cast<float>(r)*(cardH+gap),y0=y1-cardH;
        if(y0<top)break;
        const float x=left+static_cast<float>(c)*(cardW+gap);
        out[i]=Rect{x,y0,x+cardW,y1};
        ++placed;
    }
    return placed;
}

// The selection's cards (Cards): the selected units grouped by kind (mapcmd::SameKind) and owner, in the order first
// seen; each group's first unit `first` and its count. Returns the groups (at most `most`).
struct CardGroup { int first; int count; };
inline int GroupCards(const char* const* kind,const int* owner,int n,CardGroup* out,int most) noexcept {
    int k=0;
    for(int i=0;i<n;++i) {
        int at=-1;
        for(int j=0;j<k && at<0;++j)if(owner[out[j].first]==owner[i] && mapcmd::SameKind(kind[out[j].first],kind[i]))at=j;
        if(at>=0)++out[at].count;
        else if(k<most)out[k++]=CardGroup{i,1};
    }
    return k;
}

// The button under (x, y) (-1 none) of the `n` laid out.
inline int Hit(const Rect* r,int n,float x,float y) noexcept {
    for(int i=0;i<n;++i)if(x>=r[i].x0 && x<r[i].x1 && y>=r[i].y0 && y<r[i].y1)return i;
    return -1;
}

// --- The formation menu (the user, 2026-10-09: "这个编队应该点击以后展开选择里面的东西") ---
// The formation button opens a column of the shapes to pick from (before, each click stepped to the next one: the log's
// "formation: … the march column / staggered column / wedge / …" nine clicks in a row to get round to one). Its entries:
// a guarding squad's defences (formation.h kGuard) and the march's shapes (kMarch), as MenuEntry codes.
// Its last rows (2026-11-27, the card trimmed) are the squads' two switches that were buttons of their own: the paced
// march (kMenuPace) and the health boxes on a sweep (kMenuHealth), each lit when on.
constexpr int kMenuGuard=0x100,kMenuPace=0x200,kMenuHealth=0x400;
inline int MenuEntry(bool guard,int shape) noexcept { return (guard ? kMenuGuard : 0)|(shape&0xFF); }
inline bool MenuSwitch(int entry) noexcept { return entry==kMenuPace || entry==kMenuHealth; }
inline bool MenuGuard(int entry) noexcept { return (entry&kMenuGuard)!=0; }
inline int MenuShape(int entry) noexcept { return entry&0xFF; }
// The column of `n` rows `rowW` x `rowH`, `gap` apart, over `button` (its bottom row on the button's top; under the button
// when there is no room above), its left on the button's left, kept on a `width` x `height` screen. Returns the rows
// placed (fewer when the screen runs out).
inline int MenuColumn(const Rect& button,int n,float rowW,float rowH,float gap,float width,float height,Rect* out) noexcept {
    if(n<=0)return 0;
    const float total=static_cast<float>(n)*rowH+static_cast<float>(n-1)*gap;
    float y0=button.y0-gap-total;
    if(y0<0.0f)y0=button.y1+gap;
    float x0=button.x0;
    if(x0+rowW>width)x0=width-rowW;
    if(x0<0.0f)x0=0.0f;
    int placed=0;
    for(int i=0;i<n;++i) {
        const float y=y0+static_cast<float>(i)*(rowH+gap);
        if(y+rowH>height)break;
        out[i]=Rect{x0,y,x0+rowW,y+rowH};
        ++placed;
    }
    return placed;
}

// --- The ALL OUT button's menu (test site #45, 2026-11-10: 「如何让某个角色单独下车」; #44: 「预设下车指令：全体下车只留司机等」) ---
// The button opens a column like the formation menu's (MenuColumn, drawn over the same rows: one menu at a time): every
// rider off, every rider but the driver, one class's riders (the driver kept). Each row is DISMOUNT ALL with its choice
// (mapcmd_logic.h DismountParamValid), its entry code kMenuDismount | the choice. U twice gives every rider off at once.
constexpr int kMenuDismount=0x10000;
constexpr std::uint32_t kDismountMenu[]={0u,mapcmd::kDismountKeepDriver,mapcmd::kDismountKeepDriver|mapcmd::kDismountRanger,
                                         mapcmd::kDismountKeepDriver|mapcmd::kDismountWingDiver,
                                         mapcmd::kDismountKeepDriver|mapcmd::kDismountAirRaider,
                                         mapcmd::kDismountKeepDriver|mapcmd::kDismountFencer};
constexpr int kDismountMenuRows=static_cast<int>(sizeof(kDismountMenu)/sizeof(kDismountMenu[0]));
inline int DismountEntry(std::uint32_t param) noexcept { return kMenuDismount|static_cast<int>(param&0xFFFFu); }
inline bool MenuDismount(int entry) noexcept { return (entry&kMenuDismount)!=0; }
inline std::uint32_t MenuDismountParam(int entry) noexcept { return static_cast<std::uint32_t>(entry)&0xFFFFu; }
// 2026-11-27 (the card trimmed) the button is DISMOUNT and its menu has the dismount orders all: its first row the selected
// squads off their rides (DISMOUNT: kMenuDismountSquads), then ALL OUT's rows above (a vehicle's riders), and last
// UNLOAD AT (kMenuUnloadAt: arms the point a transport drives to and unloads). A row shows when some of the selection takes
// its order (mapcmd.cpp Takes). Their codes are outside a dismount param's bits (DismountParamValid: below 0x1000).
constexpr int kMenuDismountSquads=kMenuDismount|0x1000,kMenuUnloadAt=kMenuDismount|0x2000;
inline mapcmd::Order MenuDismountOrder(int entry) noexcept {
    return entry==kMenuDismountSquads ? mapcmd::Order::dismount : entry==kMenuUnloadAt ? mapcmd::Order::unloadAt
                                                                                        : mapcmd::Order::dismountAll;
}

// --- The support bar ---
// The catalog's entries a row (`names`, in catalog order): the consecutive ones whose names share what is before
// their "·" (U+00B7) are one kind of support with variants; a name with none is a row of its own.
struct Group { int first,count; };
inline int BaseLength(const wchar_t* name) noexcept {
    if(!name)return 0;
    const wchar_t* dot=std::wcschr(name,L'\u00B7');
    return static_cast<int>(dot ? dot-name : static_cast<std::ptrdiff_t>(std::wcslen(name)));
}
// The variant's word (after the "·"), or nullptr for a name with none.
inline const wchar_t* VariantOf(const wchar_t* name) noexcept {
    const wchar_t* dot=name ? std::wcschr(name,L'\u00B7') : nullptr;
    return dot ? dot+1 : nullptr;
}
inline int GroupSupport(const wchar_t* const* names,int n,Group* out,int most) noexcept {
    int rows=0;
    for(int i=0;i<n && rows<most;) {
        const int base=BaseLength(names[i]);
        int k=i+1;
        while(VariantOf(names[i]) && k<n && VariantOf(names[k]) && BaseLength(names[k])==base &&
              std::wcsncmp(names[k],names[i],static_cast<std::size_t>(base))==0)++k;
        out[rows++]=Group{i,k-i};
        i=k;
    }
    return rows;
}
// Lays the rows out top-down from `top` between x0 and x1, `rowH` high and `gap` apart, as many as end above `bottom`;
// a row with variants gets a square chip `rowH` - 2 `inset` wide for each at its right end, `gap` apart (`chip`,
// indexed by catalog entry); a row of one entry has its chip on the whole row. Returns the rows placed.
inline int Column(const Group* g,int rows,float x0,float x1,float top,float bottom,float rowH,float gap,float inset,
                  Rect* row,Rect* chip) noexcept {
    int placed=0;
    for(int r=0;r<rows;++r) {
        const float y0=top+static_cast<float>(r)*(rowH+gap),y1=y0+rowH;
        if(y1>bottom)break;
        row[r]=Rect{x0,y0,x1,y1};
        if(g[r].count<=1){chip[g[r].first]=row[r];++placed;continue;}
        const float side=rowH-2.0f*inset;
        float right=x1-inset;
        for(int k=g[r].count-1;k>=0;--k) {
            chip[g[r].first+k]=Rect{right-side,y0+inset,right,y1-inset};
            right-=side+gap;
        }
        ++placed;
    }
    return placed;
}

// The rows of a tab that fit between `top` and `bottom` (Column's count for rows enough).
inline int ColumnFit(float top,float bottom,float rowH,float gap) noexcept {
    if(bottom<top+rowH || rowH<=0.0f)return 0;
    return 1+static_cast<int>((bottom-top-rowH)/(rowH+gap));
}
// The first row a tab longer than its column shows (2026-11-27: every flown support in one tab, more rows than fit): the
// one it scrolled to (`top`), kept inside [0, rows - fit], moved the least that puts row `reveal` in the window (-1: none;
// [ ] picked an entry the window does not show).
inline int ColumnWindow(int rows,int fit,int top,int reveal) noexcept {
    const int most=rows>fit ? rows-fit : 0;
    top=top<0 ? 0 : top>most ? most : top;
    if(reveal>=0 && reveal<rows && fit>0) {
        if(reveal<top)top=reveal;
        else if(reveal>=top+fit)top=reveal-fit+1;
    }
    return top;
}

// The bar's tabs (support_call.h SupportTab) in its hit list: a tab's code, below every catalog entry (-1 is none).
constexpr int kSupportTabHit=-2;
constexpr int SupportTabHit(int tab) noexcept { return kSupportTabHit-tab; }
constexpr int SupportTabOfHit(int entry) noexcept { return kSupportTabHit-entry; }   // >= 0: a tab's

// --- The support bar's composition panel (the user, 2026-10-09: "支援栏是断剑那种，先点载具，然后选里面的人并且可以点多次，
// 直到座位满"; then: a click puts in a whole squad, not one soldier) ---
// A support with seats (support_call.h SupportCallSeats: the infantry, the transports, a crewed APC / truck) armed from the
// bar opens a panel beside it: its squads so far (a click on one takes it out, the ones after it move up), the seats they
// use over the seats there are, and a row of squad kinds a class (Ranger, Wing Diver, Fencer). A click on a kind puts in
// one more squad of it (kSquad soldiers: its leader template and three members); dim when the seats left hold no squad.
// It starts from the load the call brings uncomposed (SupportCallPreset: the ini's weapons); the next left click on the
// map sends it (SupportCallComposedAt). Every squad is its own squad aboard (a leader every four, support_call.h).
// A click's code: a kind (0..kSupportWeaponCount-1), or squad j as -1 - j.
constexpr int kSquad=4;
constexpr int kComposeSquadsMost=crew::kSupportLoadoutMost/kSquad;
constexpr int ComposeSquadCode(int squad) noexcept { return -1-squad; }
constexpr int kComposeItems=kComposeSquadsMost+crew::kSupportWeaponCount;
inline int ComposeSquads(const crew::SupportLoadout& l) noexcept { return (l.count+kSquad-1)/kSquad; }
// Whether one more squad fits the seats.
inline bool ComposeRoom(const crew::SupportLoadout& l,int seats) noexcept {
    if(seats>crew::kSupportLoadoutMost)seats=crew::kSupportLoadoutMost;
    return l.count+kSquad<=seats;
}
// The kind squad j shows (its members'; a lone leader's own).
inline crew::SupportWeapon ComposeSquadKind(const crew::SupportLoadout& l,int j) noexcept {
    const int i=j*kSquad;
    return i+1<l.count ? l.soldier[i+1] : i<l.count ? l.soldier[i] : crew::SupportWeapon::rifle;
}
inline bool ComposeApply(crew::SupportLoadout& l,int seats,int code) noexcept {
    if(code>=0) {
        if(code>=crew::kSupportWeaponCount || !crew::SupportKindInstalled(static_cast<crew::SupportWeapon>(code)) ||
           !ComposeRoom(l,seats))return false;
        for(int k=0;k<kSquad;++k)l.soldier[l.count++]=static_cast<crew::SupportWeapon>(code);
        return true;
    }
    const int squad=-1-code,from=squad*kSquad;
    if(squad<0 || from>=l.count)return false;
    const int to=from+kSquad<l.count ? from+kSquad : l.count,gone=to-from;
    for(int i=from;i+gone<l.count;++i)l.soldier[i]=l.soldier[i+gone];
    for(int i=l.count-gone;i<l.count;++i)l.soldier[i]=crew::SupportWeapon::rifle;
    l.count-=gone;return true;
}
// The installed kinds of a class in the panel's row order (support_call.h SupportSoldierClass, SupportKindInstalled).
inline int ComposeKinds(int cls,int* out,int most) noexcept {
    int n=0;
    for(int k=0;k<crew::kSupportWeaponCount && n<most;++k)
        if(crew::SupportSoldierClass(static_cast<crew::SupportWeapon>(k))==cls &&
           crew::SupportKindInstalled(static_cast<crew::SupportWeapon>(k)))out[n++]=k;
    return n;
}
// Pixel sizes at 1080 lines (times the HUD scale `s`).
constexpr float kComposePad=6.0f,kComposeTitleH=22.0f,kComposeSquadH=22.0f,kComposeGap=3.0f,
                kComposeLabelW=66.0f,kComposeKindW=56.0f,kComposeKindH=22.0f,kComposeHintH=18.0f;
constexpr int kComposeClasses=3;
// The most kinds any one class has (a row of the panel): the width fits it.
constexpr int ComposeKindsMost() noexcept {
    int most=0;
    for(int c=0;c<kComposeClasses;++c) {
        int n=0;
        for(int k=0;k<crew::kSupportWeaponCount;++k)n+=crew::SupportSoldierClass(static_cast<crew::SupportWeapon>(k))==c;
        if(n>most)most=n;
    }
    return most;
}
constexpr int kComposeKindsMost=ComposeKindsMost();
struct ComposeLayout {
    Rect box{},title{},hint{};
    int slots=0;Rect slot[kComposeSquadsMost]{};   // a squad's place each (the seats / kSquad), one row
    Rect classLabel[kComposeClasses]{};
    int kinds=0;Rect kind[crew::kSupportWeaponCount]{};int kindCode[crew::kSupportWeaponCount]{};
};
inline float ComposeWidth(float s) noexcept {
    return (2*kComposePad+kComposeLabelW+static_cast<float>(kComposeKindsMost)*kComposeKindW+
            static_cast<float>(kComposeKindsMost-1)*kComposeGap)*s;
}
inline float ComposeHeight(float s) noexcept {
    return (2*kComposePad+kComposeTitleH+kComposeSquadH+kComposeGap+kComposeClasses*(kComposeKindH+kComposeGap)+kComposeHintH)*s;
}
// The panel with its left edge at `x0`, its top at `top` but moved up to end above `bottom` (never above `ceiling`).
inline ComposeLayout ComposePanel(float x0,float top,float ceiling,float bottom,int seats,float s) noexcept {
    ComposeLayout L;
    if(seats>crew::kSupportLoadoutMost)seats=crew::kSupportLoadoutMost;
    const int slots=seats>0 ? (seats+kSquad-1)/kSquad : 0;
    const float w=ComposeWidth(s),h=ComposeHeight(s);
    if(top+h>bottom)top=bottom-h;
    if(top<ceiling)top=ceiling;
    L.box=Rect{x0,top,x0+w,top+h};
    const float in=kComposePad*s,gap=kComposeGap*s;
    float y=top+in;
    L.title=Rect{x0+in,y,x0+w-in,y+kComposeTitleH*s};y+=kComposeTitleH*s;
    L.slots=slots;
    if(slots>0) {
        const float sw=(w-2*in-static_cast<float>(kComposeSquadsMost-1)*gap)/static_cast<float>(kComposeSquadsMost);
        for(int j=0;j<slots;++j){const float sx=x0+in+static_cast<float>(j)*(sw+gap);L.slot[j]=Rect{sx,y,sx+sw,y+kComposeSquadH*s};}
    }
    y+=kComposeSquadH*s+gap;
    for(int c=0;c<kComposeClasses;++c) {
        L.classLabel[c]=Rect{x0+in,y,x0+in+kComposeLabelW*s,y+kComposeKindH*s};
        int kinds[crew::kSupportWeaponCount];const int n=ComposeKinds(c,kinds,crew::kSupportWeaponCount);
        for(int k=0;k<n && L.kinds<crew::kSupportWeaponCount;++k) {
            const float kx=x0+in+kComposeLabelW*s+static_cast<float>(k)*(kComposeKindW*s+gap);
            L.kind[L.kinds]=Rect{kx,y,kx+kComposeKindW*s,y+kComposeKindH*s};L.kindCode[L.kinds++]=kinds[k];
        }
        y+=kComposeKindH*s+gap;
    }
    L.hint=Rect{x0+in,y,x0+w-in,y+kComposeHintH*s};
    return L;
}
}  // namespace mapbtn
