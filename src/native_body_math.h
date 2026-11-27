#pragma once

namespace crew {
inline constexpr unsigned kNativeBodyMathSites=20;
unsigned InstallNativeBodyMath() noexcept;
bool InstallNativeTargetScore() noexcept;
bool InstallNativeHostileVisit() noexcept;
bool InstallNativeHostileRoster(bool nearestReady) noexcept;
bool InstallNativeSoldierScore() noexcept;
}
