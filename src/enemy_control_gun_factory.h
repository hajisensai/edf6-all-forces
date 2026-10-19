// Process-local dispatch at proven native factory call sites. Provider callbacks
// and call-site addresses are never serialized. Family codecs own their exact
// accepted outcomes, native post-create lifecycle and reliable ACK semantics.
#pragma once
#include <cstddef>
#include <cstdint>
namespace crew::enemy_native {
struct NativeGunFactoryProvider {
    std::uint32_t tag=0;
    bool (*Owns)(const void* holder) noexcept=nullptr;
    void (*Invoke)(void* holder,const float* matrix,std::uint64_t* seed)=nullptr;
    // Must fault only this owned emitter/lease; cached accepted events stay drainable.
    void (*Fault)(const void* holder) noexcept=nullptr;
};
bool RegisterNativeGunFactoryProvider(const NativeGunFactoryProvider&) noexcept;
bool InstallNativeGunFactoryDispatcher() noexcept;
bool NativeGunFactoryProviderReady(const NativeGunFactoryProvider&) noexcept;
// Calls the admitted original function, bypassing provider selection for replay.
void InvokeOriginalNativeGunFactory(void* holder,const float* matrix,std::uint64_t* seed);
struct NativeUnitFactoryProvider {
    std::uint32_t tag=0;
    bool (*Owns)(const void* factory,const void* descriptor) noexcept=nullptr;
    void* (*Invoke)(void* manager,const float* matrix,void* factory,void* descriptor)=nullptr;
    // Must preserve its private journal and signal this emitter/lease failure.
    void (*Fault)(const void* factory,const void* descriptor) noexcept=nullptr;
};
bool RegisterNativeUnitFactoryProvider(const NativeUnitFactoryProvider&) noexcept;
// Only a direct E8/E9 caller of the original 1194280 is admitted. Re-registering
// a site is idempotent. Unrelated callers/providers continue the original engine.
bool RegisterNativeUnitFactoryCallsite(std::uint32_t rva) noexcept;
bool InstallNativeUnitFactoryDispatcher() noexcept;
bool NativeUnitFactoryProviderReady(const NativeUnitFactoryProvider&) noexcept;
bool NativeUnitFactoryCallsiteReady(std::uint32_t rva) noexcept;
void* InvokeOriginalNativeUnitFactory(void* manager,const float* matrix,void* factory,void* descriptor);
// For a caller already owned by a verified native guard: delegate through the
// same provider selection instead of installing a competing hook on that site.
void* DispatchNativeUnitFactory(void* manager,const float* matrix,void* factory,void* descriptor);
// GunBattery2's existing-beam acceptance edge. This callback grants the native
// refresh/keepalive; its owning codec supplies the stable muzzle/cycle and
// actual transform parameters. It is not a new projectile factory.
struct NativeBeamRefreshProvider {
    std::uint32_t tag=0;
    bool (*Owns)(const void* bullet) noexcept=nullptr;
    void (*Invoke)(void* bullet)=nullptr;
    void (*Fault)(const void* bullet) noexcept=nullptr;
};
bool RegisterNativeBeamRefreshProvider(const NativeBeamRefreshProvider&) noexcept;
bool InstallNativeBeamRefreshDispatcher() noexcept;
bool NativeBeamRefreshProviderReady(const NativeBeamRefreshProvider&) noexcept;
void InvokeOriginalNativeBeamRefresh(void* bullet);

// The native continuous-beam consumer (state == bullet+140). Provider Invoke
// receives bullet, not state; it owns accepted active/stop and matrix state.
struct NativeBeamStepProvider {
    std::uint32_t tag=0;
    bool (*Owns)(const void* bullet) noexcept=nullptr;
    void (*Invoke)(void* bullet,void* dt)=nullptr;
    void (*Fault)(const void* bullet) noexcept=nullptr;
};
bool RegisterNativeBeamStepProvider(const NativeBeamStepProvider&) noexcept;
bool InstallNativeBeamStepDispatcher() noexcept;
bool NativeBeamStepProviderReady(const NativeBeamStepProvider&) noexcept;
void InvokeOriginalNativeBeamStep(void* bullet,void* dt);

}
