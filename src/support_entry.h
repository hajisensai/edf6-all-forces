// Physical support entry planning. No game memory or spawning: callers provide verified terrain.
#pragma once
#include "playarea.h"
#include <cmath>

namespace crew::support {
enum class Refusal { none, noArea, noSky, noEntry, unsupported, online, cooldown, unavailable };
struct Route { float from[3]{},heading[3]{}; };
constexpr float kEntryInset=30.0f,kMinJourney=600.0f,kObserverClear=800.0f;

inline float FlatDistance(const float* a,const float* b) noexcept {
    const float x=a[0]-b[0],z=a[2]-b[2];return std::sqrt(x*x+z*z);
}

// Use the measured playable edge, not target +/- a fixed offset. Try all four edges in a stable order;
// the caller's transmitted heading only ranks them. Never fall back to spawning beside the target.
// `clear` validates a full size aircraft corridor and `height` supplies the highest required altitude.
template<class Clear,class Height>
Refusal AirRoute(const PlayArea& area,const float* target,const float* observer,const float* preferred,
                 float altitude,Clear clear,Height height,Route& route) noexcept {
    if(!area.ground || !std::isfinite(target[0]+target[1]+target[2]) ||
       !(area.hi[0]-area.lo[0]>2*kEntryInset && area.hi[1]-area.lo[1]>2*kEntryInset))return Refusal::noArea;
    if(target[0]<area.lo[0] || target[0]>area.hi[0] || target[2]<area.lo[1] || target[2]>area.hi[1])return Refusal::noEntry;
    const float cx=(area.lo[0]+area.hi[0])*0.5f,cz=(area.lo[1]+area.hi[1])*0.5f;
    const float edges[8][2]={{area.lo[0]+kEntryInset,cz},{area.hi[0]-kEntryInset,cz},
                           {cx,area.lo[1]+kEntryInset},{cx,area.hi[1]-kEntryInset},
                           {area.lo[0]+kEntryInset,area.lo[1]+kEntryInset},
                           {area.hi[0]-kEntryInset,area.lo[1]+kEntryInset},
                           {area.lo[0]+kEntryInset,area.hi[1]-kEntryInset},
                           {area.hi[0]-kEntryInset,area.hi[1]-kEntryInset}};
    bool found=false;float best=-3.0f;
    for(const auto& edge:edges) {
        Route candidate{{edge[0],target[1]+altitude,edge[1]}, {}};
        const float journey=FlatDistance(candidate.from,target);
        if(journey<kMinJourney || (observer && FlatDistance(candidate.from,observer)<kObserverClear))continue;
        float top=target[1]+altitude;
        bool valid=true;
        for(int sample=0;sample<=8;++sample) {
            const float t=static_cast<float>(sample)/8.0f;
            const float x=edge[0]+(target[0]-edge[0])*t,z=edge[1]+(target[2]-edge[1])*t;
            float y;
            if(!height(x,z,target[1],y) || !std::isfinite(y)){valid=false;break;}
            if(y+altitude>top)top=y+altitude;
        }
        if(!valid)continue;
        candidate.from[1]=top;
        const float end[3]={target[0],top,target[2]};
        if(!clear(candidate.from,end))continue;
        candidate.heading[0]=(target[0]-edge[0])/journey;
        candidate.heading[2]=(target[2]-edge[1])/journey;
        const float score=candidate.heading[0]*preferred[0]+candidate.heading[2]*preferred[2];
        if(!found || score>best){found=true;best=score;route=candidate;}
    }
    return found ? Refusal::none : Refusal::noEntry;
}
} // namespace crew::support
