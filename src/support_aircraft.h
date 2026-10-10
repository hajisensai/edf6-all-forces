#pragma once
#include <cstdint>
#include "support_entry.h"
namespace crew {
struct ObjRef;
struct SupportAircraft {
    int jet=-1,heli=-1,count=0;
    std::uint32_t fuelSeconds=0;
    bool follow=false;
    bool transportPlane=false; // the paratroop plane (jet_internal.h Body::transportPlane; jet and heli -1)
};
int SupportAirCallCount() noexcept;
const wchar_t* SupportAirCallName(int index) noexcept;
const wchar_t* SupportAirCallKey(int index) noexcept;
bool SupportAircraftSpec(int catalog,SupportAircraft* out) noexcept;
// `spec`: what the entry brings (its kind, and spec.count aircraft: every one's formation slot, support_entry.h
// AirFormationSlot, is checked); the dispatcher's, so an entry the call table does not hold (the sea rescue) plans the same.
// route->from: the lead's place IN THE AIR at the edge, at the route's height; route->heading toward the target.
support::Refusal PlanAirSupport(int catalog,const float* target,const float* observer,support::Route* route,int count) noexcept;
// The same for an aircraft no catalog row brings (the transports: support_dispatch.cpp TransportSpec; the sea rescue).
support::Refusal PlanAirSupportFor(SupportAircraft spec,const float* target,const float* observer,support::Route* route,int count) noexcept;
// One aircraft from the nearest of `spots` (takeoff points: a carrier's deck, a pad) it can climb out of to over `target`
// (support_entry.h TakeoffRoute): route->from on the spot, kTakeoffLift over it. noEntry: none; the caller tries the edge.
support::Refusal PlanTakeoffSupport(const SupportAircraft& spec,const float* target,const float (*spots)[3],int count,
                                    support::Route* route) noexcept;
// Whether its hull can be made this mission (its SGO installed and preloaded: jet_spawn.cpp PreloadJets).
bool SupportAircraftReady(const SupportAircraft&) noexcept;
// Creates the hull at `matrix` (in the air, or a legacy plan's runway) with empty seats. Activation never creates a rider.
// `variant`: the body's loaded copy (support_loadout.h VehicleVariantFile, app:/object/ path) preloaded this mission
// (support_variants.h); nullptr: the stock body. It must come up as the same role, as the stock body must.
unsigned char* PrepareSupportAircraft(const SupportAircraft&,const float* matrix,const wchar_t* variant=nullptr) noexcept;
// Its seated real pilot authorizes the flight. `airborne`: it is in the air already: it flies on at once, a wing at its
// kind's cruise along its nose (jet_spawn.cpp Launch's start), a helicopter with its rotor turning (HeliCalled); else
// (a legacy plan's hull on the ground) it takes off.
bool ActivateSupportAircraft(unsigned char*,const SupportAircraft&,const float* target,bool airborne) noexcept;
bool DeleteSupportAircraft(const ObjRef&) noexcept;
// A hull this support layer made (and has not deleted). Its owner, the dispatcher, deletes it with its real crew:
// HeliReap / JetReap leave it alone (they never delete under real soldiers, and its crew is the dispatcher's).
bool SupportAircraftOwned(const void* vehicle) noexcept;
// One of ours whose flight is over: it has withdrawn (fuel, ammo, damage) far enough out to go (the heli's / jet's reap).
bool SupportAircraftLeft(const ObjRef&) noexcept;
}
