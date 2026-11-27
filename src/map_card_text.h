// The M map's command card's words (map_buttons.h Id order) and its refusals' reasons (map_buttons.h Why): the HUD's
// buttons and tooltips (hud_map.cpp MapButtons) and the refusals' notes (mapcmd.cpp Refuse).
#pragma once
#include "hudtext.h"
#include "map_buttons.h"

namespace crew {
inline const wchar_t* CardWordText(mapbtn::Id b) noexcept {
    using hudtext::Tx;
    static constexpr Tx kWord[mapbtn::kCount]={Tx::orderMove,Tx::orderAttackMove,Tx::orderGuard,Tx::orderFollow,Tx::orderRelease,
        Tx::orderEngage,Tx::orderFocus,Tx::orderBoard,Tx::orderDismount,Tx::orderDismiss,Tx::orderRecruit,Tx::orderWithdraw,
        Tx::orderDismountAll,Tx::orderHold,Tx::orderReverseMove,Tx::orderUnloadAt,Tx::orderHoldFire,Tx::orderAltitude,
        Tx::orderSprint,Tx::orderDecoy,Tx::btnFormationShort,Tx::btnSweep,Tx::btnHealth,Tx::btnPace};
    const int i=static_cast<int>(b);
    return i>=0 && i<mapbtn::kCount ? hudtext::Tr(kWord[i]) : L"?";
}
inline const wchar_t* CardWhyText(mapbtn::Why why) noexcept {
    using hudtext::Tx;using mapbtn::Why;
    switch(why) {
    case Why::scripted: return hudtext::Tr(Tx::whyScripted);
    case Why::allRiding: return hudtext::Tr(Tx::whyAllRiding);
    case Why::noneRiding: return hudtext::Tr(Tx::whyNoneRiding);
    case Why::noRoom: return hudtext::Tr(Tx::whyNoRoom);
    case Why::noRoomSelected: return hudtext::Tr(Tx::whyNoRoomSelected);
    case Why::noRoomThat: return hudtext::Tr(Tx::whyNoRoomThat);
    case Why::boardOff: return hudtext::Tr(Tx::whyBoardOff);
    case Why::noDecoys: return hudtext::Tr(Tx::whyNoDecoys);
    case Why::noWithdraw: return hudtext::Tr(Tx::whyNoWithdraw);
    default: return hudtext::Tr(Tx::whyCannot);
    }
}
}  // namespace crew
