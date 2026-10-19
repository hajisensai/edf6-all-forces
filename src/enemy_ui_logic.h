#pragma once
#include <algorithm>
#include <cstdint>
#include <cmath>

// Pure navigation and gesture ownership for the enemy-control card. Native object
// identities, skill availability and requests stay in enemy_control; this state
// only points at an ordinal in one immutable, identity-bound UI publication.
namespace crew::enemyui {
constexpr int kPageRows=6;
constexpr int kBegin=-1,kFire=-2,kExit=-3,kPrev=-4,kNext=-5,kNone=-6;
struct Identity {
    std::uint64_t world=0,owner=0,token=0;
    std::uint32_t target=0;
    std::uint64_t profile=0,request=0;
};
inline bool HealthAvailable(bool known,float current,float maximum) noexcept {
    return known && std::isfinite(current) && std::isfinite(maximum) && current>=0.0f && maximum>0.0f;
}
inline bool Same(const Identity& a,const Identity& b) noexcept {
    return a.world==b.world && a.owner==b.owner && a.token==b.token &&
        a.target==b.target && a.profile==b.profile && a.request==b.request;
}
struct Model { Identity identity{};bool visible=false,active=false;int skills=0;bool cancellable=false; };
struct State { Identity identity{};bool focused=false,held=false;int first=0,row=0; };
struct Input {
    bool toggle=false,up=false,down=false,previous=false,next=false,confirm=false,back=false;
    bool a=false,b=false,mouse=false;
};
struct Result { bool consumed=false;int action=kNone; };
inline int Count(const Model& model,int first) noexcept {
    return model.active ? (std::max)(0,(std::min)(kPageRows,model.skills-first)) : 0;
}
inline int Rows(const Model& model,int first) noexcept { return model.active ? Count(model,first)+2 : 1; }
inline int Action(const Model& model,int first,int row) noexcept {
    if(!model.active)return row==0 ? (model.cancellable ? kExit : kBegin) : kNone;
    const int count=Count(model,first);
    if(row>=0 && row<count)return first+row;
    return row==count ? kFire : row==count+1 ? kExit : kNone;
}
inline void Page(State& state,const Model& model,int direction) noexcept {
    const int pages=model.skills>0 ? 1+(model.skills-1)/kPageRows : 1;
    const int page=(state.first/kPageRows+direction%pages+pages)%pages;
    state.first=page*kPageRows;state.row=0;
}
// A held confirmation that began on SELECT must never become FIRE merely by
// navigating into that row. Leaving it, changing the selected native skill, or
// losing the original identity ends the gesture until a new confirm edge.
inline bool HoldFire(bool& latched,bool start,bool down,bool focused,bool fireRow,bool sameSnapshot) noexcept {
    if(!down || !focused || !fireRow || !sameSnapshot)latched=false;
    if(start && down && focused && fireRow && sameSnapshot)latched=true;
    return latched;
}
inline Result Step(State& state,const Model& model,const Input& in) noexcept {
    const bool wasFocused=state.focused,wasHeld=state.held;
    const bool changed=!Same(state.identity,model.identity);
    if(changed){state.identity=model.identity;state.first=state.row=0;state.focused=false;}
    if(!model.visible || in.mouse)state.focused=false;
    if(state.first<0 || state.first>=model.skills)state.first=0;
    if(in.toggle && model.visible && !in.mouse)state.focused=!state.focused;
    Result out;out.consumed=wasFocused || state.focused || (in.toggle && model.visible) || (wasHeld && (in.a || in.b));
    if(state.focused && !changed) {
        if(in.previous || in.next)Page(state,model,in.previous ? -1 : 1);
        const int rows=Rows(model,state.first);
        state.row=(std::max)(0,(std::min)(state.row,rows-1));
        if(in.up)state.row=(state.row+rows-1)%rows;
        else if(in.down)state.row=(state.row+1)%rows;
        if(in.back)state.focused=false;
        else if(in.confirm)out.action=Action(model,state.first,state.row);
    }
    // A/B never become map centring, close, or placement after leaving this card.
    state.held=state.focused || ((wasFocused || wasHeld) && (in.a || in.b));
    return out;
}
} // namespace crew::enemyui
