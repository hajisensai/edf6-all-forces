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
// kCapTransports: it knows the transport catalog entries (support_dispatch.cpp TransportCatalog: the helicopter assault and
// the paratroop drop) and their hulls; a host plans them only when every peer has it (an older one refuses the plan).
// kCapLoadout: its Validate takes a composed load (support_call.h SupportLoadout: any count up to the seats, the Wing
// Diver and Fencer kinds); a request carries the load in `challenge`. A host plans a composed load only when every peer
// has it, else the call's own load (and says why).
constexpr std::uint32_t kCapSoldierVariants=1u,kCapAirborneAir=2u,kCapTransports=4u,kCapLoadout=8u,
    kCapabilities=kCapSoldierVariants|kCapAirborneAir|kCapTransports|kCapLoadout;
static_assert(kCapabilities<kMaxUnits,"hello.index carries the capability bits");
// The hello's `index` has no bit left (it must stay below kMaxUnits for an older host's ValidMessage). The extension is
// the hello's unit, which neither side ever read for a hello (ValidMessage checks a unit only in a unit message):
// unit.resourceId = the extension bits, unit.netId = the Bloom filter (support_loadout.h, 256 bits) of the variant files
// the peer has preloaded this mission. An older build sends zeros there and ignores ours.
// kExtVariants: it applies plan unit variants (support_net.h Unit::variant: support_loadout.h looks and vehicle pylons,
// sent in the unit message's `challenge`) and writes the ones it lacks to its pending list. A host plans a variant
// applied only when every peer announced this and has its file by the filter; otherwise stock, said. The variants hook
// (support_net.h Hooks::variants) announces it.
// kExtSeaRescue: its catalog has the sea rescue entry (support_call.h SupportRescueCatalog, the catalog's last) and its
// Validate accepts that entry's plan (one 410, its pilot only: the door seats are the swimmer's). A host plans a rescue
// only when every peer has it (PeersHaveExtension); an older peer's catalog ends before that index and would refuse the
// plan mid-transaction with no reason anyone sees. Every hello of this build announces it (Session::Tick), whatever the
// hook says: it is the catalog's, not a mission's.
// kExtRescueChannel (2026-10-10, the user: 「救援应该有单独的cd」): it takes part in a sea rescue transaction while another
// one (a map call's, another player's rescue) is still being made. The host gives a rescue its own channel only when
// every peer announced it: one rescue in flight per requester, beside the map's one, never under the map's 2 s rate,
// each requester's rescues kRescueCooldownMs apart (SetRescueCooldown). Without it a rescue waits its turn as before.
// Announced as kExtSeaRescue is (the hello's index has no bit left: #100's kCapTransports 4 / kCapLoadout 8).
constexpr std::uint32_t kExtVariants=1u,kExtSeaRescue=2u,kExtRescueChannel=4u,kExtensions=kExtVariants|kExtSeaRescue|kExtRescueChannel;
constexpr std::uint32_t kExtBuilt=kExtSeaRescue|kExtRescueChannel;   // what every hello of this build announces (Tick)
constexpr std::uint64_t kRescueCooldownMs=30000;   // the default; the host's ini SeaRescueCooldownSec sets it
// What a build has, both words in one (the version notice compares and names these, support_net.cpp VersionTick): the
// capability bits low (below kMaxUnits, 4 bits), the extension bits above them.
constexpr std::uint32_t kExtShift=4;
static_assert(kMaxUnits==(1u<<kExtShift),"the capability bits fill exactly the low kExtShift bits");
constexpr std::uint32_t Features(std::uint32_t caps,std::uint32_t ext) noexcept { return (caps&(kMaxUnits-1))|(ext<<kExtShift); }
constexpr std::uint32_t FeatureExt(std::uint32_t ext) noexcept { return ext<<kExtShift; }
constexpr std::uint32_t kFeatures=Features(kCapabilities,kExtensions);
// The host's welcome carries its own features too (2026-10-10, the user: 「进入房间发现版本不同步给一下说明吧」), in its
// `catalog` (unused by a welcome before; ValidMessage bounds it below 1024, room for ten bits), and `ok` = 1 when a peer of
// the room announced fewer than the host: a guest then knows an older host, a newer one, or an older third player, and
// says so (support_net.cpp VersionTick). An older host sends 0 in both (its welcome never set them); an older guest
// ignores them. The hello's own `index` keeps its bound (below kMaxUnits).
static_assert(kFeatures<1024,"welcome.catalog carries the host's features");
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
    // `loadout`: support_call.h PackSupportLoadout (0: the call's own), sent in the request's `challenge`.
    bool Submit(std::uint32_t catalog,const float* target,std::uint64_t now,std::uint64_t loadout=0) noexcept;
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
    // Host: whether every peer announced all of `ext` (kExtVariants) and, `hash` non-zero, has that variant file by its
    // hello's filter (no peer: true). A client is never asked: true.
    bool PeersHaveVariant(std::uint32_t ext,std::uint64_t hash) const noexcept;
    // Host: whether every peer announced all of `ext` in its hello's extension word (kExtSeaRescue). A client: true.
    bool PeersHaveExtension(std::uint32_t ext) const noexcept { return PeersHaveVariant(ext,0); }
    // Host: the heard peers whose features (Features: capabilities and extensions) lack some of this build's kFeatures
    // (`missing`: what they lack between them, feature bits).
    std::uint32_t PeersBehind(std::uint32_t* missing) const noexcept;
    std::uint32_t PeersAhead() const noexcept;   // host: peers that announced features this build does not have
    // Client: whether the host's welcome came, the features it announced (0: an older host, which announces none),
    // and whether it said a peer of the room is behind it.
    bool HostCapsKnown() const noexcept { return hostCapsKnown_; }
    std::uint32_t HostCaps() const noexcept { return hostCaps_; }
    bool RoomBehindHost() const noexcept { return hostRoomBehind_; }
    // Host: the peer whose request made transaction `token` (0: this machine's own). False: no such requested one.
    bool RequesterOf(std::uint64_t token,std::uint32_t* peer) const noexcept;
    // Host: how long one requester's rescues stand apart (from its last rescue made); the host's setting decides.
    void SetRescueCooldown(std::uint64_t ms) noexcept { rescueCooldown_=ms; }
private:
    enum class Phase { empty,planning,assembling,prepared,spawning,active,cancelled };
    struct Transaction {
        Phase phase=Phase::empty;
        bool external=false;
        bool cancelConfirmed=false;
        bool spawned=false;
        bool rescue=false;   // on the rescue channel (Hooks::ownChannel, every peer kExtRescueChannel)
        RequestStatus failure=RequestStatus::cancelled;
        std::uint64_t token=0,loadout=0;
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
    std::array<std::uint32_t,kMaxPeers+1> peerCaps_{},peerExt_{};
    std::array<std::array<unsigned char,32>,kMaxPeers+1> peerBloom_{};
    std::uint32_t hostCaps_=0;bool hostCapsKnown_=false,hostRoomBehind_=false;
    std::array<std::uint64_t,kMaxPeers+1> rescueAt_{};   // host: when each requester's last rescue was made (0: none)
    std::uint64_t rescueCooldown_=kRescueCooldownMs;
    bool InFlight(const Transaction& t) const noexcept;
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
