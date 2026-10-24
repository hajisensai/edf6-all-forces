#pragma once
#include "crew.h"
#include <cstdint>

namespace crew::controlled_pvp {
// A controlled enemy is an ordinary GameObject tree, never a VehicleBase/seat surrogate.
constexpr unsigned kMaxMembers=256,kMaxEmitters=512;
struct Lease { std::uint64_t id=0; };
enum class EmitterKind : unsigned { weaponBase=1,indirectFireControl,gunBattery,ownerTeamMirror,liveOwnerTeam,liveOwnerLastTeam,
    ownerTeamCache,ownerLastTeamCache };
enum class Life : unsigned { live,gone,invalid };
struct Emitter {
    EmitterKind kind=EmitterKind::weaponBase;
    ObjRef member{};               // exact live GameObject owning this real emitter
    const void* object=nullptr;     // typed native emitter or private team mirror; never treated as a GameObject
    // liveOwnerTeam/liveOwnerLastTeam prove per-shot derivation: callbacks validate the real emitter/owner,
    // read the live owner field and perform no emitter write. Native closed state remains -1.
    // ownerTeamCache/ownerLastTeamCache are real typed caches: apply the member's current 314/318,
    // including closed -1, and restore the cache's separately captured original value on release.
    std::uint64_t incarnation=0;    // adapter changes it whenever a binding is rebuilt/reuses an address
    // Independently surviving SceneObject storage, e.g. a held BulletBase cache. Never SetTeam this object.
    // Empty means the scalar belongs to the member's own lifetime (native body/private component field).
    ObjRef storage{};
};
struct Hooks {
    void* user=nullptr;
    // Complete read-only, bounded root+native-child-tree and typed-emitter enumeration. Include root exactly once.
    // Fail for every unsupported firing path; an empty list must prove no emission or that every shot reads its real owner team synchronously without a cached team.
    bool (*collect)(void*,const ObjRef& root,ObjRef* members,unsigned memberCapacity,unsigned* memberCount,
                    Emitter* emitters,unsigned emitterCapacity,unsigned* emitterCount) noexcept=nullptr;
    // Revalidate current typed container membership AND the actual native owner before any emitter read/write.
    // gone means proven destruction/removal; unreadable/ambiguous/unknown ownership is invalid, never gone.
    Life (*readEmitter)(void*,const Emitter&,int* team) noexcept=nullptr;
    bool (*writeEmitter)(void*,const Emitter&,int team) noexcept=nullptr;
    // A new part/emitter created while PVP is applied may already contain the inherited temporary team.
    // Its typed adapter must prove its baseline; missing/unknown baseline fails closed rather than saving that team.
    bool (*memberBaseline)(void*,const ObjRef& root,const ObjRef& member,int rootTeam,int rootLastTeam,
                           int* team,int* lastTeam) noexcept=nullptr;
    bool (*emitterBaseline)(void*,const Emitter&,int ownerTeam,int ownerLastTeam,int* team) noexcept=nullptr;
};
enum class Result : unsigned { idle,ready,pvpPending,unsupported,invalidIdentity,capacity,nativeFailure,restoring };
struct Status {
    Result result=Result::idle;
    int controlledTeam=-1;         // actual root +0x314, never a requested/HUD-only value
    bool followsOwnerTeam=false;   // true only after the complete current tree and emitters were verified
    bool restoring=false;
    unsigned members=0,emitters=0;
};
// All calls are game-thread only. Hook functions/context outlive every nonzero lease, including failed restores.
// Probe never changes gameplay team/skills/network state. Typed providers may retain idempotent, bounded
// weak identity guards for ABA safety. Core calls Acquire BEFORE changing target pads, intent, vptrs or native adapter state.
bool Probe(const ObjRef& root,const Hooks& hooks) noexcept;
// Source PUID is supplied only after Core/Coop authentication; this layer never edits User/PUID/network identity.
// Even when false, a nonzero out lease owns restoration work; Core must retain it and call Restore until true.
bool Acquire(const ObjRef& root,const char* authenticatedSourcePuid,const Hooks& hooks,Lease* out) noexcept;
// Stable mode-off restores the real native baseline while retaining control. A pending/unknown owner refuses work.
bool Frame(const Lease& lease) noexcept;
// Restores all surviving fields before releasing weak identities. Failure retains the complete pending lease.
bool Restore(Lease* lease) noexcept;
bool ReadStatus(const Lease& lease,Status* out) noexcept;
// Typed dynamic-part baselines may depend on an existing hull/sibling rather than the selected controlled root.
// Lookup is read-only and succeeds only for that exact active lease root and a previously captured full member identity.
bool ReadBaseline(const ObjRef& root,const ObjRef& member,int* team,int* lastTeam) noexcept;
// Native private mirrors may otherwise overwrite closed parts from an untagged parent. Only a fully applied,
// stable current-owner PVP lease can answer; mode-off, pending and restoring return false.
bool ReadAppliedTeam(const ObjRef& member,int* team) noexcept;
// Unique exact member across live/restoring leases; independent spawns can recover their real native baseline.
bool ReadBaselineForMember(const ObjRef& member,int* team,int* lastTeam) noexcept;
// Exact full typed binding, unique across live/restoring leases. Does not read a possibly retagged native cache.
bool ReadEmitterBaseline(const Emitter& binding,int* team) noexcept;
// A resource proof supplies its actual typed subtree collector and conservative per-instance bounds.
// Hook contexts are immutable/static and outlive the control lease. No resource path or claimed type is authority.
struct SpawnProof {
    Hooks hooks{};unsigned maxMembers=0,maxEmitters=0;
    // Optional real producer initialization, only after a newborn passes the read-only class/tree/overlap gate.
    // Runs during Adopt, before its real baseline is captured; never during hover/Probe or whole-wave preflight.
    // Must be idempotent and bounded. A failure leaves deletion of the unpublished newborn to its creator.
    bool (*prepareInstance)(void* hooksUser,const ObjRef& newborn) noexcept=nullptr;
};
bool CanPrepareSpawns(const ObjRef& parent,const SpawnProof&,unsigned count) noexcept;
bool PrepareSpawns(const ObjRef& parent,const SpawnProof&,unsigned count,std::uint64_t* token) noexcept;
// Only the authenticated accepted-action consumer may use this for a closing actor.
// The known full parent identity and baseline must still exist; a held logical-death object is allowed.
// Use the same AdoptSpawn/FinishSpawns token APIs after real baseline setup, before registration/publication.
bool CanPrepareAcceptedSpawns(const ObjRef& parent,const SpawnProof&,unsigned count) noexcept;
bool PrepareAcceptedSpawns(const ObjRef& parent,const SpawnProof&,unsigned count,std::uint64_t* token) noexcept;
// Called after native creation/real baseline setup, before activation or network publication. Does not delete.
bool AdoptSpawn(std::uint64_t token,const ObjRef& parent,const ObjRef& child) noexcept;
// Return only the unused reservation; already adopted objects remain leased until the controller restores.
void FinishSpawns(std::uint64_t token) noexcept;
// Birth/ability guards use this only after preventing an untracked newborn from becoming active/published.
// Ordinary AI without a controlling lease is untouched; Core observes Frame=false and completes restoration.
bool FaultForMember(const ObjRef& member,Result reason=Result::nativeFailure) noexcept;
// Exact ownership includes unresolved baseline, PVP-pending and restoring. False alone means ordinary unleased AI.
bool OwnsMember(const ObjRef& member) noexcept;
// Scene-owned native birth ledgers cannot be cleared until every pending/restoring lease releases.
bool HasLeases() noexcept;
// Exact member is still under a temporary write epoch, including partial failure and restoring.
bool HasTemporaryTeam(const ObjRef& member) noexcept;
// Borrowed full identity of the unique owning control root, including dead/pending/restoring members.
// Game-thread only; valid while the containing lease exists. Does not acquire a new weak reference.
bool ReadControlRoot(const ObjRef& member,ObjRef* root) noexcept;
}
