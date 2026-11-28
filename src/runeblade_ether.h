// The Rune Blade's ether gauge for the other modules (docs/newclass-re/runeblade-plan.md §1.1): R3 Seven Stars and M4
// Falling Star spend it, the HUD reads it. The gauge is the soldier's own energy fields (max +0x304, level +0x308, depleted
// +0x2FC); its authority the machine the Rune Blade plays on. Only there does a spend take anything: on every other
// machine's copy (written by the ZWRB channel, runeblade.cpp) each spend is refused and each read the last heard.
// Game thread (the soldier's frame, a weapon's frame, the channel's update).
#pragma once

namespace crew::runeblade {
bool IsRuneBlade(const void* soldier) noexcept;      // a live ZwRuneBlade object (soldier_factory.h ZwType == 5)
bool EtherOwned(const void* soldier) noexcept;       // a Rune Blade of this machine's (its spends take)
float EtherLevel(const void* soldier) noexcept;      // the level; -1 not a Rune Blade
float EtherMax(const void* soldier) noexcept;        // the max; -1 not a Rune Blade
bool EtherDepleted(const void* soldier) noexcept;    // spent out, not yet refilled to kRecoverAt
// Take `amount` if there is that much: true. Too little, depleted, not this machine's Rune Blade: nothing taken, false.
bool EtherSpend(void* soldier,float amount) noexcept;
// Take all that is left (a depleting attack): how much (0: nothing to take). Depletes.
float EtherDrain(void* soldier) noexcept;
// Take up to `amount` (a held drain, a frame's): how much was taken. Reaching 0 depletes.
float EtherBleed(void* soldier,float amount) noexcept;
}  // namespace crew::runeblade
