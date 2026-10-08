#include "crew.h"
#include "ground_navigation.h"
#include "heli.h"
namespace crew {
npc::navigation::Result GroundNavigate(npc::navigation::State& state,const float* from,const float* to,float stop,
    std::uint64_t ms,float* waypoint,npc::navigation::Profile profile) noexcept {
    using namespace npc::navigation;
    // Shared budget caps collision work across the entire NPC population. Each
    // caller also yields after four edges, so one long route cannot consume it all.
    static std::uint64_t frame=~std::uint64_t{0};static int queries=0;
    static const State* start=nullptr;static const State* nextStart=nullptr;static bool waiting=false;
    const auto now=GameFrame();
    if(frame!=now) {
        // Start the next frame at the first caller denied by the shared budget,
        // instead of starving the tail of the NPC update list. A vanished caller
        // can defer others for at most one frame; pointers are compared, never read.
        frame=now;queries=0;start=waiting ? nullptr : nextStart;nextStart=nullptr;waiting=start!=nullptr;
    }
    if(waiting && start!=&state) {
        waypoint[0]=from[0];waypoint[1]=from[1];waypoint[2]=from[2];return Result::pending;
    }
    waiting=false;
    auto edge=[&](Point a,Point b,Point& out) noexcept {
        const float distance=Horizontal(a,b);
        if(distance>profile.cell*3.0f)return Edge::blocked;
        const int steps=(std::max)(1,static_cast<int>(std::ceil(distance/0.5f)));
        const int worst=steps*20; // 16 floor/clearance queries plus four water queries
        if(queries+worst>4096){if(!nextStart)nextStart=&state;return Edge::pending;}
        queries+=worst;
        bool waterUnknown=false;
        auto ground=[&](Point at,float step,float& y) noexcept {
            const float top[3]={at.x,at.y+step,at.z},bottom[3]={at.x,at.y-step,at.z};float hit[3];
            if(MapFloorRay(top,bottom,hit)<0.0f || !std::isfinite(hit[1]))return false;
            float surface=0.0f;
            const Sea sea=SeaAt(at.x,at.z,&surface);
            // A valid empty water-area list yields land, including dry caves.
            // unknown means the native probe is unavailable, not "underground".
            if(sea==Sea::unknown || (sea==Sea::water && !std::isfinite(surface))) {
                waterUnknown=true;return false;
            }
            if(sea==Sea::water && surface-hit[1]>profile.maxWaterDepth)return false;
            y=hit[1];return true;
        };
        auto clear=[](Point a0,Point b0) noexcept {
            const float a1[3]={a0.x,a0.y,a0.z},b1[3]={b0.x,b0.y,b0.z};float hit[3];
            return MapRay(a1,b1,hit)<0.0f;
        };
        const bool walkable=WalkEdge(a,b,out,profile,ground,clear);
        return walkable ? Edge::open : waterUnknown ? Edge::pending : Edge::blocked;
    };
    Point next{};
    const Result result=Navigate(state,{from[0],from[1],from[2]},{to[0],to[1],to[2]},stop,ms,next,edge,profile);
    waypoint[0]=next.x;waypoint[1]=next.y;waypoint[2]=next.z;return result;
}
}
