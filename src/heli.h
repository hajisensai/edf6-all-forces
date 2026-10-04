// heli.cpp: the helicopter pilot, its map rays and sea probe, the enemy registry walk, the sea rescue.
// Included by crew.h (the other modules call these through it).
#pragma once
#include <Windows.h>
#include <cstdint>

namespace crew {
bool IsHelicopter(const void* vehicle) noexcept;
// The player's helicopter on the ground, for the cockpit's takeoff cue: its rotor speed and the rotor speed whose
// lift holds it up (docs/aircraft-re.md: hover = g M / (60 L), 0.288 for the stock lift L 70, M 1).
struct HeliCue { float rotor,hover; };
void HeliCueStep(unsigned char* vehicle) noexcept;   // each vehicle's frame
bool PlayerHeliCue(HeliCue* out) noexcept;           // the player's, as of the last frame; false: none on the ground
// The plugin seated an NPC pilot: it flies this heli. False when its table is full of live helis (the heli is
// then left to the stock game: it sits where it is).
bool HeliCrewed(const void* vehicle) noexcept;
void HeliFrame(unsigned char* vehicle) noexcept;   // after the stock input, NPC-crewed helicopters only
// Checks only (no patch): the flight's own code, and which of avoidance, the water probe, the rescue's seat
// reach and board button and the delete of called helis can be used. False: no heli is flown.
bool CheckHeliProfile() noexcept;
bool InstallDoorGuns() noexcept;   // after CheckHeliProfile: the 410's door guns (their weapon-user hook)
// Shared with jet.cpp: map ray (metres a->b to terrain/buildings, -1 with none; `hit` gets the point).
float MapRay(const float* a,const float* b,float* hit) noexcept;
// Whether there is water at (x, z) (docs/water-re.md): the game's own water areas; `surface` gets the
// highest surface there. unknown: the probe is off (EDF.dll differs) or the map's areas are not there.
enum class Sea { unknown, land, water };
Sea SeaAt(float x,float z,float* surface) noexcept;
// Every enemy lock point of `vehicle`'s side.
using EnemyVisitor=void(*)(void* ctx,const void* object,const float* aim);
bool VisitEnemies(const unsigned char* vehicle,EnemyVisitor visit,void* ctx) noexcept;
bool VisitEnemiesOf(std::int32_t team,EnemyVisitor visit,void* ctx) noexcept;   // the enemies of a side
// Whether `point` is within `radius` of the segment from->to (between its ends).
bool NearLine(const float* from,const float* to,const float* point,float radius) noexcept;
// Whether a burst from->to would pass by the player (as the helis' guns check) or a jet the plugin flies
// other than `self` (JetInLine). Other friends are hit as the stock game hits them.
bool FriendInLine(const float* from,const float* to,const void* self) noexcept;
// Deletes the called helis that have left; call from another object's update.
void HeliReap(const void* self) noexcept;
// A heli the Air Raider called: `guard` holds over `post` and fights round it, else it follows the player;
// its weapons are not refilled, and out of ammo, after `fuelSec` or badly damaged it flies off away from the
// player and is deleted far from them.
void HeliCalled(unsigned char* vehicle,bool guard,const float* post,DWORD fuelSec) noexcept;
// A called heli's fuel (game thread): seconds until it flies off (0: leaving); false with no limit.
bool HeliFuel(const void* vehicle,float* sec) noexcept;
// Where a vehicle weapon's barrel is and points (the mean of its muzzles' frames).
bool GunBarrel(const unsigned char* v,const unsigned char* weapon,float* pos,float* dir) noexcept;
// Sea rescue: once a game frame from the frame's common step (crew.cpp FrameTick), whether or not any heli
// is out.
void RescueTick() noexcept;
}  // namespace crew
