// EDF.dll's layout as the plugin reads it: the image base, the vehicle / seat / human offsets and the
// native functions' RVAs (EDF.dll TimeDateStamp 0x678CCB46; docs/re-notes.md), over common/edf/layout.h.
#pragma once
#include <cstddef>
#include <cstdint>
#include "edf/layout.h"
#include "edf/patch.h"
#include "edf/seat.h"

namespace crew {
extern unsigned char* image;

// --- EDF.dll layout ---
// The facts EDF6AutoTurret rests on too live in common/edf/layout.h (one definition for both plugins):
// GameObject weak-this +0x28 / +0x30, the vehicle's matrix, position, dead byte, team, seats, the seat's
// rider weak_ptr, the human's pad / player flag, the dummy rider's vtable, At / Put.
using edf::kSelf; using edf::kSelfCtrl; using edf::kMatrix; using edf::kPosition; using edf::kDead; using edf::kTeam;
using edf::kSeats; using edf::kSeatCount; using edf::kSeatStride; using edf::kSeatRider; using edf::kSeatRiderCtrl;
using edf::kHumanPad; using edf::kHumanPlayer; using edf::kDummyRiderVtable; using edf::kSlotInput;
using edf::kSeatAim; using edf::kAimAxes; using edf::kAxisStride; using edf::kAxisMin; using edf::kAxisMax; using edf::kAxisAngle;
using edf::At; using edf::Put;
// Teams (mission AsCommon.h): player 0, enemy 1, friend 2, neutral 3, vehicle 5 = nobody's vehicle,
// which anyone may board (CanRideSeat skips the team test for it).
constexpr std::int32_t kTeamVehicle=5;
constexpr std::int32_t kTeamNeutral=3;   // hostile to nobody (playerjet.cpp: the parachute's canopy)
// The game's SetTeam 0x54EE70(object, team, registered). The team manager (*(image+0x20B2978)) keeps a set of objects
// per team (+0x38, 0x38 bytes a team) and finds an object's set by its team +0x314, when it adds it (0x5E0B70), takes
// it out (0x5E1C60: SetTeam, the object's destruction) or walks a team's objects. A write to +0x314 alone leaves the
// object in its old team's set while everything after looks in the new one's: freed, it stays in the old set, and the
// next walk of it reads freed memory (the crash at the next mission's start after a crewed 603_Flak, 2026-10-04).
// Every team change goes through SetTeam, the object's registration (+0x380 bit 6) kept as it is.
constexpr unsigned kSetTeam=0x54EE70;
constexpr std::size_t kObjectFlags=0x380;
// Human: the vehicle it is in (weak_ptr object +0x1548, control block +0x1550)
constexpr std::size_t kHumanVehicleCtrl=0x1550;
// VehicleBase virtual slots (input: edf::kSlotInput)
constexpr std::size_t kSlotFindSeat=49,kSlotRideAi=50;
constexpr unsigned kFindSeat=0x633B80,kRideAi=0x633030;
// Seat functions
constexpr unsigned kCanRideSeat=0x6346D0;   // (vehicle, human, seat) -> bool: team, mask, free, in reach
constexpr unsigned kCanRide=0x62DCB0;       // (vehicle, human) -> bool: any seat passes the above
constexpr unsigned kSeatRide=0x633C10;      // (vehicle, rider, index, force) -> seat or null
constexpr unsigned kSeatClear=0x634940;     // (vehicle, seat): forget the rider, no message to it
constexpr unsigned kSeatKick=0x62E1A0;      // (vehicle, seat): get-off message, then clear (a dummy rider dies)
// The on-foot ride-prompt visitor (0x5735E7): {vtable, human, bool result}; slot 1 is called per object
constexpr unsigned kPromptFunctorVtable=0x17D09B8,kPromptVisit=0x5725A0;
constexpr std::size_t kFunctorHuman=0x8,kFunctorResult=0x10;
// The patch primitives and the seat test are common/'s (shared with EDF6AutoTurret).
inline bool Matches(std::size_t rva,const unsigned char* bytes,std::size_t size) noexcept { return edf::Matches(image,rva,bytes,size); }
using edf::PatchVtableSlot;

// What sits in a seat (common/seat.cpp).
using Rider=edf::Rider;
inline Rider SeatRider(const unsigned char* seat) noexcept { return edf::SeatRider(image,seat); }
// A player of any machine in the seat (Rider::player is this machine's only: common/seat.cpp).
inline bool AnyPlayerIn(const unsigned char* seat) noexcept { return edf::AnyPlayerIn(image,seat); }
using edf::SeatAt; using edf::SeatCount; using edf::IsPlayer; using edf::IsAnyPlayer; using edf::IsAnyPlayerSelf;
}  // namespace crew
