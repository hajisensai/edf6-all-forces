// The map's support pt (support_points_logic.h; the user, 2026-10-26: 「所有插件支援改成m里面消耗pt召唤」): the local
// player's team's pool, charged from the team score as the stock pt weapons are, spent by the support bar's calls
// (support_dispatch.cpp SupportCallComposedAt). Shared by the team (the user, 2026-10-27: 「pt 改成全队互通」): the host
// keeps every team's pool and its settings (SupportPtStart / Most / Scale), a guest sees and spends its team's through
// the SPTS channel (support_points_net.h). Game thread.
#pragma once
#include "plugin_net_logic.h"
#include <cstdint>

namespace crew {
plugin_net::Channel SupportPointsNetChannel() noexcept;   // support_net.cpp RegisterPluginChannels
void SupportPointsReset() noexcept;        // a new mission (mission.cpp): SupportPtStart, nothing seen yet
void SupportPointsTick() noexcept;         // once a frame (crew.cpp SharedFrameTick): the scores' rise taken, the state sent
std::int32_t SupportPoints() noexcept;     // what the local player's team's pool holds (0: their team not known yet)
std::int32_t SupportPointsMost() noexcept; // SupportPtMost (a guest: the host's)
std::int32_t SupportPointsPrice(std::uint32_t pt) noexcept;   // a price: an entry's pt times SupportPtScale (a guest: the host's)
// A call's price, held before the call is asked (support_dispatch.cpp Request), so that paying for it once it is taken
// cannot fail (2026-10-30 review: a guest's change refused for want of room after its request had gone, a call made
// unpaid). Held: taken from the pool at once (a guest: shown spent, the room for its change kept). False (nothing held):
// the pool holds less, a guest has kChangesMost changes the host has not confirmed, or another call's price is held.
// One at a time; each hold ends, before the call returns, in exactly one of:
bool SupportPointsHold(std::int32_t price) noexcept;
void SupportPointsKeepHeld() noexcept;                       // the call taken: its pt spent (a guest: its change sent)
// An online call taken: spent and owed to support_net's `request` until SupportPointsSettle (`back`: it came to nothing,
// its pt back).
void SupportPointsOweHeld(std::uint32_t request) noexcept;
void SupportPointsDropHeld() noexcept;                       // the call not taken: the pool as it was, nothing sent
void SupportPointsRefund(std::int32_t price) noexcept;        // a call taken that came to nothing (offline): its pt back
void SupportPointsSettle(std::uint32_t request,bool back) noexcept;
}  // namespace crew
