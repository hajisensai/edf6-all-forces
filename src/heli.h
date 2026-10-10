// heli.cpp: the helicopter pilot, its map rays and sea probe, the enemy registry walk, the sea rescue.
// Included by crew.h (the other modules call these through it).
#pragma once
#include <Windows.h>
#include <cstdint>

namespace crew {
bool IsHelicopter(const void* vehicle) noexcept;
// The player's board button pressed for them (the stock button's code: the nearest seat they may take in reach).
void PressBoardButton(unsigned char* human) noexcept;
// ...the same, but the player's own way: a seat an NPC holds is taken too (crew.cpp FindSeatHook bumps the NPC).
void PressBoardButtonBumping(unsigned char* human) noexcept;
// Whether the board button and the seats' riding points can be used (their code checked, CheckHeliProfile).
bool BoardButtonReady() noexcept;
// Whether `human` rides no vehicle (its vehicle weak_ptr +0x1550 empty or expired).
bool HumanOnFoot(const unsigned char* human) noexcept;
// Seat `seat`'s riding point (world) and the stock reach round it a human must be within to board it (CanRideSeat);
// false when they cannot be read.
bool SeatPoint(const unsigned char* v,unsigned seat,float* at,float* reach) noexcept;
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
// The map's ground in caves and indoors (map_floor.h): the first floor along a->b (a cave roof seen from above skipped;
// metres, -1 with none), and of the floors over / under (x, z) the height nearest `y` (`standable`: only those with
// room to stand over them). LearnMapNormals(a point the player stands on) settles which side a hit's normal faces (map.cpp
// calls it while the map is open).
float MapFloorRay(const float* a,const float* b,float* hit) noexcept;
bool MapGroundNear(float x,float z,float y,float* h,bool standable=false) noexcept;
void LearnMapNormals(const float* standing) noexcept;
// emc.cpp: the same against the buildings alone (layer 27: no terrain, no units), metres to the nearest or -1.
float BuildingRay(const float* a,const float* b,float* hit) noexcept;
// Whether there is water at (x, z) (docs/water-re.md): the game's own water areas; `surface` gets the
// highest surface there. unknown: the probe is off (EDF.dll differs) or the map's areas are not there.
enum class Sea { unknown, land, water };
Sea SeaAt(float x,float z,float* surface) noexcept;
// Every enemy lock point of `vehicle`'s side.
using EnemyVisitor=void(*)(void* ctx,const void* object,const float* aim);
bool VisitEnemies(const unsigned char* vehicle,EnemyVisitor visit,void* ctx) noexcept;
bool VisitEnemiesOf(std::int32_t team,EnemyVisitor visit,void* ctx) noexcept;   // the enemies of a side
bool VisitLockPoints(EnemyVisitor visit,void* ctx) noexcept;   // every valid lock point, whatever its side (impact.cpp)
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
// A called heli that has left and is far enough out to go (its reap is set). HeliReap deletes such a heli only with
// no real soldier aboard; a support deployment's (real crew) is retired by its owner, the dispatcher.
bool HeliLeft(const void* vehicle) noexcept;
// transport.cpp: a transport helicopter carrying a squad. A ferry to `at` (held HeliHeight over it; `land`: down on it and
// staying down), fighting nothing and following nobody meanwhile; nullptr ends it (its post / follow as before). Whether it
// stood on the ground last frame. Sent off now (StartLeave; deleted out there, its crew by support_dispatch.cpp Retire).
bool HeliFerry(const void* vehicle,const float* at,bool land) noexcept;
bool HeliGrounded(const void* vehicle) noexcept;
// A squad's transport now: no leaving for fuel or ammo (only badly damaged, or on WITHDRAW).
bool HeliKeep(const void* vehicle) noexcept;
bool HeliStartLeaving(const void* vehicle) noexcept;
// Where a vehicle weapon's barrel is and points (the mean of its muzzles' frames).
bool GunBarrel(const unsigned char* v,const unsigned char* weapon,float* pos,float* dir) noexcept;
// Sea rescue: once a game frame from the frame's common step (crew.cpp FrameTick), whether or not any heli
// is out.
void RescueTick() noexcept;
}  // namespace crew
