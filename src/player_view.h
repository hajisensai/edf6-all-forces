// Each local player's own view-projection (split screen: the game draws one HUD pass per player's camera, the follower
// gauge hook subcarrier.cpp GaugeHook once per HUiHud, whose camera +0x18 follows that player's soldier: +0x350 its
// reference, +0x360 its SoldierBase cast, map.cpp). LastViewProj keeps only the last pass drawn (2P's in split screen),
// so a ray for a given soldier comes from here (hud.cpp CameraRayOf; the stock spot, turretaim.cpp). Pure, no EDF.dll:
// tools/spot_ray_check.cpp checks it. The caller holds its lock.
#pragma once
#include <Windows.h>
#include <cstring>

namespace crew {
namespace pview {
constexpr int kSlots=4;                 // the game's viewports at most
constexpr ULONGLONG kFreshMs=1000;      // wall ms: a slot not drawn for this long is no one's view now

struct Slot { const void* ref; const void* soldier; float vp[16]; ULONGLONG at; };
struct Store {
    Slot slot[kSlots]{};
    // A pass drawn for the camera following `ref` / `soldier` (either may be null, not both) at `now`.
    void Keep(const void* ref,const void* soldier,const float* vp,ULONGLONG now) noexcept {
        if(!vp || (!ref && !soldier))return;
        Slot* use=nullptr;
        for(auto& s:slot)if((ref && s.ref==ref) || (soldier && s.soldier==soldier)){use=&s;break;}
        if(!use)for(auto& s:slot)if(!s.at || now-s.at>kFreshMs){use=&s;break;}
        if(!use) {   // all fresh: the oldest goes
            use=&slot[0];
            for(auto& s:slot)if(s.at<use->at)use=&s;
        }
        use->ref=ref;use->soldier=soldier;std::memcpy(use->vp,vp,sizeof(use->vp));use->at=now;
    }
    // The view drawn last for `human` (its reference or its soldier cast), fresh at `now`; false: none.
    bool Find(const void* human,ULONGLONG now,float* vp) const noexcept {
        if(!human)return false;
        for(const auto& s:slot)
            if(s.at && now-s.at<=kFreshMs && (s.ref==human || s.soldier==human)){std::memcpy(vp,s.vp,sizeof(s.vp));return true;}
        return false;
    }
};
}  // namespace pview
}  // namespace crew
