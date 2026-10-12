// Fire that cannot hurt its target (wasted_fire.h; docs/feedback-2026-10-12-ai-targeting.md). The damage hook
// (damagestats.cpp DamageHook) reports each hit it settles; the AI (npcai.cpp, heli.cpp, jet_combat.cpp, ground.cpp)
// asks WastedFire before it takes a target or pulls a trigger.
//
// A teleportation ship (UfoCarrier508 vtable 0x17C9DC0, its base UfoCarrier 0x17C9930; docs/carrier-laser-re.md §1):
// its CAS events ダメージ許可 / ダメージ禁止 (0x4F68DF / 0x4F69F7 register 0x4F6590 / 0x4F6580) set +0xD85 to 1 / 0. The
// damage function asks slot 16 (0x54808A) unless +0x380 has 0x800; UfoCarrier's slot 16 (0x4F7190) says no outright
// while +0xD85 is 0, and otherwise only to a hit on its core part (+0xD8C, UfoCarrier_CorePartsName). No: the HP is
// left alone (0x548092 -> 0x5480C7 -> 0x548625) unless the hit's GameDamageInfo +0x60 has 0x40. (H, static.)
// So while +0xD85 is 0 and +0x380 lacks 0x800 nobody's round can hurt it: every shooter holds its fire at it.
#include "crew.h"
#include "memory.h"
#include "wasted_fire.h"

namespace crew {
namespace {
constexpr std::uintptr_t kCarrierVtables[]={0x17C9DC0,0x17C9930};
constexpr std::size_t kObjFlags=0x380,kCarrierDamageOpen=0xD85;
constexpr std::uint32_t kNoHitFilter=0x800;

wastedfire::Book book{};
SRWLOCK lock=SRWLOCK_INIT;

// A teleportation ship the game lets nobody hurt now (see the top).
bool ShutShip(const void* target) noexcept {
    __try {
        const auto o=static_cast<const unsigned char*>(target);
        if(!Readable(o,kCarrierDamageOpen+1))return false;
        const auto vt=At<const unsigned char*>(o,0);
        bool ship=false;
        for(const std::uintptr_t v:kCarrierVtables)ship=ship || vt==image+v;
        return ship && !(At<std::uint32_t>(o,kObjFlags)&kNoHitFilter) && o[kCarrierDamageOpen]==0;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}  // namespace

void WastedFireHit(const void* shooter,const void* target,bool hurt) noexcept {
    const ULONGLONG ms=GameMs();
    AcquireSRWLockExclusive(&lock);
    const bool held=wastedfire::Hit(book,shooter,target,hurt,ms);
    ReleaseSRWLockExclusive(&lock);
    // One line a hold (a few a minute at most per pair): the evidence in a player's log that the AI held its fire.
    if(held)Log("WASTEDFIRE %p: %d hits on %p hurt nothing: its fire at it held %.0f s",shooter,wastedfire::kWastedHits,target,
                static_cast<double>(wastedfire::kWastedHoldMs)*0.001);
}

bool WastedFire(const void* shooter,const void* target) noexcept {
    if(!target)return false;
    if(ShutShip(target)) {
        static const void* logged=nullptr;   // one line a ship shut (game thread)
        if(logged!=target){logged=target;Log("WASTEDFIRE ship %p: its hatch's damage window shut: no target for anyone",target);}
        return true;
    }
    const ULONGLONG ms=GameMs();
    AcquireSRWLockShared(&lock);
    const bool wasted=wastedfire::Wasted(book,shooter,target,ms);
    ReleaseSRWLockShared(&lock);
    return wasted;
}

void ResetWastedFire() noexcept {
    AcquireSRWLockExclusive(&lock);
    book=wastedfire::Book{};
    ReleaseSRWLockExclusive(&lock);
}
}  // namespace crew
