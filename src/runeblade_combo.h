// The Rune Blade's melee combo for the melee weapons (docs/newclass-re/runeblade-plan.md §1.2; the machine:
// runeblade_combo_logic.h). One combo state a Rune Blade (the plugin's extension of the object, runeblade.cpp); a weapon
// feeds it each frame and does what comes back. A stage started on this machine's Rune Blade is told to the room (ZWRB
// stage); on the other machines the weapon module's RemoteStageFn plays it on their copy. Game thread.
#pragma once
#include "runeblade_combo_logic.h"
#include <cstdint>

namespace crew::runeblade {
// This machine's Rune Blade: one call a frame from the melee weapon's own frame. The first call binds `spec`; another
// spec (the weapon changed) starts over idle. Not this machine's Rune Blade (or no spec): an empty Output.
combo::Output ComboFeed(void* soldier,const combo::Spec* spec,const combo::Input& in) noexcept;
bool ComboActive(const void* soldier) noexcept;   // in a stage or its window
int ComboStage(const void* soldier) noexcept;     // the current stage; -1 idle (or not a Rune Blade)
void ComboCancel(void* soldier) noexcept;         // a hard stop (knocked down, boarding)
// The weapon's tag that goes with the stages told to the room (RB-MELEE numbers its weapons).
void ComboTag(void* soldier,std::uint8_t weaponTag) noexcept;
// Another machine's Rune Blade started `stage` of its weapon `weaponTag`: called on this machine's copy of it.
using RemoteStageFn=void(*)(void* soldier,std::uint8_t weaponTag,int stage) noexcept;
void SetRemoteStageSink(RemoteStageFn fn) noexcept;
}  // namespace crew::runeblade
