// The map's support pt (support_points_logic.h; the user, 2026-10-26: 「所有插件支援改成m里面消耗pt召唤」): this machine's
// pool, charged from its local player's team score as the stock pt weapons are, spent by the support bar's calls
// (support_dispatch.cpp SupportCallComposedAt). Game thread.
#pragma once
#include <cstdint>

namespace crew {
void SupportPointsReset() noexcept;        // a new mission (mission.cpp): SupportPtStart, nothing seen yet
void SupportPointsTick() noexcept;         // once a frame (crew.cpp SharedFrameTick): the team score's rise taken
std::int32_t SupportPoints() noexcept;     // what the pool holds
std::int32_t SupportPointsMost() noexcept; // SupportPtMost
std::int32_t SupportPointsPrice(std::uint32_t pt) noexcept;   // a price: an entry's pt times SupportPtScale
bool SupportPointsSpend(std::int32_t price) noexcept;         // false: the pool holds less (nothing taken)
void SupportPointsRefund(std::int32_t price) noexcept;        // a call taken that came to nothing (offline): its pt back
// An online call's price, spent and owed to support_net's `request` until SupportPointsSettle (`back`: it came to nothing,
// its pt back). False: the pool holds less (nothing taken).
bool SupportPointsOwe(std::int32_t price,std::uint32_t request) noexcept;
void SupportPointsSettle(std::uint32_t request,bool back) noexcept;
}  // namespace crew
