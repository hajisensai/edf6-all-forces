#pragma once
#include <cstdint>
#include "support_entry.h"
namespace crew {
struct ObjRef;
struct SupportAircraft {
    int jet=-1,heli=-1,count=0;
    std::uint32_t fuelSeconds=0;
    bool follow=false;
};
int SupportAirCallCount() noexcept;
const wchar_t* SupportAirCallName(int index) noexcept;
bool SupportAircraftSpec(int catalog,SupportAircraft* out) noexcept;
support::Refusal PlanAirSupport(int catalog,const float* target,const float* observer,support::Route* route) noexcept;
// Prepare at a validated landing strip/pad with empty seats. Activation never creates a rider.
unsigned char* PrepareSupportAircraft(const SupportAircraft&,const float* matrix) noexcept;
bool ActivateSupportAircraft(unsigned char*,const SupportAircraft&,const float* target) noexcept;
bool DeleteSupportAircraft(const ObjRef&) noexcept;
}
