// The enemy lock points of a side (heli.cpp ForEachEnemyOf, VisitEnemies): the lock registry walk and its filter, and
// the scene-step copy that soldier AI, helicopters and NPC gunner seats share. Pure logic over the registry's memory;
// tools/enemy_points_check.cpp compares the shared copy with independent live walks.
// Both plugins walk the registry through here: EDF6AutoTurret's world snapshot (autoturret/src/plugin.cpp RefreshWorld)
// is Collect with no side (every team's points, each with its team). The registry's head and count, and a side's
// relation row, are read by edf/world_tables.h.
//
// Until 2026-11-07 every AI gunner seat of every vehicle walked the whole registry each frame (stress:100: 3.7 ms a
// frame in the walk alone, one cache miss per node and per target). Each reader selects its hostile teams and omits
// itself from one captured registry, without touching the native linked list again in the same scene step.
#pragma once
#include "edf/layout.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace enemypoints {
// Registry node: next at +0, its target at +0x10. Target: kind byte +0 (0: an object's), object +0x8, aim point +0x10,
// valid +0x29, lockable +0x2A (autoturret/docs/re-notes.md). Team relations: 2 = enemy (edf/world_tables.h).
constexpr std::size_t kNodeTarget=0x10,kTargetObject=0x8,kTargetAim=0x10,kTargetValid=0x29,kTargetLockable=0x2A;
constexpr std::int32_t kEnemyRelation=2,kMaxTeam=64;

struct Point { const void* object; std::int32_t team; float aim[3]; };

// f(object, team, aim) for each valid, lockable lock point on the circular list at `head` whose object is alive, is not
// `self`, and whose team is in the table and, with a `relation` (the side's row of the team table), an enemy of the
// side (null: any team), its aim finite; in list order. At most `nodes` nodes: the registry's own count (its +0x10), so
// a list that does not close on its head still ends. Returns the nodes walked.
template<class F> inline std::uint64_t WalkTeams(const unsigned char* head,const std::int32_t* relation,const void* self,F&& f,
                                                 std::uint64_t nodes=~0ull) noexcept {
    using edf::At;
    std::uint64_t n=0;
    for(auto node=At<const unsigned char*>(head,0);node!=head && n<nodes;node=At<const unsigned char*>(node,0),++n) {
        const auto target=At<const unsigned char*>(node,kNodeTarget);
        if(!target || target[0]!=0 || !target[kTargetValid] || !target[kTargetLockable])continue;
        const auto object=At<const unsigned char*>(target,kTargetObject);
        if(!object || object==self || object[edf::kDead])continue;
        const auto other=At<std::int32_t>(object,edf::kTeam);
        if(other<0 || other>=kMaxTeam || (relation && relation[other]!=kEnemyRelation))continue;
        const float* a=reinterpret_cast<const float*>(target+kTargetAim);
        if(!std::isfinite(a[0]) || !std::isfinite(a[1]) || !std::isfinite(a[2]))continue;
        f(static_cast<const void*>(object),other,a);
    }
    return n;
}
// The walk of a side (`relation` its row): f(object, aim).
template<class F> inline std::uint64_t Walk(const unsigned char* head,const std::int32_t* relation,const void* self,F&& f,
                                            std::uint64_t nodes=~0ull) noexcept {
    return WalkTeams(head,relation,self,[&](const void* object,std::int32_t,const float* aim) noexcept { f(object,aim); },nodes);
}

// The walk with no `self`, copied: what every seat of the side starts from this frame (with no `relation`: every
// team's points). Returns the nodes walked.
inline std::uint64_t Collect(const unsigned char* head,const std::int32_t* relation,std::vector<Point>& out,
                             std::uint64_t nodes=~0ull) {
    out.clear();
    return WalkTeams(head,relation,nullptr,[&](const void* object,std::int32_t team,const float* aim) {
        Point p{object,team,{}};std::memcpy(p.aim,aim,12);out.push_back(p);
    },nodes);
}

// Each side keeps a mask over the same ordered registry capture rather than copying the points.
using TeamMask=std::uint64_t;
constexpr TeamMask kAllTeams=~TeamMask{0};
static_assert(kMaxTeam==64);
constexpr TeamMask TeamBit(int team) noexcept { return TeamMask{1}<<static_cast<unsigned>(team); }
inline TeamMask EnemyTeams(const std::int32_t* relation) noexcept {
    TeamMask mask=0;
    for(int team=0;team<kMaxTeam;++team)
        if(relation[team]==kEnemyRelation)mask|=TeamBit(team);
    return mask;
}

inline bool StillOnSide(const Point& p,const void* self) noexcept {
    const auto* object=static_cast<const unsigned char*>(p.object);
    return object!=self && !object[edf::kDead] && edf::At<std::int32_t>(object,edf::kTeam)==p.team;
}

// Visit the sampled side in registry order, omitting self and objects that died or switched team afterward.
template<class F> inline void Visit(const std::vector<Point>& points,const void* self,F&& f,TeamMask enemies=kAllTeams) noexcept {
    for(const Point& p:points) {
        if(!(enemies&TeamBit(p.team)) || !StillOnSide(p,self))continue;
        f(p.object,p.aim);
    }
}

// The pick a nearest-within-reach visitor makes over Visit (npcai.cpp's gunner seats: d <= reach, d < the best so far,
// then usable): the same object and distance, `usable` asked of the same points in the same order. Only a would-be best
// is tested for self and death, so the copy is read for its aim points alone, not each object's memory.
struct Pick { const void* object; float dist; };
template<class Dist,class Usable>
inline Pick Nearest(const std::vector<Point>& points,const void* self,const float* from,float reach,Dist&& dist,
                    Usable&& usable,TeamMask enemies=kAllTeams) noexcept {
    Pick best{nullptr,1e30f};
    for(const Point& p:points) {
        if(!(enemies&TeamBit(p.team)))continue;
        const float d=dist(from,p.aim);
        if(!(d<=reach && d<best.dist))continue;
        if(!StillOnSide(p,self) || !usable(p.object))continue;
        best={p.object,d};
    }
    return best;
}
}  // namespace enemypoints
