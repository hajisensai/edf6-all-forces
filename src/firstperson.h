// First person on foot (firstperson.cpp; the user, 2026-10-12: "a proper first-person game's view", the stock body not
// shown, arms and weapon as a viewmodel, the stock moves kept, online in sync). RE: docs/fp-render-re.md,
// docs/fp-soldier-re.md. Everything here is this machine's picture: the body, its animation, the shots and the aim the
// network carries stay the game's.
#pragma once

namespace crew {
bool InstallFirstPerson() noexcept;   // at load, before InstallMap (the map's camera step chains outside this one)
bool FirstPersonOn() noexcept;        // game thread: the local player's camera is in first person this frame
// Draw thread or game thread (map.cpp's camera step): the stock HUD is off on `camera` for a vehicle's first person
// (its information goes on the cockpit's panels instead; the user, 2026-10-13: "关，信息全上面板").
bool FirstPersonHidesStockHud(const void* camera) noexcept;

// fp_vehicle.cpp (game thread): the eye of `human`'s seat in the vehicle they ride (world, into `eye`). `out`: the
// vehicle, the seat, the name the station table knows it by, whether a station placed the eye (else the rider's head).
// False: on foot, or the vehicle or seat cannot be read.
struct RideView { unsigned char* vehicle; int seat; const char* name; bool station; };
bool VehicleEye(unsigned char* human,float* eye,RideView* out) noexcept;
}  // namespace crew
