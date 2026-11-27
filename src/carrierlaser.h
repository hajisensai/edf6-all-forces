// carrierlaser.cpp: the submarine carrier's laser.
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>

namespace crew {
void ResetLaser() noexcept;       // carrierlaser.cpp
// carrierlaser.cpp: the e508 teleportation ships' portal laser (warning beam, interruptible charge, main beam)
// while a submarine carrier is out; its SGOs from tools/make_jets.py (EDF6VC_PORTAL_SIGHT / _LASER.SGO).
bool InstallLaser() noexcept;                         // at load
void PreloadLaser() noexcept;                         // from the mission's player preload
void CarrierLaserFrame(const unsigned char* sub) noexcept;   // from a flown carrier's frame, at most once a frame
}  // namespace crew
