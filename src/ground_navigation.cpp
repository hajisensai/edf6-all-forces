#include "scene.h"
#include "ground_navigation.h"
#include "ground_body_rays.h"
#include "heli.h"
namespace crew {
namespace {
// Shared budget caps collision work across the entire NPC population and the support routes alike (one budget for
// both state sizes: never a static per instantiation). Each caller also yields after four edges, so one long route
// cannot consume it all.
std::uint64_t frame=~std::uint64_t{0};int queries=0;
const void* start=nullptr;const void* nextStart=nullptr;bool waiting=false;
GroundProbeStats probeStats{};

struct GroundBox {
    float lower[3]{},upper[3]{};

    bool Covers(const GroundBox& other) const noexcept {
        for(int axis=0;axis<3;++axis)
            if(!(lower[axis]<=other.lower[axis] && upper[axis]>=other.upper[axis]))return false;
        return true;
    }
};

// Bounds and the native body pointer only live during this synchronous, read-only navigation call.
class GroundBodyView {
public:
    const GroundBodyRays* For(const GroundBox& requested,float cell,float step) noexcept {
        if(ready && bounds.Covers(requested)) {
            ++probeStats.bodyListReused;
            return &body;
        }
        // Wait for a second uncovered edge before trying a larger box. If it contains more than
        // one body, the original short box is still available for this edge.
        if(ready && !expanded) {
            expanded=true;
            GroundBox wider;
            for(int axis=0;axis<3;++axis) {
                const float pad=(std::max)(0.0f,axis==1 ? step : cell);
                wider.lower[axis]=(std::min)(bounds.lower[axis],requested.lower[axis])-pad;
                wider.upper[axis]=(std::max)(bounds.upper[axis],requested.upper[axis])+pad;
            }
            if(Capture(wider))return &body;
        }
        return Capture(requested) ? &body : nullptr;
    }

private:
    bool Capture(const GroundBox& box) noexcept {
        GroundBodyRays found;
        ++probeStats.bodyListQueries;
        if(!found.Capture(box.lower,box.upper))return false;
        ++probeStats.bodyLists;
        ready=true;
        body=found;
        bounds=box;
        return true;
    }

