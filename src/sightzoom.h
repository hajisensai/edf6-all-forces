// A vehicle sight's magnification (the user, 2026-10-08: a sight HUD on every vehicle and weapon with a sight, tanks
// and the gunship above all): a key (ini SightZoomKey, 'Z') or pad button (SightZoomButton, R3) steps it 1x -> 3x ->
// 6x -> 1x while the player sits at a vehicle's gun. How the game zooms (docs/zoom-re.md, H): the player's camera
// (CharacterGhostCamera, slot 4 0xF86A0) sets its vertical field of view every frame from its zoom (cam+0x410: the
// soldier's scope, 1 riding): cam+0x24 = (pi/4) / zoom, and builds the projection and the culling frustum from that;
// sightzoom.cpp sets it again after that step with the sight's magnification as well (Fov). Set, not scaled: the
// stock write is on one branch of the step only, so a factor applied to what is there would compound on a frame it
// is not made. Pure arithmetic (no EDF.dll): the steps and the field of view; tools/sight_zoom_check.cpp.
#pragma once
#include <cmath>

namespace crew {
namespace sightzoom {
constexpr float kSteps[]={1.0f,3.0f,6.0f};
constexpr int kStepCount=static_cast<int>(sizeof(kSteps)/sizeof(kSteps[0]));
constexpr float kBaseFov=0.785398163f;   // the stock camera's (the constant at 0x1765A20, pi/4)

// The step after `i` (the last goes back to 1x).
constexpr int Next(int i) noexcept { return i>=0 && i+1<kStepCount ? i+1 : 0; }
// The magnification of step `i` (out of range: 1x).
constexpr float At(int i) noexcept { return i>=0 && i<kStepCount ? kSteps[i] : 1.0f; }

// The field of view with the camera's own zoom `camZoom` (cam+0x410) and the sight's `zoom`; either not a number or
// under 1 taken as 1 (never wider than the stock view).
inline float Fov(float camZoom,float zoom) noexcept {
    const float c=std::isfinite(camZoom) && camZoom>=1.0f ? camZoom : 1.0f;
    const float z=std::isfinite(zoom) && zoom>=1.0f ? zoom : 1.0f;
    return kBaseFov/c/z;
}

// A turn rate (the turret camera's, rad/frame) at the sight's `zoom`: the same sweep across the screen at any zoom.
inline float Rate(float rate,float zoom) noexcept { return std::isfinite(zoom) && zoom>1.0f ? rate/zoom : rate; }

static_assert(Next(0)==1 && Next(1)==2 && Next(kStepCount-1)==0 && Next(-3)==0,"1x -> 3x -> 6x -> 1x");
static_assert(At(0)==1.0f && At(kStepCount)==1.0f,"out of range is 1x");
}  // namespace sightzoom
}  // namespace crew
