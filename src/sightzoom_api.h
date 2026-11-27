// sightzoom.cpp's hooks and readouts (the logic is sightzoom.h).
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include "sightzoom.h"

namespace crew {
// sightzoom.cpp: a vehicle gun's sight magnified (src/sightzoom.h, docs/zoom-re.md). InstallSightZoom at load (the
// player camera's step, chained with map.cpp's); SightZoomFrame from the frame of the seat the player sits at (its key
// or, `padButton`, its pad button steps 1x -> 3x -> 6x; a seat taken again starts at 1x), SightZoomStock from every stock
// vehicle's input (the player's seat in it); SightZoomNow: the magnification now (in `vehicle`, nullptr any), 1 with none.
bool InstallSightZoom() noexcept;
void SightZoomFrame(unsigned char* vehicle,unsigned seat,bool padButton) noexcept;
void SightZoomStock(unsigned char* vehicle) noexcept;
float SightZoomNow(const void* vehicle) noexcept;
bool SightZoomCanMount(const void* vehicle,unsigned seat) noexcept; // game-thread query: real current optic, including at 1x
bool SightZoomMounted(const void* vehicle=nullptr) noexcept; // active physical optic; turret input stays native
// The seat's sight raised (magnified, or at 1x in first person by the sight key: sightzoom.h Press), the look key aside.
bool SightZoomRaised(const void* vehicle=nullptr) noexcept;
// Current validated seat/weapon fire-control surface; none when unavailable or another camera owns the view.
sightzoom::Kind SightZoomView(const void* vehicle=nullptr) noexcept;
// The sight's field over the view now (sightzoom.h ScopeField, from one snapshot of the seat's cue): the vehicle, the
// soldier looking through it, its fire-control surface and magnification; false (all empty) with none, or while another
// camera owns the view. hud.cpp draws the field by it, and so holds the stock crosshair back for that soldier.
struct SightScope { const void* vehicle=nullptr; const void* human=nullptr; sightzoom::Kind kind=sightzoom::Kind::none; float zoom=1.0f;
                   sightzoom::Eyepiece eyepiece=sightzoom::Eyepiece::binocular; };
bool SightZoomScope(SightScope* out) noexcept;
// The sight's range setting (sightzoom.h kZeroStep..; 0 the bore line) and the superelevation, rad, it lays the line of
// sight under the bore for the weapon looked through now; any thread, 0 with no magnified optic.
int SightZoomRange(const void* vehicle=nullptr,float* angle=nullptr) noexcept;
// The range drum laid on the gun (sightzoom.h LayStep): from every seat aim's step, after the stock step and the
// stabilizer (stab.cpp StepHook), `before` the pitch axis' angle before that step. Only the player's magnified sight's
// aim is touched: its pitch axis turned toward the set range's superelevation over the line of sight (game thread).
void SightLayStep(unsigned char* aim,float before) noexcept;
void ResetSightZoom() noexcept;
}  // namespace crew
