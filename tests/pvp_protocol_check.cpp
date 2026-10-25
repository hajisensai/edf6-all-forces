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
namespace pm = crew::pvp_match;
namespace {
// Valid picks: edf soldier (team 2), edf2 soldier (team 7), monster unit (team 1), primer unit (team 10).
const pm::Pick kEdf{0, 0, pm::kNoUnit}, kEdf2{1, 0, pm::kNoUnit}, kMonster{4, 2, 5}, kPrimer{5, 2, 9};
const pm::Pick kBadAlienSoldier{4, 0, pm::kNoUnit}, kBadUnitless{4, 2, pm::kNoUnit};

int checks = 0;
void Check(bool value, const char* expression, int line) {
    ++checks;
    if (!value) throw std::runtime_error(std::string("line ") + std::to_string(line) + ": " + expression);
}
#define CHECK(x) Check(!!(x), #x, __LINE__)
struct Packet { std::size_t from = 0, to = 0; std::vector<std::uint8_t> bytes; std::uint64_t deliverAt = 0; };
struct Network;
struct Node {
    Network& network;
    std::size_t index;
    char puid[65]{};
    Context context{};
    State native{};
    bool rejectPrepare = false, rejectApply = false, rejectRollback = false, rejectAllApply = false;
    int prepareCalls = 0, applyCalls = 0, enabledApplyCalls = 0;
    bool configEnabled = true, extensionReady = true;
    Protocol protocol;
    Node(Network& net, std::size_t id);
    void Update(std::uint64_t now) {
        Context effective = context; effective.ready = effective.ready && configEnabled && extensionReady;
        protocol.Update(effective, now);
    }
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
    bool blockLease = false, blockLeaseAck = false;
    std::uint64_t latencyMs = 0;
    std::uint64_t now = 100;
    explicit Network(std::size_t count = 3) {
        for (std::size_t i = 0; i < count; ++i) nodes.emplace_back(std::make_unique<Node>(*this, i));
        for (auto& node : nodes) {
            node->context = {true, node->index == 0, 1000 + node->index, node->index ? 1u : 0u,
                static_cast<std::uint32_t>(count - 1), node->puid};
            node->Update(now);
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
        if ((blockLease && msg.kind == Kind::Lease) || (blockLeaseAck && msg.kind == Kind::LeaseAck)) return false;
        if (rejectAbortOnce && msg.kind == Kind::Abort && to == 1) { rejectAbortOnce = false; return false; }
        if (failOnce && from == failFrom && to == failTo && msg.kind == failKind) { failOnce = false; return false; }
        const auto* first = static_cast<const std::uint8_t*>(data);
        packets.push_back({from, to, std::vector<std::uint8_t>(first, first + size), now + latencyMs}); return true;
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
    void DrainDue() {
        std::deque<Packet> waiting;
        int limit = 10000;
        while (!packets.empty()) {
            CHECK(--limit > 0); Packet packet = std::move(packets.front()); packets.pop_front();
            if (packet.deliverAt > now) waiting.push_back(std::move(packet)); else Deliver(std::move(packet));
        }
        packets = std::move(waiting);
    }
    void Tick(std::uint64_t advance) {
        now += advance; for (auto& node : nodes) node->Update(now);
    }
    void Consistent(bool enabled, std::uint32_t revision) {
        for (auto& node : nodes) {
            if (node->protocol.Current().enabled != enabled)
                throw std::runtime_error("node " + std::to_string(node->index) + " enabled=" +
                    std::to_string(node->protocol.Current().enabled) + " expected=" + std::to_string(enabled) +
                    " revision=" + std::to_string(node->protocol.Revision()) + " expectedRevision=" + std::to_string(revision));
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
    node.native = state; if (state.enabled) ++node.enabledApplyCalls; return true;
}
Packet Encoded(std::size_t from, std::size_t to, const Message& msg) {
    std::uint8_t bytes[kMaxWireBytes]{}; const std::size_t n = wire::Encode(msg, bytes, sizeof(bytes)); CHECK(n);
    return {from, to, std::vector<std::uint8_t>(bytes, bytes + n)};
}
void TestCodec() {
    Message msg{}; msg.kind = Kind::Prepare; msg.epoch = 77; msg.revision = 2; msg.token = 18;
    msg.state.enabled = true; msg.state.count = static_cast<std::uint8_t>(kMaxMembers);
    for (std::size_t i = 0; i < kMaxMembers; ++i) { std::memset(msg.state.entries[i].puid, 'a' + static_cast<int>(i), 64); SetPick(msg.state.entries[i], i % 2 ? kMonster : kEdf2); }
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
    RejectField(0, 0); RejectField(4, kVersion + 1); RejectField(5, 255); RejectField(24, 9);
    RejectField(25, 2); RejectField(26, 1); RejectField(27, 1); RejectField(32 + kRelationBytes + 65, 3);
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
    CHECK(net.nodes[1]->protocol.RequestPick(kEdf2, net.now)); CHECK(net.nodes[1]->protocol.Busy());
    CHECK(!net.nodes[1]->protocol.RequestPick(kEdf, net.now)); net.Drain(); net.Consistent(true, 2);
    CHECK(net.nodes[0]->protocol.Current().entries[0].team == 2);
    CHECK(net.nodes[0]->protocol.Current().entries[1].team == 7);
    CHECK(net.nodes[0]->protocol.Current().entries[2].team == 2);
    CHECK(net.nodes[1]->protocol.RequestPick(kEdf2, net.now)); net.Drain(); net.Consistent(true, 2);
    CHECK(net.nodes[1]->protocol.LastResult() == Result::Complete);
    CHECK(!net.nodes[1]->protocol.RequestPick(kBadAlienSoldier, net.now));
    CHECK(net.nodes[0]->protocol.RequestMode(false, net.now)); net.Drain(); net.Consistent(false, 3);
}
void TestPrepareAndApplyFailures() {
    Network net; net.Enable();
    net.nodes[2]->rejectPrepare = true;
    CHECK(net.nodes[0]->protocol.RequestPick(kEdf2, net.now)); net.Drain(); net.Consistent(true, 1);
    CHECK(net.nodes[0]->protocol.LastResult() == Result::PrepareFailed);
    net.nodes[2]->rejectPrepare = false; net.nodes[1]->rejectApply = true;
    CHECK(net.nodes[0]->protocol.RequestPick(kEdf2, net.now)); net.Drain(); net.Consistent(true, 1);
    CHECK(net.nodes[0]->protocol.LastResult() == Result::ApplyFailed);
    net.nodes[0]->rejectPrepare = true;
    CHECK(net.nodes[1]->protocol.RequestPick(kEdf2, net.now)); net.Drain(); net.Consistent(true, 1);
    CHECK(net.nodes[1]->protocol.LastResult() == Result::Aborted);
}
void TestSendFailureAndLateAbort() {
    Network net; net.Enable();
    net.failFrom = 0; net.failTo = 2; net.failKind = Kind::Final; net.failOnce = true;
    CHECK(net.nodes[0]->protocol.RequestPick(kEdf2, net.now)); net.Drain(); net.Consistent(true, 1);
    CHECK(net.nodes[0]->protocol.LastResult() == Result::SendFailed);
    // The first client had finalized before the second send failed. Abort must
    // still undo that finalized native change and restore its previous revision.
    CHECK(net.nodes[1]->protocol.Current().entries[0].team == 2);
}
void TestFinalThenAbortSendFailure() {
    Network net; net.Enable();
    net.failFrom = 0; net.failTo = 2; net.failKind = Kind::Final; net.failOnce = true;
    net.rejectAbortOnce = true;
    CHECK(net.nodes[0]->protocol.RequestPick(kEdf2, net.now)); net.Drain();
    net.Consistent(false, 3); // 2 was already exposed by the partially sent Final
}
void TestReplayAndAuthority() {
    Network net; net.Enable();
    CHECK(net.nodes[0]->protocol.RequestPick(kEdf2, net.now));
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
    request.kind = Kind::RequestPick; request.arg = 1; request.role = 0; request.unit = pm::kNoUnit;
    Packet spoof = Encoded(1, 0, request);
    CHECK(!net.nodes[0]->protocol.Receive(net.Peer(0, 1), net.nodes[2]->puid, spoof.bytes.data(), spoof.bytes.size(), net.now));
    CHECK(!net.nodes[0]->protocol.Receive(3, net.nodes[1]->puid, spoof.bytes.data(), spoof.bytes.size(), net.now));
}
void TestTimeoutAndRuntimeFault() {
    Network net; net.Enable();
    CHECK(net.nodes[0]->protocol.RequestPick(kEdf2, net.now)); net.DrainExcept(Kind::Prepared);
    net.packets.clear(); net.now += kTimeoutMs + 1;
    net.nodes[0]->protocol.Update(net.nodes[0]->context, net.now);
    // Missing all later ordered traffic also expires the committed quorum;
    // timeout cannot reactivate the old enabled state with a fresh grace period.
    net.Drain(); net.Consistent(false, 2); CHECK(net.nodes[0]->protocol.LastResult() == Result::Faulted);
    CHECK(net.nodes[0]->protocol.RequestMode(true, net.now)); net.Drain(); net.Consistent(true, 3);
    net.nodes[1]->protocol.Fail(net.now); CHECK(!net.nodes[1]->native.enabled);
    net.Drain(); net.Consistent(false, 4);
    CHECK(net.nodes[0]->protocol.LastResult() == Result::Faulted);
    CHECK(net.nodes[0]->protocol.RequestMode(true, net.now)); net.Drain(); net.Consistent(true, 5);
    CHECK(net.nodes[1]->protocol.RequestPick(kEdf2, net.now)); net.packets.clear();
    net.now += kTimeoutMs + 1;
    net.nodes[1]->protocol.Update(net.nodes[1]->context, net.now); net.Drain(); net.Consistent(false, 6);
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
    CHECK(net.nodes[0]->protocol.RequestPick(kEdf2, net.now)); net.Drain();
    CHECK(net.nodes[0]->protocol.Busy()); // missing Final recipient cannot prepare
    net.nodes[0]->protocol.Update(net.nodes[0]->context, net.now + kTimeoutMs + 1); net.Drain();
    CHECK(net.nodes[0]->protocol.Revision() == 2); // quorum expiry forbids enabled rollback
    net.nodes[1]->protocol.Update(net.nodes[1]->context, net.now + kTimeoutMs + 1); net.Drain();
    net.Consistent(false, 2);
}
void TestRollbackFailureStopsEveryone() {
    {
        Network net; net.Enable();
        net.nodes[0]->rejectRollback = true; net.nodes[2]->rejectApply = true;
        CHECK(net.nodes[0]->protocol.RequestPick(kEdf2, net.now)); net.Drain();
        net.Consistent(false, 2);
        CHECK(net.nodes[0]->protocol.RequestMode(true, net.now)); net.Drain(); net.Consistent(true, 3);
    }
    {
        Network net; net.Enable();
        net.nodes[1]->rejectRollback = true; net.nodes[2]->rejectApply = true;
        CHECK(net.nodes[0]->protocol.RequestPick(kEdf2, net.now)); net.Drain(); net.Consistent(false, 2);
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
    CHECK(!client.protocol.RequestPick(kEdf2, net.now));
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
void TestCommittedLeaseSingleMemberLoss() {
    // Match all three real gates: session loss, local Enabled=0, and a local
    // Coop extension fault whose authenticated game link remains operational.
    for (unsigned cause = 0; cause < 3; ++cause) for (std::size_t failed = 0; failed < 3; ++failed) {
        Network net; net.Enable();
        if (cause == 0) net.nodes[failed]->context.ready = false;
        else if (cause == 1) net.nodes[failed]->configEnabled = false;
        else net.nodes[failed]->extensionReady = false;
        net.Tick(1); net.Drain();
        CHECK(!net.nodes[failed]->native.enabled);
        CHECK(net.nodes[(failed + 1) % 3]->native.enabled);
        for (unsigned step = 0; step < 5; ++step) { net.Tick(kLeaseProbeMs); net.Drain(); }
        for (auto& node : net.nodes) {
            CHECK(!node->protocol.Current().enabled); CHECK(!node->native.enabled); CHECK(!node->protocol.Busy());
        }
        // Stable disabled state has no quorum lease and emits no lease traffic.
        net.Tick(kLeaseTimeoutMs * 2); CHECK(net.packets.empty());
    }
}
void TestLeaseTemporaryFlowControl() {
    for (unsigned blockedKind = 0; blockedKind < 2; ++blockedKind) {
        Network net; net.Enable();
        net.blockLease = blockedKind == 0; net.blockLeaseAck = blockedKind == 1;
        for (unsigned step = 0; step < 3; ++step) {
            net.Tick(kLeaseProbeMs); net.Drain(); net.Consistent(true, 1);
            for (auto& node : net.nodes) CHECK(node->protocol.Ready());
        }
        net.blockLease = net.blockLeaseAck = false;
        for (unsigned step = 0; step < 12; ++step) {
            net.Tick(kLeaseProbeMs); net.Drain(); net.Consistent(true, 1);
        }
    }
}
void TestLeaseLatencyAndTeamCommit() {
    Network net; net.Enable(); net.latencyMs = 300; // 600 ms round trip
    for (unsigned step = 0; step < 320; ++step) {
        if (step == 80) CHECK(net.nodes[0]->protocol.RequestPick(kEdf2, net.now));
        net.Tick(50); net.DrainDue();
        for (auto& node : net.nodes) { CHECK(node->native.enabled); CHECK(node->protocol.Ready()); }
    }
    net.Drain(); net.Consistent(true, 2);
}
void TestLeaseAckReplayAndRevision() {
    Network net; net.Enable();
    net.Tick(kLeaseProbeMs); net.DrainExcept(Kind::LeaseAck);
    Packet replay{}; bool found = false;
    for (const auto& packet : net.packets) if (packet.from == 1 && packet.to == 0) { replay = packet; found = true; break; }
    CHECK(found); net.Drain(); CHECK(!net.Deliver(replay));
    Message forged = Network::Decode(replay); forged.epoch ^= 1;
    CHECK(!net.Deliver(Encoded(1, 0, forged)));
    forged = Network::Decode(replay); forged.token += 1000;
    CHECK(!net.Deliver(Encoded(1, 0, forged)));
    CHECK(!net.nodes[0]->protocol.Receive(net.Peer(0, 1), net.nodes[2]->puid,
        replay.bytes.data(), replay.bytes.size(), net.now));
    CHECK(net.nodes[0]->protocol.RequestPick(kEdf2, net.now)); net.Drain(); net.Consistent(true, 2);
    CHECK(!net.Deliver(replay)); // prior revision cannot renew the new nonce domain
    forged = Network::Decode(replay); forged.revision = 2;
    CHECK(!net.Deliver(Encoded(1, 0, forged))); // changing revision cannot recycle a consumed nonce
    net.blockLeaseAck = true;
    for (unsigned step = 0; step < 6; ++step) {
        net.Tick(kLeaseProbeMs); CHECK(!net.Deliver(replay)); net.Drain();
    }
    net.Consistent(false, 3);
}
void TestDelayedAppliedDoesNotExtendLease() {
    Network net; net.Enable();
    CHECK(net.nodes[0]->protocol.RequestPick(kEdf2, net.now)); net.DrainExcept(Kind::Applied);
    Packet held{}; bool found = false;
    while (!net.packets.empty()) {
        Packet packet = std::move(net.packets.front()); net.packets.pop_front();
        if (packet.from == 1) { held = std::move(packet); found = true; }
        else CHECK(net.Deliver(std::move(packet)));
    }
    CHECK(found);
    net.nodes[1]->context.ready = false;
    for (unsigned step = 0; step < 4; ++step) { net.Tick(kLeaseProbeMs); net.Drain(); }
    CHECK(net.Deliver(std::move(held))); net.Drain();
    CHECK(net.nodes[0]->protocol.Revision() == 2); CHECK(net.nodes[0]->native.enabled);
    net.Tick(kLeaseProbeMs); net.Drain();
    for (auto& node : net.nodes) CHECK(!node->native.enabled);
}
void TestDisableRollbackCannotRenewExpiredLease() {
    Network net; net.Enable();
    CHECK(net.nodes[0]->protocol.RequestMode(false, net.now)); net.DrainExcept(Kind::Applied);
    net.packets.clear(); net.nodes[1]->context.ready = false;
    for (unsigned step = 0; step < 6; ++step) { net.Tick(kLeaseProbeMs); net.Drain(); }
    for (auto& node : net.nodes) { CHECK(!node->native.enabled); CHECK(!node->protocol.Current().enabled); }
}
void TestLateAbortAfterDisabledFinalStaysOff() {
    Network net; net.Enable();
    net.failFrom = 0; net.failTo = 2; net.failKind = Kind::Final; net.failOnce = true;
    CHECK(net.nodes[0]->protocol.RequestMode(false, net.now)); net.DrainExcept(Kind::Abort);
    CHECK(!net.nodes[1]->native.enabled); CHECK(net.nodes[1]->protocol.Revision() == 2);
    const int enabledCalls = net.nodes[1]->enabledApplyCalls;
    std::deque<Packet> abort = std::move(net.packets); net.packets.clear();
    net.now += kLeaseTimeoutMs + 1;
    net.nodes[1]->Update(net.now); // stable disabled has deliberately stopped its lease
    for (auto& packet : abort) CHECK(net.Deliver(std::move(packet)));
    CHECK(net.nodes[1]->enabledApplyCalls == enabledCalls);
    net.Drain(); net.Consistent(false, 3);
}
void TestMixedProtocolVersionsStayDisabled() {
    Network net;
    for (auto& node : net.nodes) { node->protocol.Reset(); ++node->context.epoch; node->Update(net.now); }
    std::deque<Packet> hello = std::move(net.packets); net.packets.clear();
    for (auto& packet : hello) {
        if (packet.from == 1) {
            packet.bytes[4] = 1;
            wire::Put(packet.bytes.data() + 28, wire::Checksum(packet.bytes.data(), packet.bytes.size()), 4);
            CHECK(!net.Deliver(std::move(packet)));
        } else net.Deliver(std::move(packet));
    }
    net.Drain();
    CHECK(net.nodes[0]->protocol.LastResult() == Result::ProtocolError);
    for (auto& node : net.nodes) { CHECK(!node->protocol.Ready()); CHECK(!node->native.enabled); }
    CHECK(!net.nodes[0]->protocol.RequestMode(true, net.now));
}
pm::Relations CustomRelations() {
    pm::Relations r; r.FromPreset(pm::Preset::freeForAll);
    r.preset = static_cast<std::uint8_t>(pm::Preset::custom);
    r.rel[0][1] = r.rel[1][0] = pm::kFriend;   // edf and edf2 together
    r.rel[4][5] = r.rel[5][4] = pm::kNeutral;  // monsters and Primers leave each other alone
    r.rel[2][6] = r.rel[6][2] = pm::kNeutral;
    CHECK(r.Valid()); return r;
}
void TestWireSizesAndRoundTrip() {
    static_assert(kMaxWireBytes < 1024, "every datagram fits");
    static_assert(kMaxWireBytes == 32 + 64 + 8 * 70, "state = header + relations + entries");
    Message msg{}; msg.kind = Kind::Prepare; msg.epoch = 5; msg.revision = 3; msg.token = 11;
    msg.state.enabled = true; msg.state.count = 4; msg.state.relations = CustomRelations();
    const pm::Pick picks[4] = {kEdf, kEdf2, kMonster, kPrimer};
    for (std::size_t i = 0; i < 4; ++i) {
        std::memset(msg.state.entries[i].puid, 'a' + static_cast<int>(i), 64); SetPick(msg.state.entries[i], picks[i]);
        CHECK(ValidEntry(msg.state.entries[i]));
    }
    CHECK(msg.state.entries[0].team == 2); CHECK(msg.state.entries[1].team == 7);
    CHECK(msg.state.entries[2].team == 1); CHECK(msg.state.entries[3].team == 10);
    Packet packet = Encoded(0, 1, msg);
    CHECK(packet.bytes.size() == 32 + kRelationBytes + 4 * kEntryBytes);
    Message decoded{}; CHECK(wire::Decode(packet.bytes.data(), packet.bytes.size(), decoded));
    CHECK(SameState(decoded.state, msg.state)); CHECK(decoded.state.relations.Same(msg.state.relations));
    CHECK(decoded.state.relations.preset == static_cast<std::uint8_t>(pm::Preset::custom));
    for (std::size_t i = 0; i < 4; ++i) {
        const pm::Pick got = decoded.state.entries[i].Pick();
        CHECK(got.faction == picks[i].faction && got.role == picks[i].role && got.unit == picks[i].unit);
    }
    // A max-size state and the request.
    msg.state.count = static_cast<std::uint8_t>(kMaxMembers);
    for (std::size_t i = 0; i < kMaxMembers; ++i) {
        std::memset(msg.state.entries[i].puid, 'a' + static_cast<int>(i), 64); msg.state.entries[i].puid[64] = 0;
        SetPick(msg.state.entries[i], picks[i % 4]);
    }
    packet = Encoded(0, 1, msg); CHECK(packet.bytes.size() == kMaxWireBytes); CHECK(packet.bytes.size() < 1024);
    CHECK(wire::Decode(packet.bytes.data(), packet.bytes.size(), decoded)); CHECK(SameState(decoded.state, msg.state));
    Message request{}; request.kind = Kind::RequestPick; request.epoch = 5; request.revision = 1; request.token = 2;
    request.arg = kMonster.faction; request.role = kMonster.role; request.unit = kMonster.unit;
    packet = Encoded(1, 0, request); CHECK(packet.bytes.size() == kRequestBytes);
    CHECK(wire::Decode(packet.bytes.data(), packet.bytes.size(), decoded));
    CHECK(decoded.arg == kMonster.faction && decoded.role == kMonster.role && decoded.unit == kMonster.unit);
}
void TestEntryTeamMustMatchFaction() {
    Message msg{}; msg.kind = Kind::Prepare; msg.epoch = 5; msg.revision = 3; msg.token = 11;
    msg.state.count = 1; std::memset(msg.state.entries[0].puid, 'a', 64);
    SetPick(msg.state.entries[0], kEdf2);
    Message decoded{}; Packet packet = Encoded(0, 1, msg);
    CHECK(wire::Decode(packet.bytes.data(), packet.bytes.size(), decoded));
    for (std::uint8_t team : {std::uint8_t(0), std::uint8_t(1), std::uint8_t(2), std::uint8_t(8), std::uint8_t(12), std::uint8_t(255)}) {
        msg.state.entries[0].team = team; packet = Encoded(0, 1, msg);
        CHECK(!wire::Decode(packet.bytes.data(), packet.bytes.size(), decoded));
    }
    // An invalid pick on the wire state.
    msg.state.entries[0].team = 1; msg.state.entries[0].faction = 4; msg.state.entries[0].role = 0;
    packet = Encoded(0, 1, msg); CHECK(!wire::Decode(packet.bytes.data(), packet.bytes.size(), decoded));
    msg.state.entries[0].faction = 4; msg.state.entries[0].role = 2; msg.state.entries[0].unit = pm::kNoUnit;
    packet = Encoded(0, 1, msg); CHECK(!wire::Decode(packet.bytes.data(), packet.bytes.size(), decoded));
    // A relation table that is not valid (the preset's own table broken).
    SetPick(msg.state.entries[0], kEdf); msg.state.relations.rel[0][1] = pm::kFriend;
    packet = Encoded(0, 1, msg); CHECK(!wire::Decode(packet.bytes.data(), packet.bytes.size(), decoded));
}
void TestInvalidPicksRefused() {
    Network net; net.Enable();
    const pm::Pick forged[] = {kBadAlienSoldier, kBadUnitless, {9, 0, pm::kNoUnit}, {0, 2, 3}, {0, 0, 3}};
    for (const pm::Pick& bad : forged) {
        for (std::size_t who = 0; who < 3; ++who) {
            CHECK(!net.nodes[who]->protocol.RequestPick(bad, net.now));
            CHECK(net.nodes[who]->protocol.LastResult() == Result::InvalidArgument);
            CHECK(!net.nodes[who]->protocol.Busy());
        }
        CHECK(net.packets.empty());
    }
    net.Consistent(true, 1);
    // A forged wire request never decodes.
    Message request{}; request.kind = Kind::RequestPick; request.epoch = net.nodes[0]->protocol.WireEpoch();
    request.revision = 1; request.token = 900;
    for (const pm::Pick& bad : forged) {
        request.arg = bad.faction; request.role = bad.role; request.unit = bad.unit;
        Packet packet = Encoded(1, 0, request);
        Message decoded{}; CHECK(!wire::Decode(packet.bytes.data(), packet.bytes.size(), decoded));
        CHECK(!net.Deliver(packet));
    }
    // The same request with a valid pick is accepted.
    request.arg = kEdf2.faction; request.role = kEdf2.role; request.unit = kEdf2.unit;
    CHECK(net.Deliver(Encoded(1, 0, request))); net.Drain(); net.Consistent(true, 2);
    CHECK(net.nodes[0]->protocol.Current().entries[1].team == 7);
}
void TestAlienUnitPick() {
    Network net; net.Enable();
    CHECK(net.nodes[1]->protocol.RequestPick(kMonster, net.now)); net.Drain(); net.Consistent(true, 2);
    CHECK(net.nodes[2]->protocol.RequestPick(kPrimer, net.now)); net.Drain(); net.Consistent(true, 3);
    for (auto& node : net.nodes) {
        const auto& e = node->protocol.Current().entries;
        CHECK(e[0].team == 2); CHECK(e[1].team == 1 && e[1].role == 2 && e[1].unit == 5);
        CHECK(e[2].team == 10 && e[2].faction == 5 && e[2].unit == 9);
    }
}
void TestRelationsRequests() {
    Network net; net.Enable();
    const pm::Relations custom = CustomRelations();
    pm::Relations preset; preset.FromPreset(pm::Preset::humansVsAliens);
    // A guest cannot set the relations.
    CHECK(!net.nodes[1]->protocol.RequestRelations(preset, net.now));
    CHECK(net.nodes[1]->protocol.LastResult() == Result::InvalidArgument);
    CHECK(net.packets.empty()); net.Consistent(true, 1);
    // The host cannot set an invalid table.
    pm::Relations bad = custom; bad.rel[0][1] = pm::kEnemy;   // not symmetric
    CHECK(!net.nodes[0]->protocol.RequestRelations(bad, net.now));
    CHECK(net.nodes[0]->protocol.LastResult() == Result::InvalidArgument);
    bad = preset; bad.rel[0][4] = bad.rel[4][0] = pm::kFriend;   // a preset with another table
    CHECK(!net.nodes[0]->protocol.RequestRelations(bad, net.now));
    bad = custom; bad.rel[0][pm::kFactionCount] = bad.rel[pm::kFactionCount][0] = pm::kEnemy;   // past the catalogue
    CHECK(!net.nodes[0]->protocol.RequestRelations(bad, net.now));
    net.Consistent(true, 1);
    // The host's tables reach everyone.
    CHECK(net.nodes[0]->protocol.RequestRelations(preset, net.now)); net.Drain(); net.Consistent(true, 2);
    for (auto& node : net.nodes) { CHECK(node->protocol.Current().relations.Same(preset)); CHECK(node->native.relations.Same(preset)); }
    CHECK(net.nodes[0]->protocol.RequestRelations(custom, net.now)); net.Drain(); net.Consistent(true, 3);
    for (auto& node : net.nodes) { CHECK(node->protocol.Current().relations.Same(custom)); CHECK(node->native.relations.Same(custom)); }
    // The same table again is a no-op.
    CHECK(net.nodes[0]->protocol.RequestRelations(custom, net.now)); net.Drain(); net.Consistent(true, 3);
    // Busy: a request during a transaction is refused.
    CHECK(net.nodes[0]->protocol.RequestRelations(preset, net.now));
    CHECK(!net.nodes[0]->protocol.RequestRelations(custom, net.now));
    CHECK(net.nodes[0]->protocol.LastResult() == Result::Busy);
    net.Drain(); net.Consistent(true, 4);
}
void TestRequestMatchOneTransaction() {
    Network net;   // disabled, revision 0
    pm::Relations preset; preset.FromPreset(pm::Preset::edfCivilWar);
    State wanted{}; wanted.enabled = true; wanted.relations = preset; wanted.count = 3;
    const pm::Pick picks[3] = {kEdf2, kMonster, kPrimer};
    for (std::size_t i = 0; i < 3; ++i) { std::memcpy(wanted.entries[i].puid, net.nodes[i]->puid, 65); SetPick(wanted.entries[i], picks[i]); }
    // A guest cannot.
    CHECK(!net.nodes[1]->protocol.RequestMatch(wanted, net.now));
    CHECK(net.nodes[1]->protocol.LastResult() == Result::InvalidArgument); CHECK(net.packets.empty());
    // An invalid entry or table refuses the whole thing: nothing changes.
    State bad = wanted; bad.entries[1].team = 2;
    CHECK(!net.nodes[0]->protocol.RequestMatch(bad, net.now));
    CHECK(net.nodes[0]->protocol.LastResult() == Result::InvalidArgument);
    bad = wanted; bad.relations.rel[0][1] = pm::kNeutral;
    CHECK(!net.nodes[0]->protocol.RequestMatch(bad, net.now));
    net.Drain(); net.Consistent(false, 0);
    // Mode, relations and every pick in one transaction: one revision.
    CHECK(net.nodes[0]->protocol.RequestMatch(wanted, net.now));
    CHECK(!net.nodes[0]->protocol.RequestMatch(wanted, net.now));
    CHECK(net.nodes[0]->protocol.LastResult() == Result::Busy);
    net.Drain(); net.Consistent(true, 1);
    for (auto& node : net.nodes) {
        const State& s = node->protocol.Current();
        CHECK(s.enabled); CHECK(s.relations.Same(preset)); CHECK(node->native.relations.Same(preset));
        for (std::size_t i = 0; i < 3; ++i) {
            CHECK(!std::strcmp(s.entries[i].puid, net.nodes[i]->puid));
            const pm::Pick got = s.entries[i].Pick();
            CHECK(got.faction == picks[i].faction && got.role == picks[i].role && got.unit == picks[i].unit);
            CHECK(s.entries[i].team == pm::FactionTeam(picks[i].faction));
        }
    }
    CHECK(net.nodes[0]->prepareCalls == 1 && net.nodes[1]->prepareCalls == 1 && net.nodes[2]->prepareCalls == 1);
    // Unlisted members keep their pick; a member's apply failure rolls the whole match back.
    State second = wanted; second.count = 1; std::memcpy(second.entries[0].puid, net.nodes[0]->puid, 65); SetPick(second.entries[0], kEdf);
    second.relations = CustomRelations();
    net.nodes[2]->rejectApply = true;
    CHECK(net.nodes[0]->protocol.RequestMatch(second, net.now)); net.Drain(); net.Consistent(true, 1);
    CHECK(net.nodes[0]->protocol.LastResult() == Result::ApplyFailed);
    CHECK(net.nodes[0]->protocol.RequestMatch(second, net.now)); net.Drain(); net.Consistent(true, 2);
    for (auto& node : net.nodes) {
        const State& s = node->protocol.Current();
        CHECK(s.entries[0].team == 2 && s.entries[1].team == 1 && s.entries[2].team == 10);
        CHECK(s.relations.Same(second.relations));
    }
    // Disabling through a match.
    second.enabled = false; second.relations = preset;
    CHECK(net.nodes[0]->protocol.RequestMatch(second, net.now)); net.Drain(); net.Consistent(false, 3);
}
} // namespace
int main() {
    const char* runningTest = "startup";
#define RUN_TEST(test) do { runningTest = #test; test(); } while (false)
    try {
        RUN_TEST(TestCodec); RUN_TEST(TestPhasesAndTeams); RUN_TEST(TestPrepareAndApplyFailures);
        RUN_TEST(TestSendFailureAndLateAbort); RUN_TEST(TestFinalThenAbortSendFailure); RUN_TEST(TestReplayAndAuthority);
        RUN_TEST(TestTimeoutAndRuntimeFault); RUN_TEST(TestLostFinalFault); RUN_TEST(TestLostFinalAfterAbortedProposal); RUN_TEST(TestRollbackFailureStopsEveryone);
        RUN_TEST(TestIncompatibleAndMissingMembers); RUN_TEST(TestResetRestoreFailureRemainsPending);
        RUN_TEST(TestFaultBaselineFailureRestoresLater); RUN_TEST(TestCapacityAndSessionReset);
        RUN_TEST(TestCommittedLeaseSingleMemberLoss); RUN_TEST(TestLeaseTemporaryFlowControl); RUN_TEST(TestLeaseLatencyAndTeamCommit);
        RUN_TEST(TestLeaseAckReplayAndRevision); RUN_TEST(TestDelayedAppliedDoesNotExtendLease);
        RUN_TEST(TestDisableRollbackCannotRenewExpiredLease); RUN_TEST(TestLateAbortAfterDisabledFinalStaysOff);
        RUN_TEST(TestMixedProtocolVersionsStayDisabled);
        RUN_TEST(TestWireSizesAndRoundTrip); RUN_TEST(TestEntryTeamMustMatchFaction); RUN_TEST(TestInvalidPicksRefused);
        RUN_TEST(TestAlienUnitPick); RUN_TEST(TestRelationsRequests); RUN_TEST(TestRequestMatchOneTransaction);
        std::printf("pvp_protocol_check: %d checks passed\n", checks); return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "pvp_protocol_check: %s: %s\n", runningTest, error.what()); return 1;
    }
}
