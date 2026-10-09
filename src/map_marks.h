// What the map marks of the friendly side, decided from what map.cpp reads off each object (pure: no game; checked by
// tools/map_marks_check.cpp).
//  - The sets walked (docs/camera-re.md §8.6): the team walk 0x5E11D0(manager, team, functor) visits every team friendly
//    to the player's (relation 1) -- not team 5, nobody's vehicles (an empty vehicle: the stock vehicle update puts it
//    there, 0x6304C7..0x630591; the plugin's parked and delivered aircraft wait there, playerjet.cpp / boarding.cpp).
//    The stock board prompt (0x56D700) walks team 5 on its own first (0x56D768: 0x5E0D60(manager, 5, functor), the
//    one-team walk) and then the friends (0x56D77F); the stock radar takes team 5's set apart as well (0x82B907). The map
//    walks both the same way, or every empty vehicle and parked aircraft was missing from it (2026-10-06, the user:
//    "飞机没和载具一样在地图显示").
//  - A walked object's mark: a vehicle class the plugin knows is the carrier, an aircraft (kMapRotor: a helicopter) or a
//    ground vehicle; from team 5 flagged kMapEmpty (nobody in it: a seat to take). Any other object is a soldier: the
//    squad on the player's own team, else another friendly; team 5 holds no soldiers (nothing marked). A soldier seated
//    in a vehicle is not marked of its own (the user, 2026-10-09: "载具上的npc还标着可以招募的标记"): its vehicle's mark
//    stands for the crew, a squad mark on top of it read as a soldier on foot to be recruited.
#pragma once
#include "map.h"
#include <cstdint>

namespace crew::mapmarks {
constexpr std::int32_t kNobodysTeam=5;   // crew.h kTeamVehicle

// The walks a gather makes for `team` (the player's): the friends' always, team 5's unless that is the player's own
// (then the friends' walk has it already).
struct Walks { bool friends,nobodys; };
constexpr Walks WalksFor(std::int32_t team) noexcept { return Walks{true,team!=kNobodysTeam}; }

// What one walked object is, as map.cpp reads it.
struct Seen {
    bool vehicle;    // a vehicle class the plugin knows (crew.cpp KnownVehicle)
    bool sub;        // the plugin's carrier (subcarrier.cpp IsSub)
    bool jet;        // a fixed-wing aircraft of the plugin (IsJet / IsPlayerJet)
    bool heli;       // a helicopter (heli.cpp IsHelicopter: the stock ones and the plugin's rotor craft)
    bool ownTeam;    // on the player's own team
    bool nobodys;    // found by team 5's walk
    bool riding;     // a soldier seated in a vehicle (its ride alive): the vehicle's mark is the crew's
};
// Its mark: false with none; else its kind and flags.
constexpr bool FriendlyMark(const Seen& o,MapKind* kind,std::uint8_t* flags) noexcept {
    *flags=o.nobodys ? kMapEmpty : 0;
    if(!o.vehicle) {
        if(o.nobodys || o.riding)return false;
        *kind=o.ownTeam ? MapKind::squad : MapKind::ally;
        return true;
    }
    if(o.sub){*kind=MapKind::carrier;return true;}
    if(o.jet || o.heli) {
        *kind=MapKind::air;
        if(!o.jet)*flags|=kMapRotor;
        return true;
    }
    *kind=MapKind::vehicle;
    return true;
}

// An aircraft this near the ground under it (m) stands on it: its pin stands up from it as a ground vehicle's does
// (else its icon is on the aircraft and the stem goes down to the ground).
constexpr float kLandedClear=6.0f;
constexpr bool Landed(float y,float ground) noexcept { return y-ground<=kLandedClear; }
}  // namespace crew::mapmarks
