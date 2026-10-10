// The map's real play area: where its ground is (the user, 2026-10-06: "flying the jet I went out past the edge, hung in
// the void a good while, then fell from above").
//
// Why it is needed (docs/map-edge-re.md, docs/player-jet-re.md §3): the stock keeps every heli-type body inside the map's
// own move_limit box (MoveAreaManager, clamped by slot 55 0x6543A0 -> 0x5A9E50), but the plugin turns that clamp off for
// the aircraft it flies (veh+0xE00 = -1e6: the jets were pinned at the box's edge, ~1 km out on the test range) and kept
// them in with walls of its own at crew.h PlayEdge: the physics world's square (+-2400 stock, ini BigWorld less 750),
// which has nothing to do with where the map's ground ends (+-1500..2000 on a stock map; the user's own logs: BigWorld
// 6000 on a stock map, ground only within ~1750 m, walls at 5250). Past the ground's edge a map ray finds nothing
// (kNoGround), so the flight has no floor, no ground warning and no crash there: the jet flew on over the void, and once
// it sank under the box's floor (-1000 m) and the stock clamp had it again (the player out of the seat, veh+0xE00 put
// back) 0x5A9E50 put it at the box's top (`pos.y < lo.y -> pos.y = hi.y`, raised to 3377 m by bigworld.cpp RaiseSky): it
// fell from above.
//
// So the walls stand where the ground is: once a mission the map's ground is measured with map rays along each axis
// from the move area's centre (playarea.cpp), and the walls stand kVoidMargin inside its edge (never past the physics
// square). The pure part (the lane search, the walls, their turn) is here so tools/play_area_check.cpp runs it offline.
// The NPC aircraft are another branch's (fix/npc-air-soft-boundary); they can take MapPlayArea() / area::EdgeTurn as they are.
#pragma once
#include <cmath>

