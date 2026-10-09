// The stock armor gauge while the player rides (the user, 2026-10-09: "上了载具以后，可以把原版的左上角的血条hud隐藏吧，
// 我们已经有自制的hud了"; docs/hud-re.md §9.3). The gauge (HUiHudPowerGuage, lyt_HudPowerGuage01: the player's armor and
// the vehicle's durability, top left) is one layout; its root node Guage_Root is the HUD object's +0x788 (the HUiHud base's
// 0x816080 looks it up by that name), and a layout node's +0x1F8 is its shown flag (the node's constructor 0x7E999C sets
// it 1; the stock weapon gauge's layout hides its panels by writing it 0, 0x832348). stockgauge.cpp holds the root's flag
// at 0 while the HUD of ours lists the vehicle the gauge's player rides (the weapon gauge's own test: SetStockGaugeCover).
//
// The hold is map_stock_hud.h's record (the map's hold on the camera's whole-HUD switch), one a gauge: the same owner of
// one byte while it hides, the value found put back when it ends, a last mission's hold dropped without touching what may
// be another object now. Only the slot choice is this file's: a gauge's own hold, else a free record, else the oldest
// mission's hold (a gauge gone with its mission). Pure: tools/armor_hud_check.cpp.
#pragma once
#include "map_stock_hud.h"
#include <cstdint>

namespace crew {
namespace armorhud {
constexpr int kGauges=4;   // one a local player; old missions' holds besides

// The record of `gauge` in mission `generation`: its own hold, else a free one (cleared), else the oldest mission's hold
// (cleared: its gauge is gone with that mission). nullptr: all held this mission (more gauges than kGauges).
inline maphud::Record* Slot(maphud::Record (&rs)[kGauges],const void* gauge,std::uint64_t generation) noexcept {
    maphud::Record* free=nullptr;
    maphud::Record* stale=nullptr;
    for(auto& r:rs) {
        if(r.hidden && r.cam==gauge)return &r;
        if(!r.hidden){if(!free)free=&r;continue;}
        if(r.generation!=generation && (!stale || r.generation<stale->generation))stale=&r;
    }
    maphud::Record* const pick=free ? free : stale;
    if(pick)*pick=maphud::Record{};
    return pick;
}

// One update of `gauge` (its root's shown flag `shown`): hidden while `hide`, the found value back when it stops.
// Returns whether the gauge is held hidden now.
inline bool Step(maphud::Record (&rs)[kGauges],const void* gauge,std::uint64_t generation,bool hide,unsigned char* shown) noexcept {
    maphud::Record* const r=Slot(rs,gauge,generation);
    if(!r)return false;
    maphud::Step(*r,gauge,generation,hide,shown);
    return r->hidden;
}

// --- What the hidden gauge showed, for the HUD of ours to show in its place (the user, 2026-10-09: "让护甲显示在咱们的
// hud不就行了") ---
// The gauge's own numbers: its player's HP (+0x2F8) over its most (+0x2F4), the soldier the update casts its owner to
// (0x827126: the bar's share; the layout 0x826A83 / 0x826A9C: the number and its most, as integers), and the vehicle's
// the same two fields (the layout 0x8277B0's bar_vehicle). Its low state: the update sets +0xB01 while the share is at
// or under 0.25 (0x827723: comiss against the float at 0x1765A14 = 0.25).
constexpr float kLowArmor=0.25f;

// One HP readout: `hp` of `most` (a soldier's armor, a vehicle's durability). Not shown (false) unless both are finite
// and `most` above 0: no made-up number.
struct Durability { float hp,most,share; bool low; };
inline bool Read(float hp,float most,Durability* out) noexcept {
    if(!(most>0.0f) || !(most<3.4e38f) || !(hp==hp) || !(hp<3.4e38f) || !(hp>-3.4e38f))return false;
    const float h=hp<0.0f ? 0.0f : hp;
    const float share=h/most>1.0f ? 1.0f : h/most;
    *out=Durability{h,most,share,share<=kLowArmor};
    return true;
}

// The readout the HUD of ours shows in the gauge's place: given only while the gauge is held hidden (`held`: the very
// same test that hides it, so the two never show together and never both go), its player's armor read, and the vehicle's
// durability with it when it reads.
struct Readout { Durability armor,hull; bool hasHull; };
inline bool Publish(bool held,float armor,float armorMost,float hull,float hullMost,Readout* out) noexcept {
    Readout r{};
    if(!held || !Read(armor,armorMost,&r.armor))return false;
    r.hasHull=Read(hull,hullMost,&r.hull);
    *out=r;
    return true;
}
}  // namespace armorhud
}  // namespace crew
