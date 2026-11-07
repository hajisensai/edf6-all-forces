// The enemy lock points of a side (heli.cpp ForEachEnemyOf, VisitEnemies): the lock registry walk and its filter, and
// the frame's copy of its result that the NPC gunner seats share (heli.cpp VisitEnemiesThisStep). Pure logic over the
// registry's memory; tools/enemy_points_check.cpp proves the copy's visits equal the live walk's.
//
// Until 2026-11-07 every AI gunner seat of every vehicle walked the whole registry each frame (stress:100: 3.7 ms a
// frame in the walk alone, one cache miss per node and per target). The walk is the same for every seat of a side but
// for the seat's own vehicle, so it is done once per side per frame and each seat visits the copy minus itself.
#pragma once
#include "edf/layout.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace enemypoints {
// Registry node: next at +0, its target at +0x10. Target: kind byte +0 (0: an object's), object +0x8, aim point +0x10,
// valid +0x29, lockable +0x2A (EDF6AutoTurret turret.h).
constexpr std::size_t kNodeTarget=0x10,kTargetObject=0x8,kTargetAim=0x10,kTargetValid=0x29,kTargetLockable=0x2A;
constexpr std::int32_t kEnemyRelation=2,kMaxTeam=64;
constexpr int kMaxNodes=8192;

struct Point { const void* object; float aim[3]; };

// f(object, aim) for each valid, lockable lock point on the circular list at `head` whose object is alive, is not
// `self`, and whose team `relation` (the side's row of the team table) calls an enemy, its aim finite; in list order.
template<class F> inline void Walk(const unsigned char* head,const std::int32_t* relation,const void* self,F&& f) noexcept {
    using edf::At;
    int n=0;
    for(auto node=At<const unsigned char*>(head,0);node!=head && n<kMaxNodes;node=At<const unsigned char*>(node,0),++n) {
        const auto target=At<const unsigned char*>(node,kNodeTarget);
        if(!target || target[0]!=0 || !target[kTargetValid] || !target[kTargetLockable])continue;
        const auto object=At<const unsigned char*>(target,kTargetObject);
        if(!object || object==self || object[edf::kDead])continue;
        const auto other=At<std::int32_t>(object,edf::kTeam);
        if(other<0 || other>=kMaxTeam || relation[other]!=kEnemyRelation)continue;
        const float* a=reinterpret_cast<const float*>(target+kTargetAim);
        if(!std::isfinite(a[0]) || !std::isfinite(a[1]) || !std::isfinite(a[2]))continue;
        f(static_cast<const void*>(object),a);
    }
}

// The walk with no `self`, copied: what every seat of the side starts from this frame.
inline void Collect(const unsigned char* head,const std::int32_t* relation,std::vector<Point>& out) {
    out.clear();
    Walk(head,relation,nullptr,[&](const void* object,const float* aim) noexcept {
        Point p{object,{}};std::memcpy(p.aim,aim,12);out.push_back(p);
    });
}

// The copy as `self`'s walk would give it: without `self`, and without one that has died since the copy was taken.
template<class F> inline void Visit(const std::vector<Point>& points,const void* self,F&& f) noexcept {
    for(const Point& p:points) {
        if(p.object==self || static_cast<const unsigned char*>(p.object)[edf::kDead])continue;
        f(p.object,p.aim);
    }
}
}  // namespace enemypoints