namespace crew {
// The walls in x (index 0) and z (index 1): a flyer keeps within lo..hi. `ground`: measured off the map's ground
// (false: the physics-world square of crew.h PlayEdge, until the measure is in or where it found no ground).
// `floor`: the lowest ground the measure found (hasFloor): within the walls a void under the aircraft (the big map's
// seams between its blocks) is floored there (area::FloorClear), so nothing sinks into it either.
struct PlayArea {
    float lo[2],hi[2];
    bool ground;
    float floor;
    bool hasFloor;
};
// The current mission's (playarea.cpp), game thread.
PlayArea MapPlayArea() noexcept;
bool PlayAreaMeasured() noexcept;   // this mission's measurement is done (MapPlayArea is no longer the square)
void PlayAreaTick() noexcept;    // crew.cpp FrameTick: once a mission, kMeasureAfterMs into it, one side a frame
void ResetPlayArea() noexcept;   // mission.cpp: a new mission measures again

namespace area {
// The lanes (playarea.cpp): kLanes rays lines a side, kLaneGap m apart across the axis; along one a ray straight down
// every kStep m from the centre out, the ground's edge the last that found ground once kGapMost m on find none (the
// big map's seams between its blocks, ~500 m of void across it (tools/make_bigmap.py), are crossed), refined to kFine.
constexpr int kLanes=5;
constexpr float kLaneGap=300.0f,kStep=100.0f,kGapMost=1200.0f,kFine=5.0f;
// m: the walls stand this far inside the ground's edge (a frame's overshoot at 340 m/s is 6 m; the body is ~16-77 m).
constexpr float kVoidMargin=150.0f;
// m: under this between two opposite edges the measure is taken for wrong (a lane under a hole): that axis keeps the square.
constexpr float kLeastWidth=600.0f;

inline float Clamp(float x,float lo,float hi) noexcept { return x<lo ? lo : x>hi ? hi : x; }

// The last ground along one lane: `ground(s)` true where the map has ground s m out from the centre along it; out to
// `limit` m. -1: none within kGapMost of the centre.
template<class Ground> float LaneEdge(Ground ground,float limit) noexcept {
    float last=-1.0f;
    for(float s=0.0f;s<=limit;s+=kStep) {
        if(ground(s))last=s;
        else if(s-(last<0.0f ? 0.0f : last)>=kGapMost)break;
    }
    if(last<0.0f)return -1.0f;
    float in=last,out=last+kStep;
    if(out>limit)return last;
    while(out-in>kFine) {
        const float mid=0.5f*(in+out);
        if(ground(mid))in=mid;else out=mid;
    }
    return in;
}

// The median of the lanes' edges that found ground (e: kLanes of them, -1 none); -1 when fewer than half did.
inline float MedianEdge(const float* e) noexcept {
    float v[kLanes];
    int n=0;
    for(int i=0;i<kLanes;++i)if(e[i]>=0.0f)v[n++]=e[i];
    if(n*2<=kLanes)return -1.0f;
    for(int i=1;i<n;++i)for(int k=i;k>0 && v[k-1]>v[k];--k){const float t=v[k];v[k]=v[k-1];v[k-1]=t;}
    return n%2 ? v[n/2] : 0.5f*(v[n/2-1]+v[n/2]);
}

// The walls from the centre `c` (x, z) and the ground's edge each side, m out from it (`edge`: +x, -x, +z, -z; -1
// unknown): kVoidMargin inside it, never past the physics square +-`square`. An axis with an unknown side, or narrower
// than kLeastWidth, keeps the square's walls there; `ground` says whether any wall stands on the ground's edge.
inline PlayArea Combine(const float* c,const float* edge,float square) noexcept {
    PlayArea a{{-square,-square},{square,square},false,0.0f,false};
    for(int k=0;k<2;++k) {
        const float up=edge[2*k],down=edge[2*k+1];
        if(up<0.0f || down<0.0f)continue;
        const float hi=std::fmin(c[k]+up-kVoidMargin,square),lo=std::fmax(c[k]-down+kVoidMargin,-square);
        if(hi-lo<kLeastWidth)continue;
        a.lo[k]=lo;a.hi[k]=hi;a.ground=true;
    }
    return a;
}

// The way a path along unit `dir` (at `pos`) is let out toward each wall (playerjet.cpp WallTurn, from the square to
// these walls): from `buffer` m inside a wall the share of the path pointing out at it falls from all of it to none at
// the wall, and past it the path must point back in by `wallIn`; what is taken off is turned along the wall, the
// path's own sense along it kept (straight at it: to its right), its speed kept by the caller (dir stays unit). The
// heading is bent round smoothly, never flipped. True: bent.
inline bool EdgeTurn(const PlayArea& a,const float* pos,float* dir,float buffer,float wallIn) noexcept {
    bool bent=false;
    for(int i=0;i<3;i+=2) {
        const int k=i/2,o=2-i;   // o: the other horizontal axis, along the wall
        for(int side=0;side<2;++side) {
            const float out=side==0 ? 1.0f : -1.0f;
            const float room=side==0 ? a.hi[k]-pos[i] : pos[i]-a.lo[k];
            const float most=Clamp(room/buffer,-wallIn,1.0f),away=dir[i]*out;
            if(away<=most)continue;
            const float flat=std::sqrt(dir[0]*dir[0]+dir[2]*dir[2]);
            if(flat<1e-4f)continue;
            const float target=most*flat,along=std::sqrt(std::fmax(flat*flat-target*target,0.0f));
            float sense=dir[o];
            if(std::fabs(sense)<1e-3f)sense=i==0 ? dir[0] : -dir[2];   // straight at it: its right ((-dir.z, 0, dir.x))
            dir[i]=out*target;
            dir[o]=(sense>=0.0f ? 1.0f : -1.0f)*along;
            bent=true;
        }
    }
    const float l=std::sqrt(dir[0]*dir[0]+dir[1]*dir[1]+dir[2]*dir[2]);
    if(l>1e-6f){dir[0]/=l;dir[1]/=l;dir[2]/=l;}
    else{dir[0]=0.0f;dir[1]=0.0f;dir[2]=1.0f;}
    return bent;
}

// Metres over what is under `pos` (`clear`: the map ray's, `none` its value for no ground): within the walls a void is
// floored at the area's floor (see PlayArea); past them, or with no floor measured, `clear` as it is.
inline float FloorClear(const PlayArea& a,const float* pos,float clear,float none) noexcept {
    if(clear!=none || !a.hasFloor)return clear;
    if(pos[0]<a.lo[0] || pos[0]>a.hi[0] || pos[2]<a.lo[1] || pos[2]>a.hi[1])return clear;
    return pos[1]-a.floor;
}

// The cockpit's AREA warning (hud.cpp, warn.cpp): 2 past a wall, 1 within `warn` m of one and heading out at it (more
// than kHeadingOut of the path), 0 neither.
constexpr float kHeadingOut=0.25f;
inline int EdgeState(const PlayArea& a,const float* pos,const float* dir,float warn) noexcept {
    int state=0;
    for(int k=0;k<2;++k) {
        const int i=2*k;
        const float rooms[2]={a.hi[k]-pos[i],pos[i]-a.lo[k]},outs[2]={dir[i],-dir[i]};
        for(int side=0;side<2;++side) {
            if(rooms[side]<0.0f)return 2;
            if(rooms[side]<warn && outs[side]>kHeadingOut)state=1;
        }
    }
    return state;
}
}  // namespace area
}  // namespace crew
