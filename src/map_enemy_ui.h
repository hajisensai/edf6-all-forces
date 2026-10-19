#pragma once
#include "enemy_control.h"
#include "enemy_ui_logic.h"

namespace crew {
// Published only on the game thread. The renderer consumes one whole copied
// catalogue page and never resolves native objects, actions or function addresses.
struct MapEnemyControlReadout {
    bool visible=false,focused=false;
    bool expanded=false;        // the user's fold state (title click / pick / L3); never hover
    bool marked=false;          // the picked target's lock point, for its mark on the map
    float markAt[3]{};
    int first=0,count=0,padRow=-1;
    enemy_control::Snapshot snapshot{};
    enemy_control::Ability ability[enemyui::kPageRows]{};
};
constexpr int kMapEnemyItems=enemyui::kPageRows+6;   // attack rows, page arrows, return; idle: title, target arrows, control
bool ReadMapEnemyControl(MapEnemyControlReadout*) noexcept;
void MapCommandEnemyButtons(const float* rects,const int* codes,int n,const enemy_control::Snapshot*) noexcept;
} // namespace crew
