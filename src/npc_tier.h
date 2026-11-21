// A called support soldier's weapon (the maintainer, 2026-11-08, reversing 2026-10-04's 53cec944 for the plugin's own
// support: "与玩家同数值的武器，但比叫援军的玩家低一档"; docs/feature-npc-weapon-tier.md). Pure logic, no Windows or game:
// tests/npc_tier_test.cpp runs it offline; src/npc_arms.cpp feeds it the requesting player's weapon records and
// support_soldier.cpp builds the soldier with what it picks.
//
// A soldier kind carries the player weapons of its series (src/npc_tiers.inc, tools/gen_npc_tiers.py from
// docs/weapons.csv; the table for review: docs/npc-weapon-tiers.md). What it carries is one tier below the requesting
// player's:
//   base   the requester's equipped weapon of the kind's series (its current class's slots, in slot order), else the
//          highest-level one of them it owns (its weapon record's owned bit), else the kind's lowest-level weapon at 0
//          stars (a new save);
//   tier   the base's previous model in its series with the base's stars; none (the first of its series): the base
//          itself with each star one less, a star already 0 staying 0 (all 0: the base itself, unchanged).
// The pick (its weapon id and its 8 star bytes) is the requester's and travels in the support transaction
// (support_net.h Unit::arms, RequesterArms), so every machine builds the same soldier.
//
// A Fencer has two hands (WeaponSet 0 and 1; the stock templates: the main weapon, then the AI shield), and its dash /
// boost come from what each hand holds (npc_mobility.h Effective). Since 2026-11-21 a Fencer kind's soldier takes both
// from the requester's Fencer loadout (PickHands; docs/feature-npc-weapon-tier.md 2.5):
//   group  the requester's Fencer hand slots Slot1L, 1R, 2L, 2R (its Fencer class's, whatever class it plays now): the
//          base above is looked for there, in that order, and its group (1L+1R or 2L+2R) is the one taken;
//   main   (WeaponSet 0, the template's main weapon) the tier below the base, as above;
//   off    (WeaponSet 1, in place of the AI shield) the other slot of the base's group, the same model, each star one
//          less (the first-of-series rule; the off hand's model is the player's choice, not a series'); none when the
//          base was not equipped (owned / lowest) or that slot is empty: the template's shield stays.
// A preset may name both hands instead (support_loadout.h ParseSupportPreset kind#R:L, PresetHand): any Fencer hand
// weapon (kFencerHands) in either, with the planning machine's stars, each one less.
#pragma once
#include "support_call.h"
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <cwctype>

