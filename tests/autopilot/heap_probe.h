// The heap probe of the test-only autopilot (tests/autopilot/heap_probe.cpp), for mem-3 (the process grows on every
// mission cycle, docs/memloop-re.md): what is allocated after a starting point and still alive at a later one, by the
// call path that allocated it, the way UMDH diffs two snapshots, without UMDH's gflags +ust (that needs an
// administrator's registry write). The heap calls (HeapAlloc / HeapReAlloc / HeapFree, as the CRTs import them) and
// VirtualAlloc / VirtualFree are swapped in the import tables of every module loaded when it is installed.
#pragma once
#include <Windows.h>

namespace autopilot {
using LogFn=void(*)(const char* format,...) noexcept;

// Swaps the import entries; counting starts at the `startMark`-th HeapProbeMark (1: the first).
bool InstallHeapProbe(LogFn log,int startMark) noexcept;
// A cycle's way out (the autopilot's "cycle left"): the start, or, after it, the allocations made since the start and
// still alive: their total, by module, and the biggest call paths (HEAPLIVE / HEAPMOD / HEAPSITE rows).
void HeapProbeMark(const char* why) noexcept;
}  // namespace autopilot
