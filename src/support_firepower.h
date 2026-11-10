// The plugin's support vehicles and aircraft fire by level, not by the difficulty's friend scaling
// (docs/feature-npc-weapon-tier.md §4.1, src/support_level.h). EDF.dll TimeDateStamp 0x678CCB46, RVAs.
//
// A vehicle's weapons deal their SGO damage times its +0x678: the mission setup (slot 46, 0x62F800) writes it, then
// builds the weapons (0x633330), each holder's +0x40 a copy of it (0x633842 / 0x633936), and every frame each weapon's
// +0x788 = vehicle +0x398 x holder +0x40 (0x6303C4 / 0x630449). With the SetLevel bit (+0x1A bit 3: the aircraft, levelled
// before their setup) 0x62F800 makes +0x678 = friend attack coefficient (0xD7870) x level (+0x49C / 10) x setup damage and
// the HP (+0x2F4) = durability x setup durability x friend HP coefficient (0xD78D0) x the same level; without it
// +0x678 = +0x67C x setup damage (+0x67C 1.0 from the constructor 0x6295FA) and no difficulty in the HP either.
//
// A marked vehicle's +0x678 and its holders' +0x40 are written after each of its setups as a request's of the support
// level would have it: LevelDamage(curve, SupportLevel(difficulty, progress)) x +0x67C (the unlevelled branch's rule, the
// one a player's requested vehicle runs). Its HP is the game's own, untouched (the maintainer asked for its firepower).
#pragma once
#include "support_level.h"

namespace crew {
// The progress function's code checked (0xD7B60). False: no vehicle is marked (each spawn path then refuses its vehicle).
bool InstallSupportFirepower() noexcept;
// A mission's start: the marks of the last one forgotten.
void ResetSupportFirepower() noexcept;
// Mark a vehicle this machine just made for the support (before its first mission setup). False: not marked (not
// installed, no room, not a live object): the caller refuses the vehicle.
bool MarkSupportFirepower(unsigned char* vehicle,supportlevel::Curve curve) noexcept;
// After a mission setup of `vehicle` (support_spawn.cpp / real_driver_native.cpp ApplyMissionSetup): a marked one's
// firepower by level. True: not marked, or written; false: marked and not written (the setup is then reported failed).
bool ApplySupportFirepower(unsigned char* vehicle) noexcept;
}  // namespace crew
