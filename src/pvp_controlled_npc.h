// Typed SoldierBase/WeaponBase spawn proof for audited ported NPC resources.
#pragma once
#include "pvp_controlled.h"
namespace crew {
// Neutral typed emitter operations for ordinary player/support leases as well.
// Collector appends all-or-nothing and verifies the exact native owner/source weak.
// It does not inspect a Soldier roster, query PvP state, or acquire a control lease.
// Callers retain owner/storage weak identities and original scalar baselines themselves.
// Exact original primary WeaponBase tables only (29 audited classes, 41 slots each).
// This closes Weapon214 and its bounded F20 retained storage, not unrelated support/IFC lifecycles.
// Ordinary retained projectiles preserve their shot snapshot; only native mode-1 keepalive with stop bit 14 clear follows.
// Unknown classes/typed storage refuse; native list count above 512 or output exhaustion refuse.
bool PvpNativeWeaponEmitters(const ObjRef& owner,const void* weapon,controlled_pvp::Emitter*,unsigned capacity,unsigned* count) noexcept;
controlled_pvp::Life PvpNativeReadWeaponEmitter(const controlled_pvp::Emitter&,int*) noexcept;
bool PvpNativeWriteWeaponEmitter(const controlled_pvp::Emitter&,int) noexcept;

// Static contexts only. maxHeld is the largest configured FireCount of one weapon,
// proved by the shared WeaponBase keepalive-before-create branch, never AmmoCount.
// Exact path/hash registration belongs to the single generated ported manifest.
bool PvpControlledNpcProof(const char* configuredNativeClass,unsigned maxWeapons,unsigned maxHeld,
                           controlled_pvp::SpawnProof*) noexcept;
}
