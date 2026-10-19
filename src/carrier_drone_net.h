#pragma once
#include <cstddef>
#include <cstdint>

namespace crew {
struct CommandNetworkContext;
namespace carrier_drone_net {
constexpr std::uint32_t kMagic=0x51444641u; // AFDQ
constexpr std::uint8_t kVersion=1;
constexpr std::size_t kWireBytes=80;
constexpr unsigned kMaxPeers=1024,kMaxSpawnKind=2,kRecall=3,kMaxKind=kRecall,kMaxSlot=5;
constexpr float kMaxAnchor=1.0e6f;
static_assert(kWireBytes<1024,"one authenticated support-extension payload");
// Only a request (spawn kinds 0..2; recall kind kRecall with slot/anchor zero).
// The host resolves this registered parent, authenticates the
// real seated driver and derives the creation matrix / team / owner itself.
struct Request {
    std::uint64_t epoch=0,nonce=0;
    unsigned char parentId[32]{};
    std::uint8_t kind=0,slot=0;
    float anchor[3]{};
};
bool ValidId(const unsigned char*) noexcept;
bool ValidRequest(const Request&) noexcept;
bool Owned(const void*,std::size_t) noexcept;
std::uint32_t Checksum(const unsigned char*,std::size_t) noexcept;
bool Encode(const Request&,void*,std::size_t) noexcept;
bool Decode(const void*,std::size_t,Request&) noexcept;
struct Hooks {
    void* user=nullptr;
    bool (*handle)(void*,std::uint32_t peer,const char* authenticatedPuid,
        const unsigned char* parentId,unsigned kind,unsigned slot,const float* anchor) noexcept=nullptr;
};
// The support poll is the only consumer, on the game thread. Successful sends
// use its reliable ordered channel. This protocol never creates objects and
// never adds a second spawn/ACK/activate transaction beside support Session.
// A lost/rebound context retires its shared epoch, including one-sided local
// backend changes: only a freshly negotiated shared epoch permits resuming.
class Protocol {
public:
    explicit Protocol(Hooks hooks={}) noexcept:hooks_(hooks) {}
    void Reset() noexcept;
    void Update(const CommandNetworkContext&) noexcept;
    bool Submit(const unsigned char parentId[32],unsigned kind,unsigned slot,const float anchor[3]) noexcept;
    // true means a valid, fresh request was consumed, even if authorization or
    // the support transaction rejected it. Such a nonce must never run again.
    bool Receive(std::uint32_t peer,const char* authenticatedPuid,const void*,std::size_t) noexcept;
    bool Ready() const noexcept {return ready_;}
    std::uint64_t Epoch() const noexcept {return epoch_;}
private:
    struct Peer {std::uint64_t nonce=0;char puid[65]{};};
    Hooks hooks_{};
    bool ready_=false,host_=false;
    std::uint64_t epoch_=0,retiredEpoch_=0,nextNonce_=0;
    std::uint32_t hostPeer_=0,peerCount_=0;
    char localPuid_[65]{};
    bool (*send_)(std::uint32_t,const void*,std::size_t) noexcept=nullptr;
    Peer peers_[kMaxPeers+1]{};
};
} // namespace carrier_drone_net

// The dispatcher owns all native permission checks and calls the existing
// support Session's prepared-plan transaction. false has no local fallback.
bool HandleCarrierDroneNetworkRequest(std::uint32_t peer,const char* authenticatedPuid,
    const unsigned char parentId[32],unsigned kind,unsigned slot,const float anchor[3]) noexcept;
void UpdateCarrierDroneNetwork(const CommandNetworkContext&,std::uint64_t now) noexcept;
bool ReceiveCarrierDroneNetwork(std::uint32_t peer,const char* authenticatedPuid,
    const void*,std::size_t,std::uint64_t now) noexcept;
void ResetCarrierDroneNetwork() noexcept;
// true only means the request entered the reliable queue. The existing support
// transaction owns creation, registration, owner binding, ACKs and activation.
// Only a real next input should retry a rejected/failed request, using a new nonce.
bool SubmitCarrierDroneNetworkRequest(const unsigned char parentId[32],unsigned kind,
    unsigned slot,const float anchor[3]) noexcept;
} // namespace crew
