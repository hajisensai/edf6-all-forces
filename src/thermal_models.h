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
// The family EnemyModels reads `object` by (the same RTTI rules and cache): a human (HumanBase: soldiers, NPCs, players
// of any machine), a vehicle (VehicleBase: seats at +0x608), another known family, or none.
enum class Family { kNone,kHuman,kVehicle,kOther };
Family EnemyFamily(const void* object) noexcept;
// The same answers for an object the caller knows is live and whole (System's update list at the camera step, an object
// whose own control block's use count says it is there), read without asking Readable: the caller runs them under
// __try. Only a vtable not met yet resolves its RTTI (through Readable, once). frame_rate_objects.cpp's per-tick sample of
// every listed object; anything else asks EnemyModels.
std::size_t EnemyModelsLive(const void* object,const void** out,std::size_t capacity) noexcept;
const void* EnemyModelLive(const void* object) noexcept;
Family EnemyFamilyLive(const void* object) noexcept;
// Whether `model` is a registered AnimationModel (the vtable EDF.dll's or one whose RTTI says so, the +0x10 Umbra object
// set), asked through Readable: what EnemyModels keeps of a candidate model pointer.
bool IsAnimationModel(const void* model) noexcept;
}  // namespace crew::thermal
