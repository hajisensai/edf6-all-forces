// Every class in every seat of every vehicle (ini AllClassesRide, default on; the user, 2026-10-17: 「所有载具应该支持所有
// 职业上去」). Static RE and the stock seats' census: docs/all-classes-ride-re.md; the pure part: seat_class.h.
//
// Who may sit in a seat is decided by the stock seat test alone (CanRideSeat 0x6346FC, CanRide 0x62DD24: the
// soldier's game_object_vehicle_mask, human+0x31C, against the seat's class mask, seat+0x30, and seat+0x34, all ones);
// every check of the plugin's makes the same test on the same fields (crew.cpp PlayerBoardingEntrance, heli.cpp
// DoorSeat, npcai.cpp SeatTakes, seatswitch.cpp ClassMay, mission_crew_support.cpp Build). So the seats are opened
// where they are made, once, for all of them, the player and the NPCs alike:
//   1. the seat's mask. The one place a seat gets its mask is the seat build 0x62B430 (vehicle_riding_position[i][4]
//      into the seat being built, 0x62B79C; the seat then appended to the vehicle's seat vector, vehicle+0x600:
//      +0x8 its data = kSeats, +0x18 its count = kSeatCount, 0x62BE33..0x62BE4D), called from one place, the vehicle
//      base's constructor (0x629C47, once per entry). That call is redirected: the stock build runs, then the seat it
//      appended gets seatclass::Widen of its mask. A seat no class may take (0: none in the stock) stays closed; the
//      Proteus rework's closing (proteus.cpp: seat+0x30 = 0 while it holds the vehicle, the widened mask put back after)
//      works as before. Vehicles made while the option is off keep their stock masks (it is read as each seat is made);
//   2. the rider's pose. The ride state's entry (HumanBase 0x57ADF0) plays a shown seat's Animation by name on the
//      rider's CAS (0x116C2D0 at 0x57AE8E); the stock bikes' state is in the Ranger's and the Air Raider's CAS only, so
//      a Wing Diver or a Fencer on one kept their on-foot state (no crash: the name is refused, nothing changes). That
//      call is redirected: a refused name is followed by seatclass::kFallbackRide, a seated pose every class has.
//      Seats that hide their rider (every other stock pilot seat) need nothing.
// Online: who boards is decided on the boarder's own machine (the board button's FindSeat / CanRideSeat; NPCs on their
// authority, npcai.cpp), and a peer replays the ride without any class test (HUMAN_EVENT_VEHICLE_RIDE Recv 0x5775F1
// -> 0x5765E0 RideVehicle). So a player needs this on their own machine to board; a peer without it still sees them
// ride. The pose is each machine's own: a peer without it shows a Wing Diver or Fencer on a bike in their on-foot pose.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "memory.h"
#include "seat_class.h"

#include <atomic>

