// The Rune Blade's own abilities at runtime (docs/newclass-re/runeblade-plan.md §2; runeblade.cpp): AssultSoldier's live
// frame slot chained (the clones copy it) for the ZwRuneBlade objects only, the ether gauge and rune steps
// (runeblade_ether_logic.h), the combo state (runeblade_combo.h), the plugin channel "runeblade" (ZWRB, runeblade_net.h)
// and the HUD's snapshot (hud_runeblade.inc).
#pragma once
#include "plugin_net.h"
#include "runeblade_ether_logic.h"

namespace crew {
bool InstallRuneBlade() noexcept;   // module "runeblade" (src/module_table.h), kBeforeGameStatus after "vehicler foot"
plugin_net::Channel RuneBladeNetChannel() noexcept;

namespace runeblade {
// A rune circle's moment (a takeoff from the ground, a step in the air) of `soldier`, on its own machine (`local`) and on
// every other's copy: the effect module draws the circle (none registered: nothing drawn; runeblade-plan.md §3).
using StepCueFn=void(*)(void* soldier,Cue cue,bool local) noexcept;
void SetStepCueSink(StepCueFn fn) noexcept;
}  // namespace runeblade

// What the HUD draws (hud_runeblade.inc): only while this machine's player is a Rune Blade. The draw thread: a copy.
struct RuneBladeHudView {
    float level=0.0f,max=0.0f;
    bool depleted=false,gliding=false;
    int steps=0;   // steps since leaving the ground
};
bool RuneBladeHudNow(RuneBladeHudView* out) noexcept;

// An NPC Rune Blade of this machine (not the player): its next frame on the ground lifts it with the game's own jump
// request (runeblade_npc_logic.h: its AssultSoldier body's jump press is the roll). The rune steps in the air are the jump
// press as for any Rune Blade of this machine (runeblade.cpp's frame). False: not one of this machine's NPC Rune Blades.
// Game thread (src/npcai.cpp RuneHop).
bool RuneBladeNpcTakeoff(void* soldier) noexcept;
}  // namespace crew