    GroundBodyRays body;
    GroundBox bounds;
    bool ready=false,expanded=false;
};

// The frame's turn: whether `state`'s call waits for the first caller the last frame denied (the call then returns
// pending at once, its waypoint its `from`). The frame's budget is begun here, by whichever asks first: the call or
// GroundNavigateDeferred, so the order of the turn is the same whether a caller asked before calling or not.
bool Deferred(const void* state) noexcept {
    const auto now=GameFrame();
    if(frame!=now) {
        // Start the next frame at the first caller denied by the shared budget,
        // instead of starving the tail of the NPC update list. A vanished caller
        // can defer others for at most one frame; pointers are compared, never read.
        frame=now;queries=0;start=waiting ? nullptr : nextStart;nextStart=nullptr;waiting=start!=nullptr;
    }
    return waiting && start!=state;
}
template<class State>
npc::navigation::Result Navigate(State& state,const float* from,const float* to,float stop,
    std::uint64_t ms,float* waypoint,npc::navigation::Profile profile,Hull hull,GroundRayBatch* batch=nullptr) noexcept {
    using namespace npc::navigation;
    if(Deferred(&state)) {
        waypoint[0]=from[0];waypoint[1]=from[1];waypoint[2]=from[2];return Result::pending;
    }
    waiting=false;
    if(!hull.clear)hull.object=nullptr;
    // A route searched round another body (or round none) is not this one's: search again (Begin).
    if(state.hull!=hull.object){state.hull=hull.object;state.initialized=false;}
    GroundRayBatch local;
    auto& rays=batch ? *batch : local;
    GroundBodyView view;
    bool sampled=false;
    auto edge=[&](Point a,Point b,Point& out) noexcept {
        const float distance=Horizontal(a,b);
        if(distance>profile.cell*3.0f)return Edge::blocked;
        const int steps=(std::max)(1,static_cast<int>(std::ceil(distance/0.5f)));
        // 16 floor/clearance queries plus four water queries; with the vehicles' bodies (Hull), its four centre-line
        // sweeps (WalkEdge), each in up to two pieces (OutsideCircle) of up to kHullHits rays. The worst is what the edge
        // must find room for; what it did not cast (an edge blocked early, a sweep whose first ray met nothing) is given
        // back after it: every soldier's route sweeps the vehicles, and charging each the worst would cut the edges the
        // population searches a frame to some 2/5 for rays that are never cast.
        const int worst=steps*(20+(hull.clear ? 8*kHullHits : 0));
        if(queries+worst>4096){if(!nextStart)nextStart=&state;return Edge::pending;}
        int cast=0;
        bool waterUnknown=false;
        const GroundBodyRays* edgeBody=nullptr;
        bool gathered=steps<4;
        GroundBox edgeBox;
        auto& lower=edgeBox.lower;
        auto& upper=edgeBox.upper;
        auto ground=[&](Point at,float step,float& y) noexcept {
            cast+=2;
            if(!sampled){sampled=true;++probeStats.batches;}
            ++probeStats.floor;
            const float top[3]={at.x,at.y+step,at.z},bottom[3]={at.x,at.y-step,at.z};float hit[3];
            const bool use=edgeBody && at.x>=lower[0] && at.x<=upper[0] && at.z>=lower[2] && at.z<=upper[2] &&
                bottom[1]>=lower[1] && bottom[1]<=upper[1] && top[1]>=lower[1] && top[1]<=upper[1];
            if(use)++probeStats.bodyFloor;
            if(rays.Floor(top,bottom,hit,use ? edgeBody : nullptr)<0.0f || !std::isfinite(hit[1]))return false;
            float surface=0.0f;
            ++probeStats.water;
            const Sea sea=rays.Water(at.x,at.z,&surface);
            if(rays.Dry())++probeStats.dryWater;
            // Unknown water defers the edge instead of treating it as dry ground.
            if(sea==Sea::unknown || (sea==Sea::water && !std::isfinite(surface))) {
                waterUnknown=true;return false;
            }
            if(sea==Sea::water && surface-hit[1]>profile.maxWaterDepth)return false;
            y=hit[1];return true;
        };
        auto clear=[&](Point a0,Point b0,const GroundBodyRays* candidate=nullptr) noexcept {
            ++cast;
            ++probeStats.clearance;
            const float a1[3]={a0.x,a0.y,a0.z},b1[3]={b0.x,b0.y,b0.z};
            if(candidate) {
                ++probeStats.bodyRays;
                if(candidate->Unobstructed(a1,b1)) {
                    ++probeStats.bodyClear;
                    return true;
                }
            }
            return rays.Clear(a1,b1);
        };
        auto body=[&](Point a0,Point b0) noexcept {
            if(!hull.clear)return true;
            return OutsideCircle(a0,b0,{hull.inside[0],hull.inside[1],hull.inside[2]},hull.room,[&](Point p,Point q) noexcept {
                if(Horizontal(p,q)+std::fabs(p.y-q.y)<1e-3f)return true;
                const float a1[3]={p.x,p.y,p.z},b1[3]={q.x,q.y,q.z};
                cast+=kHullHits;   // its rays are the query's own (at most kHullHits)
                return hull.clear(hull.object,a1,b1);
            });
        };
        // The shared budget still charges the same logical probes in the same order.
        const float ankle=profile.step+0.08f,waist=profile.height*0.65f;
        const float lowHeight=(std::min)(ankle,(std::min)(waist,profile.height));
        const float highHeight=(std::max)(ankle,(std::max)(waist,profile.height));
        auto sweep=[&](Point prev,Point at,float rx,float rz) noexcept {
            const float sideX=std::fabs(rx),sideZ=std::fabs(rz);
            const float minY=(std::min)(prev.y,at.y)+lowHeight,maxY=(std::max)(prev.y,at.y)+highHeight;
            if(!gathered) {
                gathered=true;
                // Bound the whole short edge in X/Z. Subsequent samples may leave its height band.
                lower[0]=(std::min)(a.x,b.x)-sideX-0.005f;
                upper[0]=(std::max)(a.x,b.x)+sideX+0.005f;
                lower[2]=(std::min)(a.z,b.z)-sideZ-0.005f;
                upper[2]=(std::max)(a.z,b.z)+sideZ+0.005f;
                lower[1]=(std::min)(minY,(std::min)(prev.y,at.y)-profile.step)-0.125f;
                upper[1]=(std::max)(maxY,(std::max)(prev.y,at.y)+profile.step)+0.125f;
                edgeBody=view.For(edgeBox,profile.cell,profile.step);
            }
            const bool use=edgeBody && minY>=lower[1] && maxY<=upper[1] &&
                (std::min)(prev.x,at.x)-sideX>=lower[0] && (std::max)(prev.x,at.x)+sideX<=upper[0] &&
                (std::min)(prev.z,at.z)-sideZ>=lower[2] && (std::max)(prev.z,at.z)+sideZ<=upper[2];
            return [&,use](Point a0,Point b0) noexcept {return clear(a0,b0,use ? edgeBody : nullptr);};
        };
        const bool walkable=WalkEdge(a,b,out,profile,ground,clear,body,sweep);
        queries+=(std::min)(cast,worst);
        return walkable ? Edge::open : waterUnknown ? Edge::pending : Edge::blocked;
    };
    Point next{};
    const Result result=npc::navigation::Navigate(state,{from[0],from[1],from[2]},{to[0],to[1],to[2]},stop,ms,next,edge,profile);
    waypoint[0]=next.x;waypoint[1]=next.y;waypoint[2]=next.z;return result;
}
}  // namespace
GroundProbeStats TakeGroundProbeStats() noexcept {
    const auto result=probeStats;
    probeStats={};
    return result;
}
bool GroundNavigateDeferred(const npc::navigation::State& state) noexcept { return Deferred(&state); }
npc::navigation::Result GroundNavigate(npc::navigation::State& state,const float* from,const float* to,float stop,
    std::uint64_t ms,float* waypoint,npc::navigation::Profile profile,Hull hull,GroundRayBatch* batch) noexcept {
    return Navigate(state,from,to,stop,ms,waypoint,profile,hull,batch);
}
npc::navigation::Result GroundCoast(npc::navigation::State& state,const float* from,const float* to,float stop,
    std::uint64_t ms,float* waypoint,Hull hull) noexcept {
    npc::navigation::Point next{};
    const auto result=npc::navigation::Coast(state,{from[0],from[1],from[2]},{to[0],to[1],to[2]},stop,
        hull.clear ? hull.object : nullptr,ms,next);
    waypoint[0]=next.x;waypoint[1]=next.y;waypoint[2]=next.z;return result;
}
npc::navigation::Result GroundNavigate(npc::navigation::RouteState& state,const float* from,const float* to,float stop,
    std::uint64_t ms,float* waypoint,npc::navigation::Profile profile) noexcept {
    return Navigate(state,from,to,stop,ms,waypoint,profile,Hull{});
}
}
