#pragma once
#include <cstddef>
#include <cstdint>

namespace crew {
struct ObjRef;
namespace support_net {
constexpr std::uint32_t kMaxUnits=16,kMaxPeers=1024,kMaxTransactions=128;
constexpr std::uint32_t kExistingVehicle=0xFFFF0001u,kMissionCrewCatalog=1023;
struct Unit {
    std::uint32_t resourceId=0,role=0; // role: parent vehicle index + 1, zero for independent infantry/vehicle
    float matrix[16]{};
    unsigned char netId[32]{};
};
struct Plan {
    std::uint32_t catalogId=0,count=0;
    float target[3]{};
    Unit units[kMaxUnits]{};
};
enum class PlanResult { pending,ready,refused };
enum class RequestStatus : std::uint32_t { accepted,active,refused,timeout,cancelled,interrupted };
struct Hooks {
    PlanResult (*plan)(std::uint32_t,const float*,Plan*) noexcept=nullptr;
    bool (*validate)(const Plan&) noexcept=nullptr;
    // Tokens are process-monotonic and local, not wire indexes. Retain them
    // verbatim; never use a token as a bounded array index.
    bool (*spawn)(std::uint64_t,const Plan&,bool remote) noexcept=nullptr;
    void (*destroy)(std::uint64_t) noexcept=nullptr;
    bool (*deriveId)(std::uint32_t,unsigned char*) noexcept=nullptr;
    bool (*participants)(void**,std::uint32_t,std::uint32_t*,std::uint32_t*) noexcept=nullptr;
    bool (*admissionReady)() noexcept=nullptr;
    bool (*createdMatches)(const ObjRef*,std::uint32_t) noexcept=nullptr;
    void (*notice)(std::uint32_t,RequestStatus) noexcept=nullptr;
};
bool ValidPlan(const Plan& plan,bool requireIds=true) noexcept;
} // namespace support_net
using SupportPlan=support_net::Plan;
void ConfigureSupportNet(const support_net::Hooks& hooks) noexcept;
bool SubmitSupportRequest(int catalogId,const float* target,wchar_t* note,std::size_t noteSize) noexcept;
void SupportNetTick() noexcept;
void ResetSupportNet() noexcept;
void ReportSupportFailure(std::uint64_t transaction) noexcept;
bool SupportTransactionActive(std::uint64_t transaction) noexcept;
// Whether every peer of the running support session accepts configured soldier weapons and aircraft counts
// (support_protocol.h kCapSoldierVariants); true offline / with no session. The host's planner falls back to the
// protocol v2 plan (rifles, each call's own number of aircraft) otherwise, and says so.
bool SupportPeersAcceptVariants() noexcept;
// Whether every peer applies air support created in the air (kCapAirborneAir); true offline / with no session. Without
// it the host refuses air support with a reason: no peer may have its hull on the ground while another's flies.
bool SupportPeersAcceptAirborne() noexcept;
// Whether every peer has the sea rescue catalog entry (kCapSeaRescue); true offline / with no session. Without it the
// host refuses the rescue with a reason (the swimmer's machine is told, and logs it).
bool SupportPeersAcceptRescue() noexcept;
// The room's builds differ (version_notice.h): the HUD's line about it, for a while after it changed (draw thread).
bool SupportVersionCue(wchar_t* out,std::size_t capacity) noexcept;
// Host: the player whose request made committed transaction `token`, by its stable identity (the requester's
// authenticated PUID resolved to its mission player actor; this machine's player for its own). False when unknown.
bool SupportTransactionRequester(std::uint64_t token,ObjRef* out) noexcept;
// Requester ownership: native EOS PUID must equal the authenticated transport
// sender and belong to the sealed current-world participant set.
bool SupportCommandRequesterMatches(void* puid,const char* authenticatedPuid) noexcept;
// Called before native player construction once this world has sealed its
// actual participant PUIDs. A newly joined lobby member waits for the next world.
bool SupportParticipantAllowed(void* puid) noexcept;
bool SupportMissionPlayerAllowed(int missionIndex) noexcept;
// This machine hosts a session whose sealed current world has exactly one participant (split-screen players share
// it): this host. No peer exists to replicate to, and a lobby member joining later waits for the next world
// (SupportMissionPlayerAllowed), so the host may deploy support locally without the EDF6Coop transport.
bool SupportSoloHostWorld() noexcept;
void NoteSupportMissionPlayerCreated(int missionIndex,const ObjRef& object) noexcept;
// The mission player actors this world created so far (any machine's; up to `most`, game thread): references only, each
// to be checked live by the caller (ObjRef::Is). The sea rescue's heli finds the swimmer it was made for among them.
int SupportMissionPlayerObjects(ObjRef* out,int most) noexcept;
// Only a verified native return-to-lobby transition may call this; readiness
// loss, a disappeared actor, and InSession() are not evidence of a lobby.
void SupportMissionReturnedToLobby() noexcept;
// Host-internal mission vehicle migration. Existing-vehicle units carry their
// canonical registered ID and are resolved, never created or destroyed.
std::uint64_t SubmitPreparedSupportPlan(const SupportPlan& plan) noexcept;
} // namespace crew
