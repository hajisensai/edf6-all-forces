// The soldiers' picks through the frame grid (common/edf/npc_grid.h Grid): npc_logic.h's PickTarget and the nearest over
// the ground, each the same answer as the scan it names (tools/npc_grid_check.cpp compares them with the brute-force
// scans over thousands of random points). The grid itself, and the queries that rank nothing, are common/edf/npc_grid.h.
#pragma once
#include "edf/npc_grid.h"
#include "npc_logic.h"
#include <cmath>

namespace npc::grid {
// --- The scans, through the grid (each the same answer as the scan it names) ---

// npc_logic.h PickTarget through `g` (built over the same n points): the nearest first from the eye, done once no
// point left can rank before the best (its score's bound from the ring's distance: TargetScore is monotonic in the
// distance, the held target's kKeepTarget taken off every one). A tie: the lower index (the scan's first). usable is
// asked as the scan asks it, of a point that would become the best (its answers fixed within the frame: the same pick;
// it may be asked of other points than the scan asked).
template<class AimOf,class IsCurrent,class Usable>
int PickTarget(const Grid& g,int n,AimOf aimOf,IsCurrent isCurrent,Usable usable,const float* eye,const float* anchor,
               float scope,float reach) noexcept {
    if(g.Points()!=n || g.Odd() || !std::isfinite(eye[0]+eye[1]+eye[2]+anchor[0]+anchor[2]+scope+reach) || !(scope>=0.0f) ||
       !Grid::Regular(eye[0],eye[2]) || !Grid::Regular(anchor[0],anchor[2]))
        return npc::PickTarget(n,aimOf,isCurrent,usable,eye,anchor,scope,reach);
    int best=-1;float bestScore=0.0f;
    g.NearFirst(eye[0],eye[2],g.Within(anchor[0],anchor[2],scope),[&](float lb) noexcept {
        if(best<0)return false;
        const float d=lb*(1.0f-1e-5f);   // Dist's own rounding
        return d-kKeepTarget+(reach>0.0f && d>reach ? kOutOfReach : 0.0f)>bestScore;
    },[&](int i) {
        const float* aim=aimOf(i);
        if(Horiz(anchor,aim)>scope)return;
        const float score=TargetScore(Dist(eye,aim),reach,isCurrent(i));
        if(best>=0 && !(score<bestScore || (score==bestScore && i<best)))return;
        if(!usable(i))return;
        best=i;bestScore=score;
    });
    return best;
}

template<class FriendAt,class IsSelf>
bool BlastClear(const Grid& g,const Shot& shot,float maxRadius,FriendAt at,IsSelf self) noexcept {
    return !(shot.blast>0.0f && g.AnyAround(shot.to,shot.BlastMargin(maxRadius),[&](int i){
        return !self(i) && shot.BlocksBlast(at(i));
    }));
}

template<class FriendAt,class IsSelf>
bool ShotClear(const Grid& g,const Shot& shot,float maxRadius,FriendAt at,IsSelf self) noexcept {
    if(g.AnyInCapsule(shot.from,shot.to,maxRadius+shot.spread,[&](int i){ return !self(i) && shot.BlocksLine(at(i)); }))return false;
    return BlastClear(g,shot,maxRadius,at,self);
}

// The ground track of successive ballistic chords is straight. Query this short run once, then use its exact 3D tests.
template<class FriendAt,class IsSelf>
bool ArcChordsClear(const Grid& g,const Shot* chords,int count,float maxRadius,FriendAt at,IsSelf self) noexcept {
    return !g.AnyInCapsule(chords[0].from,chords[count-1].to,maxRadius+chords[0].spread,[&](int i) {
        if(self(i))return false;
        const auto& friendly=at(i);
        for(int j=0;j<count;++j)if(chords[j].BlocksLine(friendly))return true;
        return false;
    });
}

// The point nearest `pos` over the ground (Horiz), -1 with none (the scan: d<nd from 1e30, the first of a tie).
template<class AimOf> int NearestHoriz(const Grid& g,int n,AimOf aimOf,const float* pos) noexcept {
    const auto scan=[&]() noexcept {
        int best=-1;float nd=1e30f;
        for(int i=0;i<n;++i){const float d=Horiz(pos,aimOf(i));if(d<nd){nd=d;best=i;}}
        return best;
    };
    if(g.Points()!=n || g.Odd() || !Grid::Regular(pos[0],pos[2]))return scan();
    int best=-1;float nd=1e30f;
    g.NearFirst(pos[0],pos[2],g.All(),[&](float lb) noexcept { return best>=0 && lb*(1.0f-1e-5f)>nd; },[&](int i) {
        const float d=Horiz(pos,aimOf(i));
        if(d<nd || (d==nd && best>=0 && i<best)){nd=d;best=i;}
    });
    return best;
}

}  // namespace npc::grid
