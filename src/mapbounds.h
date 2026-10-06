// The map's real edges as the flyers see them (one place for the NPC jets, the NPC helis and the player's jet):
//  - PlayBox: the play edge every plugin flyer keeps inside (crew.h PlayEdge: 2400 m a side on the stock +-3000 m
//    physics world, the big map's ground edge with ini BigWorld), the square the jets' soft edge sits in;
//  - MoveAreaBox: the mission's move area as it is now (docs/map-edge-re.md §1: the map's move_limit rectangle, a
//    script's SetMoveArea, widened by bigworld.cpp with BigWorld), read from MoveAreaManager (bigworld.cpp);
//  - HeldBox: where the stock input actually holds a body whose inset (veh+0xE00) is `inset`: the move area shrunk by it,
//    overlapped with the play box (the helis; the jets clear their inset and are held by the play box alone).
// Game thread. airbound.h has the soft edge's logic on these boxes. Included by crew.h (after PlayEdge).
#pragma once
#include "airbound.h"

namespace crew {
// The move area's corners (x, y, z) now; false when MoveAreaManager cannot be read (bigworld.cpp).
bool MoveAreaBox(float* lo,float* hi) noexcept;

inline airbound::Box PlayBox() noexcept { return airbound::Square(PlayEdge()); }

inline airbound::Box HeldBox(float inset) noexcept {
    airbound::Box edge=PlayBox();
    float lo[3],hi[3];
    if(!MoveAreaBox(lo,hi))return edge;
    const float in=inset>0.0f && inset<200.0f ? inset : 0.0f;
    return airbound::Overlap(edge,airbound::Box{{lo[0]+in,lo[2]+in},{hi[0]-in,hi[2]-in}});
}
}  // namespace crew
