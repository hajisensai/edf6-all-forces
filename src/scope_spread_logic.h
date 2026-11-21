// The scoped weapons' spread, zoomed or not (src/scope_spread.cpp; docs/scope-accuracy-re.md,
// docs/feedback-2026-11-21-scope-accuracy.md), pure: no Windows, no game (tests/scope_spread_test.cpp).
//
// The stock spread (EDF.dll 0x691560, every round's shot matrix): a cone of half angle weapon+0xE14 x weapon+0x378
// (FireAccuracy), the polar angle drawn uniformly in [0, that] (0x691AFA..0x691B10 -> 0x4E820). Nothing on that path reads
// the scope (weapon+0x780): zoomed or not, a round leaves the same cone. Here a player's round of a scoped weapon
// (SecondaryFire_Type 1) leaves a cone set by the weapon table's category and the scope:
//   - a sniper rifle (category 2 Ranger SniperRifle, 104 Wing Diver Pale_Long): zoomed the stock cone; unzoomed the stock
//     x SniperHip, at least SniperHipMin rad (19 sniper rifles have FireAccuracy 0: a factor alone would leave them exact);
//   - any other scoped weapon: zoomed x Aim (a little tighter), unzoomed x Hip (a little wider); a cone of 0 stays 0
//     (the Air Raider's markers and calls: 97 of 112 scoped ones are 0, and they are not guns).
#pragma once
#include <cstdint>

namespace crew::scopespread {
// The weapon table's categories (row +0x10, docs/loadout-re.md §8.1) that are sniper rifles. Root.cpk's scoped rows:
// Ranger SniperRifle 36, Wing Diver Pale_Long 27; the ported EDF5 / EDF4.1 sniper rifles carry the same categories
// (edf5port/weapons.json, edf41port/weapons.json "category").
inline constexpr std::int32_t kSniperRifle=2,kPaleLong=104;
constexpr bool SniperCategory(std::int32_t category) noexcept { return category==kSniperRifle || category==kPaleLong; }

// The ini's values (EDF6VehicleCrew.ini ScopeSpread*), or the host's online (host_settings_logic.h).
struct Params {
    bool on=true;
    float sniperHip=4.0f;       // ScopeSniperHipSpread: a sniper rifle unzoomed, x the stock cone
    float sniperHipMin=0.015f;  // ScopeSniperHipMinSpread: ...at least this half angle (rad)
    float hip=1.25f;            // ScopeHipSpread: another scoped weapon unzoomed
    float aim=0.8f;             // ScopeAimSpread: ...zoomed
};
// Their ranges (plugin.cpp clamps the ini to them; the host's word is clamped the same on arrival).
struct Range { float lo,hi; };
inline constexpr Range kSniperHipRange{1.0f,20.0f},kSniperHipMinRange{0.0f,0.2f},kHipRange{1.0f,5.0f},kAimRange{0.1f,1.0f};
constexpr float Clamp(float v,Range r) noexcept { return !(v>=r.lo) ? r.lo : v>r.hi ? r.hi : v; }   // NaN: the low end
constexpr Params Clamped(Params p) noexcept {
    p.sniperHip=Clamp(p.sniperHip,kSniperHipRange);
    p.sniperHipMin=Clamp(p.sniperHipMin,kSniperHipMinRange);
    p.hip=Clamp(p.hip,kHipRange);
    p.aim=Clamp(p.aim,kAimRange);
    return p;
}

// The half angle this round leaves with, `cone` the stock one (E14 x FireAccuracy).
constexpr float Cone(bool sniper,bool zoomed,float cone,const Params& p) noexcept {
    if(!p.on || !(cone>=0.0f))return cone;               // off, or a value no stock weapon has (NaN, negative): untouched
    if(sniper) {
        if(zoomed)return cone;
        const float wide=cone*p.sniperHip;
        return wide>p.sniperHipMin ? wide : p.sniperHipMin;
    }
    if(cone==0.0f)return cone;
    return cone*(zoomed ? p.aim : p.hip);
}
// The factor written into the log (the cone over the stock one; 0 when the stock is 0).
constexpr float Factor(bool sniper,bool zoomed,float cone,const Params& p) noexcept {
    return cone>0.0f ? Cone(sniper,zoomed,cone,p)/cone : 0.0f;
}

// How to make E14 x FireAccuracy that cone for one round, writing as little as possible: with both positive only E14
// (the charge weapons' per-frame E14, 0x69DB11, is put back after the round as every write here is); otherwise E14 1 and
// FireAccuracy the cone. FireAccuracy's other reader (0x767150, the online shot mode: < 0.01 counted, else full) runs
// once at the weapon's setup (0x68EBDE -> 0x691BC0), never inside a round, so a round's write does not change it.
struct Write { bool any=false; float cone=0.0f,accuracy=0.0f; };   // the values of +0xE14 and +0x378 for the round
constexpr Write Plan(float e14,float accuracy,float want) noexcept {
    if(!(want>=0.0f) || want==e14*accuracy)return Write{false,e14,accuracy};
    if(e14>0.0f && accuracy>0.0f)return Write{true,want/accuracy,accuracy};
    return Write{true,1.0f,want};
}
}  // namespace crew::scopespread
