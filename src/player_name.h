// A player's name as the game's own multiplayer name tag shows it (player_name.cpp; tests/player_name_native_audit.py).
#pragma once
#include <cstddef>

namespace crew {
// The name of the player soldier `player` (any machine's, as this machine's world has it): the game's name tag source
// 0x784830 on the soldier's user (+0x1ED0, its control block +0x1ED8), fitted to `count` (FitName). Game thread.
// False (out empty): no user, an empty name, EDF.dll not as read, or a fault.
bool ReadPlayerName(const void* player,wchar_t* out,std::size_t count) noexcept;

// `name` (`length` characters) fitted to `count` with its terminator: a longer one cut and ended with an ellipsis, control
// characters dropped (a name is shown on one line). Pure (tests/qmark_test.cpp).
inline void FitName(const wchar_t* name,std::size_t length,wchar_t* out,std::size_t count) noexcept {
    if(!out || !count)return;
    std::size_t n=0;
    for(std::size_t i=0;i<length && name && name[i];++i) {
        if(name[i]<0x20 || name[i]==0x7F)continue;
        if(n+1>=count){if(n)out[n-1]=0x2026;break;}
        out[n++]=name[i];
    }
    out[n<count ? n : count-1]=0;
}
}  // namespace crew
