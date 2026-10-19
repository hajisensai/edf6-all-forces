#include "../src/pvp_protocol.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <deque>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
using namespace pvp_protocol;
namespace {
void Check(bool value, const char* expression, int line) {
    if (!value) throw std::runtime_error(std::string("line ") + std::to_string(line) + ": " + expression);
}
#define CHECK(x) Check(!!(x), #x, __LINE__)
struct Packet { std::size_t from = 0, to = 0; std::vector<std::uint8_t> bytes; };
struct Network;
struct Node {
    Network& network;
    std::size_t index;
    char puid[65]{};
    Context context{};
    State native{};
    bool rejectPrepare = false, rejectApply = false, rejectRollback = false, rejectAllApply = false;
    int prepareCalls = 0, applyCalls = 0;
    Protocol protocol;
    Node(Network& net, std::size_t id);
    static bool Send(void* user, std::uint32_t peer, const void* data, std::size_t size);
    static bool Prepare(void* user, const State& state);
    static bool Apply(void* user, const State& state);
};
struct Network {
    std::vector<std::unique_ptr<Node>> nodes;
    std::deque<Packet> packets;
    std::size_t failFrom = kMaxMembers, failTo = kMaxMembers;
    Kind failKind = Kind::Hello;
    bool failOnce = false, rejectAbortOnce = false;
    std::uint64_t now = 100;
    explicit Network(std::size_t count = 3) {
        for (std::size_t i = 0; i < count; ++i) nodes.emplace_back(std::make_unique<Node>(*this, i));
        for (auto& node : nodes) {
            node->context = {true, node->index == 0, 1000 + node->index, node->index ? 1u : 0u,
                static_cast<std::uint32_t>(count - 1), node->puid};
            node->protocol.Update(node->context, now);
        }
        Drain();
        for (auto& node : nodes) CHECK(node->protocol.Ready());
    }
    std::uint32_t Peer(std::size_t local, std::size_t remote) const {
        return static_cast<std::uint32_t>(remote < local ? remote + 1 : remote);
    }
    std::size_t Remote(std::size_t local, std::uint32_t peer) const {
        const std::size_t value = peer - 1;
        return value < local ? value : value + 1;
    }
    static Message Decode(const Packet& packet) {
        Message msg{}; CHECK(wire::Decode(packet.bytes.data(), packet.bytes.size(), msg)); return msg;
    }
    bool Enqueue(std::size_t from, std::uint32_t peer, const void* data, std::size_t size) {
        CHECK(size <= 1024);
        const std::size_t to = Remote(from, peer);
        CHECK(to < nodes.size());
        Message msg{}; CHECK(wire::Decode(data, size, msg));
        if (rejectAbortOnce && msg.kind == Kind::Abort && to == 1) { rejectAbortOnce = false; return false; }
        if (failOnce && from == failFrom && to == failTo && msg.kind == failKind) { failOnce = false; return false; }
        const auto* first = static_cast<const std::uint8_t*>(data);
        packets.push_back({from, to, std::vector<std::uint8_t>(first, first + size)}); return true;
    }
    bool Deliver(Packet packet) {
        return nodes[packet.to]->protocol.Receive(Peer(packet.to, packet.from), nodes[packet.from]->puid,
            packet.bytes.data(), packet.bytes.size(), now);
    }
    void Drain() {
        int limit = 10000;
        while (!packets.empty()) { CHECK(--limit > 0); Packet packet = std::move(packets.front()); packets.pop_front(); Deliver(std::move(packet)); }
    }
    void DrainExcept(Kind held) {
        std::deque<Packet> saved;
        int limit = 10000;
        while (!packets.empty()) {
            CHECK(--limit > 0); Packet packet = std::move(packets.front()); packets.pop_front();
            if (Decode(packet).kind == held) saved.push_back(std::move(packet)); else Deliver(std::move(packet));
        }
        packets = std::move(saved);
    }
    void Tick(std::uint64_t advance) {
        now += advance; for (auto& node : nodes) node->protocol.Update(node->context, now);
    }
    void Consistent(bool enabled, std::uint32_t revision) {
        for (auto& node : nodes) {
            CHECK(node->protocol.Current().enabled == enabled);
            CHECK(node->native.enabled == enabled);
            CHECK(node->protocol.Revision() == revision);
            CHECK(!node->protocol.Busy());
            CHECK(SameState(node->protocol.Current(), nodes[0]->protocol.Current()));
        }
    }
    void Enable() { CHECK(nodes[0]->protocol.RequestMode(true, now)); Drain(); Consistent(true, 1); }
};
Node::Node(Network& net, std::size_t id) : network(net), index(id), protocol(Hooks{this, Send, Prepare, Apply}) {
    // Exercise the full 64-character identifier: maximum roster still fits.
    std::memset(puid, 'a' + static_cast<int>(id), 64); puid[64] = 0;
}
bool Node::Send(void* user, std::uint32_t peer, const void* data, std::size_t size) {
    auto& node = *static_cast<Node*>(user); return node.network.Enqueue(node.index, peer, data, size);
}
bool Node::Prepare(void* user, const State&) {
    auto& node = *static_cast<Node*>(user); ++node.prepareCalls; return !node.rejectPrepare;
}
bool Node::Apply(void* user, const State& state) {
    auto& node = *static_cast<Node*>(user); ++node.applyCalls;
    if (node.rejectAllApply) return false;
    if (node.rejectApply) { node.rejectApply = false; return false; }
    if (node.rejectRollback && state.enabled && state.entries[0].team == 2) { node.rejectRollback = false; return false; }
    node.native = state; return true;
}
Packet Encoded(std::size_t from, std::size_t to, const Message& msg) {
    std::uint8_t bytes[kMaxWireBytes]{}; const std::size_t n = wire::Encode(msg, bytes, sizeof(bytes)); CHECK(n);
    return {from, to, std::vector<std::uint8_t>(bytes, bytes + n)};
}
void TestCodec() {
    Message msg{}; msg.kind = Kind::Prepare; msg.epoch = 77; msg.revision = 2; msg.token = 18;
    msg.state.enabled = true; msg.state.count = static_cast<std::uint8_t>(kMaxMembers);
    for (std::size_t i = 0; i < kMaxMembers; ++i) { std::memset(msg.state.entries[i].puid, 'a' + static_cast<int>(i), 64); msg.state.entries[i].team = 1; }
    Packet packet = Encoded(0, 1, msg); CHECK(packet.bytes.size() == kMaxWireBytes); CHECK(packet.bytes.size() < 1024);
    Message decoded{}; CHECK(wire::Decode(packet.bytes.data(), packet.bytes.size(), decoded)); CHECK(SameState(decoded.state, msg.state));
    for (std::size_t i = 0; i < packet.bytes.size(); ++i) {
        auto bytes = packet.bytes; bytes[i] ^= 0x01; CHECK(!wire::Decode(bytes.data(), bytes.size(), decoded));
    }
    for (std::size_t n = 0; n < packet.bytes.size(); ++n) CHECK(!wire::Decode(packet.bytes.data(), n, decoded));
    auto malformed = packet.bytes;
    auto RejectField = [&](std::size_t pos, std::uint8_t value) {
        malformed = packet.bytes; malformed[pos] = value;
        wire::Put(malformed.data() + 28, wire::Checksum(malformed.data(), malformed.size()), 4);
        CHECK(!wire::Decode(malformed.data(), malformed.size(), decoded));
    };
    RejectField(0, 0); RejectField(4, 2); RejectField(5, 255); RejectField(24, 9);
    RejectField(25, 2); RejectField(26, 1); RejectField(27, 1); RejectField(32 + 65, 3);
    msg.state.entries[1] = msg.state.entries[0]; packet = Encoded(0, 1, msg);
    CHECK(!wire::Decode(packet.bytes.data(), packet.bytes.size(), decoded));
}
void TestPhasesAndTeams() {
    Network net;
    for (auto& node : net.nodes) { CHECK(node->protocol.Current().count == 3); for (const auto& entry : node->protocol.Current().entries) CHECK(entry.team == 2); }
    CHECK(!net.nodes[1]->protocol.RequestMode(true, net.now)); CHECK(net.nodes[1]->protocol.LastResult() == Result::InvalidArgument);
    CHECK(net.nodes[0]->protocol.RequestMode(true, net.now)); net.DrainExcept(Kind::Prepared);
    CHECK(net.packets.size() == 2); CHECK(!net.nodes[0]->native.enabled);
    Packet first = std::move(net.packets.front()); net.packets.pop_front(); CHECK(net.Deliver(std::move(first))); CHECK(!net.nodes[0]->native.enabled);
    net.DrainExcept(Kind::Applied);
    CHECK(net.packets.size() == 2);
    for (auto& node : net.nodes) { CHECK(node->native.enabled); CHECK(node->protocol.Busy()); CHECK(node->protocol.Revision() == 0); }
    net.Drain(); net.Consistent(true, 1);
    CHECK(net.nodes[1]->protocol.RequestTeam(1, net.now)); CHECK(net.nodes[1]->protocol.Busy());
    CHECK(!net.nodes[1]->protocol.RequestTeam(2, net.now)); net.Drain(); net.Consistent(true, 2);
    CHECK(net.nodes[0]->protocol.Current().entries[0].team == 2);
    CHECK(net.nodes[0]->protocol.Current().entries[1].team == 1);
    CHECK(net.nodes[0]->protocol.Current().entries[2].team == 2);
    CHECK(net.nodes[1]->protocol.RequestTeam(1, net.now)); net.Drain(); net.Consistent(true, 2);
    CHECK(net.nodes[1]->protocol.LastResult() == Result::Complete);
    CHECK(!net.nodes[1]->protocol.RequestTeam(3, net.now));
    CHECK(net.nodes[0]->protocol.RequestMode(false, net.now)); net.Drain(); net.Consistent(false, 3);
}
void TestPrepareAndApplyFailures() {
    Network net; net.Enable();
    net.nodes[2]->rejectPrepare = true;
    CHECK(net.nodes[0]->protocol.RequestTeam(1, net.now)); net.Drain(); net.Consistent(true, 1);
    CHECK(net.nodes[0]->protocol.LastResult() == Result::PrepareFailed);
    net.nodes[2]->rejectPrepare = false; net.nodes[1]->rejectApply = true;
    CHECK(net.nodes[0]->protocol.RequestTeam(1, net.now)); net.Drain(); net.Consistent(true, 1);
    CHECK(net.nodes[0]->protocol.LastResult() == Result::ApplyFailed);
    net.nodes[0]->rejectPrepare = true;
    CHECK(net.nodes[1]->protocol.RequestTeam(1, net.now)); net.Drain(); net.Consistent(true, 1);
    CHECK(net.nodes[1]->protocol.LastResult() == Result::Aborted);
}
void TestSendFailureAndLateAbort() {
    Network net; net.Enable();
    net.failFrom = 0; net.failTo = 2; net.failKind = Kind::Final; net.failOnce = true;
    CHECK(net.nodes[0]->protocol.RequestTeam(1, net.now)); net.Drain(); net.Consistent(true, 1);
    CHECK(net.nodes[0]->protocol.LastResult() == Result::SendFailed);
    // The first client had finalized before the second send failed. Abort must
    // still undo that finalized native change and restore its previous revision.
    CHECK(net.nodes[1]->protocol.Current().entries[0].team == 2);
}
void TestFinalThenAbortSendFailure() {
    Network net; net.Enable();
    net.failFrom = 0; net.failTo = 2; net.failKind = Kind::Final; net.failOnce = true;
    net.rejectAbortOnce = true;
    CHECK(net.nodes[0]->protocol.RequestTeam(1, net.now)); net.Drain();
    net.Consistent(false, 3); // 2 was already exposed by the partially sent Final
}
void TestReplayAndAuthority() {
    Network net; net.Enable();
    CHECK(net.nodes[0]->protocol.RequestTeam(1, net.now));
    Packet replay = net.packets.front(); net.nodes[2]->rejectPrepare = true;
    net.Drain(); net.Consistent(true, 1);
    CHECK(!net.Deliver(replay));
    Message forged = Network::Decode(replay); forged.token += 100; forged.revision += 1;
    CHECK(!net.Deliver(Encoded(0, 1, forged)));
    forged = Network::Decode(replay); forged.token += 100; forged.epoch ^= 123;
    CHECK(!net.Deliver(Encoded(0, 1, forged)));
    forged = Network::Decode(replay); forged.token += 100; std::memcpy(forged.state.entries[2].puid, "unknown-player", 15);
    CHECK(!net.Deliver(Encoded(0, 1, forged)));
    forged = Network::Decode(replay); forged.token += 100;
    CHECK(!net.Deliver(Encoded(2, 1, forged))); // authenticated non-host cannot commit
    Message request{}; request.kind = Kind::RequestMode; request.epoch = net.nodes[0]->protocol.WireEpoch();
    request.revision = 1; request.token = 500; request.arg = 0;
    CHECK(!net.Deliver(Encoded(1, 0, request)));
    request.kind = Kind::RequestTeam; request.arg = 1;
    Packet spoof = Encoded(1, 0, request);
    CHECK(!net.nodes[0]->protocol.Receive(net.Peer(0, 1), net.nodes[2]->puid, spoof.bytes.data(), spoof.bytes.size(), net.now));
    CHECK(!net.nodes[0]->protocol.Receive(3, net.nodes[1]->puid, spoof.bytes.data(), spoof.bytes.size(), net.now));
}
void TestTimeoutAndRuntimeFault() {
    Network net; net.Enable();
    CHECK(net.nodes[0]->protocol.RequestTeam(1, net.now)); net.DrainExcept(Kind::Prepared);
    net.packets.clear(); net.nodes[0]->protocol.Update(net.nodes[0]->context, net.now + kTimeoutMs + 1);
    net.Drain(); net.Consistent(true, 1); CHECK(net.nodes[0]->protocol.LastResult() == Result::TimedOut);
    net.nodes[1]->protocol.Fail(net.now); CHECK(!net.nodes[1]->native.enabled);
    net.Drain(); net.Consistent(false, 2);
    CHECK(net.nodes[0]->protocol.LastResult() == Result::Faulted);
    CHECK(net.nodes[0]->protocol.RequestMode(true, net.now)); net.Drain(); net.Consistent(true, 3);
    CHECK(net.nodes[1]->protocol.RequestTeam(1, net.now)); net.packets.clear();
    net.nodes[1]->protocol.Update(net.nodes[1]->context, net.now + kTimeoutMs + 1); net.Drain(); net.Consistent(false, 4);
}
void TestLostFinalFault() {
    Network net;
    CHECK(net.nodes[0]->protocol.RequestMode(true, net.now)); net.DrainExcept(Kind::Final);
    CHECK(net.nodes[0]->protocol.Revision() == 1); CHECK(net.nodes[1]->protocol.Revision() == 0);
    std::deque<Packet> finalPackets = std::move(net.packets); net.packets.clear();
    for (auto& packet : finalPackets) if (packet.to != 1) net.Deliver(std::move(packet));
    net.nodes[1]->protocol.Update(net.nodes[1]->context, net.now + kTimeoutMs + 1);
    net.Drain(); net.Consistent(false, 2);
}
void TestLostFinalAfterAbortedProposal() {
    Network net;
    CHECK(net.nodes[0]->protocol.RequestMode(true, net.now)); net.DrainExcept(Kind::Final);
    std::deque<Packet> finalPackets = std::move(net.packets); net.packets.clear();
    for (auto& packet : finalPackets) if (packet.to != 1) net.Deliver(std::move(packet));
    CHECK(net.nodes[0]->protocol.RequestTeam(1, net.now)); net.Drain();
    CHECK(net.nodes[0]->protocol.Busy()); // missing Final recipient cannot prepare
    net.nodes[0]->protocol.Update(net.nodes[0]->context, net.now + kTimeoutMs + 1); net.Drain();
    CHECK(net.nodes[0]->protocol.Revision() == 1);
    net.nodes[1]->protocol.Update(net.nodes[1]->context, net.now + kTimeoutMs + 1); net.Drain();
    net.Consistent(false, 2);
}
void TestRollbackFailureStopsEveryone() {
    {
        Network net; net.Enable();
        net.nodes[0]->rejectRollback = true; net.nodes[2]->rejectApply = true;
        CHECK(net.nodes[0]->protocol.RequestTeam(1, net.now)); net.Drain();
        net.Consistent(false, 2);
        CHECK(net.nodes[0]->protocol.RequestMode(true, net.now)); net.Drain(); net.Consistent(true, 3);
    }
    {
        Network net; net.Enable();
        net.nodes[1]->rejectRollback = true; net.nodes[2]->rejectApply = true;
        CHECK(net.nodes[0]->protocol.RequestTeam(1, net.now)); net.Drain(); net.Consistent(false, 2);
    }
}
void TestIncompatibleAndMissingMembers() {
    Network net;
    auto& client = *net.nodes[1];
    client.context.epoch += 100; client.protocol.Update(client.context, net.now + 1);
    net.packets.clear(); CHECK(!client.protocol.Ready());
    Message hello{}; hello.kind = Kind::Hello; hello.token = 7;
    std::memcpy(hello.puid, net.nodes[0]->puid, 65);
    Packet old = Encoded(0, 1, hello); old.bytes[4] = kVersion + 1;
    wire::Put(old.bytes.data() + 28, wire::Checksum(old.bytes.data(), old.bytes.size()), 4);
    CHECK(!net.Deliver(old)); CHECK(!client.protocol.Ready());
    CHECK(!client.protocol.RequestTeam(1, net.now));
    Packet known = Encoded(0, 1, hello); CHECK(net.Deliver(known));
    CHECK(!client.protocol.Ready()); // no third member Hello and no new Welcome
    CHECK(!client.protocol.RequestMode(true, net.now));
}
void TestResetRestoreFailureRemainsPending() {
    Network net; net.Enable();
    auto& client = *net.nodes[1];
    client.rejectApply = true; client.context.ready = false;
    client.protocol.Update(client.context, net.now + 1);
    CHECK(client.protocol.Restoring()); CHECK(!client.protocol.Ready());
    CHECK(client.protocol.LastResult() == Result::ApplyFailed); CHECK(client.native.enabled);
    client.protocol.Update(client.context, net.now + 2);
    CHECK(!client.protocol.Restoring()); CHECK(!client.native.enabled); CHECK(!client.protocol.Ready());
    auto& host = *net.nodes[0];
    host.rejectApply = true; host.context.epoch += 100;
    host.protocol.Update(host.context, net.now + 1);
    CHECK(host.protocol.Restoring()); CHECK(!host.protocol.Ready()); CHECK(host.native.enabled);
    host.protocol.Update(host.context, net.now + 2);
    CHECK(!host.protocol.Restoring()); CHECK(!host.native.enabled);
}
void TestFaultBaselineFailureRestoresLater() {
    {
        Network net; net.Enable(); auto& client = *net.nodes[1];
        client.rejectAllApply = true; client.protocol.Fail(net.now); net.Drain();
        CHECK(client.protocol.Restoring()); CHECK(!client.protocol.Ready()); CHECK(client.native.enabled);
        CHECK(client.protocol.Revision() == 2); // accepted host disabled state while restoration was pending
        net.Tick(1); CHECK(client.protocol.Restoring()); CHECK(client.native.enabled);
        client.rejectAllApply = false; net.Tick(1); net.Drain(); net.Consistent(false, 2);
        CHECK(client.protocol.Ready());
    }
    {
        Network net; net.Enable(); auto& host = *net.nodes[0];
        host.rejectAllApply = true; host.protocol.Fail(net.now); net.Drain();
        CHECK(host.protocol.Restoring()); CHECK(!host.protocol.Ready()); CHECK(host.native.enabled);
        host.rejectAllApply = false; net.Tick(1); net.Drain(); net.Consistent(false, 2); CHECK(host.protocol.Ready());
    }
    {
        Network net; net.Enable(); auto& client = *net.nodes[1];
        net.nodes[0]->protocol.Fail(net.now); client.rejectAllApply = true; net.Drain();
        CHECK(client.protocol.Restoring()); CHECK(client.native.enabled);
        client.rejectAllApply = false; net.Tick(1); net.Drain(); net.Consistent(false, 2); CHECK(client.protocol.Ready());
    }
}
void TestCapacityAndSessionReset() {
    Network net(8); net.Enable();
    net.nodes[0]->context.ready = false; net.nodes[0]->protocol.Update(net.nodes[0]->context, net.now + 10);
    CHECK(!net.nodes[0]->native.enabled); CHECK(!net.nodes[0]->protocol.Ready());
    net.nodes[1]->context.epoch += 100; net.nodes[1]->protocol.Update(net.nodes[1]->context, net.now + 10);
    CHECK(!net.nodes[1]->native.enabled); CHECK(!net.nodes[1]->protocol.Ready());
    Context oversized{true, true, 1, 0, 8, "host"};
    Protocol rejected; rejected.Update(oversized, 10); CHECK(!rejected.Ready()); CHECK(rejected.LastResult() == Result::UnsupportedRoster);
    Network solo(1); CHECK(solo.nodes[0]->protocol.RequestMode(true, solo.now)); solo.Consistent(true, 1);
    const auto oldEpoch = solo.nodes[0]->protocol.WireEpoch();
    solo.nodes[0]->protocol.Reset(); CHECK(!solo.nodes[0]->native.enabled);
    solo.nodes[0]->protocol.Update(solo.nodes[0]->context, solo.now);
    CHECK(solo.nodes[0]->protocol.Ready()); CHECK(solo.nodes[0]->protocol.WireEpoch() != oldEpoch);
    Message hello{}; hello.kind = Kind::Hello; hello.token = 99; std::memcpy(hello.puid, net.nodes[2]->puid, 65);
    Packet packet = Encoded(2, 3, hello);
    CHECK(!net.nodes[3]->protocol.Receive(net.Peer(3, 2), net.nodes[4]->puid, packet.bytes.data(), packet.bytes.size(), net.now));
    CHECK(!net.nodes[3]->protocol.Receive(net.Peer(3, 4), net.nodes[2]->puid, packet.bytes.data(), packet.bytes.size(), net.now));
}
} // namespace
int main() {
    try {
        TestCodec(); TestPhasesAndTeams(); TestPrepareAndApplyFailures();
        TestSendFailureAndLateAbort(); TestFinalThenAbortSendFailure(); TestReplayAndAuthority();
        TestTimeoutAndRuntimeFault(); TestLostFinalFault(); TestLostFinalAfterAbortedProposal(); TestRollbackFailureStopsEveryone();
        TestIncompatibleAndMissingMembers(); TestResetRestoreFailureRemainsPending();
        TestFaultBaselineFailureRestoresLater(); TestCapacityAndSessionReset();
        std::puts("pvp_protocol_check: all checks passed"); return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "pvp_protocol_check: %s\n", error.what()); return 1;
    }
}