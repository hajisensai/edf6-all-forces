// The Epsilon 403's store holders on its slot 48 (testhub #12, docs/feedback-2026-11-08-403-store-recoil.md;
// docs/recoil-re.md §3.1).
//
// VehicleBase slot 5 0x630250 calls the class's slot 48 for each holder (veh+0x638, 0x48 each, count +0x648) whose gun
// fired this frame (0x630471). The 403's slot 48 (Vehicle403_Tank 0x5FED60) runs the VehicleBase part 0x62EA10, then
// takes the holder's index (0x62DB10: its position in veh+0x638) and branches on it, and on nothing else:
//   index 0       the rail gun: the holder's BodyRecoil record (0x5FA150 variant 0) pushes the hull (0x5F9FB0);
//   index i != 0  seat i's machine gun: the holder's AimRecoil record (0x5FA150 variant 1) jitters seat i's aim
//                 (0x5F9F10 -> 0x5FA8F0 adds to [seat[i]+0xE0+0x24] / +0x64, seat[i] = [veh+0x608] + i*0x340), with no
//                 test of i against the seat count (veh+0x618).
// The stock 403 has three holders and three seats, holder i on seat i. tools/make_stock_stores.py appends the AGM-114s
// beside the rail gun as holder 3 on seat 0 (its row a copy of holder 0's). Fired, the stock code takes it for seat 3's
// machine gun: with its BodyRecoil record the AimRecoil lookup is null and 0x5F9F2B reads [0+0xC] (the 2026-10-11
// crash, EDF+5F9F2B reading 0xC); an AimRecoil record would not help: the jitter would be written past the end of the
// three-seat vector (seat[3]+0xE0+0x24), into the heap. No record makes the stock code safe for a holder past the seats.
//
// Here: the 403's slot 48 is chained. A holder past the seats (store_recoil::Route) runs Vehicle404_Tank's slot 48
// 0x5FFEE0 instead: VehicleBase code only (0x62EA10, the dead test, the variant 0 record, the hull's body +0x1698, its
// liveness, 0x62D7F0, the gun's matrix +0xB0, 0x5F9FB0), the same steps as the 403's own index 0 branch; the store's
// BodyRecoil record (make_stock_stores._params: [0, 0] for a missile) pushes the hull by that. Every other holder takes
// the 403's own code. All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "memory.h"
#include "storerecoil.h"

