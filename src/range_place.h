// The M map's 「摆放」 panel in the test range (the user, 2026-10-17: 「测试场支持m里面手动摆放怪物和各种载具之类的」): its
// decisions apart from the game, so tests/range_place_test.cpp checks them off it. mapcmd.cpp reads the keys, the pad and
// the clicks and steps the panel here; debug_spawn.cpp PlaceRow makes the row at the map's ground point (the same
// catalog and the same making as the debug spawn tool: src/debug_spawn.h kEntries); hud.cpp draws the panel.
//  - a tab a category (debug_spawn.h Category), a window of kWindow rows of it round the one picked;
//  - the mouse (as the support bar): a click on a row picks and arms it, the next left clicks on the map place it there
//    (it stays armed: one after another), the right button or a click on it again disarms it; a tab's click shows that
//    category; the arrows page through the rows;
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
constexpr int kWindow=12;   // rows shown at once

struct Panel {
    bool open=false;
    int category=0;
    int pick[kCategoryCount]{};   // the row picked in each category (its place in it), kept while another is shown
    int armed=-1;                 // the kEntries row the map's next left click places; -1 none
};
// One frame's presses (each an edge). `row`: a row clicked (kEntries index, -1 none); `tab`: a category's tab clicked;
// `page`: an arrow (-1 up, +1 down, 0 none).
struct Press {
    bool toggle=false,close=false,prev=false,next=false,catPrev=false,catNext=false,place=false,cancel=false;
    int row=-1,tab=-1,page=0;
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
// The first row (its place in the category) of the window of `window` rows round `pick` of `count`.
constexpr int WindowFirst(int pick,int count,int window) noexcept {
    if(count<=window || pick<window/2)return 0;
    return pick-window/2+window>count ? count-window : pick-window/2;
}

// One frame of the panel. One action a frame, in this order: shut (toggle / close), a row's click (picks and arms it;
// the armed one again: disarmed), a tab, cancel (disarms), place, the moves (opening a shut panel). A shut panel keeps
// nothing armed.
inline Result Step(Panel& p,const Press& k) noexcept {
    if(p.category<0 || p.category>=kCategoryCount)p.category=0;
    if(k.toggle || (k.close && p.open)) {
        const bool was=p.open;
        p.open=k.toggle ? !p.open : false;p.armed=-1;
        return {p.open ? Act::opened : was ? Act::closed : Act::none,-1};
    }
    if(k.row>=0 && k.row<debugspawn::kEntryCount) {
        const int c=static_cast<int>(debugspawn::kEntries[k.row].category);
        p.open=true;p.category=c;p.pick[c]=IndexIn(k.row);
        if(p.armed==k.row){p.armed=-1;return {Act::disarmed,k.row};}
        p.armed=k.row;return {Act::armed,k.row};
    }
    if(k.tab>=0 && k.tab<kCategoryCount){p.open=true;p.category=k.tab;return {Act::moved,-1};}
    if(k.cancel && p.armed>=0){const int was=p.armed;p.armed=-1;return {Act::disarmed,was};}
    if(k.place && p.open){const int row=Picked(p);return {row>=0 ? Act::place : Act::none,row};}
    const bool move=k.prev || k.next || k.catPrev || k.catNext || k.page!=0;
    if(!move)return {};
    const bool opened=!p.open;
    p.open=true;
    if(opened)return {Act::opened,-1};   // the first press shows the panel as it was
    if(k.catPrev || k.catNext) {
        p.category=(p.category+(k.catNext ? 1 : kCategoryCount-1))%kCategoryCount;
        return {Act::moved,-1};
    }
    const int n=debugspawn::CountIn(static_cast<Category>(p.category));
    int& at=p.pick[p.category];
    if(n<=0){at=0;return {};}
    if(at<0 || at>=n)at=0;
    if(k.page)at=at+k.page*kWindow<0 ? 0 : at+k.page*kWindow>=n ? n-1 : at+k.page*kWindow;
    else at=(at+(k.next ? 1 : n-1))%n;
    return {Act::moved,-1};
}
}  // namespace rangeplace
}  // namespace crew
