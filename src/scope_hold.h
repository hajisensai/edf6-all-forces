// The stock crosshair under the plugin's own sight field (the user, 2026-10-21: "进入瞄具状态的时候没隐藏准星").
// A vehicle's optic magnified, or up at 1x in its first person (sightzoom.h ScopeField), shows hud.cpp's eyepiece and
// the weapon's reticle (ScopeShade, ScopeArm); first person yields the camera to that sight (firstperson.cpp
// VehicleYields: "the gunner's sight"), so the stock HUD is on again and its crosshair (HudPlayer_CrossHair, slot 3
// 0x8039F0, infantryscope.h) drew its hip-fire ring at the screen's centre over the scope's reticle. A soldier's scope
// already held it back (infantryscope.cpp); a vehicle's never did. Now each HudDraw says whose field it drew (Drawn: the
// soldier looking through it, when), and the crosshair's draw is held back for that soldier while that is fresh: the
// crosshair goes exactly when the field's reticle is on the screen, never when the HUD did not draw it (VehicleHud off,
// the map's view, a pause), and it is the local soldier's only (EDF6Coop's remote players have their own machines).
// Pure: tests/scope_hold_test.cpp.
#pragma once

namespace crew {
namespace scopehold {
// Wall ms a drawn field holds the crosshair back: a few frames at the slowest frame rate the HUD still draws at, so
// the order of the two draws in a frame does not flicker it; HudDraw says "none" on every pass without the field.
constexpr unsigned long long kHeldMs=250;
struct Drawn { const void* soldier=nullptr; unsigned long long at=0; };
// The crosshair for `soldier` is held back at `now`: the field was last drawn for that soldier, at most kHeldMs ago.
constexpr bool Holds(const Drawn& d,const void* soldier,unsigned long long now) noexcept {
    return d.soldier!=nullptr && soldier==d.soldier && now>=d.at && now-d.at<=kHeldMs;
}
static_assert(Holds(Drawn{&kHeldMs,100},&kHeldMs,100+kHeldMs) && !Holds(Drawn{&kHeldMs,100},&kHeldMs,101+kHeldMs) &&
              !Holds(Drawn{&kHeldMs,100},nullptr,100) && !Holds(Drawn{nullptr,100},nullptr,100) && !Holds(Drawn{&kHeldMs,100},&kHeldMs,99),
              "held for its own soldier, while fresh");
}  // namespace scopehold
}  // namespace crew