namespace crew::npctier {
struct TierRow { std::uint8_t kind; std::uint8_t series; std::uint16_t id; std::int32_t prev; std::int32_t level; const wchar_t* sgo; };
struct HandRow { std::uint16_t id; const wchar_t* sgo; };
#include "npc_tiers.inc"
inline constexpr int kTierRowCount=static_cast<int>(sizeof(kTierRows)/sizeof(kTierRows[0]));
inline constexpr int kFencerHandCount=static_cast<int>(sizeof(kFencerHands)/sizeof(kFencerHands[0]));
inline constexpr int kFencerHandSlots=4;   // Slot1L, 1R, 2L, 2R (docs/loadout-re.md 1.2); a group: slots 2g and 2g+1
inline constexpr int kStarCount=8;    // a weapon record's star bytes (docs/loadout-re.md: GS+0xEB54+id*12)
inline constexpr int kStarMost=10;    // unlock_weapon's min(arg, 10) (docs/loadout-re.md 8.2); a nibble each on the wire
inline constexpr int kWeaponSlots=6;  // GameStatus: 6 weapon ids per class (src/loadout.cpp kWeapons)

constexpr const TierRow* RowOf(SupportWeapon kind,int id) noexcept {
    for(const auto& r:kTierRows)if(r.kind==static_cast<int>(kind) && r.id==id)return &r;
    return nullptr;
}
constexpr const HandRow* HandOf(int id) noexcept {
    for(const auto& r:kFencerHands)if(r.id==id)return &r;
    return nullptr;
}
constexpr bool FencerKind(SupportWeapon kind) noexcept { return SupportSoldierClass(kind)==2; }
// The WEAPONTABLE name of weapon `id` in `kind`'s main hand: one of its series, or for a Fencer kind any Fencer hand
// weapon (a preset's hands, PresetHand). nullptr: not one it may hold.
constexpr const wchar_t* MainSgo(SupportWeapon kind,int id) noexcept {
    if(const TierRow* r=RowOf(kind,id))return r->sgo;
    if(const HandRow* h=FencerKind(kind) ? HandOf(id) : nullptr)return h->sgo;
    return nullptr;
}
// Whether `kind` carries player weapons at all (every kind has since 2026-11-10: the X-Morph infantry the rifle's series).
constexpr bool KindArmed(SupportWeapon kind) noexcept {
    for(const auto& r:kTierRows)if(r.kind==static_cast<int>(kind))return true;
    return false;
}

struct Arms {
    bool on=false;
    std::uint16_t id=0;
    std::uint8_t stars[kStarCount]{};
    constexpr bool operator==(const Arms& o) const noexcept {
        if(on!=o.on || (on && id!=o.id))return false;
        for(int i=0;on && i<kStarCount;++i)if(stars[i]!=o.stars[i])return false;
        return true;
    }
};

// 64 bits: bit 63 on, bits 0-15 the weapon id, bits 16-47 the stars a nibble each (star i at 16+4i), bits 48-62 zero.
inline constexpr std::uint64_t kArmsOn=1ull<<63;
constexpr std::uint64_t PackArms(const Arms& a) noexcept {
    if(!a.on)return 0;
    std::uint64_t v=kArmsOn|a.id;
    for(int i=0;i<kStarCount;++i)v|=static_cast<std::uint64_t>(a.stars[i]&0xFu)<<(16+4*i);
    return v;
}
namespace detail {
// The word back into arms (on bit, no stray bits, every star 0..kStarMost), the id not looked at. 0: none.
constexpr bool Unpack(std::uint64_t v,Arms* out) noexcept {
    Arms a;
    if(!v){*out=a;return true;}
    if(!(v&kArmsOn) || ((v>>48)&0x7FFFu))return false;
    a.on=true;a.id=static_cast<std::uint16_t>(v&0xFFFFu);
    for(int i=0;i<kStarCount;++i) {
        const auto s=static_cast<std::uint8_t>((v>>(16+4*i))&0xFu);
        if(s>kStarMost)return false;
        a.stars[i]=s;
    }
    *out=a;
    return true;
}
}  // namespace detail
// Back into arms `kind` may carry in its main hand (whatever a peer sent: the id one of its series' or, a Fencer kind,
// any Fencer hand weapon (MainSgo), every star 0..kStarMost, nothing else set). 0: none, true with out->on false.
constexpr bool UnpackArms(std::uint64_t v,SupportWeapon kind,Arms* out) noexcept {
    Arms a;
    if(!detail::Unpack(v,&a) || (a.on && !MainSgo(kind,a.id)))return false;
    if(out)*out=a;
    return true;
}
// The same for a Fencer kind's off hand (WeaponSet 1, support_net.h Unit::offhand): any Fencer hand weapon; another
// kind holds none (only 0 passes).
constexpr bool UnpackHand(std::uint64_t v,SupportWeapon kind,Arms* out) noexcept {
    Arms a;
    if(!detail::Unpack(v,&a) || (a.on && (!FencerKind(kind) || !HandOf(a.id))))return false;
    if(out)*out=a;
    return true;
}

// The requesting player's weapons as its machine reads them (npc_arms.cpp: GameStatus, docs/loadout-re.md).
struct Inventory {
    int equipped[kWeaponSlots]{-1,-1,-1,-1,-1,-1};   // its current class's slots; -1 empty
    int fencer[kFencerHandSlots]{-1,-1,-1,-1};       // its Fencer class's hand slots 1L, 1R, 2L, 2R; -1 empty
    const void* context=nullptr;
    bool (*owned)(const void* context,int id) noexcept=nullptr;
    // The 8 star bytes of weapon `id`'s record; false: no record (then 0 stars).
    bool (*stars)(const void* context,int id,std::uint8_t* out) noexcept=nullptr;
};

// Each star one less, 0 staying 0 (the tier below the first model of a series, and an off hand's).
constexpr Arms StarsDown(const Arms& base) noexcept {
    Arms a=base;
    for(int i=0;a.on && i<kStarCount;++i)if(a.stars[i])--a.stars[i];
    return a;
}
constexpr Arms TierDown(const Arms& base) noexcept {
    if(!base.on)return base;
    Arms a=base;
    for(const auto& r:kTierRows) {
        if(r.id!=base.id)continue;
        if(r.prev>=0){a.id=static_cast<std::uint16_t>(r.prev);return a;}
        break;
    }
    return StarsDown(a);
}
// Weapon `id` with the requester's stars for it (its record's, clamped to kStarMost; no record: 0).
inline Arms ReadArms(const Inventory& inv,int id) noexcept {
    Arms a;a.on=true;a.id=static_cast<std::uint16_t>(id);
    if(!inv.stars || !inv.stars(inv.context,id,a.stars)) {
        for(auto& s:a.stars)s=0;
        return a;
    }
    for(auto& s:a.stars)if(s>kStarMost)s=kStarMost;
    return a;
}
// The requester's base weapon for `kind` (the rule above); `*slot` the Fencer hand slot it was found in (a Fencer kind
// looks in its Fencer loadout, every other kind in its current class's slots), -1 when it was not equipped there.
inline Arms BaseArms(SupportWeapon kind,const Inventory& inv,int* slot) noexcept {
    Arms base;
    *slot=-1;
    if(FencerKind(kind)) {
        for(int i=0;i<kFencerHandSlots && !base.on;++i)
            if(inv.fencer[i]>=0 && RowOf(kind,inv.fencer[i])){base=ReadArms(inv,inv.fencer[i]);*slot=i;}
    } else {
        for(int i=0;i<kWeaponSlots && !base.on;++i)if(inv.equipped[i]>=0 && RowOf(kind,inv.equipped[i]))base=ReadArms(inv,inv.equipped[i]);
    }
    if(!base.on && inv.owned) {
        const TierRow* best=nullptr;
        for(const auto& r:kTierRows)
            if(r.kind==static_cast<int>(kind) && inv.owned(inv.context,r.id) &&
               (!best || r.level>best->level || (r.level==best->level && r.id>best->id)))best=&r;
        if(best)base=ReadArms(inv,best->id);
    }
    if(!base.on) {
        const TierRow* low=nullptr;
        for(const auto& r:kTierRows)
            if(r.kind==static_cast<int>(kind) && (!low || r.level<low->level || (r.level==low->level && r.id<low->id)))low=&r;
        if(!low)return Arms{};
        base.on=true;base.id=low->id;   // 0 stars
    }
    return base;
}
// What a soldier of `kind` holds: `main` (WeaponSet 0) and, a Fencer kind's, `off` (WeaponSet 1; off: the template's
// AI shield stays). The rule above.
struct Hands { Arms main,off; };
inline Hands PickHands(SupportWeapon kind,const Inventory& inv) noexcept {
    Hands h;
    int slot=-1;
    h.main=TierDown(BaseArms(kind,inv,&slot));
    if(slot>=0 && FencerKind(kind)) {
        const int other=inv.fencer[slot^1];
        if(other>=0 && HandOf(other))h.off=StarsDown(ReadArms(inv,other));
    }
    return h;
}
// The weapon one tier below the requester's for `kind` (the rule above). Off for a kind carrying none.
inline Arms PickArms(SupportWeapon kind,const Inventory& inv) noexcept { return PickHands(kind,inv).main; }
// A preset's hand weapon `id` (support_loadout.h FencerHands) with the planning machine's stars, each one less; off when
// `id` is no Fencer hand weapon.
inline Arms PresetHand(const Inventory& inv,int id) noexcept {
    return id>=0 && HandOf(id) ? StarsDown(ReadArms(inv,id)) : Arms{};
}
// Commander soldiers use their player's base weapon without lowering its tier.
inline Arms PickBase(SupportWeapon kind,const Inventory& inv) noexcept {
    int slot=-1;
    return BaseArms(kind,inv,&slot);
}

// The CreateObject path of `a`'s weapon (app:/weapon/<WEAPONTABLE name lower case>.sgo, the form the stock soldier
// templates' soldier_load_weapon uses). 0 when off, unknown or it does not fit.
namespace detail {
inline std::size_t SgoPath(const wchar_t* sgo,wchar_t* out,std::size_t capacity) noexcept {
    if(out && capacity)out[0]=0;
    if(!sgo || !out)return 0;
    constexpr wchar_t kDir[]=L"app:/weapon/",kExt[]=L".sgo";
    std::size_t n=0;
    const auto put=[&](wchar_t c) noexcept {if(n+1<capacity)out[n]=c;++n;};
    for(const wchar_t* s=kDir;*s;++s)put(*s);
    for(const wchar_t* s=sgo;*s;++s)put(static_cast<wchar_t>(std::towlower(*s)));
    for(const wchar_t* s=kExt;*s;++s)put(*s);
    if(n+1>capacity){out[0]=0;return 0;}
    out[n]=0;
    return n;
}
}  // namespace detail
inline std::size_t ArmsPath(SupportWeapon kind,const Arms& a,wchar_t* out,std::size_t capacity) noexcept {
    return detail::SgoPath(a.on ? MainSgo(kind,a.id) : nullptr,out,capacity);
}
// The same for a Fencer's second hand (any Fencer hand weapon, UnpackHand).
inline std::size_t HandPath(const Arms& a,wchar_t* out,std::size_t capacity) noexcept {
    const HandRow* h=a.on ? HandOf(a.id) : nullptr;
    return detail::SgoPath(h ? h->sgo : nullptr,out,capacity);
}
}  // namespace crew::npctier
