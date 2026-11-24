#pragma once
#include "support_net.h"
#include <array>

namespace crew::support_net {
// v3 (2026-11-21, docs/feature-npc-weapon-tier.md 2.5): kWireSize 176 -> 200, the tail carrying a Fencer's second hand
// (support_net.h Unit::offhand, RequesterArms::offhand). Older builds are kept out by the room protocol (mod_room.h 11).
constexpr std::uint32_t kMagic=0x54525053,kVersion=3;
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
// kExtVariants is the one extension bit: runtime state (the variants hook runs this mission), not a build's. The bits
// that said which features a build had (the sea rescue and its own channel, the EDF4.1 outfit, players 5+, backpack
// weapons, the host's roster, the plugin's channels) and the welcome's features word were removed by net-6 (2026-11-24):
// every machine of a room runs this build (mod_room.h kAllForcesRoomProtocol 16: an older or newer build neither finds
// nor joins the room), so nothing is asked of a peer per feature.
constexpr std::uint32_t kExtVariants=1u;
constexpr std::uint64_t kRescueCooldownMs=30000;   // the default; the host's ini SeaRescueCooldownSec sets it
// The host's welcome: `ok` = 1 once it heard every peer's hello of this handshake (Session::RoomReady: plugin_net.h's
// channels tell their peers `joined` only then); `catalog` 0; unit.role its kDetails.
// Additional capability word in hello/welcome unit.role, outside the full ten-bit features word.
// A requester-bearing BEGIN uses only the unused unit fields and is sent only to a room where every peer
// announced kDetailPvpSupport. Older builds ignore BEGIN units, so this all-peer gate is mandatory.
// kDetailDynamicDrones separately announces exact prepared-child resources and
// their parent/slot lifecycle. A build that understands only PvP metadata must
// reject those children before any native object is created.
// kDetailTransactionGenerations: transaction carries an epoch-unique monotonic wire identity,
// mapped to one of kMaxTransactions live slots. A slot is reusable only after every peer acknowledges
// cancellation with kCancelAckIndex (ordinary failure is not an ACK). Older v2 decoders bound
// transaction to kMaxTransactions, so never reuse with such a peer.
constexpr std::uint32_t kDetailPvpSupport=1u,kDetailDynamicDrones=2u,kDetailTransactionGenerations=4u,
    kDetails=kDetailPvpSupport|kDetailDynamicDrones|kDetailTransactionGenerations;
// The existing submarine catalog also carries host-prepared children of a
// registered submarine / gun / blast / doll carrier. This is a separate narrow
// admission contract, never permission to submit an arbitrary prepared plan.
constexpr std::uint32_t kPreparedDroneCatalog=16;
enum class PreparedKind : std::uint8_t { missionCrew,dynamicDrone };
bool ValidPreparedDronePlan(const Plan&) noexcept;
constexpr std::uint32_t kBeginRequesterMetadata=0x52565050u;
// Request statuses kept per requester (host) and for this machine's own requests (any machine): the map's call and the
// sea rescue are separate requests, each told its own outcome even when the other came later. This machine never has
// more than this many requests waiting at once (Session::Submit refuses another): each is told its outcome.
constexpr std::uint32_t kRecentRequests=4;
enum class Kind : std::uint32_t { hello=1,welcome,request,begin,unit,prepare,ready,commit,result,cancel,activate,activated,requestStatus };
// RESULT !ok with this reserved index acknowledges processing the host's CANCEL. A guest-local
// failure uses RequestStatus instead, and must keep reporting failure until it actually hears CANCEL.
// The unchanged old v2 decoder accepts this index; new generation-capable hosts require the marker
// to recycle a slot. Unmarked legacy responses can only settle an unrecyclable legacy tombstone.
constexpr std::uint32_t kCancelAckIndex=kMaxUnits-1;
static_assert(kCancelAckIndex>static_cast<std::uint32_t>(RequestStatus::interrupted),"cancel ACK cannot be an ordinary failure status");
struct Message {
    Kind kind=Kind::hello;
    std::uint64_t epoch=0,challenge=0;
    std::uint32_t transaction=0,request=0,catalog=0,count=0,index=0,ok=0;
    float target[3]{};
    Unit unit{};
    // A request's: the requester's soldier weapons (support_net.h RequesterArms), on the wire in the unit fields a request
    // never used (support_protocol.cpp: 6 bytes a kind), the Fencer kinds' second hands in the tail. A unit message
    // carries its own in unit.arms / unit.offhand.
    RequesterArms arms{};
};
// The v2 fields (176 bytes) and a 24-byte tail: a request's four Fencer second hands (6 bytes each), a unit message's
// offhand (8 bytes, the rest zero), zero in every other message.
constexpr std::size_t kWireSize=200;
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
    // Suspended (a transport generation lost, 2026-10-30): a Start with the same room shape resumes, a new handshake and
    // epoch with the made transactions kept (their ids, tokens and actors), instead of staying shut until the mission
    // reset. Running: a Start suspends.
    void Start(bool host,std::uint32_t peers,std::uint32_t hostPeer,std::uint64_t now) noexcept;
    void Stop() noexcept;
    void Suspend() noexcept;
    bool Suspended() const noexcept { return suspended_; }
    // `loadout`: support_call.h PackSupportLoadout (0: the call's own), sent in the request's `challenge`. `arms`: the
    // requester's soldier weapons (null: none, the stock templates), sent in the request's unit fields.
    bool Submit(std::uint32_t catalog,const float* target,std::uint64_t now,std::uint64_t loadout=0,
                const RequesterArms* arms=nullptr) noexcept;
    // The default preserves existing-vehicle migration. An explicit dynamic
    // intent additionally requires all-peer details and preflight native Validate.
    std::uint64_t SubmitPrepared(const Plan&,std::uint64_t now,PreparedKind kind=PreparedKind::missionCrew) noexcept;
    void Receive(std::uint32_t peer,const Message&,std::uint64_t now) noexcept;
    void Tick(std::uint64_t now) noexcept;
    void Failed(std::uint64_t token,RequestStatus reason=RequestStatus::cancelled) noexcept;
    bool IsActive(std::uint64_t token) const noexcept;
    bool Ready() const noexcept;
    std::uint32_t PendingHandshakes() const noexcept {
        if(!running_)return peers_+1;
        if(!host_)return epoch_ ? 0u : 1u;
        std::uint32_t missing=0;for(std::uint32_t i=1;i<=peers_;++i)if(!challenges_[i])++missing;
        return missing;
    }
    std::uint64_t Epoch() const noexcept { return epoch_; }
    std::uint32_t ActiveCount() const noexcept;
    // Host: whether every peer of this session announced all of `caps` in its current hello (no peer: true).
    // A client is never asked to plan: true.
    bool PeersHave(std::uint32_t caps) const noexcept;
    // Current authenticated handshake only. Host: all peers; client: its host. Unknown/not ready: false.
    bool PeersHaveDetails(std::uint32_t bits) const noexcept;
    std::uint32_t HostDetails() const noexcept { return running_ && epoch_ ? (host_ ? kDetails : hostDetails_) : 0u; }
    // Host: whether every peer announced all of `ext` (kExtVariants) and, `hash` non-zero, has that variant file by its
    // hello's filter (no peer: true). A client is never asked: true.
    bool PeersHaveVariant(std::uint32_t ext,std::uint64_t hash) const noexcept;
    // The room's handshake done: Ready, and the host heard every peer's hello (a guest: its host's current welcome
    // said so, support_protocol.h welcome `ok`). plugin_net.h's channels tell their peers `joined` only then.
    bool RoomReady() const noexcept { return Ready() && (host_ || hostRoomReady_); }
    // Host: the peer whose request made transaction `token` (0: this machine's own). False: no such requested one.
    bool RequesterOf(std::uint64_t token,std::uint32_t* peer) const noexcept;
    // Host: how long one requester's rescues stand apart (from its last rescue made); the host's setting decides.
    void SetRescueCooldown(std::uint64_t ms) noexcept { rescueCooldown_=ms; }
private:
    // queued (host): a map request waiting for the one map plan in flight to end (2026-10-30: it was refused, "房主拒绝了
    // 本机支援请求" whenever another player's call was being routed); not in flight, nothing sent for it yet.
    enum class Phase { empty,planning,assembling,prepared,spawning,active,cancelled,queued };
    struct Transaction {
        Phase phase=Phase::empty;
        bool external=false;
        bool dynamicDrone=false; // independent prepared-child channel; same parent/slot serialized only while pending
        bool cancelConfirmed=false;
        bool spawned=false;
        bool rescue=false;   // on the rescue channel (Hooks::ownChannel)
        bool readyPending=false;   // a peer: PREPARE heard, its READY waiting on Hooks::prepare (a load going on)
        bool planned=false;        // host: planned, its soldiers armed; its BEGIN waiting on Hooks::prepare here (a load)
        bool identified=false;     // host: its units' NetIds derived (once, before the BEGIN that may wait on a load)
        RequestStatus failure=RequestStatus::cancelled;
        std::uint64_t token=0,loadout=0;
        Plan plan{};
        RequesterArms arms{};      // host: the request's (Submit / HostRequest), given to the plan's soldiers once planned
        std::uint32_t id=0,requester=0,request=0,received=0; // id is wire identity; array position is bounded storage only
        std::uint64_t since=0;
        std::array<unsigned char,kMaxPeers+1> ready{},result{},activated{}; // cancelled result: 1 legacy response, 2 explicit CANCEL ACK
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
    std::array<std::uint32_t,kMaxPeers+1> peerCaps_{},peerExt_{},peerDetails_{};
    std::array<std::array<unsigned char,32>,kMaxPeers+1> peerBloom_{};
    std::uint32_t hostDetails_=0;bool hostCapsKnown_=false,hostRoomReady_=false;   // client: the host's current welcome
    std::array<std::uint64_t,kMaxPeers+1> rescueAt_{};   // host: when each requester's last rescue was made (0: none)
    std::uint64_t rescueCooldown_=kRescueCooldownMs;
    bool InFlight(const Transaction& t) const noexcept;
    bool MapBusy() const noexcept;   // a map transaction in flight (one plan at a time: the planner's state is one)
    void Retain() noexcept;          // keep the made transactions, cancel the rest: a suspension or a re-handshake
    void Refuse(std::uint32_t peer,const Message&,const char* why) noexcept;
    Transaction* AllocateTransaction() noexcept;
    struct Reply { std::uint32_t request=0;RequestStatus status=RequestStatus::accepted;bool dirty=false; };
    using Replies=std::array<Reply,kRecentRequests>;
    std::array<Replies,kMaxPeers+1> replies_{};
    Replies localReplies_{};
    std::array<Transaction,kMaxTransactions> transactions_{};
    bool Send(std::uint32_t peer,Message message) noexcept;
    bool Broadcast(Message message) noexcept;
    void Cancel(std::uint32_t id,bool broadcast,RequestStatus reason=RequestStatus::cancelled) noexcept;
    void ClearTransactions() noexcept;
    void InterruptLocalRequests() noexcept;   // this machine's requests still waiting: told interrupted
    std::uint32_t LocalWaiting() const noexcept;
    void Welcome(std::uint32_t peer) noexcept;
    void HostRequest(std::uint32_t peer,const Message&,std::uint64_t now) noexcept;
    void Advance(std::uint32_t id,std::uint64_t now) noexcept;
    void Publish(std::uint32_t peer,std::uint32_t request,RequestStatus status) noexcept;
    void Notice(std::uint32_t request,RequestStatus status) noexcept;
    void ReplyTo(std::uint32_t peer) noexcept;
    static Reply* Record(Replies& replies,std::uint32_t request,RequestStatus status) noexcept;
};
} // namespace crew::support_net
