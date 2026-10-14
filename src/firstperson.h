// First person on foot (firstperson.cpp; the user, 2026-10-12: "a proper first-person game's view", the stock body not
// shown, arms and weapon as a viewmodel, the stock moves kept, online in sync). RE: docs/fp-render-re.md,
// docs/fp-soldier-re.md. Everything here is this machine's picture: the body, its animation, the shots and the aim the
// network carries stay the game's.
#pragma once
#include <cstdint>

namespace crew {
bool InstallFirstPerson() noexcept;   // at load, before InstallMap (the map's camera step chains outside this one)
bool FirstPersonOn() noexcept;        // game thread: the local player's camera is in first person this frame
// Draw thread or game thread (map.cpp's camera step): the stock HUD is off on `camera` for a vehicle's first person
// (its information goes on the cockpit's panels instead; the user, 2026-10-13: "关，信息全上面板").
bool FirstPersonHidesStockHud(const void* camera) noexcept;
// Any thread: the player has first person on for vehicles (the key's switch). A seat whose gun carries a mounted optic
// is then seen through that optic at every magnification, 1x too (War Thunder's gunner sight, the user, 2026-10-14:
// "完全把战雷的搬过来": the view on the gun, the reticle on the bore, the barrel never across it; sightzoom.cpp owns that
// camera); the other seats through their cockpit's eye.
bool FirstPersonGunnerSight() noexcept;

// fp_vehicle.cpp (game thread): the eye of `human`'s seat in the vehicle they ride (world, into `eye`): the model's
// `vc_eye_<seat>` marker, put there with the seat's cockpit. `out`: the vehicle, the seat, its class name, whether the
// marker was found. False: on foot, the vehicle or seat cannot be read, or the seat has no cockpit (no marker).
struct RideView { unsigned char* vehicle; int seat; const char* name; bool station; };
bool VehicleEye(unsigned char* human,float* eye,RideView* out) noexcept;

// A seat's cockpit screens (the model's vc_scr_<seat>_<kind>_<w>x<h> markers, pylib/cockpits.py Screen): where the HUD
// draws its live read-outs (hud.cpp CockpitScreens). `off`: the screen's centre from the eye, `right` / `up` its unit
// axes as seen from its front (world), w x h metres. Relative to the eye, so the draw (a frame after the camera step)
// puts it round the eye its own view has.
enum class ScreenKind : std::uint8_t { flight, weapons, status, hud, map, sensor };   // sensor: a remote station's monitor
struct CockpitScreen { ScreenKind kind; float off[3],right[3],up[3],w,h; };
constexpr int kMostScreens=8;
struct CockpitScreens { int n; CockpitScreen s[kMostScreens]; };
// fp_vehicle.cpp (game thread): `vehicle`'s seat `seat` screens round the eye `eye`.
int VehicleScreens(const unsigned char* vehicle,int seat,const float* eye,CockpitScreens* out) noexcept;
// Game thread (HudPublish): this machine's player's cockpit screens as of the last camera step; false: no vehicle first
// person now (the HUD's own panels then).
bool FirstPersonScreens(CockpitScreens* out) noexcept;
}  // namespace crew
