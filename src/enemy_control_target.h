#pragma once
#include "crew.h"

namespace crew::enemy_target {
// This ray belongs to the authenticated controller's current view. A network host
// reconstructs it from the controlled body and accepted angles; no P1 fallback.
struct Ray { float origin[3]{},direction[3]{};float range=6000.0f; };
// Borrowed until Assign. lock is a real LockPoint weak pair, not a GameObject.
struct Selection {
    ObjRef actor{},lock{};
    float point[3]{};
    int lockIndex=-1;
    unsigned char nativeId[32]{};
    // Unreleased old pins survive a failed replacement until TryClear retries.
    ObjRef retiredActor{},retiredLock{};
};
bool Pick(const ObjRef& owner,const ObjRef& controlled,const Ray&,Selection*) noexcept;
bool Resolve(const ObjRef& owner,const ObjRef& controlled,const unsigned char fullNativeId[32],const Ray&,Selection*) noexcept;
// Missing/dead/illegal focus clears the ID, and is a successful empty selection.
// False means the controller/body/ray itself is invalid. Never revoke a lease.
bool Normalize(const ObjRef& owner,const ObjRef& controlled,unsigned char fullNativeId[32],const Ray&,Selection* =nullptr) noexcept;
bool Valid(const ObjRef& owner,const ObjRef& controlled,const Ray&,const Selection&) noexcept;
// Preflight failure leaves the record unchanged. A partially completed native
// release keeps every remaining pin in the record for a later cleanup attempt.
bool TryAssign(Selection& retained,const Selection& next) noexcept;
bool TryClear(Selection&) noexcept;
void Assign(Selection& retained,const Selection& next) noexcept;
void Clear(Selection&) noexcept;
// Copies the already selected real native LockPoint and increments its weak count.
// `out` must be empty storage; its recipient owns the returned weak reference.
bool CopyLock(const Selection&,void* out16) noexcept;

// The lease preserves both native target weak pairs independently. The native
// forced/current setter suppresses stock AI retargeting while the lease is held.
struct NativeLease {
    ObjRef root{},savedCurrent{},savedForced{},applied{};
    ObjRef retiredCurrent{},retiredForced{},retiredApplied{};
    bool active=false;
};
bool Install() noexcept;
bool Begin(const ObjRef& controlled,NativeLease*) noexcept;
bool Apply(NativeLease&,const Selection*) noexcept;
bool Restore(NativeLease&) noexcept;
}
