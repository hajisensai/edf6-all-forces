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
const wchar_t* SupportAirCallKey(int index) noexcept;
bool SupportAircraftSpec(int catalog,SupportAircraft* out) noexcept;
// `count`: the aircraft this call brings; every one's formation slot (support_entry.h AirFormationSlot) is checked.
// route->from: the lead's place IN THE AIR at the edge, at the route's height; route->heading toward the target.
support::Refusal PlanAirSupport(int catalog,const float* target,const float* observer,support::Route* route,int count) noexcept;
// Creates the hull at `matrix` (in the air, or a legacy plan's runway) with empty seats. Activation never creates a rider.
unsigned char* PrepareSupportAircraft(const SupportAircraft&,const float* matrix) noexcept;
// Its seated real pilot authorizes the flight. `airborne`: it is in the air already: it flies on at once, a wing at its
// kind's cruise along its nose (jet_spawn.cpp Launch's start), a helicopter with its rotor turning (HeliCalled); else
// (a legacy plan's hull on the ground) it takes off.
bool ActivateSupportAircraft(unsigned char*,const SupportAircraft&,const float* target,bool airborne) noexcept;
bool DeleteSupportAircraft(const ObjRef&) noexcept;
}
