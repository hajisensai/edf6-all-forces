// The plugin's aircraft bodies (body506.cpp): which plugin body a vehicle is (PluginBody), the 506 physics slot's
// vtable and its check. Each body's step is declared by its owner (jet.h, playerjet.h, sazabi.h, dreadnought.h,
// subcarrier.h).
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>

namespace crew {
// body506.cpp: the 506 body the plugin's jets, carriers and player jets fly in. Which one a vehicle is comes
// from its SGO's mark (veh+0x162C, kMark* in body506.cpp, the one table of them); the 506's physics step
// (slot 57) is hooked once, there, and hands each body to its owner's step, which returns the velocity and
// spin to set (false: leave the stock step's).
enum class PluginBody { none, jet, sub, playerJet, sazabi, dreadnought };
PluginBody BodyOf(const void* vehicle) noexcept;
float BodyMark(const void* vehicle) noexcept;      // the mark of a 506 body, 0 for anything else
// A 506's vtable (BodyMark is 0 for any other: so is IsJet): for an object the caller knows is live (one the game's
// walk hands over), whose vtable it read itself.
bool Heli506Vtable(const void* vtable) noexcept;
bool InstallBody506() noexcept;                    // before InstallJets / InstallSub / InstallPlayerJets
bool Body506Ok() noexcept;                         // the physics hook is in
bool Body506ReadsSolver() noexcept;                // ...and it reads the solver's velocity before the stock step (GroundContact)
}  // namespace crew
