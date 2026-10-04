// The EDF.dll object layout both plugins rely on: one copy of each fact (EDF6VehicleCrew docs/re-notes.md,
// autoturret/docs/re-notes.md). All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace edf {
template<class T> T At(const void* base,std::size_t offset) noexcept {
    T value;std::memcpy(&value,static_cast<const unsigned char*>(base)+offset,sizeof(T));return value;
}
template<class T> void Put(void* base,std::size_t offset,T value) noexcept {
    std::memcpy(static_cast<unsigned char*>(base)+offset,&value,sizeof(T));
}

// GameObject: weak-this at +0x28 (object) / +0x30 (control block, use count at +8)
constexpr std::size_t kSelf=0x28,kSelfCtrl=0x30,kCtrlUses=0x8;
// Vehicle
constexpr std::size_t kMatrix=0x60,kPosition=0x90,kDead=0x2E8,kTeam=0x314;
constexpr std::size_t kSeats=0x608,kSeatCount=0x618,kSeatStride=0x340;
// Seat: rider object / its weak_ptr control block (occupied while the use count is non-zero, 0x634710)
constexpr std::size_t kSeatRider=0x260,kSeatRiderCtrl=0x268;
// Human: pad / player-controlled (the test 0x572EFF and 0x673AC2 make before reading a pad)
constexpr std::size_t kHumanPad=0x340,kHumanPlayer=0x354;
// A rider's network object (rider+0x120); bit 0 of its +8 set: another machine runs it (the weapon fire
// step 0x690C1A refuses such an operator).
constexpr std::size_t kRiderNet=0x120,kNetFlags=0x8;
// The rider RideAi (VehicleBase slot 50, 0x633030) seats in an NPC-crewed vehicle: it drives through the
// vehicle AI
constexpr unsigned kDummyRiderVtable=0x17D7320;

// VehicleBase slot 55, the per-frame input. Both plugins chain it on the same vehicles (403, 404, 603), so
// both forward the same four register arguments: CarBase's input also reads r8 (its drive block), and a hook
// that dropped r8/r9 would hand the next one in the chain garbage. (this, has input, r8, r9)
constexpr std::size_t kSlotInput=55;
using VehicleInputFn=void(__fastcall*)(void*,std::uintptr_t,void*,void*);
}  // namespace edf
