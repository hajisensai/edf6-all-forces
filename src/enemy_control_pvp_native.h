// Native controlled-enemy PvP adapters for EDF.dll 678CCB46.
#pragma once
#include "enemy_control_native.h"
#include "pvp_controlled.h"

namespace crew {
// Context is the caller-owned, stable NativeLease; it must outlive restoration.
// Startup only; scene reset refuses while any pending/restoring lease remains.
bool EnemyControlNativePvpInstall() noexcept;
bool EnemyControlNativePvpReset() noexcept;
bool EnemyControlNativePvpSpawnProof(const wchar_t*,controlled_pvp::SpawnProof*) noexcept;
bool EnemyControlNativePvpProfileReady(const char* className) noexcept;
bool EnemyControlNativePvpHooks(const enemy_native::NativeLease&,controlled_pvp::Hooks*) noexcept;
// Root-independent delegates also serve separately spawned native sub-objects.
bool EnemyControlNativePvpCollect(const ObjRef&,ObjRef*,unsigned,unsigned*,controlled_pvp::Emitter*,unsigned,unsigned*) noexcept;
// Append the actual NPC weapon list and its current owned keepalive beams.
bool EnemyControlNativePvpCollectHumanoidWeapons(const ObjRef&,controlled_pvp::Emitter*,unsigned,unsigned*) noexcept;
// Append one current GB2 plus its owned keepalive beam cache bindings.
bool EnemyControlNativePvpCollectBigBattery(const ObjRef&,const unsigned char*,const void*,controlled_pvp::Emitter*,unsigned,unsigned*) noexcept;
// Sealed cache callbacks do not recurse into a family or require the old owner to survive.
bool EnemyControlNativePvpCacheRetained(const controlled_pvp::Emitter&) noexcept;
controlled_pvp::Life EnemyControlNativePvpReadRetainedCache(const controlled_pvp::Emitter&,int*) noexcept;
bool EnemyControlNativePvpWriteRetainedCache(const controlled_pvp::Emitter&,int) noexcept;
controlled_pvp::Life EnemyControlNativePvpRead(const controlled_pvp::Emitter&,int*) noexcept;
bool EnemyControlNativePvpWrite(const controlled_pvp::Emitter&,int) noexcept;
bool EnemyControlNativePvpBaseline(const ObjRef&,const ObjRef&,int,int,int*,int*) noexcept;
bool EnemyControlNativePvpBaseline(const controlled_pvp::Emitter&,int,int,int*) noexcept;
// Complex boss/transport groups delegate to their typed family providers.
// Unknown skill/child paths still fail closed; startup installation stays explicit.
}
