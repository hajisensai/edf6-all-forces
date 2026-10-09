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
    formation, split, merge,                                                                     // the squads' shape and fireteams
    sweep, health,                                                                               // the box sweep, its health-box switch
    count
};
constexpr int kCount=static_cast<int>(Id::count);
// The card's order (the user, 2026-10-09: "解散解除交战集火是不是重叠了"): moving, fighting, vehicles, membership, then
// the squads' tools; CLEAR ORDER (release) last among the orders, apart from DISMISS it was mistaken for.
constexpr Id kCardOrder[kCount]={Id::move,Id::attackMove,Id::guard,Id::follow,Id::engage,Id::focus,Id::board,Id::dismount,
                                 Id::withdraw,Id::recruit,Id::dismiss,Id::release,Id::formation,Id::split,Id::merge,Id::sweep,
                                 Id::health};
constexpr int kOrders=static_cast<int>(Id::formation);
struct Rect { float x0,y0,x1,y1; };

// The order a button gives (an order button: Id below formation).
inline mapcmd::Order OrderOf(Id b) noexcept {
    using mapcmd::Order;
    static const Order kOrder[kOrders]={Order::move,Order::attackMove,Order::guard,Order::follow,Order::none,Order::engage,
                                         Order::focus,Order::board,Order::dismount,Order::dismiss,Order::recruit,Order::withdraw};
    const int i=static_cast<int>(b);
    return i>=0 && i<kOrders ? kOrder[i] : Order::none;
}
inline bool IsOrder(Id b) noexcept { return static_cast<int>(b)>=0 && static_cast<int>(b)<kOrders; }
// An order button arms a click on the map (its point) instead of acting at once.
inline bool Arms(Id b) noexcept { return IsOrder(b) && mapcmd::PointOrder(OrderOf(b)); }
// Shown on the card: an order the selection takes (`allowedOrders`: Order bits), a squad tool with squads selected,
// the sweep and its switch always (they work on the recruited squads with nothing selected).
inline bool Shown(Id b,std::uint32_t allowedOrders,bool squadTools) noexcept {
    if(IsOrder(b))return (allowedOrders&(1u<<static_cast<unsigned>(OrderOf(b))))!=0;
    if(b==Id::sweep || b==Id::health)return true;
    return squadTools;
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

// The button under (x, y) (-1 none) of the `n` laid out.
inline int Hit(const Rect* r,int n,float x,float y) noexcept {
    for(int i=0;i<n;++i)if(x>=r[i].x0 && x<r[i].x1 && y>=r[i].y0 && y<r[i].y1)return i;
    return -1;
}

// --- The formation menu (the user, 2026-10-09: "这个编队应该点击以后展开选择里面的东西") ---
// The formation button opens a column of the shapes to pick from (before, each click stepped to the next one: the log's
// "formation: … the march column / staggered column / wedge / …" nine clicks in a row to get round to one). Its entries:
// a guarding squad's defences (formation.h kGuard) and the march's shapes (kMarch), as MenuEntry codes.
constexpr int kMenuGuard=0x100;
inline int MenuEntry(bool guard,int shape) noexcept { return (guard ? kMenuGuard : 0)|(shape&0xFF); }
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
        if(code>=crew::kSupportWeaponCount || !ComposeRoom(l,seats))return false;
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
// The kinds of a class in the panel's row order (support_call.h SupportSoldierClass).
inline int ComposeKinds(int cls,int* out,int most) noexcept {
    int n=0;
    for(int k=0;k<crew::kSupportWeaponCount && n<most;++k)
        if(crew::SupportSoldierClass(static_cast<crew::SupportWeapon>(k))==cls)out[n++]=k;
    return n;
}
// Pixel sizes at 1080 lines (times the HUD scale `s`).
constexpr float kComposePad=6.0f,kComposeTitleH=22.0f,kComposeSquadH=22.0f,kComposeGap=3.0f,
                kComposeLabelW=66.0f,kComposeKindW=56.0f,kComposeKindH=22.0f,kComposeHintH=18.0f;
constexpr int kComposeClasses=3;
struct ComposeLayout {
    Rect box{},title{},hint{};
    int slots=0;Rect slot[kComposeSquadsMost]{};   // a squad's place each (the seats / kSquad), one row
    Rect classLabel[kComposeClasses]{};
    int kinds=0;Rect kind[crew::kSupportWeaponCount]{};int kindCode[crew::kSupportWeaponCount]{};
};
inline float ComposeWidth(float s) noexcept {
    return (2*kComposePad+kComposeLabelW+5*kComposeKindW+4*kComposeGap)*s;
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
