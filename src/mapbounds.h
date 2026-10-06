// The map's real edges as the flyers see them (one place for the NPC jets, the NPC helis and the player's jet):
//  - PlayBox: the walls every plugin flyer keeps inside: the map's real play area (playarea.h MapPlayArea: where its
//    ground ends, measured with map rays once a mission, kVoidMargin inside it, never past the physics square of
//    crew.h PlayEdge; that square until the measure is in). The jets' soft edge sits inside it; one implementation of
//    the walls for the NPC jets, the NPC helis and the player's jet (playerjet.cpp WallTurn);
//  - MoveAreaBox: the mission's move area as it is now (docs/map-edge-re.md §1: the map's move_limit rectangle, a
//    script's SetMoveArea, widened by bigworld.cpp with BigWorld), read from MoveAreaManager (bigworld.cpp);
//  - HeldBox: where the stock input actually holds a body whose inset (veh+0xE00) is `inset`: the move area shrunk by it,
//    overlapped with the play box (the helis; the jets clear their inset and are held by the play box alone).
// Game thread. airbound.h has the soft edge's logic on these boxes. Included by crew.h (after PlayEdge).
#pragma once
#include "airbound.h"
#include "playarea.h"

namespace crew {
// The move area's corners (x, y, z) now; false when MoveAreaManager cannot be read (bigworld.cpp).
bool MoveAreaBox(float* lo,float* hi) noexcept;

inline airbound::Box PlayBox() noexcept {
    const PlayArea a=MapPlayArea();
    return airbound::Box{{a.lo[0],a.lo[1]},{a.hi[0],a.hi[1]}};
}

inline airbound::Box HeldBox(float inset) noexcept {
    airbound::Box edge=PlayBox();
    float lo[3],hi[3];
    if(!MoveAreaBox(lo,hi))return edge;
    const float in=inset>0.0f && inset<200.0f ? inset : 0.0f;
    return airbound::Overlap(edge,airbound::Box{{lo[0]+in,lo[2]+in},{hi[0]-in,hi[2]-in}});
}
}  // namespace crew
