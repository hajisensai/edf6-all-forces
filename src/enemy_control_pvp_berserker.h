#pragma once
#include "pvp_controlled.h"
namespace crew {
bool EnemyControlBerserkerPvpReady() noexcept;
bool EnemyControlBerserkerPvpSupports(const ObjRef&) noexcept;
bool EnemyControlBerserkerPvpInstall() noexcept;
bool EnemyControlBerserkerPvpReset() noexcept;
// Accepted walking actions enter this only for an actual native execution/replay, never while a guest defers.
bool EnemyControlBerserkerPvpExecuteParts(void* root,int index,void(__fastcall* actualOriginal)(void*,int)) noexcept;
// Pure validation runs before and after native activation, including online leases with PvP mode disabled.
bool EnemyControlBerserkerPvpExecutePartsValidated(void* root,int index,void(__fastcall* original)(void*,int),bool(*validate)(void*) noexcept,void* context) noexcept;
bool EnemyControlBerserkerPvpPreflight(const ObjRef&,unsigned count=0) noexcept;
bool EnemyControlBerserkerPvpCollect(const ObjRef&,ObjRef*,unsigned,unsigned*,controlled_pvp::Emitter*,unsigned,unsigned*) noexcept;
controlled_pvp::Life EnemyControlBerserkerPvpRead(const controlled_pvp::Emitter&,int*) noexcept;
bool EnemyControlBerserkerPvpWrite(const controlled_pvp::Emitter&,int) noexcept;
bool EnemyControlBerserkerPvpBaseline(const ObjRef&,const ObjRef&,int,int,int*,int*) noexcept;
bool EnemyControlBerserkerPvpBaseline(const controlled_pvp::Emitter&,int,int,int*) noexcept;
}