namespace crew {
namespace {
constexpr std::size_t kSeatBuildCall=0x629C47;   // call 0x62B430 (rcx = the vehicle, rdx = the entry), after mov rcx,r14
constexpr std::size_t kSeatBuild=0x62B430;
constexpr std::size_t kRideStateCall=0x57AE8E;   // call 0x116C2D0(rider+0xB90, [rider+0x1420], seat->Animation, 0)
constexpr std::size_t kSetCasState=0x116C2D0;
constexpr std::size_t kSeatClassMask=0x30;
const unsigned char kSeatBuildCallSig[]={0x49,0x8B,0xCE,0xE8,0xE4,0x17,0x00,0x00};   // 0x629C44 mov rcx,r14; call 0x62B430
const unsigned char kSeatBuildSig[]={0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57};
const unsigned char kMaskStoreSig[]={0x89,0x85,0x90,0x00,0x00,0x00};   // 0x62B79C mov [rbp+0x90],eax: the class mask (seat = rbp+0x60)
const unsigned char kVectorSig[]={0x49,0x8D,0x9F,0x00,0x06,0x00,0x00};   // 0x62BD57 lea rbx,[r15+0x600]: the seat vector
// 0x62BE33: rax = count; rcx = data + count * 0x340; ++count; the copy (0x628E20) into rcx
const unsigned char kAppendSig[]={0x48,0x8B,0x43,0x18,0x48,0x69,0xC8,0x40,0x03,0x00,0x00,0x48,0x03,0x4B,0x08,0x48,0xFF,0xC0,0x48,0x89,
    0x43,0x18};
// 0x57AE5C: rsi = rider+0x1540 (the seat); DispCharacter +0x254; dl = 1, 0x6C04B0 (shown); r8 = seat+0x10; the CAS call
const unsigned char kRideStateSig[]={0x48,0x8B,0xB7,0x40,0x15,0x00,0x00,0x48,0x8D,0x8F,0x60,0x08,0x00,0x00,0x44,0x38,0xBE,0x54,0x02,
    0x00,0x00,0x74,0x2A,0xB2,0x01,0xE8,0x36,0x56,0x14,0x00,0x4C,0x8B,0x46,0x10,0x48,0x8D,0x8F,0x90,0x0B,0x00,0x00,0x8B,0x97,0x20,0x14,
    0x00,0x00,0x45,0x33,0xC9,0xE8,0x3D,0x14,0xBF,0x00};
const unsigned char kSetCasStateSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x41};
static_assert(kSeats==0x608 && kSeatCount==0x618 && kSeatStride==0x340,"the seat vector 0x62BE33 appends to");
// What SeatBuild reads of the vehicle: the seat vector's data pointer (kSeats) through its count (kSeatCount, 8 bytes).
constexpr std::size_t kSeatVectorRead=kSeatCount+sizeof(std::uint64_t)-kSeats;
static_assert(kSeatVectorRead==0x18,"vehicle+0x608 .. +0x61F");

using SeatBuildFn=void(__fastcall*)(unsigned char*,void*);
using SetCasStateFn=bool(__fastcall*)(void*,std::int32_t,const wchar_t*,std::int32_t);
std::atomic<int> widenedSaid{0},fallbackSaid{0};

bool On() noexcept { return Cfg().enabled && Cfg().allClassesRide; }

// The vehicle base's seat build, then the seat it appended opened to every class.
void __fastcall SeatBuild(unsigned char* vehicle,void* entry) {
    const bool seen=Readable(vehicle+kSeats,kSeatVectorRead,true);
    const std::uint64_t before=seen ? At<std::uint64_t>(vehicle,kSeatCount) : 0;
    reinterpret_cast<SeatBuildFn>(image+kSeatBuild)(vehicle,entry);
    if(!seen || !On())return;
    const std::uint64_t count=At<std::uint64_t>(vehicle,kSeatCount);
    if(count!=before+1)return;   // an entry of another shape builds no seat (0x62B4B4)
    auto* seat=At<unsigned char*>(vehicle,kSeats)+(count-1)*kSeatStride;
    if(!Readable(seat+kSeatClassMask,4,true))return;
    const std::uint32_t mask=At<std::uint32_t>(seat,kSeatClassMask),wide=seatclass::Widen(mask);
    if(wide==mask)return;
    Put<std::uint32_t>(seat,kSeatClassMask,wide);
    if(widenedSaid.fetch_add(1)<8)
        Log("ALLCLASS vehicle %p seat %llu: class mask %u -> %u (every class)",static_cast<void*>(vehicle),count-1,mask,wide);
}

// The ride state's pose: the seat's own, else (a class without it) the bench pose every class has.
bool __fastcall RideState(void* cas,std::int32_t casPlayer,const wchar_t* name,std::int32_t flag) {
    const auto stock=reinterpret_cast<SetCasStateFn>(image+kSetCasState);
    if(stock(cas,casPlayer,name,flag))return true;
    if(!name || !On())return false;   // refused: the state is unchanged (0x116C2EF), so the bench pose may be asked next
    const bool sat=stock(cas,casPlayer,seatclass::kFallbackRide,flag);
    if(fallbackSaid.fetch_add(1)<8)
        Log("ALLCLASS rider CAS %p has no ride state \"%ls\": %ls %s",cas,Readable(name,64) ? name : L"?",seatclass::kFallbackRide,
            sat ? "played instead" : "missing too (the rider keeps its pose)");
    return sat;
}
}  // namespace

bool InstallAllClassesRide() noexcept {
    bool build=false,pose=false;
    __try {
        build=Matches(kSeatBuildCall-3,kSeatBuildCallSig,sizeof(kSeatBuildCallSig)) &&
              Matches(kSeatBuild,kSeatBuildSig,sizeof(kSeatBuildSig)) && Matches(0x62B79C,kMaskStoreSig,sizeof(kMaskStoreSig)) &&
              Matches(0x62BD57,kVectorSig,sizeof(kVectorSig)) && Matches(0x62BE33,kAppendSig,sizeof(kAppendSig));
        pose=Matches(0x57AE5C,kRideStateSig,sizeof(kRideStateSig)) && Matches(kSetCasState,kSetCasStateSig,sizeof(kSetCasStateSig));
    } __except(EXCEPTION_EXECUTE_HANDLER) {build=pose=false;}
    bool changed=false;
    const bool seats=build && RedirectCall(image+kSeatBuildCall,image+kSeatBuild,reinterpret_cast<void*>(&SeatBuild),changed);
    changed=false;
    const bool poses=pose && RedirectCall(image+kRideStateCall,image+kSetCasState,reinterpret_cast<void*>(&RideState),changed);
    Log("ALLCLASS seat build %s, ride pose %s (AllClassesRide=%d)",seats ? "hooked" : build ? "NOT hooked" : "not as read: not hooked",
        poses ? "hooked" : pose ? "NOT hooked" : "not as read: not hooked",Cfg().allClassesRide ? 1 : 0);
    return seats;
}
}  // namespace crew
