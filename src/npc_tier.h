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
#pragma once
#include "support_call.h"
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <cwctype>

namespace crew::npctier {
struct TierRow { std::uint8_t kind; std::uint8_t series; std::uint16_t id; std::int32_t prev; std::int32_t level; const wchar_t* sgo; };
#include "npc_tiers.inc"
inline constexpr int kTierRowCount=static_cast<int>(sizeof(kTierRows)/sizeof(kTierRows[0]));
inline constexpr int kStarCount=8;    // a weapon record's star bytes (docs/loadout-re.md: GS+0xEB54+id*12)
inline constexpr int kStarMost=10;    // unlock_weapon's min(arg, 10) (docs/loadout-re.md 8.2); a nibble each on the wire
inline constexpr int kWeaponSlots=6;  // GameStatus: 6 weapon ids per class (src/loadout.cpp kWeapons)

constexpr const TierRow* RowOf(SupportWeapon kind,int id) noexcept {
    for(const auto& r:kTierRows)if(r.kind==static_cast<int>(kind) && r.id==id)return &r;
    return nullptr;
}
// Whether `kind` carries player weapons at all (the X-Morph infantry does not).
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
// Back into arms `kind` may carry (whatever a peer sent: the id one of its series', every star 0..kStarMost, nothing
// else set). 0: none, true with out->on false.
constexpr bool UnpackArms(std::uint64_t v,SupportWeapon kind,Arms* out) noexcept {
    Arms a;
    if(!v){if(out)*out=a;return true;}
    if(!(v&kArmsOn) || ((v>>48)&0x7FFFu))return false;
    a.on=true;a.id=static_cast<std::uint16_t>(v&0xFFFFu);
    if(!RowOf(kind,a.id))return false;
    for(int i=0;i<kStarCount;++i) {
        const auto s=static_cast<std::uint8_t>((v>>(16+4*i))&0xFu);
        if(s>kStarMost)return false;
        a.stars[i]=s;
    }
    if(out)*out=a;
    return true;
}

// The requesting player's weapons as its machine reads them (npc_arms.cpp: GameStatus, docs/loadout-re.md).
struct Inventory {
    int equipped[kWeaponSlots]{-1,-1,-1,-1,-1,-1};   // its current class's slots; -1 empty
    const void* context=nullptr;
    bool (*owned)(const void* context,int id) noexcept=nullptr;
    // The 8 star bytes of weapon `id`'s record; false: no record (then 0 stars).
    bool (*stars)(const void* context,int id,std::uint8_t* out) noexcept=nullptr;
};

constexpr Arms TierDown(const Arms& base) noexcept {
    if(!base.on)return base;
    Arms a=base;
    for(const auto& r:kTierRows) {
        if(r.id!=base.id)continue;
        if(r.prev>=0){a.id=static_cast<std::uint16_t>(r.prev);return a;}
        break;
    }
    for(int i=0;i<kStarCount;++i)if(a.stars[i])--a.stars[i];
    return a;
}
// The weapon one tier below the requester's for `kind` (the rule above). Off for a kind carrying none.
inline Arms PickArms(SupportWeapon kind,const Inventory& inv) noexcept {
    Arms base;
    const auto read=[&](int id) noexcept {
        Arms a;a.on=true;a.id=static_cast<std::uint16_t>(id);
        if(inv.stars && inv.stars(inv.context,id,a.stars))
            for(auto& s:a.stars)if(s>kStarMost)s=kStarMost;
        return a;
    };
    for(int i=0;i<kWeaponSlots && !base.on;++i)if(inv.equipped[i]>=0 && RowOf(kind,inv.equipped[i]))base=read(inv.equipped[i]);
    if(!base.on && inv.owned) {
        const TierRow* best=nullptr;
        for(const auto& r:kTierRows)
            if(r.kind==static_cast<int>(kind) && inv.owned(inv.context,r.id) &&
               (!best || r.level>best->level || (r.level==best->level && r.id>best->id)))best=&r;
        if(best)base=read(best->id);
    }
    if(!base.on) {
        const TierRow* low=nullptr;
        for(const auto& r:kTierRows)
            if(r.kind==static_cast<int>(kind) && (!low || r.level<low->level || (r.level==low->level && r.id<low->id)))low=&r;
        if(!low)return Arms{};
        base.on=true;base.id=low->id;   // 0 stars
    }
    return TierDown(base);
}

// The CreateObject path of `a`'s weapon (app:/weapon/<WEAPONTABLE name lower case>.sgo, the form the stock soldier
// templates' soldier_load_weapon uses). 0 when off, unknown or it does not fit.
inline std::size_t ArmsPath(SupportWeapon kind,const Arms& a,wchar_t* out,std::size_t capacity) noexcept {
    if(out && capacity)out[0]=0;
    const TierRow* r=a.on ? RowOf(kind,a.id) : nullptr;
    if(!r || !out)return 0;
    constexpr wchar_t kDir[]=L"app:/weapon/",kExt[]=L".sgo";
    std::size_t n=0;
    const auto put=[&](wchar_t c) noexcept {if(n+1<capacity)out[n]=c;++n;};
    for(const wchar_t* s=kDir;*s;++s)put(*s);
    for(const wchar_t* s=r->sgo;*s;++s)put(static_cast<wchar_t>(std::towlower(*s)));
    for(const wchar_t* s=kExt;*s;++s)put(*s);
    if(n+1>capacity){out[0]=0;return 0;}
    out[n]=0;
    return n;
}
}  // namespace crew::npctier
