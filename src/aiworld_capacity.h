#pragma once

namespace crew {
// Plugin initialization only, before the game makes any ai::World (one per scene, built at mission load): every
// World made after it holds crew::aiworld::kAgentCapacity pathfinding agents instead of 1024. All sites or none;
// refused (stock 1024, one log line) when any site's bytes are not the stock ones.
bool InstallAiWorldCapacity() noexcept;
}  // namespace crew
