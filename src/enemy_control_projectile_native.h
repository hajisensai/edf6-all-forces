// Semantic BulletCreateInfo snapshots. Every field is encoded explicitly by
// its owning accepted-outcome codec; sizeof(sample) is never a wire format.
// Trusted game thread only. Identity lookup is a read-only snapshot; the public
// resolver acquires only the final strong reference handed to its caller.
#pragma once
#include "enemy_control_native.h"
namespace crew::enemy_native {
inline constexpr unsigned kProjectileScalar32[]={0x30,0x50,0x54,0x58,0x5C,0x90,0x94,0x98,0x9C,0xA0,0xA4,0xA8,0xAC,0xB0,0xC0,0xC4,0xC8,0xCC,0xD0,0xD4,0xD8,0xDC,0xE0,0x108,0x10C,0x110,0x114,0x118,0x170,0x174};
inline constexpr unsigned kProjectileScalar8[]={0x28,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0x11C};
inline constexpr unsigned kProjectileWords=sizeof(kProjectileScalar32)/sizeof(kProjectileScalar32[0]);
inline constexpr unsigned kProjectileBytes=sizeof(kProjectileScalar8)/sizeof(kProjectileScalar8[0]);
struct NativeProjectileSample {
    float matrix[16]{},muzzleMatrix[16]{};
    std::uint32_t scalar[kProjectileWords]{},random=0,shot=0,flags=0;
    unsigned char bytes[kProjectileBytes]{};std::uint16_t scalar178=0;
    unsigned char target[32]{},extra[32]{};std::uint32_t lock=0;
    bool hasMuzzle=false,extraObject=false;unsigned char resourceMask=0;
};
// Persistent local ownership ledger. Never serialize this structure or move it
// while busy. A failed/uncertain native cleanup retains its descriptor, pins and
// created result so the controller cannot forget the lease or spawn a retry.
struct NativeProjectileJournal {
    alignas(16) unsigned char descriptor[0x1E0]{};
    alignas(16) float matrix[16]{};
    void* objects[3]{};const void* controls[3]{};
    const void* ownerObject=nullptr;const void* ownerControl=nullptr;
    void* created=nullptr;unsigned heldMask=0;
    bool made=false,busy=false,copyEntered=false,createEntered=false,cleanupEntered=false,uncertain=false;
};
// Creation and descriptor cleanup are independent outcomes. A real native
// object must be adopted by its caller even when cleanup remains in journal.
// cleanupComplete never grants permission to repeat a successful creation.
struct NativeProjectileOutcome {void* created=nullptr;bool cleanupComplete=false;};
bool NativeProjectileJournalEmpty(const NativeProjectileJournal&) noexcept;
bool RestoreNativeProjectileJournal(NativeProjectileJournal&) noexcept;
// Lock-point adapters for native maker parameters which share the same target
// identity contract but not the complete BulletCreateInfo descriptor layout.
bool CaptureNativeProjectileLock(const void* weak16,unsigned char id[32],std::uint32_t* ordinal) noexcept;
// A nonnull result owns one strong reference until the matching release.
// Actor resolution uses the same complete-ID lifecycle and strong ownership.
// Release it through ReleaseNativeProjectileLock (the common native control ABI).
bool ResolveNativeProjectileActor(const unsigned char id[32],void** object,const void** control) noexcept;
bool ResolveNativeProjectileLock(const unsigned char id[32],std::uint32_t ordinal,void** object,const void** control) noexcept;
// consumed distinguishes a preflight refusal from an entered release. If false
// is returned after consumed=true, retain an uncertain journal and NEVER retry.
bool ReleaseNativeProjectileLock(void* object,const void* control,bool* consumed=nullptr) noexcept;
// Caller proves the template/factory belongs to its stable asset/part ordinal.
// Capture rejects a callback changing nonnumeric asset resources/variants.
bool CaptureNativeProjectileSample(const NativeLease&,const void* localTemplate,
    const float* actualMatrix,const void* actualDescriptor,NativeProjectileSample*) noexcept;
bool ValidNativeProjectileSample(const NativeProjectileSample&) noexcept;
bool CanReplayNativeProjectileSample(const NativeLease&,void* localFactory,const void* localTemplate,
    float* persistentMuzzleMatrix,const NativeProjectileSample&) noexcept;
using NativeProjectileCreateFn=void*(*)(void* user,const float* matrix,void* preparedDescriptor) noexcept;
bool NativeProjectileDependenciesReady(const NativeLease&,const NativeProjectileSample&) noexcept;
bool CanPrepareNativeProjectileSample(const NativeLease&,const void* localTemplate,
    float* persistentMuzzleMatrix,const NativeProjectileSample&) noexcept;
// A local-only callback receives a complete native-owned descriptor for this
// synchronous call. Normal cleanup uses native 100C90; an entered cleanup
// failure retains the persistent journal instead of losing ownership.
// Pointer wrappers return null until cleanup finishes, which alone does not
// prove no object was created. New lifecycle owners use the explicit outcome.
void* WithNativeProjectileSample(const NativeLease&,const void* localTemplate,
    float* persistentMuzzleMatrix,const NativeProjectileSample&,NativeProjectileCreateFn,void* user) noexcept;
void* WithNativeProjectileSampleInPlace(NativeProjectileJournal&,const NativeLease&,const void* localTemplate,
    float* persistentMuzzleMatrix,const NativeProjectileSample&,NativeProjectileCreateFn,void* user) noexcept;
NativeProjectileOutcome WithNativeProjectileOutcomeInPlace(NativeProjectileJournal&,const NativeLease&,const void* localTemplate,
    float* persistentMuzzleMatrix,const NativeProjectileSample&,NativeProjectileCreateFn,void* user) noexcept;
// Legacy wrappers use a bounded persistent internal ledger, never a stack
// descriptor. Its owner must include these gates in Empty/Restore as well.
bool NativeProjectileJournalsEmpty(const NativeLease&) noexcept;
bool RestoreNativeProjectileJournals(const NativeLease&) noexcept;
// Nonnull means the original 1194280 factory registered a real native object.
// The caller still owns its original post-create cache/list/network callbacks.
void* ReplayNativeProjectileSampleInPlace(NativeProjectileJournal&,const NativeLease&,void* localFactory,const void* localTemplate,
    float* persistentMuzzleMatrix,const NativeProjectileSample&) noexcept;
NativeProjectileOutcome ReplayNativeProjectileOutcomeInPlace(NativeProjectileJournal&,const NativeLease&,void* localFactory,const void* localTemplate,
    float* persistentMuzzleMatrix,const NativeProjectileSample&) noexcept;
void* ReplayNativeProjectileSample(const NativeLease&,void* localFactory,const void* localTemplate,
    float* persistentMuzzleMatrix,const NativeProjectileSample&) noexcept;
}
