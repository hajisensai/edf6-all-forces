// EDF4.1 plugin-owned controllers: semantic input enters their real body/gun machines.
#pragma once
#include "enemy_control_adapter.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace crew::enemy_legacy {
// One sampled control frame plus a bounded edge inbox. Repeated held samples never
// replay an edge; refusing overflow leaves the previous accepted frame intact.
struct Press { std::uint32_t id=0;std::uint64_t seed=0; };
struct Control {
    static constexpr unsigned kPending=64;
    std::uint64_t lease=0,sequence=0;
    enemy_control::Input input{};
    float yaw=0,pitch=0;
    std::array<Press,kPending> pressed{};
    unsigned pending=0;
    bool Active() const noexcept { return lease!=0; }
    bool Begin(std::uint64_t id,float direction) noexcept {
        if(!id || !std::isfinite(direction))return false;
        if(lease)return lease==id;
        *this=Control{};lease=id;yaw=direction;return true;
    }
    bool Apply(const enemy_control::Input& value,bool first) noexcept {
        if(!lease || !enemy_control::ValidInput(value))return false;
        if(!value.sequence) {
            if(first || value.absoluteAim || value.aimYaw!=0 || value.aimPitch!=0)return false;
            for(float axis:value.move)if(axis!=0)return false;
            for(float axis:value.look)if(axis!=0)return false;
            for(const auto& action:value.active)if(action.id)return false;
            input={};ClearEdges();return true;
        }
        if(!value.absoluteAim || (first && value.sequence<=sequence) || (!first && value.sequence!=sequence))return false;
        unsigned incoming=0;
        if(first)for(const auto& action:value.active)incoming+=action.id && action.phase==2 ? 1u : 0u;
        if(incoming>kPending-pending)return false;
        input=value;
        if(first) {
            // The source camera integrates stick/mouse input before network
            // coalescing. Repeated samples and receiver cadence cannot change aim.
            yaw=value.aimYaw;pitch=value.aimPitch;
            sequence=value.sequence;
            for(const auto& action:value.active)if(action.id && action.phase==2)pressed[pending++]={action.id,value.seed};
        }
        return true;
    }
    void ClearEdges() noexcept { pending=0; }
    void Restore() noexcept { *this=Control{}; }
    void Aim(const float* position,float distance,float* out) const noexcept {
        const float cp=std::cos(pitch);
        out[0]=position[0]+std::sin(yaw)*cp*distance;
        out[1]=position[1]+std::sin(pitch)*distance;
        out[2]=position[2]+std::cos(yaw)*cp*distance;
    }
};
}
namespace crew {
namespace controlled_pvp { struct SpawnProof; }
const EnemyControlAdapter& Edf41RoboControlAdapter() noexcept;
// Real producer setup runs at Adopt, before saving the newborn's native team baseline.
bool Edf41RoboPvpSpawnProof(controlled_pvp::SpawnProof*) noexcept;
// Called at plugin startup after InstallEnemyControl; registration never changes
// native object vtables, teams, health, physics, or scene ownership.
bool InstallEnemyControlLegacy() noexcept;
}
