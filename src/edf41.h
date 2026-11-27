// The EDF4.1 port's installers (edf41*.cpp, legacymap.cpp).
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>

namespace crew {
// legacymap.cpp: EDF4.1 maps' Havok 2014 collision migrated to hknp on the map path (plan P6).
bool InstallLegacyMap() noexcept;
// edf41sound.cpp: EDF4.1's weapon SE bank and voice banks beside EDF6's own (plan P7).
bool InstallEdf41Sound() noexcept;
// edf41ai.cpp: the converted EDF4.1 Hectors attack as 4.1's do (per-arm range gate, back-off cycle).
bool InstallEdf41Ai() noexcept;
// edf41fortress.cpp: the converted EDF4.1 four-legged fortress stands from the start and drops through its core hatch.
bool InstallEdf41Fortress() noexcept;
// edf41dragon.cpp: the converted EDF4.1 giant dragon breathes 4.1's flame stream and keeps 4.1's ranges and glide pass.
bool InstallEdf41Dragon() noexcept;
// edf41smalldragon.cpp: the DLC's EDF4.1 small dragons' hit reaction drives 4.1's ragdoll bodies (koshi, spine1, spine2), not mune.
bool InstallEdf41SmallDragon() noexcept;
// edf41insects.cpp: the DLC's EDF4.1 queen bee and Nephilas: 4.1's needle speed and thread scatter.
bool InstallEdf41Insects() noexcept;
// edf41mech_boost.cpp: the 406 Begaruta's jet group skips the thruster slots its old begaruta_boost parser leaves unbuilt (its jump crashed).
bool InstallEdf41MechBoost() noexcept;
// edf41natives.cpp: EDF4.1 script natives EDF6's BVM executor lacks (SetEncountReportRadius as table 1 id 3003).
bool InstallEdf41Natives() noexcept;
// edf41before.cpp: EDF4.1's pre-mission scenes (BEFORE.BVM) play with the menu's UIs (its black fade) closed, as in 4.1.
bool InstallEdf41Before() noexcept;
// edf41mother.cpp: UfoMother301 (EDF4.1's mothership) and its part classes, registered in the game's own factory table.
bool InstallEdf41Mother() noexcept;
bool InstallEdf41Robo() noexcept;      // edf41robo.cpp: EDF4.1's UfoRobo (ini Edf41Robo)
bool InstallEdf41Mother401() noexcept;   // edf41mother401.cpp
// edf41deiroi.cpp: a falling Deiroi's landing ray, cast by EDF6 against a null Havok world (crash in EDF4.1's M412), goes through the map ray.
bool InstallEdf41Deiroi() noexcept;
void Edf41Mother401DevTick() noexcept;   // development (Edf41Mother401Dev bit 32768): Edf41DevShoot at the mission's ordinary enemies, once a frame
}  // namespace crew
