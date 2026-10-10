#pragma once
// The mission start's phases watched (mission.cpp): which one the game thread is in, logged as it starts, and a
// watchdog thread that reports one that does not finish (2026-10-10: an online joiner's game stopped inside the
// plugin's mission start with no crash line and no dump; the log only showed the last phase that had logged).
namespace crew {
// The game thread enters `phase` (a string literal); nullptr: the watched work is over. Logs the phase with the time
// since the previous one. A phase still running after kMissionPhaseStuckMs is logged once with the memory state; one
// still running after kMissionPhaseDumpMs writes Mods\Plugins\EDF6VehicleCrew.hang.dmp (every thread's stack), the
// first such phase of the process only. The dump is written from inside the game and suspends its other threads while
// it is taken, so it waits for a phase long past any slow machine's (a joiner's was killed after 80 s).
void WatchMissionPhase(const char* phase) noexcept;
constexpr unsigned long long kMissionPhaseStuckMs=15000;
constexpr unsigned long long kMissionPhaseDumpMs=60000;
}
