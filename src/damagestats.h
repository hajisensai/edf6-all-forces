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
// map.cpp, a pad on the page (it has no pointer): LB / RB a tab before / after (`tabStep`), Y the other scope.
void DamageStatsPad(int tabStep,bool flipScope) noexcept;
void DamageStatsShow(bool shown) noexcept;

// wastedfire.cpp (wasted_fire.h): fire that cannot hurt its target. The damage hook reports every hit it settles with an
// attacker (WastedFireHit: whether it took HP off); the AI asks WastedFire before it picks a target or fires. True when
// the game is not letting the target be hurt at all (a teleportation ship with its hatch's damage window shut), or when
// this shooter's last hits on it all hurt nothing (wasted_fire.h). Game thread.
void WastedFireHit(const void* shooter,const void* target,bool hurt) noexcept;
bool WastedFire(const void* shooter,const void* target) noexcept;
// The trigger's question (hit_predict.h): true unless a round fired now from `from` (null: the shooter's eye) at `speed`
// m/s dropping `gravity` m/s^2 (0: a straight line) would be taken by the target when it gets there: shut now (also while
// its window is due to open: the target kept, the trigger held), its predicted hit refused, or wasted as above. `aim`
// (optional) gets the point to aim at for the hit it would take (its weak spot's lock point, led).
bool HoldFire(const void* shooter,const void* target,const float* from=nullptr,float speed=0.0f,float gravity=0.0f,
              float* aim=nullptr) noexcept;
void ResetWastedFire() noexcept;      // mission.cpp MissionStart: the last mission's objects forgotten
}  // namespace crew
