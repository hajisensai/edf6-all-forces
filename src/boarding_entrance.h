// Native aircraft boarding points sampled on the game thread; the HUD only consumes this value snapshot.
#pragma once
namespace crew {
// `hail`: no aircraft to board near the player, `at` is the one the hail key calls down (an NPC-flown one up in the
// air: boardable only once low and slow); `coming`: called, on its way down.
struct BoardingEntrance { float at[3]{}; float reach=0.0f,distance=0.0f; bool inReach=false,hail=false,coming=false; };
bool PlayerBoardingEntrance(BoardingEntrance* out) noexcept;
}
