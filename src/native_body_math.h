#pragma once

namespace crew {
inline constexpr unsigned kNativeBodyMathSites=18;
unsigned InstallNativeBodyMath() noexcept;
bool InstallNativeTargetScore() noexcept;
bool InstallNativeHostileVisit() noexcept;
bool InstallNativeHostileRoster(bool nearestReady) noexcept;
}
