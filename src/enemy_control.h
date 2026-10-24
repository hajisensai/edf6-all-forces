#pragma once
#include "enemy_control_types.h"

namespace crew { struct ObjRef; }

namespace crew {
// Install/Reset/Frame run only on the game thread. Reset restores each live native
// object before releasing retained weak controls; destroyed/reused addresses are never
// dereferenced through the stale object pointer.
bool InstallEnemyControl() noexcept;
void ConfigureEnemyControl(bool enabled) noexcept;
void ResetEnemyControl() noexcept;
void EnemyControlFrame() noexcept;
// The map passes its own local player; split-screen never falls back to P1.
bool EnemyControlDescribe(const ObjRef& requester,const ObjRef& target,enemy_control::Snapshot*) noexcept;
// Cheap game-thread family probe for picking on the map: a registered plugin controller
// matches it or its exact native class has an admitted input profile. Admission itself
// (skills, network, requester) is still EnemyControlDescribe's.
bool EnemyControlPickable(const void* object) noexcept;
bool EnemyControlReadSnapshot(const ObjRef& requester,enemy_control::Snapshot*) noexcept;
bool EnemyControlReadSnapshotForView(std::uintptr_t ownerHumanKey,enemy_control::Snapshot*) noexcept;
// Any thread: `human` (a player's object or its SoldierBase) controls an enemy now, so its stock HUD gives way to the
// enemy's (stockgauge.cpp). Only the published active lease of that exact owner counts.
bool EnemyControlReplacesPlayerHud(const void* human) noexcept;
unsigned EnemyControlAbilities(const enemy_control::Snapshot&,unsigned first,enemy_control::Ability*,unsigned capacity) noexcept;
bool EnemyControlRequest(const enemy_control::Snapshot&) noexcept;
bool EnemyControlSelectAbility(const enemy_control::Snapshot&,std::uint32_t ability) noexcept;
bool EnemyControlSetAbilityHeld(const enemy_control::Snapshot&,bool held) noexcept;
// The M card holds this one attack directly (0 lets go); no selection is changed.
bool EnemyControlHoldAbility(const enemy_control::Snapshot&,std::uint32_t ability) noexcept;
bool EnemyControlExit(const enemy_control::Snapshot&) noexcept;
// Called before the local Soldier's native control input. Its physics/lifetime and
// identity stay native; only input competing with an active enemy lease is blocked.
bool EnemyControlSuppressHuman(const unsigned char* human) noexcept;
// True while this core actually owns the native input table, including neutral preparation.
bool EnemyControlTargetActive(const void* target) noexcept;
// True only for this exact retained control session. Its immutable online flag
// remains known during closing/fault cleanup; an unknown global probe is no fallback.
bool EnemyControlNetworkContext(const void* target,bool* online) noexcept;
bool EnemyControlCurrentInput(const void* target,enemy_control::Input* out) noexcept;
// Same canonical accepted aim as the camera/skill command; no fabricated target object.
bool EnemyControlWeaponAim(const void* target,float out[3]) noexcept;
// True includes a legal empty target/lock. Actor is borrowed; a nonempty native
// lock pair retains its actual weak control for the native recipient.
bool EnemyControlAimTarget(const void* root,ObjRef* out) noexcept;
bool EnemyControlWeaponLock(const void* root,void* out16) noexcept;
// Borrowed canonical homing identity; no weak count change. True+zero/-1 is empty.
bool EnemyControlWeaponLockIdentity(const void* root,unsigned char outId32[32],int* lockIndex) noexcept;
bool EnemyControlOwnsTargetSelector(const void* root,const void* function) noexcept;
// Game-thread, bounded borrowed native roots for typed child membership checks.
unsigned EnemyControlWeaponAimRoots(const void** out,unsigned capacity) noexcept;
// Game-thread only; borrowed for immediate map use, never retained by drawing.
const unsigned char* EnemyControlBodyForMap(const unsigned char* human) noexcept;
// Camera reads no raw input and changes no native player/body orientation. Its view
// is the controlled body's independent camera, for that source player only.
bool EnemyControlCamera(const unsigned char* human,const float* wasEye,const float* wasLook,float* eye,float* look) noexcept;

// Authenticated network bridge: these functions do not authenticate PUIDs themselves.
// The transport must resolve both IDs and compare the real source player's PUID at
// every begin/input request. Native enemy network ownership stays on its original host.
struct EnemyControlNetwork {
    bool (*request)(const enemy_control::Snapshot&) noexcept=nullptr;
    bool (*input)(const enemy_control::Snapshot&,const enemy_control::Input&) noexcept=nullptr;
    bool (*stop)(const enemy_control::Snapshot&) noexcept=nullptr;
    bool (*online)() noexcept=nullptr;
};
void ConfigureEnemyControlNetwork(const EnemyControlNetwork&) noexcept;
bool EnemyControlPrepare(const ObjRef& requester,const ObjRef& target,std::uint64_t profileHash,enemy_control::Reason*) noexcept;
bool EnemyControlBegin(const ObjRef& requester,const ObjRef& target,std::uint64_t profileHash,std::uint64_t lease,bool local,const char* authenticatedPuid=nullptr) noexcept;
bool EnemyControlActivate(std::uint64_t lease) noexcept;
bool EnemyControlNormalizeNetworkInput(std::uint64_t lease,enemy_control::Input*) noexcept;
bool EnemyControlValidateInput(std::uint64_t lease,const enemy_control::Input&) noexcept;
bool EnemyControlApply(std::uint64_t lease,const enemy_control::Input&) noexcept;
bool EnemyControlRestore(std::uint64_t lease) noexcept;
bool EnemyControlLeaseLive(std::uint64_t lease) noexcept;
}
