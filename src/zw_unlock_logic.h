// The Vehicler's later vehicles unlocked (docs/newclass-re/vehicler-v4v9-plan.md §2; pylib/zw_vehicles.py UNLOCKS): the pure
// part, no game and no Windows (tests/zw_v4v9_test.cpp).
//
// Progress is the EDF6 story's (content 0, campaign.h GameContentId): the current mode's save (mission_packs.h ModeSave,
// GS+0x57A4) keeps a byte a mission row a class (its bits the difficulties cleared). A row counts as cleared when any class
// cleared it on any difficulty: the unlock is the campaign's progress, not one class's. The stock story has kStoryMissions
// rows in that order; its last row is the final mission.
//   mid    a third of the rows cleared
//   late   two thirds
//   final  the last row cleared
// The grant itself (zw_unlock.cpp) owns the row's weapon record (unlock_ported.h Own: NEW the first time) and never takes
// one back: a save that has a vehicle keeps it.
//
// Capture (a vehicle with ZwVehicleSpec::capture): a Vehicler of this machine boarding, or standing within kCaptureReach of,
// an intact (alive) hull of that kind it does not own yet unlocks it on this machine's save.
#pragma once
#include "zw_vehicle_catalog.h"
#include <cstddef>
#include <cstdint>

namespace crew::zwunlock {
inline constexpr int kStoryMissions=147;   // the EDF6 story's mission rows (content 0)
inline constexpr int kStoryContent=0;
inline constexpr float kCaptureReach=6.0f;   // m from the hull's origin (the stock tank is 5.6 m half length)

// How many of the first `rows` rows any of `classes` classes cleared: clear[c * stride + r] != 0.
constexpr int Cleared(const std::uint8_t* clear,std::size_t classes,std::size_t stride,int rows) noexcept {
    if(!clear || rows<=0)return 0;
    int n=0;
    for(int r=0;r<rows;++r) {
        bool any=false;
        for(std::size_t c=0;c<classes && !any;++c)any=clear[c*stride+static_cast<std::size_t>(r)]!=0;
        if(any)++n;
    }
    return n;
}

// Whether stage `u` is reached with `cleared` rows of `rows` cleared and the last one cleared or not.
constexpr bool Reached(ZwUnlock u,int cleared,int rows,bool lastCleared) noexcept {
    switch(u) {
    case ZwUnlock::start:return true;
    case ZwUnlock::mid:return rows>0 && cleared*3>=rows;
    case ZwUnlock::late:return rows>0 && cleared*3>=rows*2;
    case ZwUnlock::final:return lastCleared;
    }
    return false;
}

// The kinds progress unlocks (bit k: kZwVehicles[k]), the start ones excluded (the table owns them).
constexpr std::uint32_t ProgressKinds(int cleared,int rows,bool lastCleared) noexcept {
    std::uint32_t out=0;
    for(int k=0;k<kZwVehicleCount;++k)
        if(kZwVehicles[k].unlock!=ZwUnlock::start && Reached(kZwVehicles[k].unlock,cleared,rows,lastCleared))out|=1u<<k;
    return out;
}

// Whether this machine's Vehicler captures the hull of `kind`: a capture kind it does not own, the hull alive, and the
// player in it or within reach of it.
struct CaptureFacts {
    int kind=-1;            // the hull's (ZwVehicleKind), -1 not a Vehicler's vehicle
    bool vehicler=false;    // the player is the Vehicler (soldier::ZwType 4)
    bool alive=false;       // the hull is not destroyed
    bool aboard=false;      // the player rides it
    float distance=1e9f;    // m, the player to the hull's origin
    std::uint32_t owned=0;  // this machine's ZwUnlockedKinds
};
constexpr bool Captures(const CaptureFacts& f) noexcept {
    const ZwVehicleSpec* v=ZwVehicleInfo(f.kind);
    return v && v->capture && f.vehicler && f.alive && !(f.owned>>f.kind&1u) && (f.aboard || f.distance<=kCaptureReach);
}
}  // namespace crew::zwunlock
