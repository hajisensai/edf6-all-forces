// damagestats.cpp: the damage statistics (README 伤害统计, src/damage_stats.h). Every hit the game takes off an object's HP
// passes the one damage call (0x54A586 -> 0x547C30, docs/subcarrier-re.md §8.1); the hook there reads the HP before and
// after, sorts the hit out (who dealt it with what, what it hit) and books it for the mission. The page is drawn over the
// map (hud_stats.inc) and worked with the map's pointer: its STATS tab, or the stats key (ini DamageStatsKey) opens the
// map on it. Included by crew.h.
#pragma once
#include "damage_stats.h"

namespace crew {
bool InstallDamageStats() noexcept;   // at load, after InstallSub (it checks the damage call's stock bytes first)
void ResetDamageStats() noexcept;     // mission.cpp MissionStart: a new book
// The draw thread's: the book (copied into `book` only when it changed since the last copy, `fresh` then true) and the
// page's state, the mission's time so far; false when the statistics are off.
bool DamageStatsRead(dmgstat::Book* book,bool* fresh,dmgstat::View* view,std::uint32_t* missionMs) noexcept;
// hud_stats.inc: the page's click targets as drawn this frame (four floats a rectangle, a dmgstat::UiCode each), the bar
// tab's rows and how many show (for the scroll's bounds). n=0: the page not drawn.
void DamageStatsUi(const float* rects,const int* codes,int n,int rows,int visible) noexcept;
// mapcmd.cpp, the game thread: the page's target under (x, y) (0: none), a click on one, the page shown.
int DamageStatsUiAt(float x,float y) noexcept;
void DamageStatsClick(int code) noexcept;
bool DamageStatsShown() noexcept;
// map.cpp: the wheel's notches while the page is shown (they scroll its bars, not the map's zoom); the page shown or not
// (the stats key, the map closing).
void DamageStatsWheel(int notches) noexcept;
void DamageStatsShow(bool shown) noexcept;
}  // namespace crew
