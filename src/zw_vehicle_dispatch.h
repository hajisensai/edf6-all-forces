// The Vehicler's vehicles made and handed out, host-authoritative (docs/newclass-re/vehicler-plan.md §2.2, §2.3, §3.3, §5 M2).
// The interface M3 (vehicler_start: the start seat, the NPC driver-seat filter) and M4 (vehicler_logistics: the resupply)
// call; the implementation is zw_vehicle_dispatch.cpp, the plan's pure part zw_vehicle_dispatch_logic.h.
//
// ZwStart (catalog kZwStartCatalog): "a vehicle of kind K for player X where X stands". The requester's machine asks; the
// host (offline this machine) plans it: one start a player a mission, the requester a live Vehicler (soldier::ZwType 4),
// not underground (no support hull goes there), the vehicle beside the player along its side axis, never on another
// planned vehicle, a person or uneven / covered ground (zwplace::Place; refused when no spot within kMaxShift steps).
// Every machine makes the same empty hull (support_spawn.h SpawnZwVehicle, its native NetId the transaction's) and
// records whose it is. No driver: the owner boards it itself (M3, on its own machine).
//
// ZwDeliver (catalog kZwDeliverCatalog): a resupply driven in from the map's edge (§3.3), as the maintainer set it on
// 2026-11-28 (over vehicler-plan.md §2.3 / §5 decision 6 "the old one stays a friendly vehicle"):
//  - any vehicle the player has unlocked (its row owned: weapon_records.h flag bit 0), each at its own price
//    (ZwVehiclePrice, zw_vehicle_catalog.h); the host checks the kind is in the requester's unlocked set and holds the
//    price of its points (M4's ledger, SetZwDeliverLedger) before it plans, settles on commit, refunds on any failure;
//  - the courier (a support soldier) drives the new one to a parking spot near the owner and gets off; if the owner's
//    old vehicle is still alive and nobody is aboard it, the courier boards the old one, drives it back to the map's
//    edge and it is deleted there. One vehicle of its own per Vehicler on the field, always; a wrecked old one is left
//    to the game. Online the host does it all (plan, the courier's orders, the deletion) and every peer follows the
//    transaction (the same hulls by NetId, the deletion by the support session's rollback / withdraw path).
// Not in this build: see RequestZwDeliver.
//
// Game thread only, every function.
#pragma once
#include "support_net.h"
#include "zw_vehicle_catalog.h"
#include <cstdint>

