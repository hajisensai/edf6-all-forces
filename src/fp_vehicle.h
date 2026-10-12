// First person in a vehicle (fp_vehicle.cpp; the user, 2026-10-13: "做载具第一人称", each seat its own view: "炮舰机的
// 炮手位和驾驶位" differ, every vehicle a real cockpit). No game state here: tools/fp_view_check.cpp runs it offline.
//  - A station is where one seat's eye is: a bone of the vehicle's model and a point in that bone's frame (x left,
//    y up, z ahead: the game's model axes). The eye rides the bone (a gunner's on the turret turns with it); the
//    camera's turn stays whatever drives it (the turret camera, the heli's mouse camera, the stock seat camera).
//  - The table is looked up by the vehicle's name (crew.cpp kClasses, or the plugin's own bodies) and seat; a row with
//    seat -1 serves any seat of that vehicle not listed on its own. No row: the rider's own head (the seat's place).
#pragma once
#include "fp_view.h"
#include <cstring>

namespace crew {
namespace fpveh {
struct Station {
    const char* vehicle;    // crew.cpp kClasses name ("403_Tank") or a plugin body ("sazabi")
    int seat;               // the seat index, -1: any seat of the vehicle without a row of its own
    const wchar_t* bone;    // the vehicle model's bone the eye rides
    float at[3];            // the eye in that bone's frame, m
};

// The row for `vehicle`'s seat `seat` in `table` (`n` rows): its own, else the vehicle's any-seat row, else null.
inline const Station* Find(const Station* table,int n,const char* vehicle,int seat) noexcept {
    const Station* any=nullptr;
    if(!vehicle)return nullptr;
    for(int i=0;i<n;++i) {
        const Station& s=table[i];
        if(!s.vehicle || std::strcmp(s.vehicle,vehicle)!=0)continue;
        if(s.seat==seat)return &s;
        if(s.seat<0 && !any)any=&s;
    }
    return any;
}

// The eye in the world: the station's point through its bone's world matrix (rows right, up, ahead, place).
inline void EyeOn(const fpview::M4& bone,const float* at,float* eye) noexcept { fpview::Point(bone,at,eye); }
}  // namespace fpveh
}  // namespace crew
