// A soldier's scoped weapon looked through (the user, 2026-10-11: "包括玩家的狙击枪之类的，那个瞄具一点不现实。全换成
// 现实的那种，或者稍微未来一点也行"). The stock game zooms the camera (docs/zoom-re.md §1: the weapon's
// SecondaryFire_Type 1 scope, weapon +0x780 open, +0x784 its power, the soldier's +0x1D0, the camera's FOV) and keeps
// drawing the same crosshair model it draws unzoomed (the weapon's Sight_animation_model, app:/HUD/sightNN.rab: a ring
// or brackets the size of the hip-fire spread) over the magnified picture. With InfantryScope on, while the local
// player's soldier, on foot, has a scope open:
//  - the stock crosshair's draw (HudPlayer_CrossHair, vtable 0x17F6B98 slot 3, 0x8039F0) is held back for that
//    soldier: the call returns as the function's own no-draw paths do (its HUD switch off), nothing else of it skipped;
//    the same for the soldier looking through a vehicle sight's field hud.cpp drew (scope_hold.h, 2026-10-21);
//  - hud.cpp InfantryScope draws a real optic: the eyepiece (scopeview.h), and the reticle by what the weapon is
//    (scopereticle.h): a marksman's mil-dot duplex, a quick-firing rifle's combat optic (chevron, drop bars), a rocket
//    launcher's chevrons and stadiametric rangefinder; every mark sized from the live projection, as the glass is.
// The cue is gathered on the game thread (map.cpp MapHumanFrame) and published whole under a lock; the crosshair hook
// and the HUD read only that. Addresses: RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#pragma once
#include "scopereticle.h"
#include "sightzoom.h"
#include <cstdint>

namespace crew {
namespace infantryscope {
constexpr int kCombatFrames=20;   // a weapon firing at least 3 a second (FireInterval under 20 frames): a combat optic
// The optic a soldier's scoped weapon is: a rocket / guided round or one that bursts a launcher's, a quick-firing one a
// combat optic, the rest a marksman's scope.
constexpr scope::Look LookFor(int fireInterval,float blast,bool rocket) noexcept {
    return rocket || blast>0.0f ? scope::Look::launcher : fireInterval>0 && fireInterval<kCombatFrames ? scope::Look::combat :
           scope::Look::marksman;
}
// What the soldier's eye is at (sightzoom.h Eyepiece; the user, 2026-10-30: "有些是单眼看的瞄具"): a hand-held weapon's
// optic, a marksman's rifle scope, a combat optic or a launcher's telescope, is a telescope at one eye, the other open on
// what is round it: monocular, every look.
constexpr sightzoom::Eyepiece EyepieceOf(scope::Look) noexcept { return sightzoom::Eyepiece::monocular; }
}  // namespace infantryscope

struct InfantryScopeCue {
    const void* human;     // the soldier looking through it (the HUD pass's camera must follow it)
    float zoom;            // the scope's power (weapon +0x784)
    scope::Look look;
};
void InfantryScopeFrame(unsigned char* human) noexcept;   // game thread, the local player's frame (MapHumanFrame)
bool InfantryScopeNow(InfantryScopeCue* out) noexcept;    // any thread: the cue, false when none is fresh
bool InstallInfantryScope() noexcept;                     // at load: the stock crosshair's draw chained (checked)
void ResetInfantryScope() noexcept;                       // mission start
}  // namespace crew
