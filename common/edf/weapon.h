// A vehicle weapon as both plugins read it (common/weapon.cpp): its round's parameters, its muzzles, the world gravity
// its rounds fall by and the mod's marks. All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46
// (autoturret/docs/re-notes.md "Weapon", "Muzzles", "Gravity and the ballistic solve").
#pragma once
#include "edf/layout.h"

namespace edf {
// Weapon fields filled from its SGO (0x68D4A0): LockonTargetType (the mod's mark), FireAccuracy (the cone's half angle
// in radians: fire 0x691B02 hands weapon+0xE14 x it to 0x4E820, which draws the polar angle uniform in [0, it]),
// AmmoSpeed (m/frame), AmmoAlive (frames), AmmoGravityFactor.
constexpr std::size_t kWeaponMark=0x6B4,kWeaponAccuracy=0x378,kWeaponAccuracyScale=0xE14;
constexpr std::size_t kWeaponAmmoSpeed=0x894,kWeaponAmmoAlive=0x898,kWeaponAmmoGravity=0x8E0;
// The mod's marks in LockonTargetType (tools/build.py MARK_*, tools/make_*.py): the only stock reader of the field is
// the lock query (0x696792), which a range-0, type-0 gun never makes, so a mark changes nothing in the stock game.
// kMarkAir: an anti-air gun (EDF6AutoTurret: air targets first, fused rounds). kMarkGround: a ground-attack gun (ground
// targets first, the low arc). kMarkLofted: a ground-attack launcher lobbing its rounds (the Katyusha: the high arc,
// and EDF6VehicleCrew shows its rider where they land).
constexpr std::int32_t kMarkAir=7301,kMarkGround=7302,kMarkLofted=7303;

// Weapon muzzles: array at +0x1D0, count at +0x1E0, stride 0xF0. Muzzle +0 is its bone (world rows
// right/up/forward/position at +0xB0..+0xEF, updated every frame), +0x10 its local 4x4 matrix,
// +0xE0 how fire orients it (0x696B70): mode 0 takes the weapon's own world rows (weapon+0x150,
// copied from its aim bone each frame by 0x633DD0), mode 1 the local matrix times the bone.
// The round leaves along row 2 (+0x70 of the built matrix, 0x69168B).
constexpr std::size_t kMuzzles=0x1D0,kMuzzleCount=0x1E0,kMuzzleStride=0xF0,kMuzzleLocal=0x10,kBoneRows=0xB0;
constexpr std::size_t kMuzzleMode=0xE0,kWeaponMatrix=0x150;
constexpr std::int32_t kModeWeaponRows=0;
// One muzzle's world position and direction as 0x6969A0 builds them for a shot: the position is row 3 of local x
// bone, the direction row 2 of the matrix its mode picks. Under the caller's __try.
bool MuzzleFrame(const unsigned char* weapon,const unsigned char* muzzle,float* pos,float* dir) noexcept;
// The mean of a weapon's first `most` muzzles (a launcher's tubes, the Titan's two side-cannon barrels): position
// and unit direction. False when it has none or more than `most`, or one cannot be read. Under the caller's __try.
bool MeanMuzzle(const unsigned char* weapon,std::uint64_t most,float* pos,float* dir) noexcept;

// The world gravity vector (m/s^2): *(image+0x20B2958)+0x68 is the physics world; its object at +0x20 returns it from
// virtual slot 0, as the game's vehicle aim (0x622706) and every round's spawn (0x231E7B) read it. False when it
// cannot be read. Under the caller's __try.
bool WorldGravity(const unsigned char* image,float* g) noexcept;
}  // namespace edf
