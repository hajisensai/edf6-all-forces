#pragma once
#include "pvp_protocol.h"

namespace crew {
struct ObjRef;
// Game thread only; native references are never exported to the network or the HUD.
// Prepare never changes a team. Apply verifies the complete current-world roster,
// applies through native SetTeam outside enumeration and rolls back on failure.
bool InstallPvpNative() noexcept;
bool PvpNativePrepare(const pvp_protocol::State& state) noexcept;
bool PvpNativeApply(const pvp_protocol::State& state) noexcept;
bool PvpNativeTick() noexcept;
void ResetPvpNative() noexcept;
bool PvpNativeAttachSupport(const char* puid,const ObjRef* objects,unsigned count,int team) noexcept;
void PvpNativeForgetSupport(const ObjRef& object) noexcept;
void PvpNativeClearSupportOwners() noexcept;
}
