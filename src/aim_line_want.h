#pragma once
// What a seat's stock red aim lines get this frame (crew.cpp AimLines, docs/aim-line-re.md): kept from the game's own
// reading of the seat only, so it is checked without the game (tools/aim_line_want_check.cpp).
//
// Who holds the seat is the seat's rider as an NPC test sees it (npcai.cpp NpcInSeat), not the rider's object class: a
// legacy DummyVehicleRider and a live soldier no machine's player drives are both an NPC. The support's crews are real
// soldiers seated on the spot (#88), Rider::other to the seat: read by class alone their lines were left drawn (the
// user, 2026-10-10: "npc载具红线会显示出来").
#include <cstdint>

namespace crew::aimline {

enum class Holder : std::uint8_t { empty, npc, player, other };   // other: another machine's player, or a rider not read
enum class LineWant : std::uint8_t { keep, hide, show };

// Hidden while an NPC holds the seat, or while it is empty in a vehicle an NPC drives (the 410's door guns, aimed by
// the plugin with nobody in them), and while the player holds it in a vehicle whose HUD draws a sight of its own
// (`ownSight`: PlayerJetOwnSight / PlayerHeliOwnSight / PlayerStockOwnSight). Given back for the player with no sight
// of ours (the ini turned off: the next frame). Any other seat is left as it was: an empty one of a vehicle no NPC
// drives (one the player got out of keeps its line hidden, nobody there to see it, until they or an NPC sit in it) and
// a remote player's.
constexpr LineWant Want(Holder holder,bool npcDriven,bool ownSight) noexcept {
    switch(holder) {
        case Holder::npc: return LineWant::hide;
        case Holder::empty: return npcDriven ? LineWant::hide : LineWant::keep;
        case Holder::player: return ownSight ? LineWant::hide : LineWant::show;
        default: return LineWant::keep;
    }
}

}  // namespace crew::aimline
