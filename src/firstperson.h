// First person on foot (firstperson.cpp; the user, 2026-10-12: "a proper first-person game's view", the stock body not
// shown, arms and weapon as a viewmodel, the stock moves kept, online in sync). RE: docs/fp-render-re.md,
// docs/fp-soldier-re.md. Everything here is this machine's picture: the body, its animation, the shots and the aim the
// network carries stay the game's.
#pragma once
#include <cstdint>
#include "cockpit_instruments.h"

namespace crew {
void ResetFirstPerson() noexcept;   // publish a new mission; camera/render thread retires old pointers
bool InstallFirstPerson() noexcept;   // at load, before InstallMap (the map's camera step chains outside this one)
// Outer camera owner (map/TV) finished its step: suspend this viewport's viewmodel while it owns the view.
void FirstPersonCameraOverride(const void* camera,bool overridden) noexcept;
bool FirstPersonOn() noexcept;        // game thread: the local player's camera is in first person this frame
// Draw thread or game thread (map.cpp's camera step): the stock HUD is off on `camera` for a vehicle's first person
// (its information goes on the cockpit's panels instead; the user, 2026-10-13: "关，信息全上面板").
bool FirstPersonHidesStockHud(const void* camera) noexcept;
// Any thread: the player has first person on for vehicles (the key's switch). A seat is then seen through its cockpit's
// eye; one whose gun carries a mounted optic through that optic once the sight key raises it, 1x too (War Thunder's
// gunner sight, the user, 2026-10-14: "完全把战雷的搬过来": the view on the gun, the reticle on the bore, the barrel never
// across it; sightzoom.cpp owns that camera; 2026-10-30: the cockpit first, "该成瞄准键", sightzoom.h Press).
bool FirstPersonGunnerSight() noexcept;

// Game thread: this exact live local seat requests a marker-backed first-person view. Higher camera owners
// (map/TV, high view and mounted optics) resolve their own precedence; never infer this from a rendered camera ray.
bool FirstPersonVehicleView(const void* vehicle,unsigned seat) noexcept;
// Sazabi's controller owns camera rotation independently of its animated neck; frame carries the physical eye.
bool SazabiFirstPersonFrame(const void* vehicle,unsigned seat,float* frame) noexcept;



// fp_vehicle.cpp (game thread): the eye of `human`'s seat in the vehicle they ride (world, into `eye`): the model's
// `vc_eye_<seat>` marker, put there with the seat's cockpit. `out`: the vehicle, the seat, its class name, whether the
// marker was found, whether the seat's sight line is its gun's (`gaze`: the model's vc_gaze_<seat> marker, an open
// seat's kit: fp_view.h GunCameraSeat). False: on foot, the vehicle or seat cannot be read, or the seat has no cockpit
// (no marker).
struct RideView { unsigned char* vehicle; int seat; const char* name; bool station; bool gaze; float frame[16]; };
bool VehicleEye(unsigned char* human,float* eye,RideView* out) noexcept;

// A seat's cockpit screens (the model's vc_scr_<seat>_<kind>_<w>x<h> markers, pylib/cockpits.py Screen): where the HUD
// draws its live read-outs (hud.cpp CockpitScreens). `off`: the screen's centre from the eye, `right` / `up` its unit
// axes as seen from its front (world), w x h metres. Relative to the eye, so the draw (a frame after the camera step)
// puts it round the eye its own view has.
enum class ScreenKind : std::uint8_t { flight, weapons, status, hud, map, sensor };   // sensor: a remote station's monitor
struct CockpitScreen { ScreenKind kind; float off[3],right[3],up[3],w,h; };
constexpr int kMostScreens=8;
// `frame`: the seat's eye frame (vc_eye_<seat>, rows left, up, ahead, the eye) the screens were taken in: the head-up
// glass's bank scale reads the bank from it (instruments::BankOf, as the attitude cards).
struct CockpitScreens { int n; CockpitScreen s[kMostScreens]; bool physical; float frame[16]; };
// Game thread, from HudPublish. Consumed only by the vehicle's model draw, never by physics / native bone updates.
void FirstPersonInstrumentReadings(const instruments::Readings& readings,const unsigned char* human) noexcept;
// fp_vehicle.cpp (game thread): `vehicle`'s seat `seat` screens round the eye `eye`.
int VehicleScreens(const unsigned char* vehicle,int seat,const float* eye,CockpitScreens* out) noexcept;
// Game thread (HudPublish): this machine's player's cockpit screens as of the last camera step; false: no vehicle first
// person now (the HUD's own panels then).
bool FirstPersonScreens(CockpitScreens* out,const unsigned char* human) noexcept;
// Game thread: this machine's players (split screen: two), as their own cameras' last steps (within a second) saw them;
// up to `most` into `out`, how many. None before the camera step is chained (InstallFirstPerson).
int LocalPlayers(unsigned char** out,int most) noexcept;
// Render thread: the local player whose camera submitted the frame `view` (a world view's parameter block: its native
// view at +0x20) is drawn with, first person or not; null when no camera's recent submit is it, or two players' are
// (docs/fp-render-re.md 2026-10-17: the block is reused for shadow cascades and both viewports, only its value tells).
const void* LocalViewPlayer(const void* view) noexcept;
}  // namespace crew
