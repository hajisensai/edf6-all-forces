// highcam.cpp: the high camera over a vehicle's gun.
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>

namespace crew {
// highcam.cpp: the artillery's high camera (an indirect-fire vehicle the player drives: its key / pad button switches
// the vehicle's camera block between its own points and a high view, docs/camera-re.md). From every vehicle's input,
// the plugin off too (it gives the block back then). PlayerHighCam (hud.cpp): whether the toggle is offered this
// moment, the view's state and whether the player is on keys (else a pad).
void HighCamFrame(unsigned char* vehicle) noexcept;
bool PlayerHighCam(bool* on,bool* keys) noexcept;
bool HighCamOn(const void* vehicle) noexcept;   // turretcam.cpp: the high view is on in `vehicle` now
// The seat holds an indirect-fire weapon (the Katyusha's rockets, the howitzer's shells: lofted or ground marked, rounds
// living 10 s or more): its high view (HighCamClass 1) and no gun stabilizer (stab.cpp: it fires from a halt).
bool IndirectFireSeat(const unsigned char* seat) noexcept;
void ResetHighCam() noexcept;
// The high view is offered in `vehicle` (seat 0 the player's, HighCam on): its button is the high view's there.
bool HighCamOffered(unsigned char* vehicle) noexcept;
// Game thread, after HighCamFrame: this frame the shared pad button is the gunner sight's in `vehicle` (first person,
// a mounted optic, the high view off), not the high view's.
bool HighCamYieldsButton(const void* vehicle) noexcept;
}  // namespace crew
