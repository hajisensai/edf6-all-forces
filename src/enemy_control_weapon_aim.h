#pragma once
namespace crew::enemy_native {struct NativeProfile;}
namespace crew {
// Validated point consumers only; no native target ObjRef or weapon ownership is replaced.
bool InstallEnemyControlWeaponAim() noexcept;
// Rotate an emitter basis onto the common point, retaining native translation/scale/roll and later spread.
bool EnemyControlAimMatrix(const void* root,float matrix[16]) noexcept;
// Strict per-family admission. Unknown families and incomplete/foreign-modified patches are never ready.
bool EnemyControlWeaponAimReady(const enemy_native::NativeProfile*) noexcept;
}
