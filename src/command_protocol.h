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
// Version 8 (2026-10-31): the requester is the player's requester ID (net_object_ref.h: its mission slot, the same on
// every machine; a stock mission numbers the players' NetIds per machine), no longer its NetId. A build before it is not
// spoken to at all (its messages do not decode, ours do not there; no compatibility with older builds).
// Version 9 (2026-10-31): the requester is its object reference (net_object_ref.h: its slot, the tag at bytes 20..23) and
// the focus any object's reference (a player's by its slot: an enemy player in PvP, which a NetId named on no other
// machine); an older build is not spoken to.
// Version 10 (2026-11-10, test site #45): DISMOUNT ALL's choice in Command::param (mapcmd_logic.h DismountParamValid:
// every rider but the driver, or only some classes' riders); an older build would refuse the order's parameter, so it
// is not spoken to (the room protocol went to 9 with it: an older build is not let into the room).
// Version 11 (2026-11-27, the Broken Arrow controls): the orders FAST MOVE, REVERSE MOVE, UNLOAD AT, HOLD FIRE, ALTITUDE,
// SPRINT, SMOKE, DECOY (mapcmd::Order past formation), a point order's line in Command::param (mapcmd_logic.h LineParam:
// a right drag's line and its facing), the two switches' params (OrderParamValid), and the pings (Kind::ping: F1-F4 on
// the map, relayed by the host to every member). An older build would refuse all of them: it is not spoken to.
// One version, not negotiated (net-6, 2026-11-24: the v6 / v7 negotiation removed): every machine of the room runs
// this build (mod_room.h kAllForcesRoomProtocol), every message goes out at kVersion and one of another does not decode.
constexpr std::uint32_t kMagic=0x444D434Eu,kVersion=11,kMaxPeers=1024;
constexpr std::uint32_t kQueued=1u;   // Request::flags: after the points the unit is on its way to (Shift)
constexpr std::size_t kWireSize=828;
// ways (version 7): the host's Shift queue of one unit, for a client's map to draw (the queue itself is the host's:
// mapcmd.cpp MapCommandAuthority). units[0] the unit's native ID, sequence the host's revision (newest wins), the
// order it is carrying out now and its point (current: a guard / move / attack-move, so the client's line runs from the
// unit to it and on through the rest with no gap) and the points after it, in Message::ways (written where a result's
// words are); none: its queue is gone (order and current zero). Sent to every peer when it changes, and all of them
// again to a peer when it says hello (a new member, a reconnect).
// ping (version 11): a player's ping on the map (F1-F4): request.requester the player's reference, request.command.at
// the point, request.command.param its kind (0..kPingKinds-1), sequence the sender's count; nothing else. A client sends
// it to the host, the host shows it and sends it on to every other member (at most one a peer every kPingGapMs).
enum class Kind : std::uint32_t { hello=1,capability,request,result,ways,ping };
constexpr std::uint32_t kPingKinds=4;
constexpr std::uint64_t kPingGapMs=250;
struct Ping { unsigned char requester[32]{}; float at[3]{}; std::uint32_t kind=0; };
constexpr unsigned kWaysMost=8;   // mapcmd::kWaypointsMost
static_assert(kWaysMost==static_cast<unsigned>(mapcmd::kWaypointsMost),"a ways message carries a whole queue");
struct Ways { std::uint32_t order=0; float current[3]{}; std::uint32_t n=0; float at[kWaysMost][3]{}; };
enum class Rpc : std::uint32_t { none,notReady,unsupported,invalid,stale,rateLimited,timeout,interrupted,count };
struct Request {
    std::uint32_t count=0;
    mapcmd::Command command{};   // command.param: mapcmd_logic.h OrderParamValid (the formation's v7, DISMOUNT ALL's v10, a line's and the switches' v11)
    std::uint32_t flags=0;       // kQueued (version 7)
    unsigned char requester[32]{},focus[32]{},units[kCommandNetUnits][32]{};
    std::uint32_t formationTotal=0,formationSlots[kCommandNetUnits]{};
};
struct Message {
    Kind kind=Kind::hello;
    std::uint64_t epoch=0;
    std::uint32_t sequence=0;
    Request request{};
    Rpc rpc=Rpc::none;
    struct Result { std::uint32_t reason=0,affected=0; } results[kCommandNetUnits]{};
    Ways ways{};   // Kind::ways only (its count in request.formationTotal on the wire)
};
bool CanonicalId(const unsigned char* id,bool allowEmpty=false) noexcept;
bool ValidRequest(const Request&) noexcept;
bool Encode(const Message&,void*,std::size_t) noexcept;
bool Decode(const void*,std::size_t,Message&) noexcept;
bool Owned(const void*,std::size_t) noexcept;
struct Backend {
    void* context=nullptr;
    bool (*send)(void*,std::uint32_t,const Message&) noexcept=nullptr;
    void (*execute)(void*,const char* authenticatedPuid,const Request&,Message::Result*) noexcept=nullptr;
    // Host: whether the sender `authenticatedPuid` is the player `requester` names (a ping speaks for its player).
    bool (*speaks)(void*,const char* authenticatedPuid,const unsigned char* requester) noexcept=nullptr;
};
class Session {
public:
    void Configure(const Backend& backend) noexcept { backend_=backend; }
    void Reset() noexcept;
    void Update(bool ready,bool host,std::uint64_t epoch,std::uint32_t hostPeer,std::uint32_t peers,std::uint64_t now) noexcept;
    void Receive(std::uint32_t peer,const char* puid,const Message&,std::uint64_t now) noexcept;
    std::uint32_t Submit(const Request&,const char* localPuid,std::uint64_t now) noexcept;
    bool Ready() const noexcept;
    const CommandNetworkResult& Result() const noexcept { return result_; }
    // The Shift queues the host publishes for the clients' maps (Kind::ways). Host: `unit`'s points now (n 0: none), sent
    // to every peer when they differ from the last sent; `Published` lists what is out. Client: the
    // points its host published for `unit` (false: none). Both forget them with the epoch; a client also at each
    // capability (the host's answer to its hello, which every queue out follows: a reconnect starts from those).
    static constexpr unsigned kWaysUnits=96;
    void PublishWays(const unsigned char* unit,const mapcmd::Command& current,const float (*at)[3],unsigned n) noexcept;
    unsigned PublishedWays(unsigned char (*out)[32],unsigned most) const noexcept;
    bool WaysOf(const unsigned char* unit,Ways* out) const noexcept;
    // The pings (Kind::ping): this machine's out (a client's to the host, the host's to every member; false: not sent),
    // and the ones heard, oldest first (a host's own and its members' alike: every map shows them).
    bool SendPing(const Ping&,std::uint64_t now) noexcept;
    bool TakePing(Ping* out) noexcept;
private:
    static constexpr unsigned kPingsKept=16;
    std::array<Ping,kPingsKept> pings_{};
    unsigned pingHead_=0,pingCount_=0;
    std::uint32_t pingSequence_=0;
    std::uint64_t lastPing_=0;
    void Heard(const Ping&) noexcept;
    void SendPingTo(std::uint32_t peer,const Ping&) noexcept;
    struct WaysEntry { bool used=false; unsigned char unit[32]{}; std::uint32_t revision=0; Ways ways{}; };
    std::array<WaysEntry,kWaysUnits> ways_{};
    std::uint32_t waysRevision_=0;
    WaysEntry* WaysSlot(const unsigned char* unit,bool make) noexcept;
    void SendWays(std::uint32_t peer,const WaysEntry& e) noexcept;
    struct Peer {
        bool capable=false,have=false;
        char puid[65]{};
        std::uint64_t lastAt=0,lastPingAt=0;
        Message request{},reply{};
    };
    Backend backend_{};
    bool ready_=false,host_=false,hostCapable_=false;
    std::uint64_t epoch_=0,lastHello_=0,pendingAt_=0,lastSend_=0;
    std::uint32_t hostPeer_=0,peers_=0,nextSequence_=0;
    std::array<Peer,kMaxPeers+1> peer_{};
    Message pending_{};
    CommandNetworkResult result_{};
    bool Send(std::uint32_t,const Message&) noexcept;
    void Execute(std::uint32_t,const char*,const Message&,std::uint64_t) noexcept;
    void ResultOf(const Message&) noexcept;
};
}
