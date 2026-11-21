// Native aircraft boarding points sampled on the game thread; the HUD only consumes this value snapshot.
#pragma once
#include <cstdint>
namespace crew {
// `hail`: no aircraft to board near the player, `at` is the one the hail key calls down (an NPC-flown one up in the
// air: boardable only once low and slow); `coming`: called, on its way down.
// `of`: the seat it is of (its vehicle and seat), steady while it is the one shown (the HUD's world label keeps its facing by it).
struct BoardingEntrance { float at[3]{}; float reach=0.0f,distance=0.0f; bool inReach=false,hail=false,coming=false; std::uint64_t of=0; };
bool PlayerBoardingEntrance(BoardingEntrance* out) noexcept;
}
