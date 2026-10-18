// Game-thread identity only: no render state, native object ownership or batch writes.
#pragma once
#include <cstddef>

namespace crew::thermal {
// The currently registered AnimationModels of a known native GameObject family.
// Humanoids have a native array of body/limb models, not one embedded model.
// Returns the number written (at most capacity). Unknown layouts return zero.
// Call while the game thread owns the live object; publish only pointer values to rendering.
std::size_t EnemyModels(const void* object,const void** out,std::size_t capacity) noexcept;
const void* EnemyModel(const void* object) noexcept;
}  // namespace crew::thermal
