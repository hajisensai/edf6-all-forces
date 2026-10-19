#pragma once
#include "enemy_control_types.h"
#include <cstdint>
namespace crew::controlled_pvp { struct Hooks; }
namespace crew {
// Plugin-owned enemy controllers register their real state-machine adapter here.
// Match/Abilities/ProfileHash are read-only. Begin pauses autonomous decisions;
// Apply supplies native commands, never a replacement physics simulation. Restore
// is idempotent and returns false while any baseline restoration still needs retry.
struct EnemyControlAdapter {
    const char* name=nullptr;
    bool (*Match)(const void*) noexcept=nullptr;
    unsigned (*Abilities)(const void*,enemy_control::Ability*,unsigned) noexcept=nullptr;
    std::uint64_t (*ProfileHash)(const void*) noexcept=nullptr;
    bool (*Begin)(const void*,std::uint64_t) noexcept=nullptr;
    bool (*Apply)(const void*,const enemy_control::Input&,bool firstSequence) noexcept=nullptr;
    bool (*Restore)(const void*) noexcept=nullptr;
    bool (*NetworkPoseReady)(const void*) noexcept=nullptr;
    bool (*PvpHooks)(const void*,controlled_pvp::Hooks*) noexcept=nullptr;
};
bool RegisterEnemyControlAdapter(const EnemyControlAdapter*) noexcept;
}
