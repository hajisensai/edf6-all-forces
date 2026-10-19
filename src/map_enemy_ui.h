#pragma once
#include "enemy_control.h"
#include "enemy_ui_logic.h"

namespace crew {
// Published only on the game thread. The renderer consumes one whole copied
// catalogue page and never resolves native objects, actions or function addresses.
struct MapEnemyControlReadout {
    bool visible=false,focused=false;
    int first=0,count=0,padRow=-1;
    enemy_control::Snapshot snapshot{};
    enemy_control::Ability ability[enemyui::kPageRows]{};
};
constexpr int kMapEnemyItems=enemyui::kPageRows+5;
bool ReadMapEnemyControl(MapEnemyControlReadout*) noexcept;
void MapCommandEnemyButtons(const float* rects,const int* codes,int n,const enemy_control::Snapshot*) noexcept;
} // namespace crew
