// Native aircraft boarding points sampled on the game thread; the HUD only consumes this value snapshot.
#pragma once
namespace crew {
struct BoardingEntrance { float at[3]{}; float reach=0.0f,distance=0.0f; bool inReach=false; };
bool PlayerBoardingEntrance(BoardingEntrance* out) noexcept;
}
