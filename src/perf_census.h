// The object census (Debug=1 and PerfProbe>=1; tools/perf_report.py reads it): what the scene holds, by kind, sampled
// every kSampleMs on the game thread and reported with PERF OBJECTS every period. Read only: it walks the game's own
// lists under their own locks (TryEnterCriticalSection: a lock another thread holds skips the sample, it never waits)
// and changes nothing.
//  - By team: the team manager's sets (*(EDF+0x20B2978) +0x38, a 0x38-byte row a team, each an MSVC std::set whose
//    node holds the object at +0x20; its lock a CRITICAL_SECTION at +8), the objects counted as the game's
//    GetTeamObjectCount counts them (0x5E13E0: not the dead not yet deleted, +0x2E8, nor those flagged +0x380 & 0x2000;
//    docs/swarm-team-re.md Q3), each by its SGO (the resource key at +0x08) or, without one, its RTTI class.
//  - The scene: every object the System holds (*(EDF+0x20B2958) +0x3D0, the std::list of the scene's objects; its lock
//    the CRITICAL_SECTION at +0x308; docs/step-objects-re.md), by RTTI class: the rounds, effects and props as well
//    as what the teams hold, as far as they are objects of the System's.
// A sample's own time is reported (PERF CENSUS ... ms a sample), so a run can tell it from what it measures.
#pragma once

namespace crew::perfcensus {
// Every step on the game thread (crew.cpp PerfTick): a sample once kSampleMs has passed since the last.
void Tick() noexcept;
// The period's PERF CENSUS lines (debug, PerfProbe>=1), then zeroed: from PerfTick with the PERF OBJECTS line.
void Report() noexcept;
}  // namespace crew::perfcensus
