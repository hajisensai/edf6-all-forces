#pragma once
#include "crew.h"

namespace crew {
enum class SupportSquadKind { rangerSquad, rangerPlatoon };
constexpr unsigned kMaxSupportSoldiers=12;
struct SupportSquad {
    ObjRef members[kMaxSupportSoldiers]{};
    unsigned count=0;
    ObjRef leaders[3]{};
    unsigned leaderCount=0;
};
enum class SupportSpawnFailure {
    none, disabled, profile, mission, onlineReplication, transform, capacity, create, wrongClass, setup
};
bool InstallSupportSoldiers() noexcept;
// MissionStart: release old weak records, then queue the stock resources before WaitPreload.
void PreloadSupportSoldiers() noexcept;
void ResetSupportSoldiers() noexcept;
bool SupportSoldiersReady() noexcept;
// Game thread: every newly created soldier is held until its support transaction becomes Active.
// The NPC Think hook consumes this query to suppress AI intent, without freezing physics or teleporting.
bool HoldSupportSoldier(const ObjRef& soldier,bool held) noexcept;
bool SupportSoldierHeld(const void* soldier) noexcept;
SupportSpawnFailure SupportSoldierLastFailure() noexcept;
const wchar_t* SupportSoldierFailureText() noexcept;
// Game thread, after the mission's WaitPreload. Caller supplies a checked entry/transport-exit matrix.
// Genuine stock AssultSoldier + AiSoldierRifle01; no Dummy, player identity, direct HP or weapon edits.
// Output identities are borrowed; this module retains their weak control blocks until mission reset.
bool SpawnSupportSoldier(const float* worldMatrix,ObjRef* out) noexcept;
// Reliable support-network receiver only: the caller authenticates epoch/host/spec and de-duplicates IDs.
// NativeNetId is 32 bytes, generated identically on every peer; the game assigns host ownership itself.
bool ApplySupportSoldierSpawn(const float* worldMatrix,bool leader,const unsigned char* nativeNetId32,ObjRef* out) noexcept;
// The same for any support_call.h soldier resource (its stock weapon variant). `local` (no ID): a soldier this
// machine alone owns, allowed offline and to the host of a world with no other participant; clients never.
bool ApplySupportSoldierResource(const float* worldMatrix,std::uint32_t resource,const unsigned char* nativeNetId32,
                                 bool local,ObjRef* out) noexcept;
// The same, but an ID-bearing soldier is not registered yet: the caller seats it first (a crew made aboard an aircraft
// in the air) and then registers it with RegisterSupportObject(soldier, id); on failure it deletes it.
bool CreateSupportSoldierUnregistered(const float* worldMatrix,std::uint32_t resource,const unsigned char* nativeNetId32,
                                      bool local,ObjRef* out) noexcept;
bool DeriveSupportSoldierNetId(const void* registeredAnchor,unsigned ordinal,unsigned char* out32) noexcept;
// Register a freshly created canonical support object (soldier/aircraft/ground vehicle) on this peer.
// Never call twice; the reliable spawn transaction owns validation and rollback on failure.
bool RegisterSupportObject(const void* object,const unsigned char* nativeNetId32) noexcept;
bool ReadNativeObjectId(const unsigned char* object,unsigned char* out32) noexcept;
// Establish a squad among our current soldiers, or let one follow a live player after delivery.
// Apply on each peer after all members exist; arbitrary script-NPC leaders are rejected.
bool FollowSupportSoldier(const ObjRef& soldier,const ObjRef& leader) noexcept;
// Four rangers per squad; platoon = three separate four-person squads. Exactly one matrix per member.
// The caller chooses and validates ALL positions. Entire transaction rolls back on failure.
bool SpawnSupportSquad(SupportSquadKind kind,const float* worldMatrices,unsigned matrixCount,SupportSquad* out) noexcept;
// Deletes only objects this module created in the current mission, never arbitrary mission NPCs.
bool DeleteSupportSoldier(const ObjRef& soldier) noexcept;
}
