// The bullets' broadphase group expansion cut to the parts the sweep can reach (docs/bullet-expand-cull-re.md).
// Stock: a bullet's sweep (xgs World::CastShape 0x11A70B0 from the bullet step's three move types) hands each body
// the broadphase overlaps to the collector's addBody 0x109DA0; a body of a group (an enemy's parts) puts every active
// part of the group in the collector's list (core+0x818), whether the sweep goes near the part or not. That list is
// filtered into the ray's (0x8C8) and the step-3 sphere's (0x8D8) lists, each part fetched, a command built and the
// narrowphase run against it: a bullet through a swarm pays for thousands of parts it cannot touch.
// The cave at the walk (0x109F6F) appends, inside one of the three bullet casts and with no filter functor, only the
// parts whose world box (the shape's calcAabb in the body's transform, once a body a step: the batch step 0x237CF0's
// call bumps the cache's generation) touches the segment swept by max |extent| + kMargin: the ray and the sphere lie
// in it, so a part out of it gives no hit; the list keeps the others in stock's order. A group that would put parts
// in an empty list keeps its first active part (the list's emptiness decides the step's path). Left as stock: a blast
// bullet (core+0xAF4 bit 0x1000: its point query reads the 0x8D8 list), walks with a filter functor or outside the
// bullet casts, ExpandCull=0 (read every cast).
// With debug, 1 bullet cast in 256 is left whole and its step-3 build (0x2382B6) runs the narrowphase on the whole
// list and on the cut one and compares the hit fields the game reads (EXPANDCULL MISMATCH; the whole list used);
// PerfProbe: the PERF EXPANDCULL row.
#pragma once

namespace crew {
bool InstallBulletExpandCull() noexcept;
void ReportBulletExpandCull(unsigned frames) noexcept;
}  // namespace crew
