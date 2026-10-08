#pragma once
#include "command_net.h"
#include <array>
namespace crew::command_net {
constexpr std::uint32_t kMagic=0x444D434Eu,kVersion=2,kMaxPeers=1024;
constexpr std::size_t kWireSize=828;
enum class Kind : std::uint32_t { hello=1,capability,request,result };
enum class Rpc : std::uint32_t { none,notReady,unsupported,invalid,stale,rateLimited,timeout,interrupted,count };
struct Request {
    std::uint32_t count=0;
    mapcmd::Command command{};
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
private:
    struct Peer {
        bool capable=false,have=false;
        char puid[65]{};
        std::uint64_t lastAt=0;
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
