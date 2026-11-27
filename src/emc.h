// emc.cpp: the EMC.
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>

namespace crew {
// emc.cpp: the EMC's charged beam (docs/emc-re.md, README EMC 蓄力光束): the player's trigger in a Vehicle510_Maser charges,
// and one thick beam carries the stock burst's damage through every enemy and building on its line, a blast at its end.
// EmcInput before the stock input (the trigger taken for the charge), EmcFrame after it.
bool InstallEmc() noexcept;
bool IsEmc(const void* vehicle) noexcept;
void EmcInput(unsigned char* vehicle) noexcept;
void EmcFrame(unsigned char* vehicle) noexcept;
void ResetEmc() noexcept;
void EmcTick() noexcept;   // once a frame: an EMC gone mid-charge or mid-beam has its sound and rounds dropped
// The local player's EMC (hud.cpp): the charge (0..1), the beam's seconds left, the beams its rounds still make (the stock
// burst's rounds each), and its state; `pos` the EMC's (the HUD draws the line only on that vehicle's block). False with none.
struct EmcCue { float charge,beamLeft,rearm; int beams; bool charging,firing,empty; float pos[3]; };
bool PlayerEmcCue(EmcCue* out) noexcept;
}  // namespace crew
