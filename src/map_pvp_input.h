#pragma once
#include "pvp.h"
#include <cstring>

namespace crew::pvpmenu {
constexpr int kMode=0,kTeam1=1,kTeam2=2,kItems=3;
inline bool Same(const PvpStatus& a,const PvpStatus& b) noexcept {
    return a.epoch==b.epoch && a.revision==b.revision && a.enabled==b.enabled &&
        std::memcmp(a.localPuid,b.localPuid,sizeof(a.localPuid))==0;
}
inline bool Allowed(const PvpStatus& status,int code) noexcept {
    return status.ready && !status.pending && status.epoch && status.localPuid[0] &&
        (code==kMode ? status.host : code==kTeam1 || code==kTeam2);
}
struct Pad { bool focused=false,held=false;int selected=kTeam1; };
struct Input { bool toggle=false,up=false,down=false,confirm=false,back=false,a=false,b=false,mouse=false; };
struct Result { bool consumed=false;int action=-1; };
inline Result Step(Pad& pad,const PvpStatus& status,bool visible,const Input& input) noexcept {
    const bool wasFocused=pad.focused,wasHeld=pad.held;
    const bool available=visible && status.ready && !status.pending && status.epoch && status.localPuid[0];
    if(!available || input.mouse)pad.focused=false;
    if(input.toggle && available && !input.mouse)pad.focused=!pad.focused;
    if(pad.focused && !Allowed(status,pad.selected))pad.selected=kTeam1;
    // Leaving focus does not end an A/B gesture: all downstream map commands wait for its release.
    Result result;result.consumed=wasFocused || pad.focused || (input.toggle && available) ||
        (wasHeld && (input.a || input.b));
    if(pad.focused) {
        const int first=status.host ? kMode : kTeam1;
        if(input.up)pad.selected=pad.selected==first ? kTeam2 : pad.selected-1;
        else if(input.down)pad.selected=pad.selected==kTeam2 ? first : pad.selected+1;
        if(input.back)pad.focused=false;
        else if(input.confirm && Allowed(status,pad.selected))result.action=pad.selected;
    }
    pad.held=pad.focused || ((wasFocused || wasHeld) && (input.a || input.b));
    return result;
}
} // namespace crew::pvpmenu
