#pragma once
#include "pvp_controlled.h"
namespace crew {
bool EnemyControlBossPvpReady() noexcept;
bool EnemyControlBossPvpSupports(const ObjRef&) noexcept;
bool EnemyControlBossPvpInstall() noexcept;
// Call after every controlled lease has restored, before disposing the scene.
bool EnemyControlBossPvpReset() noexcept;
// Before native input/actions: count=0 derives the stock meteor volley size (20..40).
bool EnemyControlBossPvpPreflight(const ObjRef&,unsigned count=0) noexcept;
bool EnemyControlBossPvpCollect(const ObjRef&,ObjRef*,unsigned,unsigned*,controlled_pvp::Emitter*,unsigned,unsigned*) noexcept;
controlled_pvp::Life EnemyControlBossPvpRead(const controlled_pvp::Emitter&,int*) noexcept;
bool EnemyControlBossPvpWrite(const controlled_pvp::Emitter&,int) noexcept;
bool EnemyControlBossPvpBaseline(const ObjRef&,const ObjRef&,int,int,int*,int*) noexcept;
bool EnemyControlBossPvpBaseline(const controlled_pvp::Emitter&,int,int,int*) noexcept;
}
