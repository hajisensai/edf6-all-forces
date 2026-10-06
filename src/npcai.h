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
// The mark for the HUD (draw thread): where it is; false with none (or none published lately).
bool NpcMarkReadout(float* at) noexcept;

// npcpost.cpp: NPC tanks back to their post (docs/npc-ai-design.md §8). Each vehicle's input, before the stock input
// reads seat 0's stick (crew.cpp InputHook).
void NpcPostInput(unsigned char* vehicle) noexcept;
// A tank's post moved to `at` (a map command); false when the vehicle keeps no post now.
bool NpcPostCommand(const void* vehicle,const float* at) noexcept;
void ResetNpcPosts() noexcept;   // a new mission
// A ground vehicle an NPC in seat 0 drives (and a map command sends to a post): the stock CarBase AI (0x661440: the
// tanks, the Titan, the Grape), armed.
bool NpcDrivable(const unsigned char* vehicle) noexcept;
// The AI riders in a CarBase vehicle's gunner seats aim and fire (§7): each vehicle's input, before the stock input.
void NpcGunnersInput(unsigned char* vehicle) noexcept;
// The seat's rider is an AI that should work its gun (NpcGunners, offline): RideAi's DummyVehicleRider, or a local
// soldier (CustomNpcAi and NpcBoarding on). Used by the ground gunners and the 410's door guns under a player pilot.
bool AiGunner(const unsigned char* seat) noexcept;
}  // namespace crew
