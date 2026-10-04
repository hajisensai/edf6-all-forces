// EDF.dll layout the core modules (crew, heli, ground, hud) share: one definition of each offset, vtable
// and signature they used to repeat file by file. All addresses are RVAs into EDF.dll TimeDateStamp
// 0x678CCB46; see docs/re-notes.md. Included only by the core modules' .cpp files.
#pragma once
#include <cstddef>
#include <cstdint>

namespace crew {
// The vehicle's HP (as jet.cpp reads it).
constexpr std::size_t kHpMax=0x2F4,kHp=0x2F8;
// A seat's weapons: holder array / count; a holder's weapon (docs/aim-line-re.md).
constexpr std::size_t kSeatWeapons=0xC8,kSeatWeaponCount=0xD8,kHolderWeapon=0x10;
// The vehicle's weapon holders (the triggers slot 57 / slot 5 pull): veh+0x638, stride 0x48, count at
// veh+0x648; each: +8 the weapon's weak_ptr control block, +0x10 the weapon.
constexpr std::size_t kHolders=0x638,kHolderCount=0x648,kHolderStride=0x48,kHolderCtrl=0x8;
// A weapon: LockonType, round speed (m/frame), gravity factor, round life (frames), ammo.
constexpr std::size_t kWeaponLockon=0x6B0,kWeaponSpeed=0x894,kWeaponAlive=0x898,kWeaponGravity=0x8E0,kWeaponAmmo=0xBE8;
constexpr std::int32_t kHoming=1;     // LockonType 1: a homing missile
constexpr std::int32_t kTeamFriend=2;
// Deleting a game object (the manager's Delete, as jet.cpp / subcarrier.cpp call it) and its entry bytes.
constexpr unsigned kDelete=0x118A1B0;
constexpr unsigned char kDeleteSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x60,0x48};
// The vehicle classes whose vtables two modules test (crew.cpp kClasses, heli.cpp kHeliTypes, ground.cpp).
constexpr unsigned kVt506=0x17DB238,kVt409=0x17DEF98,kVt410=0x17DF338,kVtHeliBase=0x17DF790,kVt502=0x17DA028;
}  // namespace crew
