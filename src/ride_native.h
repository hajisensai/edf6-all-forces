// The stock human-side ride (docs/newclass-re/vehicler.md §2.2): its addresses and the bytes they are checked against,
// shared by npcai.cpp (NPC boardings, a peer's ride replayed) and vehicler_start.cpp (the Vehicler's start seat).
// tests/vehicler_start_native_audit.py reads them from EDF.dll (TimeDateStamp 0x678CCB46).
//
// RideVehicle 0x5765E0(human, shared_ptr<vehicle>* by value, seat): off the old vehicle, +0x1540 / +0x1548 / +0x1550,
// SeatRide 0x633C10 with force 0, the riding action {0x56C9F0, 0} on human +0x1150 (0x5768B6). It refuses a dead human
// (+0x2E8) or one with +0x39C set (0x576865..0x576875: +0x1540 = 0, 0x570160). It releases one strong reference of the
// argument at its end (0x57690D), on every path, so the caller takes one first. It checks neither team, nor class mask,
// nor reach.
// Announce 0x5763E0(human): the board button's 0x56D799 call: online, message 6 (the vehicle's id, the seat, ++ride
// sequence) that every peer's copy of the human replays by RideVehicle (0x5774C0's call at 0x577661); nothing offline.
#pragma once
#include <cstddef>

namespace crew::ride_native {
constexpr unsigned kRideVehicle=0x5765E0,kRideRelease=0x57690D,kAnnounceRide=0x5763E0;
constexpr unsigned kRemoteRideCall=0x577661;   // HUMAN_EVENT_VEHICLE_RIDE (HumanBase slot 15 0x5774C0): call 0x5765E0
inline constexpr unsigned char kRideSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7C,0x24,0x20,0x48};
inline constexpr unsigned char kRideReleaseSig[]={0x49,0x8B,0x5E,0x08,0x48,0x85,0xDB,0x74,0x27,0x8B,0xC7,0xF0,0x0F,0xC1,0x43,0x08};
inline constexpr unsigned char kRemoteRideSig[]={0x48,0x8D,0x8E,0xE0,0xFE,0xFF,0xFF,0xE8};   // 0x57765A: lea rcx,[rsi-0x120]; call
inline constexpr unsigned char kAnnounceRideSig[]={0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x56,0x57,0x41,0x56,0x48,0x81};
// The human's seat (a seat record of the vehicle's +0x608 array), the vehicle and its control block, set by RideVehicle.
constexpr std::size_t kHumanSeat=0x1540,kHumanRiding=0x1548,kHumanRidingCtrl=0x1550;
// The board button's tail after the ride (0x56D7D1): human +0x3F0 = (seat +0x2B4 == 3); the leave 0x56FE80 clears it.
constexpr std::size_t kHumanSeatFlag=0x3F0,kSeatKeyRow=0x2B4;   // the key row's value: vehicler_start_logic.h SetsSeatFlag

struct SharedRef { void* obj; void* ctrl; };
using RideFn=void(__fastcall*)(void*,SharedRef*,int);
using AnnounceFn=void(__fastcall*)(void*);
}  // namespace crew::ride_native
