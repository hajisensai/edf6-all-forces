// npcai.cpp: the plugin's own AI for the friendly NPC soldiers (docs/npc-ai-design.md §3, §4). Included by crew.h.
#pragma once
#include <cstdint>

namespace crew {
// The soldier classes' Think (vtable slot 7) wrapped, chained after any other plugin's: once, from EnsureInputs
// (every plugin loaded by then). False when the intent block's code is not as read (the AI stays stock).
bool InstallNpcAi() noexcept;
// A new mission (mission.cpp MissionStart): the last mission's soldiers are gone.
void ResetNpcAi() noexcept;
// Whether `human` is one of the four soldier classes (AssultSoldier, PaleWing, HeavyArmor, Engineer).
bool IsSoldierClass(const void* human) noexcept;
// An enemy is marked now (the Q mark, §6.3; the focus order needs one).
bool NpcMarked() noexcept;

// npcpost.cpp: NPC tanks back to their post (docs/npc-ai-design.md §8). Each vehicle's input, before the stock input
// reads seat 0's stick (crew.cpp InputHook).
void NpcPostInput(unsigned char* vehicle) noexcept;
// A tank's post moved to `at` (a map command); false when the vehicle keeps no post now.
bool NpcPostCommand(const void* vehicle,const float* at) noexcept;
void ResetNpcPosts() noexcept;   // a new mission
}  // namespace crew
