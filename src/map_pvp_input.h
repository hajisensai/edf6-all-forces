#pragma once
#include "pvp.h"
#include <cstring>

namespace crew::pvpmenu {
// The panel's rows: the host's mode and relation preset, then one row per faction (pvp_match.h kFactions): a click joins
// it (its default role and unit, pvp.h PvpPickForFaction).
constexpr int kMode=0,kPreset=1,kFaction0=2,kItems=kFaction0+pvp_match::kFactionCount;
constexpr bool FactionCode(int code) noexcept { return code>=kFaction0 && code<kItems; }
constexpr int CodeFaction(int code) noexcept { return FactionCode(code) ? code-kFaction0 : -1; }
inline bool Same(const PvpStatus& a,const PvpStatus& b) noexcept {
    return a.epoch==b.epoch && a.revision==b.revision && a.enabled==b.enabled && a.preset==b.preset &&
        a.local.faction==b.local.faction && std::memcmp(a.localPuid,b.localPuid,sizeof(a.localPuid))==0;
}
inline bool Allowed(const PvpStatus& status,int code) noexcept {
    return status.ready && !status.pending && status.epoch && status.localPuid[0] &&
        (code==kMode || code==kPreset ? status.host : FactionCode(code));
}
struct Pad { bool focused=false,held=false;int selected=kFaction0; };
struct Input { bool toggle=false,up=false,down=false,confirm=false,back=false,a=false,b=false,mouse=false; };
struct Result { bool consumed=false;int action=-1; };
inline Result Step(Pad& pad,const PvpStatus& status,bool visible,const Input& input) noexcept {
    const bool wasFocused=pad.focused,wasHeld=pad.held;
    const bool available=visible && status.ready && !status.pending && status.epoch && status.localPuid[0];
    if(!available || input.mouse)pad.focused=false;
    if(input.toggle && available && !input.mouse)pad.focused=!pad.focused;
    if(pad.focused && !Allowed(status,pad.selected))pad.selected=kFaction0;
    // Leaving focus does not end an A/B gesture: all downstream map commands wait for its release.
    Result result;result.consumed=wasFocused || pad.focused || (input.toggle && available) ||
        (wasHeld && (input.a || input.b));
    if(pad.focused) {
        const int first=status.host ? kMode : kFaction0,last=kItems-1;
        if(input.up)pad.selected=pad.selected<=first ? last : pad.selected-1;
        else if(input.down)pad.selected=pad.selected>=last ? first : pad.selected+1;
        if(input.back)pad.focused=false;
        else if(input.confirm && Allowed(status,pad.selected))result.action=pad.selected;
    }
    pad.held=pad.focused || ((wasFocused || wasHeld) && (input.a || input.b));
    return result;
}
} // namespace crew::pvpmenu
