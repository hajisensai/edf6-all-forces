// The Vehicler's logistics points at runtime (docs/newclass-re/vehicler-plan.md §3, §5 M4): the kills booked, the
// resupply's price held / settled / given back (zw_vehicle_dispatch.h ZwDeliverLedger), the call key, the HUD's snapshot,
// and the plugin channel "vehicler logistics" (ZWLG, vehicler_logistics_net.h) that keeps the room on the host's book.
//  - The host keeps every Vehicler's points (offline this machine). A kill counts on the machine that settles it (the
//    victim's authority: damagestats.cpp's damage hook sees its HP go to dead there only); a guest that settles one
//    reports it (credit, sent again until the host acks), the host books each victim once.
//  - The key (ini VehiclerLogisticsKey, K; VehiclerLogisticsButton a pad chord): a short press picks the next unlocked
//    vehicle, a long one (kHoldMs) calls the picked one (zw_vehicle_dispatch.h RequestZwDeliver) when the points cover it.
//  - Game thread throughout but VehiclerHudNow (the HUD's draw thread: a copy under a lock).
#pragma once
#include "plugin_net.h"
#include <cstdint>

namespace crew {
plugin_net::Channel VehiclerLogisticsNetChannel() noexcept;

// Whether a kill could earn points now (damagestats.cpp asks before it tags a round or looks at a death): the plugin on
// and online (any member may be a Vehicler), or offline a local Vehicler.
bool VehiclerLogisticsWanted() noexcept;
// A kill this machine settled (damagestats.cpp NoteKill): `victim` now dead, `shooter` the round's owner or the hit's
// attacker (may be null or stale: checked), `weapon` the weapon that fired the round (null unknown); `enemy` the victim
// was of a team hostile to the players, `part` it is a weak part that dies on its own (worth nothing).
void VehiclerKill(unsigned char* victim,unsigned char* shooter,const unsigned char* weapon,bool enemy,bool part) noexcept;
// zw_vehicle_dispatch.cpp ZwVehicleTick (each game frame of a mission): the key, the transit flags, the state sent, the
// guest's credits sent again, the HUD's snapshot.
void VehiclerLogisticsTick() noexcept;
// zw_vehicle_dispatch.cpp ResetZwVehicles (a new mission's start, its teardown): the book and the mirror empty.
void ResetVehiclerLogistics() noexcept;
// The host: a delivery's word to every guest (ZWLG handed / retired; zw_vehicle_dispatch.cpp, online only).
void VehiclerSendDelivery(bool retired,const unsigned char hull[32],const unsigned char courier[32],bool follows) noexcept;

// What the HUD draws (hud_vehicler.inc): only while this machine's player is a Vehicler.
enum class VehiclerNotice : std::uint8_t { none,arrived,refused,poor };
struct VehiclerHudView {
    unsigned points=0,gauge=0,price=0;   // the player's points, the gauge, the price of the vehicle picked
    wchar_t name[24]{};                  // the vehicle picked (its row's short name)
    int choices=0;                       // the kinds unlocked (a short press cycles them when more than one)
    bool heard=true;                     // a guest: the host's book heard (false: the numbers are not known yet)
    bool transit=false;                  // a resupply on its way (or its courier still driving the old one off)
    float distance=-1.0f;                // metres from the player to the vehicle on its way (-1: not made yet)
    VehiclerNotice notice=VehiclerNotice::none;   // a moment's word: it arrived, it was refused, too few points
    int key=0,button=0;                  // the ini's key and pad chord (0: none)
};
bool VehiclerHudNow(VehiclerHudView* out) noexcept;
}  // namespace crew
