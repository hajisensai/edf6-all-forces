#pragma once
#include "support_net.h"
#include <array>

namespace crew::support_net {
constexpr std::uint32_t kMagic=0x54525053,kVersion=1;
enum class Kind : std::uint32_t { hello=1,welcome,request,begin,unit,prepare,ready,commit,result,cancel };
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
    bool Submit(std::uint32_t catalog,const float* target,std::uint64_t now) noexcept;
    std::uint32_t SubmitPrepared(const Plan&,std::uint64_t now) noexcept;
    void Receive(std::uint32_t peer,const Message&,std::uint64_t now) noexcept;
    void Tick(std::uint64_t now) noexcept;
    void Failed(std::uint32_t transaction) noexcept;
    bool Ready() const noexcept;
    std::uint64_t Epoch() const noexcept { return epoch_; }
    std::uint32_t ActiveCount() const noexcept;
private:
    enum class Phase { empty,planning,assembling,prepared,spawning,active,cancelled };
    struct Transaction {
        Phase phase=Phase::empty;
        bool external=false;
        bool cancelConfirmed=false;
        Plan plan{};
        std::uint32_t requester=0,request=0,received=0;
        std::uint64_t since=0;
        std::array<unsigned char,kMaxPeers+1> ready{},result{};
    };
    Backend backend_{};
    bool running_=false,host_=false;
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
    std::array<Transaction,kMaxTransactions> transactions_{};
    bool Send(std::uint32_t peer,Message message) noexcept;
    bool Broadcast(Message message) noexcept;
    void Cancel(std::uint32_t id,bool broadcast) noexcept;
    void ClearTransactions() noexcept;
    void Welcome(std::uint32_t peer) noexcept;
    void HostRequest(std::uint32_t peer,const Message&,std::uint64_t now) noexcept;
    void Advance(std::uint32_t id,std::uint64_t now) noexcept;
};
} // namespace crew::support_net
