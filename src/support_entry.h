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

// Ground support must prove a complete route with the bounded planner (ground_navigation.h: +-256 cells round its
// origin, kNodes nodes) before anything is created. A stock map's edges stand 1100-1700 m from a central target, past
// that bound: every edge search ran out of nodes after ~1 min of planning and the request was refused (2026-10-09:
// nothing came, online or not). So the entries are candidates on rings round the target, inside the measured play
// area, still kMinJourney from the target and kObserverClear from the caller, within the planner's reach.
constexpr float kGroundRings[]={650.0f,800.0f,950.0f};
constexpr int kGroundRingCount=static_cast<int>(sizeof(kGroundRings)/sizeof(kGroundRings[0]));
constexpr int kGroundBearings=8,kGroundEntryMost=kGroundRingCount*kGroundBearings;
constexpr float kGroundPlanCell=4.0f;   // the support route profiles' cell (support_dispatch.cpp)
static_assert(kGroundRings[kGroundRingCount-1]+kEntryInset<256*kGroundPlanCell,"every ring entry within the planner's square");
static_assert(kGroundRings[0]>=kMinJourney,"a visible journey from every entry");
struct GroundEntries { float at[kGroundEntryMost][2]{}; int count=0; };
// Valid candidates, nearest ring first and within a ring the farthest from the caller first (less pop-in).
inline GroundEntries GroundEntryCandidates(const PlayArea& area,const float* target,const float* observer) noexcept {
    GroundEntries out;
    if(!area.ground || !std::isfinite(target[0]+target[2]))return out;
    for(int ring=0;ring<kGroundRingCount;++ring) {
        const int first=out.count;
        for(int b=0;b<kGroundBearings;++b) {
            const float a=6.28318531f*static_cast<float>(b)/kGroundBearings;
            const float p[3]={target[0]+std::cos(a)*kGroundRings[ring],target[1],target[2]+std::sin(a)*kGroundRings[ring]};
            if(p[0]<area.lo[0]+kEntryInset || p[0]>area.hi[0]-kEntryInset || p[2]<area.lo[1]+kEntryInset || p[2]>area.hi[1]-kEntryInset)continue;
            if(observer && FlatDistance(p,observer)<kObserverClear)continue;
            out.at[out.count][0]=p[0];out.at[out.count][1]=p[2];++out.count;
        }
        // Stable insertion sort of this ring: farther from the caller first.
        for(int i=first+1;i<out.count;++i)for(int j=i;j>first;--j) {
            const float pj[3]={out.at[j][0],0,out.at[j][1]},pk[3]={out.at[j-1][0],0,out.at[j-1][1]};
            if(!observer || FlatDistance(pj,observer)<=FlatDistance(pk,observer))break;
            for(int k=0;k<2;++k){const float t=out.at[j][k];out.at[j][k]=out.at[j-1][k];out.at[j-1][k]=t;}
        }
    }
    return out;
}

// Air support flies in from the edge (2026-10-09): created in the air at the entry, at the route's height. Several
// aircraft of one call fly line abreast there: slot 0 the lead at the entry, then alternately left / right of it,
// kFormationSide apart across the heading, level with it. Not behind it: the entry already stands kEntryInset inside the
// measured area, a slot behind it would be made outside. `spacing` scales the interval (helicopters fly closer).
constexpr float kFormationSide=70.0f;
inline void AirFormationSlot(const Route& route,int slot,float spacing,float* out) noexcept {
    const int pair=(slot+1)/2;
    const float side=slot==0 ? 0.0f : (slot%2 ? 1.0f : -1.0f)*static_cast<float>(pair)*kFormationSide*spacing;
    const float hx=route.heading[0],hz=route.heading[2];
    out[0]=route.from[0]+hz*side;out[1]=route.from[1];out[2]=route.from[2]-hx*side;
}
} // namespace crew::support