namespace crew {
namespace zwdispatch {
// Support transaction catalogs (support_net.h: below kMissionCrewCatalog 1023; 1020 the commander, 1021 / 1022 the debug
// spawn's, 16 the prepared drone).
inline constexpr std::uint32_t kZwStartCatalog=1018,kZwDeliverCatalog=1019;
}  // namespace zwdispatch

// ---- M3: the start vehicle ----
// Ask for `human`'s start vehicle of `kind` (ZwMissionVehicle). `human` is this machine's player (offline also the split
// screen's second); online it must be PlayerHuman() (a request speaks for this machine's player: the host takes the
// requester from the authenticated transport). Once per human a mission: a call while one is pending or ready is refused
// (false; after `refused` it may ask again, the host still makes one a player), as is a kind not ready here
// (ZwVehicleReady), underground, or a request that could not be sent. True: asked (see ZwStartStatus).
bool RequestZwStart(unsigned char* human,int kind) noexcept;
enum class ZwStartState : std::uint8_t {
    none,      // never asked for this human this mission
    pending,   // asked, the host planning / every machine making it
    ready,     // made and active (online: the transaction active on every peer): `*vehicle` is the hull, empty
    refused,   // the host refused it / the transaction failed / the hull was not made: the player walks in (logged why)
};
// Where `human`'s start request stands; `vehicle` (may be null) gets the hull when ready, else null. The hull stays the
// world's: the caller adds its own reference when it keeps one (RideVehicle's contract, vehicler-plan.md §2.2).
ZwStartState ZwStartStatus(const unsigned char* human,unsigned char** vehicle) noexcept;

// ---- M3 / M4: whose vehicle ----
// The owner of a Vehicler's vehicle (its start vehicle or a delivered one), on every machine: the owner's player object,
// live; nullptr for any other vehicle, a vehicle no longer alive, or an owner not (or no longer) found in the mission.
// A vehicle being replaced (the courier driving it off, ZwDeliver) has no owner from the new one's delivery on.
const unsigned char* ZwVehicleOwner(const void* vehicle) noexcept;
// Its kind (kZwVehicles index), -1 for anything not a Vehicler's live vehicle.
int ZwVehicleKind(const void* vehicle) noexcept;
// `human`'s current vehicle (the last one made for it and still its), nullptr when none.
unsigned char* ZwOwnedVehicle(const unsigned char* human) noexcept;

// ---- M4: the resupply ----
// What may be called: zw_vehicle_catalog.h ZwUnlockedKinds (the request carries it), at ZwVehiclePrice each.
// M4's points, host side (offline this machine). The host calls `hold` while it plans a resupply (false: not enough
// points, the request refused), then exactly one of `settle` (the delivery committed) or `refund` (refused later, the
// transaction failed, no hull made) with the same ticket. `requester` is the player reference (net_object_ref.h, the
// plan's requester; command_identity.h ResolveMarkIdentities gives its slot). Not set: every resupply is refused, logged.
struct ZwDeliverLedger {
    bool (*hold)(const unsigned char requester[32],unsigned price,std::uint64_t* ticket) noexcept=nullptr;
    void (*settle)(std::uint64_t ticket) noexcept=nullptr;
    void (*refund)(std::uint64_t ticket) noexcept=nullptr;
};
void SetZwDeliverLedger(const ZwDeliverLedger& ledger) noexcept;
// Ask the host for a resupply of `kind` (any kind in ZwUnlockedKinds) for `human`, driven to a parking spot near `around`
// (the owner's position). One at a time a player (ZwDeliverPending). Not in this build (the courier's route, arrival and
// the old vehicle's drive-off are not written): false and a log line, nothing held.
bool RequestZwDeliver(unsigned char* human,int kind,const float* around) noexcept;
// Whether `human` has a resupply on its way (asked, not yet delivered / refused); M4 shows it and refuses a second.
bool ZwDeliverPending(const unsigned char* human) noexcept;

// ---- support_dispatch.cpp's hooks for kZwStartCatalog / kZwDeliverCatalog (as commander.h's) ----
// True for a catalog of this module's.
constexpr bool IsZwCatalog(std::uint32_t catalog) noexcept {
    return catalog==zwdispatch::kZwStartCatalog || catalog==zwdispatch::kZwDeliverCatalog;
}
// Host (or offline): the plan of `peer`'s request (its authenticated player; `loadout` the kind it asked for).
support_net::PlanResult PlanZwVehicle(std::uint32_t peer,std::uint32_t catalog,const float* target,std::uint64_t loadout,
                                      SupportPlan* out) noexcept;
// Every peer, before anything is made: one hull of a known kind, ready here, at its target, not underground.
bool ValidateZwVehiclePlan(const SupportPlan& plan) noexcept;
// Every peer: the hull made (its NetId registered online), its owner recorded.
bool ApplyZwVehiclePlan(std::uint64_t transaction,const SupportPlan& plan,bool remote) noexcept;
void DestroyZwVehiclePlan(std::uint64_t transaction) noexcept;
// The request notices: true for this machine's ZwStart request (consumed here).
bool ZwVehicleRequestNotice(std::uint32_t request,support_net::RequestStatus status) noexcept;
// support_dispatch.cpp's tick (every frame, the game thread): activation of made hulls, the offline requests.
void ZwVehicleTick() noexcept;
// A new mission (support_dispatch.cpp's reset): the last mission's records dropped (its objects are the teardown's).
void ResetZwVehicles() noexcept;
}  // namespace crew
