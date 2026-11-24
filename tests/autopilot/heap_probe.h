// The heap probe of the test-only autopilot (tests/autopilot/heap_probe.cpp), for mem-3 (the process grows on every
// mission cycle, docs/memloop-re.md): what is allocated after a starting point and still alive at a later one, by the
// call path that allocated it, the way UMDH diffs two snapshots, without UMDH's gflags +ust (that needs an
// administrator's registry write). The heap calls (HeapAlloc / HeapReAlloc / HeapFree, as the CRTs import them) and
// VirtualAlloc / VirtualFree are swapped in the import tables of every module loaded at the starting mark.
#pragma once
#include <Windows.h>

namespace autopilot {
using LogFn=void(*)(const char* format,...) noexcept;

// Arms the probe: at the `startMark`-th HeapProbeMark (1: the first) the import entries are swapped and counting
// starts. The module holding the probe is left out (the autopilot's own rows), unless countOwnModule (its test).
bool InstallHeapProbe(LogFn log,int startMark,bool countOwnModule=false) noexcept;
// A cycle's way out (the autopilot's "cycle left"): the start, or, after it, the allocations made since the start and
// still alive: their total, by module, and the biggest call paths (HEAPLIVE / HEAPMOD / HEAPSITE rows).
void HeapProbeMark(const char* why) noexcept;
}  // namespace autopilot
