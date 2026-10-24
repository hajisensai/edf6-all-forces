#pragma once
#include "pvp_controlled.h"

namespace crew {
// Startup-only hooks. Reset releases scene-owned weak identities without removing hooks.
bool EnemyControlTransportPvpInstall() noexcept;
void EnemyControlTransportPvpReset() noexcept;
bool EnemyControlTransportPvpReady() noexcept;
bool EnemyControlTransportPvpSupports(const ObjRef&) noexcept;
// Accepted TimeShip pod creation: prepare before native construction, adopt each body before registration.
// A zero token denotes ordinary unleased AI. Finish releases only unused budget and is idempotent.
bool EnemyControlTransportPvpCanPrepareCapsules(const ObjRef& parent,unsigned count) noexcept;
bool EnemyControlTransportPvpPrepareCapsules(const ObjRef& parent,unsigned count,std::uint64_t* token) noexcept;
bool EnemyControlTransportPvpAdoptCapsule(std::uint64_t token,const ObjRef& parent,const ObjRef& child) noexcept;
void EnemyControlTransportPvpFinishCapsules(std::uint64_t token) noexcept;
bool EnemyControlTransportPvpCollect(const ObjRef&,ObjRef*,unsigned,unsigned*,controlled_pvp::Emitter*,unsigned,unsigned*) noexcept;
controlled_pvp::Life EnemyControlTransportPvpRead(const controlled_pvp::Emitter&,int*) noexcept;
bool EnemyControlTransportPvpWrite(const controlled_pvp::Emitter&,int) noexcept;
bool EnemyControlTransportPvpBaseline(const ObjRef&,const ObjRef&,int,int,int*,int*) noexcept;
bool EnemyControlTransportPvpBaseline(const controlled_pvp::Emitter&,int,int,int*) noexcept;
}
