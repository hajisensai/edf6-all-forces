// hud.cpp: the vehicle HUD (docs/hud-re.md), drawn from the follower gauge's call (subcarrier.cpp GaugeHook).
#pragma once
namespace crew {
bool InstallHud() noexcept;                         // at load: checks the draw and text functions it calls
void HudSee(unsigned char* vehicle) noexcept;       // from every vehicle's input hook (game thread): a readout's data
// The game's text language (Option_Language: 0 ja, 1 en, 2 kr, 3 cn, 4 sc), -1 when its read did not check out at load
// (docs/hud-re.md §11). The HUD's words follow it unless the ini's HudLanguage names a language (hudtext.h).
int GameTextLanguage() noexcept;
// Once a game frame (crew.cpp FrameTick, game thread): what HudSee gathered is published, whole, for the draw.
void HudPublish() noexcept;
// A carrier's panel (subcarrier.cpp publishes it from the game thread): the hull, each deck part, and where each
// stands in the world (its bar is drawn there). `key` tells one carrier from another across draws (the bars'
// damage trails); it is never read through.
struct CarrierPanel {
    const void* key;
    float hull,hullMax;
    float at[3];
    int parts;
    struct Part { const char* name; float hp,max,repairSec; bool down; float at[3]; } part[4];
};
// From the follower gauge's draw (the draw thread): the published readouts and `count` carrier panels.
// `viewProj`, `ctx` and `viewport` as the gauge drawer 0x804300 gets them.
void HudDraw(const float* viewProj,void* ctx,const void* viewport,const CarrierPanel* panels,int count) noexcept;
// Whether the stock crosshair's draw for `soldier` gives way to the vehicle sight's field HudDraw drew for that soldier
// (scope_hold.h; infantryscope.cpp CrossDrawHook asks it). Any thread.
bool HudScopeHoldsCrosshair(const void* soldier) noexcept;
// A keyboard / mouse binding (a Windows virtual key) and a pad binding (MapButton's XInput bits) named as the HUD names
// them (the 整备 vehicle page's fire keys, armory.cpp). 0: "unbound".
void ControlKeyName(int vk,wchar_t* out,int size) noexcept;
void ControlPadName(int mask,wchar_t* out,int size) noexcept;
}  // namespace crew

namespace crew {
void ResetHud() noexcept;         // hud.cpp
// The camera's view-projection (row vectors, the HUD's) as of the last frame drawn; false before one (hud.cpp).
bool LastViewProj(float* out) noexcept;
// The camera's eye and its unit look through the screen's centre, from LastViewProj (hud.cpp); false: no camera yet.
bool CameraRay(float* eye,float* dir) noexcept;
// Split screen: each local player's own view (player_view.h). KeepPlayerViewProj from each HUD pass with the camera it
// draws for (subcarrier.cpp GaugeHook); CameraRayOf the ray of the view last drawn for `human`, false with none fresh.
void KeepPlayerViewProj(const void* camera,const float* viewProj) noexcept;
bool CameraRayOf(const void* human,float* eye,float* dir) noexcept;
}  // namespace crew