namespace crew {
namespace {
using FiredFn=void(__fastcall*)(unsigned char*,unsigned char*);
using store_recoil::Path;
using store_recoil::Route;

constexpr std::size_t kSlotFired=48;
constexpr std::size_t kHolders=0x638,kHolderCount=0x648,kHolderStride=0x48;
static_assert(kSeatCount==0x618,"the seat vector's count (crew.h)");
constexpr unsigned kVtable403=0x17D8FA0,kFired403=0x5FED60,kHullFired=0x5FFEE0;
// 0x5FED92 call 0x62DB10 (the holder's index)
constexpr unsigned kIndexCall=0x5FED92;
const unsigned char kIndexCallCode[]={0xE8,0x79,0xED,0x02,0x00};
// 0x5FEE40 mov edx,1; call 0x5FA150 (AimRecoil); rbx = [veh+0x608] + index*0x340; ...; lea rdx,[rbx+0xE0]; call 0x5F9F10
constexpr unsigned kAimBranch=0x5FEE40;
const unsigned char kAimBranchCode[]={0xBA,0x01,0x00,0x00,0x00,0xE8,0x06,0xB3,0xFF,0xFF,
    0x48,0x8B,0x9B,0x08,0x06,0x00,0x00,0x48,0x8B,0xCE,0x48,0x69,0xD5,0x40,0x03,0x00,0x00,0x48,0x8B,0xF8,0x48,0x03,0xDA,
    0xE8,0x8A,0xE9,0x02,0x00,0x0F,0x28,0xD0,0x48,0x8D,0x93,0xE0,0x00,0x00,0x00,0x48,0x8B,0xCF,0xE8,0x98,0xB0,0xFF,0xFF};
// 0x5FFEE0: call 0x62EA10; cmp byte [rdi+0x2E8],0 (dead); ... 0x5FFF82: 0x62D7F0 (the gun's strength), r9 = the gun's
// matrix +0xB0, call 0x5F9FB0 (the BodyRecoil push)
constexpr unsigned kHullHead=0x5FFEE0,kHullPush=0x5FFF82;
const unsigned char kHullHeadCode[]={0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xF2,0x48,0x8B,0xF9,0xE8,0x1B,
    0xEB,0x02,0x00,0x80,0xBF,0xE8,0x02,0x00,0x00,0x00,0x0F,0x85,0xB0,0x00,0x00,0x00};
const unsigned char kHullPushCode[]={0x48,0x89,0x5C,0x24,0x30,0x48,0x8B,0xCE,0x48,0x8B,0x5E,0x18,0xE8,0x5D,0xD8,0x02,0x00,0x4C,
    0x8D,0x8B,0xB0,0x00,0x00,0x00,0x0F,0x28,0xD0,0x48,0x8B,0xD7,0x48,0x8B,0xCD,0xE8,0x08,0xA0,0xFF,0xFF};
// 0x5FFF0B xor edx,edx; call 0x5FA150 (BodyRecoil)
constexpr unsigned kHullRecord=0x5FFF0B;
const unsigned char kHullRecordCode[]={0x33,0xD2,0xE8,0x3E,0xA2,0xFF,0xFF};

void* next=nullptr;   // the 403's slot 48 as it was (the stock 0x5FED60)
std::atomic<int> said{0};

void __fastcall Fired403(unsigned char* v,unsigned char* holder) {
    const unsigned char* const first=At<const unsigned char*>(v,kHolders);
    const std::uint64_t holders=At<std::uint64_t>(v,kHolderCount);
    const std::uint64_t index=holder>=first ? static_cast<std::uint64_t>(holder-first)/kHolderStride : holders;
    const std::uint64_t seats=At<std::uint64_t>(v,kSeatCount);
    if(Route(index,holders,seats)==Path::hull) {
        if(!said.exchange(1))Log("STORE RECOIL 403 v=%p holder %llu (no seat %llu of %llu): the hull push of its BodyRecoil",
                                 v,static_cast<unsigned long long>(index),static_cast<unsigned long long>(index),
                                 static_cast<unsigned long long>(seats));
        reinterpret_cast<FiredFn>(image+kHullFired)(v,holder);
        return;
    }
    reinterpret_cast<FiredFn>(next)(v,holder);
}
}  // namespace

bool InstallStoreRecoil() noexcept {
    bool read=false;
    __try {
        read=reinterpret_cast<void**>(image+kVtable403)[kSlotFired]==image+kFired403 &&
             Matches(kIndexCall,kIndexCallCode,sizeof(kIndexCallCode)) &&
             Matches(kAimBranch,kAimBranchCode,sizeof(kAimBranchCode)) &&
             Matches(kHullHead,kHullHeadCode,sizeof(kHullHeadCode)) &&
             Matches(kHullRecord,kHullRecordCode,sizeof(kHullRecordCode)) &&
             Matches(kHullPush,kHullPushCode,sizeof(kHullPushCode));
    } __except(EXCEPTION_EXECUTE_HANDLER) { read=false; }
    if(!read){Log("STORE RECOIL off: the 403's slot 48 is not the one read (its store holders' recoil is the stock code's)");return false;}
    if(!edf::ChainVtableSlot(reinterpret_cast<void**>(image+kVtable403)+kSlotFired,reinterpret_cast<void*>(&Fired403),&next)) {
        Log("STORE RECOIL: the 403's slot 48 patch failed");
        return false;
    }
    Log("HOOK store recoil=1 (a 403 holder past its seats pushes the hull by its own BodyRecoil)");
    return true;
}
}  // namespace crew
