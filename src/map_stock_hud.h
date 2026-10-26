// The stock HUD while the map shows (the user, 2026-10-06: "按下m应该吧原本的地图和武器之类的隐藏"; docs/hud-re.md §11).
// The game's own switch for the whole player HUD is the camera's +0x200 (xgs camera; the mission scripts'
// SetPlayerHudShow(bool), 0x1BA790, writes it through 0x118DF30 on the camera of each viewport, for the cutscenes):
// the radar, the weapon gauges, the armor gauge, the damage marks (HUiHud*Object draw 0x8168B0), the crosshair
// (0x8039F0) and the rescue line (0x808410) skip their draw while it is 0. map.cpp keeps it 0 on the map's camera
// while the map shows there and puts back what the game last asked for when it stops.
//
// The record is the one owner of that byte while it hides: the camera step (Step) writes it, and the scripts' writes
// (GameSet, from the redirected call in SetPlayerHudShow) go into `want` instead of the byte, so a cutscene that hides
// or shows the HUD while the map is open gets what it asked for when the map closes. Pure: tools/map_hud_check.cpp.
#pragma once
#include <cstdint>

namespace maphud {
struct Record {
    const void* cam=nullptr;     // the camera whose switch is held (only ever written from inside its own step)
    std::uint64_t generation=0;  // the mission it was taken in (map_camera_state.h CameraSession)
    bool hidden=false;           // the switch is held at 0
    bool want=true;              // what the game last asked for: put back when the hold ends
    bool damage=false;           // the hold is a vehicle's first person's alone: the damage layer is lent the switch (LendsDamage)
    bool lent=false;             // the switch is lent to the damage layer's draw now (Lend .. Return): its 1 is not the game's
};

// Follower gauges do not read the stock switch. Suppress only gauges belonging to the camera held this mission.
inline bool Hides(const Record& r,const void* cam,std::uint64_t generation) noexcept {
    return cam && r.hidden && r.cam==cam && r.generation==generation;
}

// The camera `cam` in its step (alive: it is being stepped), the mission `generation`, whether the map shows on it
// (`hide`), its switch byte `shown`. Returns whether a hold is on (this camera's or another's).
// `damage`: the hold is for a vehicle's first person alone (not the map's view): the damage layer keeps drawing.
inline bool Step(Record& r,const void* cam,std::uint64_t generation,bool hide,unsigned char* shown,bool damage=false) noexcept {
    if(r.hidden && r.generation!=generation) {
        // The last mission's hold: its camera may be gone and this one built at the same address (its constructor
        // set 1). Showing is safe either way; a hide it asked for stays with the camera it was asked of.
        if(r.cam==cam && r.want)*shown=1;
        r=Record{};
    }
    if(r.hidden && r.cam!=cam)return true;   // another camera (a second local player's): left as it is
    if(hide) {
        if(!r.hidden)r=Record{cam,generation,true,*shown!=0};
        else if(*shown && !r.lent)r.want=true;   // written by something not seen here: the game's latest (not the lent 1)
        *shown=0;
        r.damage=damage;
        return true;
    }
    if(r.hidden){*shown=r.want ? 1 : 0;r=Record{};}
    return false;
}

// The damage layer (the user, 2026-10-25: "第一人称的伤害显示没了"). The damage numbers (ui::DamageNumberDrawTexture) and
// the hit direction marks are ui::HUiHudDamage, drawn through its wrapper HUiHudDamageObject (vtable 0x17FCA28, its
// draw slot 3 the shared 0x8168B0, which skips everything while the camera's +0x200 is 0: docs/hud-re.md §12.3). A
// vehicle's first person holds the switch at 0 (its gauges are on the cockpit's screens), and the numbers went with
// the gauges. They are no gauge: the hit's own feedback in the world. While the hold on `cam` is a first person's alone
// and the game wants its HUD (`want`: no cutscene hiding it), map.cpp lends that one draw the switch.
inline bool LendsDamage(const Record& r,const void* cam,std::uint64_t generation) noexcept {
    return Hides(r,cam,generation) && r.damage && r.want;
}

// The damage layer's draw borrows the switch (under the caller's lock; the draw itself runs without it, so nothing it
// calls can wait on that lock): true when it wrote 1 (`shown` was 0 and LendsDamage), then Return after the draw.
inline bool Lend(Record& r,const void* cam,std::uint64_t generation,unsigned char* shown) noexcept {
    if(*shown || r.lent || !LendsDamage(r,cam,generation))return false;
    *shown=1;r.lent=true;
    return true;
}
// After the lent draw (under the lock again): 0 back while the hold still stands on `cam`; a hold that ended meanwhile
// (Step put the game's value back, the record cleared) is left as Step left it.
inline void Return(Record& r,const void* cam,unsigned char* shown) noexcept {
    if(!r.lent)return;
    r.lent=false;
    if(r.hidden && r.cam==cam)*shown=0;
}

// SetPlayerHudShow's write of `show` to `cam` in mission `generation`: true when the caller writes it (no hold on that
// camera), false when it is kept for the end of the hold instead.
inline bool GameSet(Record& r,const void* cam,std::uint64_t generation,bool show) noexcept {
    if(!r.hidden || r.cam!=cam)return true;
    if(r.generation!=generation){r=Record{};return true;}   // a stale hold (the last mission's): the game's write wins
    r.want=show;
    return false;
}
}  // namespace maphud
