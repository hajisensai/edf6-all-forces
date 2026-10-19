// Final native Unit guide weak-reference selection; does not change authored projectile matrices.
#pragma once
namespace crew::enemy_native {struct NativeProfile;}
namespace crew {
bool InstallEnemyControlUnitGuide() noexcept;
bool EnemyControlUnitGuideReady(const enemy_native::NativeProfile*) noexcept;
// Exact retained root identity; failed/uncertain weak cleanup keeps its journal and refuses release.
bool RestoreEnemyControlUnitGuide(const void* root,const void* control) noexcept;
}
