// The M map's 「摆放」 panel in the test range (the user, 2026-10-17: 「测试场支持m里面手动摆放怪物和各种载具之类的」): its
// decisions apart from the game, so tests/range_place_test.cpp checks them off it. mapcmd.cpp reads the keys, the pad and
// the clicks and steps the panel here; debug_spawn.cpp PlaceRow makes the row at the map's ground point (the same
// catalog and the same making as the debug spawn tool: src/debug_spawn.h kEntries); hud.cpp draws the panel.
//  - a tab a category (debug_spawn.h Category), a scrolling list of its rows (as many as the screen has room for);
//  - the mouse (as the support bar): a click on a row picks and arms it, the next left clicks on the map place it there
//    (it stays armed: one after another), the right button or a click on it again disarms it; a tab's click shows that
//    category; the wheel over the list scrolls it, the arrows page through the rows;
//  - the keyboard: , / . the previous / next row, / the next category (each opens the panel), Enter places the row
//    picked at the pointer, Backspace shuts the panel;
//  - a pad (the map has no pointer then: its point is the screen's centre): D-pad up / down the row, left / right the
//    category (each opens the panel), A places the row picked at the centre, B shuts the panel (the map's own A / B, the
//    centring and the closing, wait while the panel is open: map.cpp asks MapPlaceHoldsPad).
#pragma once
#include "debug_spawn.h"

