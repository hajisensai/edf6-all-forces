// The broadphase collectors' group expansion (hit_query::NpTargetCollector::addBody 0x109DA0, docs/group-expand-re.md):
// a body that is part of a group (body property key 1: an object's list of bodies) puts every active body of the group
// in the collector's list. Stock walks the group's linked list again for each of its bodies the query overlaps; a
// bullet's sweep through a dense swarm overlaps the same enemy's bodies many times (the bullet step's pre_update,
// O(bullets x overlapped bodies x group size)).
//  - the appends (every collector without a filter functor): a walk's ids gathered and put in the collector's list
//    in one copy when the list has the room (no append would grow it), else each through the append 0x11A9EC0 (its
//    growth as stock's); the same ids in the same order;
//  - inside one bullet's cast (0x11A70B0 from the bullet step's three move types, 0x234334 / 0x234982 / 0x234B98), with
//    no filter functor: a group walked once, its ids appended again from the walk's copy when another of its bodies is
//    overlapped. Nothing runs between the overlaps but the collectors' own checks (the broadphase calls them back
//    one after another on this thread), so the group's list is the one the first walk read.
// With debug, 1 copy in 64 also walks the list and compares (GROUPEXPAND MISMATCH, the walk's ids used); PerfProbe:
// the PERF GROUPEXPAND row. ini GroupExpandMemo=0: nothing installed.
#pragma once

namespace crew {
bool InstallGroupExpand() noexcept;
void ReportGroupExpand(unsigned frames) noexcept;
}  // namespace crew
