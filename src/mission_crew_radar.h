// A mission AI vehicle's real crew is not a dot of its own on the stock radar while it sits in that vehicle (测试站 #41,
// the maintainer 2026-11-10: "雷达上的点 改"; docs/feedback-2026-11-10-crew-radar-dot.md, docs/mission-crew-quiet-re.md §2).
// The radar shows the vehicle's own dot, as for a stock AI vehicle with its invisible Dummy driver; dismounted, dead, the
// vehicle destroyed, the deployment rolled back or the plugin off, the soldier's dot is back.
//  - The source (H): HUiHudRader's update 0x82B4D0 fills its friendly row through the one-team walk 0x82B220(hud, list,
//    team): every object of that team's set (team manager [0x20B2978] +0x38, a 0x38-byte row per team whose first qword
//    is a MSVC std::set head; node: +0 left, +8 parent, +0x10 right, +0x19 isnil, +0x20 the object) appends its +0x90 to
//    `list` (a native vector of float4: +8 data, +0x10 capacity, +0x18 size, cleared to 0 first), in the set's order.
//    The row is positions only, so it is filtered right after the walk: the same set walked again in the same order, one
//    row per node, and the rows of the hidden soldiers dropped. Nothing of the soldier, its team or the set is written.
//  - The friendly call 0x82BB50 is already the plugin's (pvp_native.cpp RadarFriends, installed always): the filter runs
//    there, for whatever team that row walks.
//  - The set hidden is the mission crew registry's (mission_crew_support.cpp: a committed deployment's soldier alive in
//    its own live vehicle, the plugin on), published whole every frame by the game thread; the radar's thread compares
//    the set's object pointers with it and reads no soldier.
#pragma once
#include <cstddef>
#include <cstdint>

namespace crew {
// The bytes 0x82B220 the filter mirrors (its whole body), checked once; without them the radar is left as it is.
bool InstallMissionCrewRadar() noexcept;
// Game thread: the complete set (replacing the last one) of soldiers now hidden from the radar; count 0 clears it.
void MissionCrewRadarPublish(const void* const* objects,unsigned count) noexcept;
// Whether `object` is in the published set (any thread).
bool MissionCrewRadarHidden(const void* object) noexcept;
// The radar's thread, right after 0x82B220(hud,list,team) filled `list` from the set whose head node is `head`: drop the
// rows of the hidden soldiers. A set whose node count is not the row count (changed meanwhile) is left as it is.
void MissionCrewRadarFilterRows(const void* head,void* list) noexcept;
// The same for `team` of this image's team manager (RadarFriends).
void MissionCrewRadarFilter(void* list,int team) noexcept;

namespace crewradar {
inline constexpr unsigned kMost=128*15;   // every deployment's every soldier (mission_crew_support.cpp)
inline constexpr std::size_t kLeft=0,kParent=8,kRight=0x10,kIsNil=0x19,kValue=0x20;   // MSVC _Tree_node
inline constexpr std::size_t kData=8,kSize=0x18,kRow=16;                               // the native float4 vector
inline constexpr unsigned kTeamManager=0x20B2978;
inline constexpr std::size_t kTeams=0x38,kTeamRow=0x38;
}
}
