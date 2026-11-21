// The commander mode's decisions, pure (no Windows, no game: tests/commander_logic_test.cpp). commander.cpp feeds them the
// player's loadout, the friendly NPC soldiers it counts and the clock (docs/feature-2026-11-21-commander-mode.md).
#pragma once
#include "npc_tier.h"
#include "support_call.h"
#include <cstdint>

namespace crew::commander {
// The support catalog of a commander's NPC request (support_net.h): below the test range's rows (1021 / 1022) and the
// mission crews' 1023, above every map entry. Its plan is one soldier at the requester's player (commander.cpp).
inline constexpr std::uint32_t kCatalog=1020;

// GameStatus classes (docs/loadout-re.md): 0 Ranger, 1 Wing Diver, 2 Air Raider, 3 Fencer.
inline constexpr int kRanger=0,kWingDiver=1,kAirRaider=2,kFencer=3;
// The soldier kinds (support_call.h) a class's NPC may be, its default first. The X-Morph infantry is no player's class.
// An Air Raider has no AI template in Root.cpk (support_call.h): its NPC is a Ranger carrying an assault rifle.
struct Kinds { const SupportWeapon* kind; int count; };
inline constexpr SupportWeapon kRangerKinds[]={SupportWeapon::rifle,SupportWeapon::flame,SupportWeapon::rocket,SupportWeapon::shotgun,
                                               SupportWeapon::sniper};
inline constexpr SupportWeapon kWingKinds[]={SupportWeapon::wingLance,SupportWeapon::wingLaser,SupportWeapon::wingMonster,
                                             SupportWeapon::wingIzuna,SupportWeapon::wingThunderBow};
inline constexpr SupportWeapon kFencerKinds[]={SupportWeapon::fencerCannon,SupportWeapon::fencerMiddleCannon,
                                               SupportWeapon::fencerPileBanker,SupportWeapon::fencerShotgun};
inline constexpr SupportWeapon kRaiderKinds[]={SupportWeapon::rifle};
constexpr Kinds KindsOf(int cls) noexcept {
    return cls==kWingDiver ? Kinds{kWingKinds,5} : cls==kFencer ? Kinds{kFencerKinds,4} :
           cls==kAirRaider ? Kinds{kRaiderKinds,1} : Kinds{kRangerKinds,5};
}
// The commander's NPC kind: the kind whose weapon series holds the first of its class's equipped weapons (slot order) that
// any of the class's kinds carries; none: the class's default. -1 for no class (an unreadable GameStatus).
constexpr int KindFor(int cls,const int* equipped,int slots) noexcept {
    if(cls<kRanger || cls>kFencer)return -1;
    const Kinds k=KindsOf(cls);
    for(int i=0;equipped && i<slots;++i) {
        if(equipped[i]<0)continue;
        for(int j=0;j<k.count;++j)if(npctier::RowOf(k.kind[j],equipped[i]))return static_cast<int>(k.kind[j]);
    }
    return static_cast<int>(k.kind[0]);
}
// A kind a commander's plan may carry (every peer's Validate): a player class's kind, installed by every stock install.
constexpr bool CommanderKind(std::uint32_t kind) noexcept {
    for(int cls=kRanger;cls<=kFencer;++cls) {
        const Kinds k=KindsOf(cls);
        for(int j=0;j<k.count;++j)if(static_cast<std::uint32_t>(k.kind[j])==kind)return true;
    }
    return false;
}

// The request: once the mission is ready for it, again when nothing came of it (a refused / lost request, a host still
// sealing the world) after kRetryMs, at most kTries times; then the mode is given up for this player this mission (its
// soldier is its own again: commander.cpp Release).
inline constexpr std::uint64_t kRetryMs=15000;
inline constexpr int kTries=4;
struct Request {
    int tries=0;
    std::uint64_t at=0;      // the last try
    bool given=false;        // its NPC is here (seen alive once): never asked again this mission
    bool gaveUp=false;
};
enum class Ask : std::uint8_t { wait, ask, giveUp };
constexpr Ask NextAsk(const Request& r,bool ready,std::uint64_t now) noexcept {
    if(r.given || r.gaveUp || !ready)return Ask::wait;
    if(r.tries && now-r.at<kRetryMs)return Ask::wait;
    return r.tries>=kTries ? Ask::giveUp : Ask::ask;
}

// The defeat: armed once this player's own NPC has been seen alive (before that there is nothing to lose: a mission may
// start with no friendly soldier at all); lost once no friendly NPC soldier has been alive for kLostMs (a seat change or
// a registry's churn of one sample is no wipe). Every machine counts the same replicated soldiers.
inline constexpr std::uint64_t kLostMs=1500;
struct Watch {
    bool armed=false,lost=false;
    std::uint64_t noneSince=0;   // 0: some alive at the last sample
};
// One sample: whether this player's own NPC is alive, how many friendly NPC soldiers are. True once: the mission is lost.
constexpr bool Sample(Watch& w,bool ownAlive,int friendly,std::uint64_t now) noexcept {
    if(w.lost)return false;
    if(ownAlive)w.armed=true;
    if(!w.armed)return false;
    if(friendly>0){w.noneSince=0;return false;}
    if(!w.noneSince){w.noneSince=now ? now : 1;return false;}
    if(now-w.noneSince<kLostMs)return false;
    w.lost=true;
    return true;
}
}  // namespace crew::commander