namespace crew {
namespace rangeplace {
using debugspawn::Category;
using debugspawn::kCategoryCount;
constexpr int kWindow=16;   // rows shown at most (fewer when the screen has less room: Panel::window)
constexpr int kWheelRows=3; // rows a wheel notch scrolls

// The list scrolls on its own (`top`, the first row shown of each category) and the pick stays in view: a page
// (the arrows) moves both by the rows shown, the wheel scrolls the list (the pick dragged along only when it would leave
// the view), the keys / D-pad move the pick (the list follows it). `window` is how many rows the HUD drew last
// (hud.cpp MapPlacePanel, published with its click targets); the arrows page by that, never past what was seen.
struct Panel {
    bool open=false;
    int category=0;
    int pick[kCategoryCount]{};   // the row picked in each category (its place in it), kept while another is shown
    int top[kCategoryCount]{};    // the first row shown of each category
    int window=kWindow;           // the rows the HUD has room for (>= 1)
    int armed=-1;                 // the kEntries row the map's next left click places; -1 none
};
// One frame's presses (each an edge). `row`: a row clicked (kEntries index, -1 none); `tab`: a category's tab clicked;
// `page`: an arrow (-1 up, +1 down, 0 none); `scroll`: the wheel over the list (rows, + down).
struct Press {
    bool toggle=false,close=false,prev=false,next=false,catPrev=false,catNext=false,place=false,cancel=false;
    int row=-1,tab=-1,page=0,scroll=0;
};
enum class Act : std::uint8_t { none, opened, closed, moved, armed, disarmed, place };
struct Result { Act act=Act::none; int row=-1; };

// The row `p` points at in its category (kEntries index), -1 none.
inline int Picked(const Panel& p) noexcept {
    if(p.category<0 || p.category>=kCategoryCount)return -1;
    return debugspawn::RowOf(static_cast<Category>(p.category),p.pick[p.category]);
}
// `row`'s place in its category.
inline int IndexIn(int row) noexcept {
    if(row<0 || row>=debugspawn::kEntryCount)return -1;
    int n=0;
    for(int i=0;i<row;++i)n+=debugspawn::kEntries[i].category==debugspawn::kEntries[row].category;
    return n;
}
constexpr int Clamp(int v,int lo,int hi) noexcept { return v<lo ? lo : v>hi ? hi : v; }
// The first row shown of `count` rows `window` at a time, from the list's own `top`, with `pick` in view.
constexpr int ViewFirst(int top,int pick,int count,int window) noexcept {
    if(window<1)window=1;
    if(count<=window)return 0;
    int first=Clamp(top,0,count-window);
    if(pick>=0 && pick<count) {
        if(pick<first)first=pick;
        if(pick>=first+window)first=pick-window+1;
    }
    return first;
}
// The pages of `count` rows `window` at a time and the one `first` is on (both from 1), for the page line.
constexpr int Pages(int count,int window) noexcept { return window<1 || count<=0 ? 1 : (count+window-1)/window; }
constexpr int PageOf(int first,int count,int window) noexcept {
    if(window<1 || count<=window)return 1;
    return first+window>=count ? Pages(count,window) : first/window+1;
}

// A held key / D-pad direction repeats (a long list is walked by holding it, not by pressing 60 times): the press
// itself, then after kRepeatFirstMs one every kRepeatMs while it stays down. `now` ms.
constexpr unsigned long long kRepeatFirstMs=350,kRepeatMs=60;
struct Repeat { unsigned long long next=0; };
inline bool Repeats(Repeat& r,bool down,bool was,unsigned long long now) noexcept {
    if(!down){r.next=0;return false;}
    if(!was || !r.next){r.next=now+kRepeatFirstMs;return true;}
    if(now<r.next)return false;
    r.next=now+kRepeatMs;
    return true;
}

// One frame of the panel. One action a frame, in this order: shut (toggle / close), a row's click (picks and arms it;
// the armed one again: disarmed), a tab, cancel (disarms), place, the wheel, the moves (opening a shut panel). A shut
// panel keeps nothing armed.
inline Result Step(Panel& p,const Press& k) noexcept {
    if(p.category<0 || p.category>=kCategoryCount)p.category=0;
    if(p.window<1)p.window=1;
    if(k.toggle || (k.close && p.open)) {
        const bool was=p.open;
        p.open=k.toggle ? !p.open : false;p.armed=-1;
        return {p.open ? Act::opened : was ? Act::closed : Act::none,-1};
    }
    if(k.row>=0 && k.row<debugspawn::kEntryCount) {
        const int c=static_cast<int>(debugspawn::kEntries[k.row].category);
        p.open=true;p.category=c;p.pick[c]=IndexIn(k.row);
        p.top[c]=ViewFirst(p.top[c],p.pick[c],debugspawn::CountIn(static_cast<Category>(c)),p.window);
        if(p.armed==k.row){p.armed=-1;return {Act::disarmed,k.row};}
        p.armed=k.row;return {Act::armed,k.row};
    }
    if(k.tab>=0 && k.tab<kCategoryCount){p.open=true;p.category=k.tab;return {Act::moved,-1};}
    if(k.cancel && p.armed>=0){const int was=p.armed;p.armed=-1;return {Act::disarmed,was};}
    if(k.place && p.open){const int row=Picked(p);return {row>=0 ? Act::place : Act::none,row};}
    const int n=debugspawn::CountIn(static_cast<Category>(p.category));
    int& at=p.pick[p.category];
    int& top=p.top[p.category];
    if(k.scroll && p.open) {   // the list moves, the pick only as far as it must to stay in view
        if(n<=p.window){top=0;return {};}
        const int was=top;
        top=Clamp(ViewFirst(top,at,n,p.window)+k.scroll,0,n-p.window);
        at=Clamp(at,top,top+p.window-1);
        return {top!=was ? Act::moved : Act::none,-1};
    }
    const bool move=k.prev || k.next || k.catPrev || k.catNext || k.page!=0;
    if(!move)return {};
    const bool opened=!p.open;
    p.open=true;
    if(opened)return {Act::opened,-1};   // the first press shows the panel as it was
    if(k.catPrev || k.catNext) {
        p.category=(p.category+(k.catNext ? 1 : kCategoryCount-1))%kCategoryCount;
        return {Act::moved,-1};
    }
    if(n<=0){at=0;top=0;return {};}
    if(at<0 || at>=n)at=0;
    if(k.page) {   // a page: the list and the pick by the rows shown, stopping at the ends
        const int first=ViewFirst(top,at,n,p.window);
        const int next=Clamp(first+k.page*p.window,0,n>p.window ? n-p.window : 0);
        at=Clamp(at+(next-first ? next-first : k.page*p.window),0,n-1);
        top=next;
    } else at=(at+(k.next ? 1 : n-1))%n;
    top=ViewFirst(top,at,n,p.window);
    return {Act::moved,-1};
}
}  // namespace rangeplace
}  // namespace crew
