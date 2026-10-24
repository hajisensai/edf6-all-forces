// The EDF4.1 port's own kill counters, in the order the battle record shows them (pylib/edf41kills.py COUNTERS: the
// same names; tools/selftest.py edf41_kill_counters_plugin_table checks the two agree). Each is an AchievementVariable the
// installer adds to Mods/ETC/ACHIEVEMENT.SGO and the 4.1 objects' game_object_kill_counter (docs/kill-counter-re.md).
#pragma once

namespace killcount {
inline constexpr const wchar_t* kEdf41Counters[]={
    L"edf41_HectorKillCount",
    L"edf41_4LegTankKillCount",
    L"edf41_DeiroiKillCount",
    L"edf41_AlienTrailerKillCount",
    L"edf41_UfoRoboKillCount",
    L"edf41_MotherShip401KillCount",
    L"edf41_MotherShip301KillCount",
    L"edf41_Carrier2KillCount",
    L"edf41_Carrier1KillCount",
    L"edf41_UfoSmall401KillCount",
    L"edf41_UfoSmall301KillCount",
    L"edf41_UfoSmall301AceKillCount",
    L"edf41_BigDragonKillCount",
    L"edf41_DragonKillCount",
    L"edf41_NestKillCount",
    L"edf41_GiantAntKillCount",
    L"edf41_GiantBeeQueenKillCount",
    L"edf41_NephilaKillCount",
};
}  // namespace killcount
