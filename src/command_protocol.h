#pragma once
#include "command_net.h"
#include <array>
namespace crew::command_net {
// Version 3 (2026-10-09): the move / attack-move orders (mapcmd::Order, formation slots for every point order) and
// the riding refusal (NpcCommandReason::riding); a version-2 peer would reject or misread them.
// Version 4 (2026-10-09): the transport's WITHDRAW order and its refusal (NpcCommandReason::noTransport).
// Version 5 (2026-10-09): DISMOUNT ALL (mapcmd::Order::dismountAll) and DISMOUNT's refusal with nobody on a passenger
// seat (NpcCommandReason::noPassengers); DISMOUNT itself now leaves the driver and the gunners aboard.
// Version 6 (2026-10-19): HOLD (mapcmd::Order::hold, the map's H): the authority guards where its unit stands.
// Version 7 (2026-10-20): the Shift queue (Request::flags kQueued: the authority keeps the point after the ones its unit
// is on its way to, mapcmd.cpp MapCommandAuthority) and the formation order (mapcmd::Order::formation, its parameter in
// Command::param: the map's T and formation menu). Both in words a version-6 message keeps zero (the one after `kind`,
// the one after the point): the layout and the size are 6's.
// Negotiated, not assumed: every hello goes out as version kMinVersion (which a version-6 build reads) with the
// sender's highest version in the hello's formationTotal (never read in a hello by version 6); the host answers each
// peer at the lower of the two and the client speaks that version from then on. A version-6 host therefore gives a
// version-7 client everything version 6 has, and a queued or formation order to it is refused before it is sent, with
// the reason (SubmitMapCommand) -- never sent as an immediate order instead.
constexpr std::uint32_t kMagic=0x444D434Eu,kVersion=7,kMinVersion=6,kMaxPeers=1024;
constexpr bool Compatible(std::uint32_t version) noexcept { return version>=kMinVersion && version<=kVersion; }
constexpr std::uint32_t kQueued=1u;   // Request::flags: after the points the unit is on its way to (Shift)
constexpr std::size_t kWireSize=828;
// ways (version 7): the host's Shift queue of one unit, for a client's map to draw (the queue itself is the host's:
// mapcmd.cpp MapCommandAuthority). units[0] the unit's native ID, sequence the host's revision (newest wins), the
// points in Message::ways (written where a result's words are); none: its queue is gone. Sent to every peer at version
// 7 when it changes, and all of them again to a peer when it says hello (a new member, a reconnect).
enum class Kind : std::uint32_t { hello=1,capability,request,result,ways };
constexpr unsigned kWaysMost=8;   // mapcmd::kWaypointsMost
static_assert(kWaysMost==static_cast<unsigned>(mapcmd::kWaypointsMost),"a ways message carries a whole queue");
struct Ways { std::uint32_t n=0; float at[kWaysMost][3]{}; };
enum class Rpc : std::uint32_t { none,notReady,unsupported,invalid,stale,rateLimited,timeout,interrupted,count };
struct Request {
    std::uint32_t count=0;
    mapcmd::Command command{};   // command.param: the formation order's (version 7), 0 for every other order
    std::uint32_t flags=0;       // kQueued (version 7)
    unsigned char requester[32]{},focus[32]{},units[kCommandNetUnits][32]{};
    std::uint32_t formationTotal=0,formationSlots[kCommandNetUnits]{};
};
struct Message {
    std::uint32_t version=kVersion;   // the wire's (Encode writes it, Decode reads it): one Compatible
    Kind kind=Kind::hello;
    std::uint64_t epoch=0;
    std::uint32_t sequence=0;
    Request request{};
    Rpc rpc=Rpc::none;
    struct Result { std::uint32_t reason=0,affected=0; } results[kCommandNetUnits]{};
    Ways ways{};   // Kind::ways only (its count in request.formationTotal on the wire)
};
bool CanonicalId(const unsigned char* id,bool allowEmpty=false) noexcept;
// `version`: the one it is to travel at (a version-6 peer takes no flag, no parameter, no order past HOLD).
bool ValidRequest(const Request&,std::uint32_t version=kVersion) noexcept;
// The lowest version that carries `r` (7 for a queued or formation order, else 6).
std::uint32_t RequestVersion(const Request& r) noexcept;
bool Encode(const Message&,void*,std::size_t) noexcept;
bool Decode(const void*,std::size_t,Message&) noexcept;
bool Owned(const void*,std::size_t) noexcept;
struct Backend {
    void* context=nullptr;
    bool (*send)(void*,std::uint32_t,const Message&) noexcept=nullptr;
    void (*execute)(void*,const char* authenticatedPuid,const Request&,Message::Result*) noexcept=nullptr;
};
class Session {
public:
    void Configure(const Backend& backend) noexcept { backend_=backend; }
    void Reset() noexcept;
    void Update(bool ready,bool host,std::uint64_t epoch,std::uint32_t hostPeer,std::uint32_t peers,std::uint64_t now) noexcept;
    void Receive(std::uint32_t peer,const char* puid,const Message&,std::uint64_t now) noexcept;
    std::uint32_t Submit(const Request&,const char* localPuid,std::uint64_t now) noexcept;
    bool Ready() const noexcept;
    // The version this machine and its host agreed (the host: its own); 0 before the host answered.
    std::uint32_t HostVersion() const noexcept { return host_ ? kVersion : hostCapable_ ? hostVersion_ : 0u; }
    const CommandNetworkResult& Result() const noexcept { return result_; }
    // Offline tests only: this session behaves as a build whose highest version is `version` (an older peer).
    void LimitVersion(std::uint32_t version) noexcept { if(Compatible(version))maxVersion_=version; }
    // The Shift queues the host publishes for the clients' maps (Kind::ways). Host: `unit`'s points now (n 0: none), sent
    // to every peer of version 7 when they differ from the last sent; `Published` lists what is out. Client: the
    // points its host published for `unit` (false: none). Both forget them with the epoch.
    static constexpr unsigned kWaysUnits=96;
    void PublishWays(const unsigned char* unit,const float (*at)[3],unsigned n) noexcept;
    unsigned PublishedWays(unsigned char (*out)[32],unsigned most) const noexcept;
    bool WaysOf(const unsigned char* unit,Ways* out) const noexcept;
private:
    struct WaysEntry { bool used=false; unsigned char unit[32]{}; std::uint32_t revision=0; Ways ways{}; };
    std::array<WaysEntry,kWaysUnits> ways_{};
    std::uint32_t waysRevision_=0;
    WaysEntry* WaysSlot(const unsigned char* unit,bool make) noexcept;
    void SendWays(std::uint32_t peer,const WaysEntry& e) noexcept;
    struct Peer {
        std::uint32_t version=kMinVersion;   // the host's: what it answers this peer at
        bool capable=false,have=false;
        char puid[65]{};
        std::uint64_t lastAt=0;
        Message request{},reply{};
    };
    Backend backend_{};
    bool ready_=false,host_=false,hostCapable_=false;
    std::uint64_t epoch_=0,lastHello_=0,pendingAt_=0,lastSend_=0;
    std::uint32_t hostPeer_=0,peers_=0,nextSequence_=0,hostVersion_=0,maxVersion_=kVersion;
    std::array<Peer,kMaxPeers+1> peer_{};
    Message pending_{};
    CommandNetworkResult result_{};
    bool Send(std::uint32_t,const Message&) noexcept;
    void Execute(std::uint32_t,const char*,const Message&,std::uint64_t) noexcept;
    void ResultOf(const Message&) noexcept;
};
}
