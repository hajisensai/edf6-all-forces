// Complete host-accepted Walking detached-part groups. Game thread only.
#pragma once
#include "enemy_control_native.h"
#include "enemy_control_action_types.h"
namespace crew::enemy_native {
bool InstallWalkingPartsAcceptedActions() noexcept;
bool WalkingPartsControlReady() noexcept;
// The PvP caller guards remain the sole owners of their native callsites.
// Outside an active complete-group scope these bridges call original unchanged.
// A refused constructor returns null ONLY to the sole PvP caller guard, which
// completes/deletes a real native object before returning to the game caller.
using WalkingPartCtorFn=void*(__fastcall*)(void*,const void*);
void* InvokeWalkingPartConstructor(void* storage,const void* descriptor,WalkingPartCtorFn original);
using WalkingPartFallbackFn=void*(__fastcall*)(void*,void*,const float*,void*);
void* InvokeWalkingPartFallback(void* manager,void* outWeak,const float* matrix,void* descriptor,WalkingPartFallbackFn original);
bool BeforeWalkingParts(const NativeLease&,bool authority,Input*) noexcept;
bool AfterWalkingParts(const NativeLease&) noexcept;
bool CaptureWalkingParts(const NativeLease&,void*,std::size_t,std::size_t*) noexcept;
bool OwnsWalkingParts(const void*,std::size_t) noexcept;
enemy_control::ActionResult ApplyWalkingParts(const NativeLease&,const void*,std::size_t) noexcept;
bool QuiesceWalkingParts(const NativeLease&) noexcept;
bool WalkingPartsEmpty(const NativeLease&) noexcept;
bool AbortWalkingParts(const NativeLease&) noexcept;
bool RestoreWalkingParts(const NativeLease&) noexcept;
}
