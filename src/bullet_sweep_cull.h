// The bullet step's object shape cast against only the bodies it can reach (docs/bullet-sweep-cull-re.md).
// Stock: each bullet's step-3 sphere cast (0x231720 builds it, 0x11A8E40 runs it) goes through every body of its
// core+0x8D8 list (the broadphase box's overlaps, each enemy's whole body list, cut to the object layers), each a full
// cast in the body's frame with no box test first: a bullet through a swarm casts against hundreds of parts a step.
// The hook on the build's only call (0x2382B6) first takes out of the list the bodies whose world box, grown by the
// sphere's radius and kMargin, the segment core+0x8A0 -> core+0x8B0 misses: those give no hit, so the collector gets
// the same hits in the same order. Left as stock: a blast bullet (core+0xAF4 bit 0x1000: no cast, the blast's point
// query reads the list), lists of 0 / 1 body, ini SweepCull=0 (read every build). At least one body kept.
// With debug, 1 cut in 256 also runs the narrowphase on the whole list and on the cut one and compares the hits
// (SWEEPCULL MISMATCH: logged, the whole list used); PerfProbe: the PERF SWEEPCULL row (every report, 5 s).
#pragma once

namespace crew {
bool InstallBulletSweepCull() noexcept;
void ReportBulletSweepCull(unsigned frames) noexcept;
}  // namespace crew
