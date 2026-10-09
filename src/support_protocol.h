#pragma once
#include "support_net.h"
#include <array>

namespace crew::support_net {
constexpr std::uint32_t kMagic=0x54525053,kVersion=2;
// Capabilities a peer announces in its hello's `index` (wire v2 unchanged: an older peer sends 0 there and an older
// host ignores the field, ValidMessage bounds it below kMaxUnits). kCapSoldierVariants: its Validate accepts the
// support_call.h soldier weapon resources ((weapon << 8) | role) and a configured number of aircraft. A host plans
// those only when every peer announced it (Session::PeersHave); otherwise rifles and each call's own number.
// kCapAirborneAir: it applies support_call.h airborne aircraft (created in the air, crew seated at once). A host
// plans air support only when every peer has it (an older peer would make the hull empty and its crew mid-air).
// kCapSeaRescue: its catalog has the sea rescue entry (support_call.h SupportRescueCatalog, after the ground entries)
// and its Validate accepts that entry's plan (one 410, its pilot only: the door seats are the swimmer's). A host
// plans a rescue only when every peer has it; an older peer's catalog ends before that index and would refuse the
// plan mid-transaction with no reason anyone sees.
constexpr std::uint32_t kCapSoldierVariants=1u,kCapAirborneAir=2u,kCapSeaRescue=4u,
    kCapabilities=kCapSoldierVariants|kCapAirborneAir|kCapSeaRescue;
static_assert(kCapabilities<kMaxUnits,"hello.index carries the capability bits");
// The host's welcome carries its own capabilities too (2026-10-10, the user: 「进入房间发现版本不同步给一下说明吧」), in its
// `catalog` (unused by a welcome before; ValidMessage bounds it below 1024, room for ten bits), and `ok` = 1 when a peer of
// the room announced fewer than the host: a guest then knows an older host, a newer one, or an older third player, and
// says so (support_net.cpp VersionTick). An older host sends 0 in both (its welcome never set them); an older guest
// ignores them. The hello's own `index` keeps its bound (below kMaxUnits).
static_assert(kCapabilities<1024,"welcome.catalog carries the host's capability bits");
// Request statuses kept per requester (host) and for this machine's own requests (any machine): the map's call and the
// sea rescue are separate requests, each told its own outcome even when the other came later.
constexpr std::uint32_t kRecentRequests=4;
enum class Kind : std::uint32_t { hello=1,welcome,request,begin,unit,prepare,ready,commit,result,cancel,activate,activated,requestStatus };
struct Message {
    Kind kind=Kind::hello;
    std::uint64_t epoch=0,challenge=0;
    std::uint32_t transaction=0,request=0,catalog=0,count=0,index=0,ok=0;
    float target[3]{};
    Unit unit{};
};
constexpr std::size_t kWireSize=176;
bool Encode(const Message&,void* bytes,std::size_t size) noexcept;
bool Decode(const void* bytes,std::size_t size,Message&) noexcept;

// No game or networking calls in this state machine. The production bridge and
// offline peers exercise this exact implementation. Peer 0 is this machine.
struct Backend {
    void* context=nullptr;
    bool (*send)(void*,std::uint32_t,const Message&) noexcept=nullptr;
    std::uint64_t (*nonce)(void*) noexcept=nullptr;
    Hooks hooks{};
};
class Session {
public:
    void Configure(const Backend& backend) noexcept { backend_=backend; }
    void Start(bool host,std::uint32_t peers,std::uint32_t hostPeer,std::uint64_t now) noexcept;
    void Stop() noexcept;
    void Suspend() noexcept;
    bool Suspended() const noexcept { return suspended_; }
    bool Submit(std::uint32_t catalog,const float* target,std::uint64_t now) noexcept;
    std::uint64_t SubmitPrepared(const Plan&,std::uint64_t now) noexcept;
    void Receive(std::uint32_t peer,const Message&,std::uint64_t now) noexcept;
    void Tick(std::uint64_t now) noexcept;
    void Failed(std::uint64_t token,RequestStatus reason=RequestStatus::cancelled) noexcept;
    bool IsActive(std::uint64_t token) const noexcept;
    bool Ready() const noexcept;
    std::uint64_t Epoch() const noexcept { return epoch_; }
    std::uint32_t ActiveCount() const noexcept;
    // Host: whether every peer of this session announced all of `caps` in its current hello (no peer: true).
    // A client is never asked to plan: true.
    bool PeersHave(std::uint32_t caps) const noexcept;
    // Host: the peers that announced fewer capabilities than this build (`missing`: what they lack between them).
    std::uint32_t PeersBehind(std::uint32_t* missing) const noexcept;
    std::uint32_t PeersAhead() const noexcept;   // host: peers that announced capabilities this build does not have
    // Client: whether the host's welcome came, the capabilities it announced (0: an older host, which announces none),
    // and whether it said a peer of the room is behind it.
    bool HostCapsKnown() const noexcept { return hostCapsKnown_; }
    std::uint32_t HostCaps() const noexcept { return hostCaps_; }
    bool RoomBehindHost() const noexcept { return hostRoomBehind_; }
    // Host: the peer whose request made transaction `token` (0: this machine's own). False: no such requested one.
    bool RequesterOf(std::uint64_t token,std::uint32_t* peer) const noexcept;
private:
    enum class Phase { empty,planning,assembling,prepared,spawning,active,cancelled };
    struct Transaction {
        Phase phase=Phase::empty;
        bool external=false;
        bool cancelConfirmed=false;
        bool spawned=false;
        RequestStatus failure=RequestStatus::cancelled;
        std::uint64_t token=0;
        Plan plan{};
        std::uint32_t requester=0,request=0,received=0;
        std::uint64_t since=0;
        std::array<unsigned char,kMaxPeers+1> ready{},result{},activated{};
    };
    Backend backend_{};
    bool running_=false,host_=false,suspended_=false;
    std::uint64_t nextToken_=0;
    std::uint32_t peers_=0,hostPeer_=0,nextTransaction_=0,nextRequest_=0;
    std::uint32_t epochSerial_=0,seenEpochSerial_=0;
    std::uint32_t missionSerial_=0;
    // Never reset when mission/roster changes: a still-live anchor must never
    // reuse an ordinal. Exhaustion fails closed instead of wrapping.
    std::uint32_t ordinal_=0x40000000u;
    std::uint64_t epoch_=0,challenge_=0,lastHello_=0;
    std::array<std::uint64_t,kMaxPeers+1> challenges_{},lastRequestAt_{};
    std::array<std::uint32_t,kMaxPeers+1> requests_{};
    std::array<std::uint32_t,kMaxPeers+1> peerMissions_{};
    std::array<std::uint32_t,kMaxPeers+1> peerCaps_{};
    std::uint32_t hostCaps_=0;bool hostCapsKnown_=false,hostRoomBehind_=false;
    struct Reply { std::uint32_t request=0;RequestStatus status=RequestStatus::accepted;bool dirty=false; };
    using Replies=std::array<Reply,kRecentRequests>;
    std::array<Replies,kMaxPeers+1> replies_{};
    Replies localReplies_{};
    std::array<Transaction,kMaxTransactions> transactions_{};
    bool Send(std::uint32_t peer,Message message) noexcept;
    bool Broadcast(Message message) noexcept;
    void Cancel(std::uint32_t id,bool broadcast,RequestStatus reason=RequestStatus::cancelled) noexcept;
    void ClearTransactions() noexcept;
    bool HasSpawned() const noexcept;
    void Welcome(std::uint32_t peer) noexcept;
    void HostRequest(std::uint32_t peer,const Message&,std::uint64_t now) noexcept;
    void Advance(std::uint32_t id,std::uint64_t now) noexcept;
    void Publish(std::uint32_t peer,std::uint32_t request,RequestStatus status) noexcept;
    void Notice(std::uint32_t request,RequestStatus status) noexcept;
    void ReplyTo(std::uint32_t peer) noexcept;
    static Reply* Record(Replies& replies,std::uint32_t request,RequestStatus status) noexcept;
};
} // namespace crew::support_net
