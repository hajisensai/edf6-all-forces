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

// Hidden while an NPC holds the seat, and while it is empty: nobody there needs the line, whoever drives the vehicle (the
// 410's door guns aimed by the plugin, a requested aircraft delivered empty, a drone the plugin flies with no one seated,
// a support aircraft whose crew is still walking aboard; the user, 2026-10-14: 「飞机在外面会有红色预瞄线」). Hidden too
// while the player holds it in a vehicle whose HUD draws a sight of its own (`ownSight`: PlayerJetOwnSight /
// PlayerHeliOwnSight / PlayerStockOwnSight), given back for the player with no sight of ours (the ini turned off: the next
// frame; the count was kept when it was hidden, crew.cpp SetLine). A remote player's seat gets back what we hid while it
// was empty and is not hidden: theirs to see (SetLine gives back only a line it hid).
constexpr LineWant Want(Holder holder,bool ownSight) noexcept {
    switch(holder) {
        case Holder::npc:
        case Holder::empty: return LineWant::hide;
        case Holder::player: return ownSight ? LineWant::hide : LineWant::show;
        default: return LineWant::show;
    }
}

}  // namespace crew::aimline
