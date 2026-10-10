#pragma once
// The mission start's phases watched (mission.cpp): which one the game thread is in, logged as it starts, and a
// watchdog thread that reports one that does not finish (2026-10-10: an online joiner's game stopped inside the
// plugin's mission start with no crash line and no dump; the log only showed the last phase that had logged).
namespace crew {
// The game thread enters `phase` (a string literal); nullptr: the watched work is over. Logs the phase with the time
// since the previous one. A phase still running after kMissionPhaseStuckMs is logged once with the memory state, and
// the first stuck phase of the process writes Mods\Plugins\EDF6VehicleCrew.hang.dmp (every thread's stack).
void WatchMissionPhase(const char* phase) noexcept;
constexpr unsigned long long kMissionPhaseStuckMs=15000;
}
