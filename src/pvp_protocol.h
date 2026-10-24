#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

// Authenticated transport peer indices are local-only. No payload claims sender
// authority: a PUID is accepted only from Receive's authenticatedPuid argument.
// Generation is opaque/local; only an authenticated host Welcome creates the
// shared wire epoch. At most eight total members keep every datagram < 1024 B.
// Transport contract: successful sends are reliable and ordered; permanent
// delivery/link failure must invalidate the affected Context. That invalidation
// can be local: enabled commits additionally require fresh ACKs from every
// participant, so an isolated extension fault cannot leave the others enabled.
namespace pvp_protocol {
constexpr std::uint8_t kVersion = 2;
constexpr std::size_t kMaxMembers = 8;
constexpr std::size_t kMaxWireBytes = 32 + kMaxMembers * 66;
constexpr std::uint64_t kTimeoutMs = 5000;
constexpr std::uint64_t kLeaseProbeMs = 1000;
constexpr std::uint64_t kLeaseTimeoutMs = 5000;
constexpr std::size_t kLeaseProbeWindow = kLeaseTimeoutMs / kLeaseProbeMs + 1;
struct Entry { char puid[65]{}; std::uint8_t team = 2; };
struct State { bool enabled = false; std::uint8_t count = 0; Entry entries[kMaxMembers]{}; };
struct Context {
    bool ready = false, host = false;
    std::uint64_t epoch = 0;
    std::uint32_t hostPeer = 0, peerCount = 0;
    const char* localPuid = nullptr;
};
struct Hooks {
    void* user = nullptr;
    bool (*Send)(void*, std::uint32_t, const void*, std::size_t) = nullptr;
    bool (*Prepare)(void*, const State&) = nullptr;
    bool (*Apply)(void*, const State&) = nullptr;
};
enum class Result {
    Idle, Pending, Complete, NotReady, Busy, InvalidArgument, UnsupportedRoster,
    ProtocolError, PrepareFailed, ApplyFailed, SendFailed, TimedOut, Aborted,
    SessionLost, Faulted
};
enum class Kind : std::uint8_t {
    Hello = 1, Welcome, RequestMode, RequestTeam, Prepare, Prepared,
    Apply, Applied, Final, Abort, Fault, FailSafe, Reject, Lease, LeaseAck
};

inline bool ValidPuid(const char* value) noexcept {
    if (!value || !value[0]) return false;
    for (std::size_t i = 0; i < 65; ++i) {
        const auto c = static_cast<unsigned char>(value[i]);
        if (!c) return true;
        if (c < 33 || c > 126) return false;
    }
    return false;
}
inline bool SameState(const State& a, const State& b) noexcept {
    if (a.enabled != b.enabled || a.count != b.count) return false;
    for (std::size_t i = 0; i < a.count; ++i)
        if (a.entries[i].team != b.entries[i].team ||
            std::strcmp(a.entries[i].puid, b.entries[i].puid)) return false;
    return true;
}

// The byte codec deliberately does not serialize native structs or their padding.
// FNV-1a protects corruption/framing; authentication belongs to the Coop channel.
struct Message {
    Kind kind = Kind::Hello;
    std::uint64_t epoch = 0;
    std::uint32_t revision = 0, token = 0;
    std::uint8_t arg = 0, status = 0;
    char puid[65]{};
    State state{};
};
namespace wire {
inline void Put(std::uint8_t* out, std::uint64_t value, std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i) out[i] = static_cast<std::uint8_t>(value >> (i * 8));
}
inline std::uint64_t Get(const std::uint8_t* in, std::size_t n) noexcept {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < n; ++i) value |= std::uint64_t(in[i]) << (i * 8);
    return value;
}
inline std::uint32_t Checksum(const std::uint8_t* bytes, std::size_t n) noexcept {
    std::uint32_t hash = 2166136261u;
    for (std::size_t i = 0; i < n; ++i) hash = (hash ^ ((i >= 28 && i < 32) ? 0 : bytes[i])) * 16777619u;
    return hash;
}
inline bool HasState(Kind kind) noexcept { return kind == Kind::Welcome || kind == Kind::Prepare || kind == Kind::FailSafe; }
inline std::size_t Encode(const Message& msg, void* buffer, std::size_t capacity) noexcept {
    const bool state = HasState(msg.kind);
    if (msg.state.count > kMaxMembers) return 0;
    const std::size_t n = 32 + (msg.kind == Kind::Hello ? 65 : state ? msg.state.count * 66 : 0);
    if (!buffer || n > capacity) return 0;
    auto* out = static_cast<std::uint8_t*>(buffer);
    std::memset(out, 0, n);
    Put(out, 0x56504641u, 4); // AFPV
    out[4] = kVersion; out[5] = static_cast<std::uint8_t>(msg.kind);
    Put(out + 6, n, 2); Put(out + 8, msg.epoch, 8);
    Put(out + 16, msg.revision, 4); Put(out + 20, msg.token, 4);
    if (state) { out[24] = msg.state.count; out[25] = msg.state.enabled ? 1 : 0; }
    out[26] = msg.arg; out[27] = msg.status;
    if (msg.kind == Kind::Hello) std::memcpy(out + 32, msg.puid, 65);
    for (std::size_t i = 0; state && i < msg.state.count; ++i) {
        std::memcpy(out + 32 + i * 66, msg.state.entries[i].puid, 65);
        out[32 + i * 66 + 65] = msg.state.entries[i].team;
    }
    Put(out + 28, Checksum(out, n), 4);
    return n;
}
inline bool Decode(const void* buffer, std::size_t n, Message& msg) noexcept {
    if (!buffer || n < 32 || n > kMaxWireBytes) return false;
    const auto* in = static_cast<const std::uint8_t*>(buffer);
    if (Get(in, 4) != 0x56504641u || in[4] != kVersion || Get(in + 6, 2) != n ||
        Get(in + 28, 4) != Checksum(in, n) || in[5] < 1 || in[5] > static_cast<std::uint8_t>(Kind::LeaseAck)) return false;
    Message next{};
    next.kind = static_cast<Kind>(in[5]);
    next.epoch = Get(in + 8, 8); next.revision = static_cast<std::uint32_t>(Get(in + 16, 4));
    next.token = static_cast<std::uint32_t>(Get(in + 20, 4)); next.arg = in[26]; next.status = in[27];
    const bool state = HasState(next.kind);
    if (in[24] > kMaxMembers || in[25] > 1 || (!state && (in[24] || in[25]))) return false;
    const std::size_t expected = 32 + (next.kind == Kind::Hello ? 65 : state ? std::size_t(in[24]) * 66 : 0);
    if (n != expected) return false;
    if (next.kind == Kind::Hello) {
        std::memcpy(next.puid, in + 32, 65);
        if (!ValidPuid(next.puid) || next.epoch || next.revision || !next.token) return false;
    }
    if (state) {
        next.state.count = in[24]; next.state.enabled = in[25] != 0;
        if (!next.state.count) return false;
        for (std::size_t i = 0; i < next.state.count; ++i) {
            auto& entry = next.state.entries[i];
            std::memcpy(entry.puid, in + 32 + i * 66, 65); entry.team = in[32 + i * 66 + 65];
            if (!ValidPuid(entry.puid) || (entry.team != 1 && entry.team != 2)) return false;
            for (std::size_t j = 0; j < i; ++j)
                if (!std::strcmp(entry.puid, next.state.entries[j].puid)) return false;
        }
    }
    const bool request = next.kind == Kind::RequestMode || next.kind == Kind::RequestTeam;
    const bool ack = next.kind == Kind::Prepared || next.kind == Kind::Applied || next.kind == Kind::Reject;
    if ((!request && next.arg) || (!ack && next.status) || (ack && next.status > 1) ||
        (next.kind == Kind::RequestMode && next.arg > 1) ||
        (next.kind == Kind::RequestTeam && next.arg != 1 && next.arg != 2)) return false;
    if (next.kind != Kind::Hello && (!next.epoch || !next.token)) return false;
    msg = next;
    return true;
}
} // namespace wire

class Protocol {
public:
    explicit Protocol(Hooks hooks = {}) noexcept : hooks_(hooks) {}
    const State& Current() const noexcept { return state_; }
    bool Restoring() const noexcept { return restorePending_; }
    bool Busy() const noexcept { return phase_ != Phase::None || requestPending_; }
    bool Ready() const noexcept { return context_.ready && !broken_ && !restorePending_ && initialized_ && AllKnown(); }
    Result LastResult() const noexcept { return result_; }
    std::uint32_t Revision() const noexcept { return revision_; }
    std::uint64_t WireEpoch() const noexcept { return sharedEpoch_; }
    void Reset() noexcept {
        const State disabled{};
        const bool restored = Baseline(disabled);
        context_ = {}; local_[0] = 0; state_ = {}; previous_ = {}; pending_ = {};
        std::memset(peers_, 0, sizeof(peers_));
        std::memset(peerTokens_, 0, sizeof(peerTokens_));
        std::memset(requests_, 0, sizeof(requests_));
        initialized_ = saved_ = requestPending_ = preparedOk_ = false;
        broken_ = false; phase_ = Phase::None;
        revision_ = transaction_ = committedToken_ = exposedRevision_ = previousRevision_ = localToken_ = 0;
        sharedEpoch_ = deadline_ = lastHello_ = 0;
        StopLease();
        result_ = restored ? Result::SessionLost : Result::ApplyFailed;
        ++seed_;
    }
    void Update(const Context& next, std::uint64_t now) noexcept {
        if (restorePending_ && !Baseline(restoreState_)) { result_ = Result::ApplyFailed; return; }
        const bool valid = next.ready && ValidPuid(next.localPuid) && next.peerCount < kMaxMembers &&
            (next.host || (next.hostPeer && next.hostPeer <= next.peerCount));
        if (!valid) {
            if (restorePending_ || context_.ready || state_.enabled || Busy()) Reset();
            if (restorePending_) return;
            result_ = next.ready && next.peerCount >= kMaxMembers ? Result::UnsupportedRoster : Result::NotReady;
            return;
        }
        const bool changed = !context_.ready || context_.epoch != next.epoch || context_.host != next.host ||
            context_.hostPeer != next.hostPeer || context_.peerCount != next.peerCount || std::strcmp(local_, next.localPuid);
        if (changed) {
            Reset(); if (restorePending_) return;
            context_ = next; std::memcpy(local_, next.localPuid, std::strlen(next.localPuid) + 1);
            context_.localPuid = local_;
            std::uint64_t mix = next.epoch ^ now ^ (std::uint64_t(seed_) << 32) ^ 0x5046563153455353ull;
            for (const char* p = local_; *p; ++p) mix = (mix ^ static_cast<unsigned char>(*p)) * 1099511628211ull;
            localToken_ = static_cast<std::uint32_t>(mix ^ (mix >> 32));
            if (!localToken_) localToken_ = 1;
            if (context_.host) sharedEpoch_ = mix ? mix : 1;
            result_ = Result::Idle;
            Initialize();
            SendHello(now);
        }
        if (broken_) return;
        if (Busy() && now >= deadline_) {
            if (context_.host) Abort(Result::TimedOut, now);
            else Fail(now);
        }
        UpdateLease(now);
        if (!Ready() && (!lastHello_ || now - lastHello_ >= 1000)) SendHello(now);
    }
    bool RequestMode(bool enabled, std::uint64_t now) noexcept {
        if (!context_.host) { result_ = Result::InvalidArgument; return false; }
        return Request(Kind::RequestMode, enabled ? 1 : 0, now);
    }
    bool RequestTeam(std::uint8_t team, std::uint64_t now) noexcept {
        if (team != 1 && team != 2) { result_ = Result::InvalidArgument; return false; }
        return Request(Kind::RequestTeam, team, now);
    }
    // A runtime invariant failed after commit. Restore the native baseline on the
    // reporting peer immediately, and make the authenticated host disable all.
    void Fail(std::uint64_t now) noexcept {
        (void)now;
        if (!context_.ready) { Reset(); return; }
        if (context_.host) { FailSafe(); return; }
        State disabled = state_; disabled.enabled = false;
        const bool restored = Baseline(disabled);
        state_ = disabled; phase_ = Phase::None; saved_ = requestPending_ = false;
        StopLease();
        result_ = restored ? Result::Faulted : Result::ApplyFailed;
        if (sharedEpoch_) {
            Message msg = Control(Kind::Fault); msg.token = transaction_ ? transaction_ : NextToken();
            if (!Send(context_.hostPeer, msg)) broken_ = true;
        }
    }
    bool Receive(std::uint32_t peer, const char* authenticatedPuid, const void* data,
                 std::size_t size, std::uint64_t now) noexcept {
        if (!context_.ready || broken_ || !peer || peer > context_.peerCount || !ValidPuid(authenticatedPuid)) return false;
        Message msg{};
        if (!wire::Decode(data, size, msg)) {
            const auto* bytes = static_cast<const std::uint8_t*>(data);
            if (bytes && size >= 5 && wire::Get(bytes, 4) == 0x56504641u && bytes[4] != kVersion)
                result_ = Result::ProtocolError;
            return false;
        }
        if (msg.kind == Kind::Hello) return Hello(peer, authenticatedPuid, msg, now);
        if (!peers_[peer][0] || std::strcmp(peers_[peer], authenticatedPuid)) return false;
        if (msg.kind == Kind::Welcome) return Welcome(peer, msg);
        const bool fromHost = !context_.host && peer == context_.hostPeer;
        if (msg.epoch != sharedEpoch_) return false;
        const bool safetyDuringRestore = restorePending_ && initialized_ && AllKnown() && fromHost && msg.kind == Kind::FailSafe;
        if (!Ready() && !safetyDuringRestore) return false;
        if (msg.kind == Kind::Lease || msg.kind == Kind::LeaseAck) {
            if (!state_.enabled || msg.revision != revision_) return false;
            if (msg.kind == Kind::Lease) {
                Message ack = Control(Kind::LeaseAck); ack.token = msg.token;
                // Admission can be temporarily flow-controlled. A missed ACK
                // does not renew the sender's lease or reset this session.
                return Send(peer, ack);
            }
            if (!leaseArmed_ || leaseRevision_ != revision_ || now >= leaseDeadlines_[peer]) return false;
            // Measure freshness from our challenge, not the arrival of an old
            // delayed ACK. Each nonce can renew exactly once.
            for (auto& probe : leaseProbes_[peer]) {
                if (probe.token != msg.token) continue;
                const auto renewed = probe.issued + kLeaseTimeoutMs;
                probe = {};
                if (now >= renewed) return false;
                if (renewed > leaseDeadlines_[peer]) leaseDeadlines_[peer] = renewed;
                return true;
            }
            return false;
        }
        if (msg.kind == Kind::Fault) {
            const bool lostFinal = revision_ && msg.revision == revision_ - 1 && msg.token == committedToken_;
            if (!context_.host || (msg.revision != revision_ && !lostFinal)) return false;
            FailSafe(); return true;
        }
        if (msg.kind == Kind::FailSafe) {
            if (!fromHost || msg.revision <= revision_ || msg.state.enabled || !ValidateRoster(msg.state)) return false;
            const State disabled = msg.state;
            const bool ok = Baseline(disabled);
            state_ = disabled; revision_ = msg.revision; transaction_ = committedToken_ = msg.token;
            phase_ = Phase::None; saved_ = requestPending_ = false; result_ = ok ? Result::Faulted : Result::ApplyFailed;
            StopLease();
            return true;
        }
        if (msg.kind == Kind::Reject) {
            if (!fromHost || !requestPending_ || msg.token != serial_ || msg.revision < revision_) return false;
            requestPending_ = false; result_ = msg.status ? Result::Aborted : Result::Complete; return true;
        }
        if (msg.kind == Kind::RequestMode || msg.kind == Kind::RequestTeam) {
            if (!context_.host || msg.kind == Kind::RequestMode || msg.token <= requests_[peer]) return false;
            if (Busy() || msg.revision != revision_) {
                Message reject = Control(Kind::Reject); reject.token = msg.token; reject.status = 1;
                Send(peer, reject); return false;
            }
            requests_[peer] = msg.token;
            State candidate = state_;
            if (msg.kind == Kind::RequestMode) candidate.enabled = msg.arg != 0;
            else if (!SetTeam(candidate, authenticatedPuid, msg.arg)) return false;
            const bool unchanged = SameState(candidate, state_);
            const bool accepted = Begin(candidate, now);
            if (!accepted || unchanged) {
                Message reply = Control(Kind::Reject); reply.token = msg.token; reply.status = accepted ? 0 : 1;
                Send(peer, reply);
            }
            return accepted;
        }
        if (msg.kind == Kind::Prepare) {
            if (!fromHost || phase_ != Phase::None || msg.token <= transaction_ || msg.revision != revision_ + 1 || !msg.revision || !ValidateRoster(msg.state)) return false;
            requestPending_ = false;
            previous_ = state_; previousRevision_ = revision_; pending_ = msg.state;
            transaction_ = msg.token; saved_ = true; phase_ = Phase::Prepared; deadline_ = now + kTimeoutMs;
            const bool ok = !hooks_.Prepare || hooks_.Prepare(hooks_.user, pending_);
            preparedOk_ = ok;
            Message ack = Control(Kind::Prepared); ack.revision = msg.revision; ack.status = ok ? 1 : 0;
            if (!Send(peer, ack)) { Fail(now); return false; }
            if (!ok) result_ = Result::PrepareFailed; else result_ = Result::Pending;
            return true;
        }
        if (msg.kind == Kind::Abort) {
            if (!fromHost || !saved_ || msg.token != transaction_ || msg.revision != previousRevision_ + 1) return false;
            if (!Rollback(Result::Aborted, now)) Fail(now); return true;
        }
        if (msg.kind == Kind::Final) {
            if (!fromHost || phase_ != Phase::Applied || !Matches(msg)) return false;
            revision_ = msg.revision; committedToken_ = transaction_; phase_ = Phase::None; result_ = Result::Complete;
            UpdateLease(now); return true;
        }
        if (msg.kind == Kind::Apply) {
            if (!fromHost || phase_ != Phase::Prepared || !preparedOk_ || !Matches(msg)) return false;
            const bool ok = !hooks_.Apply || hooks_.Apply(hooks_.user, pending_);
            if (ok) { state_ = pending_; phase_ = Phase::Applied; }
            Message ack = Control(Kind::Applied); ack.revision = msg.revision; ack.status = ok ? 1 : 0;
            if (!Send(peer, ack)) { Fail(now); return false; }
            if (!ok) result_ = Result::ApplyFailed;
            return true;
        }
        if (msg.kind == Kind::Prepared || msg.kind == Kind::Applied) {
            const Phase expected = msg.kind == Kind::Prepared ? Phase::Preparing : Phase::Applying;
            if (!context_.host || phase_ != expected || !Matches(msg) || acknowledgements_[peer]) return false;
            if (!msg.status) { Abort(msg.kind == Kind::Prepared ? Result::PrepareFailed : Result::ApplyFailed, now); return true; }
            acknowledgements_[peer] = true;
            if (AllAcknowledged()) Advance(now);
            return true;
        }
        return false;
    }
private:
    enum class Phase { None, Preparing, Applying, Prepared, Applied };
    Hooks hooks_{}; Context context_{}; char local_[65]{};
    char peers_[kMaxMembers][65]{};
    std::uint32_t peerTokens_[kMaxMembers]{}, requests_[kMaxMembers]{};
    bool acknowledgements_[kMaxMembers]{};
    State state_{}, previous_{}, pending_{}, restoreState_{};
    bool initialized_ = false, broken_ = false, saved_ = false, requestPending_ = false, preparedOk_ = false, restorePending_ = false;
    Phase phase_ = Phase::None; Result result_ = Result::Idle;
    std::uint32_t revision_ = 0, transaction_ = 0, committedToken_ = 0, exposedRevision_ = 0, previousRevision_ = 0, localToken_ = 0, seed_ = 0, serial_ = 0;
    std::uint64_t sharedEpoch_ = 0, deadline_ = 0, lastHello_ = 0;
    struct LeaseProbe { std::uint32_t token = 0; std::uint64_t issued = 0; };
    bool leaseArmed_ = false;
    std::uint32_t leaseSerial_ = 0, leaseRevision_ = 0;
    std::size_t leaseRound_ = 0;
    LeaseProbe leaseProbes_[kMaxMembers][kLeaseProbeWindow]{};
    std::uint64_t nextLeaseProbe_ = 0, leaseDeadlines_[kMaxMembers]{};
    void StopLease() noexcept {
        leaseArmed_ = false; leaseRevision_ = 0; leaseRound_ = 0; nextLeaseProbe_ = 0;
        for (auto& peer : leaseProbes_) for (auto& probe : peer) probe = {};
        std::memset(leaseDeadlines_, 0, sizeof(leaseDeadlines_));
    }
    void UpdateLease(std::uint64_t now) noexcept {
        if (!Ready()) { StopLease(); return; }
        if (!state_.enabled) {
            // A disable Apply can still roll back to enabled. Retain its old
            // deadlines until Final, rather than granting fresh time on Abort.
            if (phase_ == Phase::None) StopLease();
            return;
        }
        if (!leaseArmed_) {
            if (phase_ != Phase::None) return; // first enable is still uncommitted
            StopLease(); leaseArmed_ = true; leaseRevision_ = revision_;
            for (std::uint32_t i = 1; i <= context_.peerCount; ++i) leaseDeadlines_[i] = now + kLeaseTimeoutMs;
        } else if (leaseRevision_ != revision_) {
            // A delayed Final/rollback is not fresh evidence of peer liveness.
            // Change the nonce domain without extending any confirmed deadline.
            leaseRevision_ = revision_; leaseRound_ = 0; nextLeaseProbe_ = 0;
            for (auto& peer : leaseProbes_) for (auto& probe : peer) probe = {};
        }
        if (LeaseExpired(now)) { Fail(now); StopLease(); return; }
        if (now < nextLeaseProbe_) return;
        nextLeaseProbe_ = now + kLeaseProbeMs;
        for (std::uint32_t i = 1; i <= context_.peerCount; ++i) {
            if (!++leaseSerial_) ++leaseSerial_;
            Message msg = Control(Kind::Lease); msg.token = leaseSerial_;
            auto& probe = leaseProbes_[i][leaseRound_]; probe = {};
            // Keep a bounded window of independently fresh challenges: delayed
            // ACKs remain valid, while temporary probe/ACK admission refusal is
            // naturally covered by the next round without extending any nonce.
            if (Send(i, msg)) probe = {msg.token, now};
        }
        leaseRound_ = (leaseRound_ + 1) % kLeaseProbeWindow;
    }
    bool LeaseExpired(std::uint64_t now) const noexcept {
        if (leaseArmed_) for (std::uint32_t i = 1; i <= context_.peerCount; ++i)
            if (now >= leaseDeadlines_[i]) return true;
        return false;
    }
    bool Baseline(const State& disabled) noexcept {
        const bool ok = !hooks_.Apply || hooks_.Apply(hooks_.user, disabled);
        restorePending_ = !ok;
        if (!ok) restoreState_ = disabled;
        return ok;
    }
    std::uint32_t NextToken() noexcept { if (!++serial_) ++serial_; return serial_; }
    bool AllKnown() const noexcept {
        for (std::uint32_t i = 1; i <= context_.peerCount; ++i) if (!peers_[i][0]) return false;
        return true;
    }
    bool ValidateRoster(const State& state) const noexcept {
        if (!AllKnown() || state.count != context_.peerCount + 1) return false;
        for (std::size_t i = 0; i < state.count; ++i) {
            const auto& entry = state.entries[i];
            if (!ValidPuid(entry.puid) || (entry.team != 1 && entry.team != 2)) return false;
            bool known = !std::strcmp(entry.puid, local_);
            for (std::uint32_t j = 1; j <= context_.peerCount; ++j)
                known = known || !std::strcmp(entry.puid, peers_[j]);
            if (!known) return false;
            for (std::size_t j = 0; j < i; ++j)
                if (!std::strcmp(entry.puid, state.entries[j].puid)) return false;
        }
        return true;
    }
    void Initialize() noexcept {
        if (!AllKnown()) return;
        if (context_.host && !initialized_) {
            state_ = {}; state_.count = static_cast<std::uint8_t>(context_.peerCount + 1);
            std::memcpy(state_.entries[0].puid, local_, 65);
            for (std::uint32_t i = 1; i <= context_.peerCount; ++i) std::memcpy(state_.entries[i].puid, peers_[i], 65);
            initialized_ = true;
        } else if (!context_.host && sharedEpoch_ && ValidateRoster(state_)) initialized_ = true;
        if (context_.host && initialized_ && revision_ == 0 && !Busy()) {
            for (std::uint32_t i = 1; i <= context_.peerCount; ++i) {
                Message msg = Control(Kind::Welcome); msg.token = peerTokens_[i]; msg.state = state_;
                if (!Send(i, msg)) { broken_ = true; result_ = Result::SendFailed; }
            }
        }
    }
    bool Hello(std::uint32_t peer, const char* authenticatedPuid, const Message& msg, std::uint64_t now) noexcept {
        if (std::strcmp(authenticatedPuid, msg.puid) || !std::strcmp(local_, authenticatedPuid)) return false;
        for (std::uint32_t i = 1; i <= context_.peerCount; ++i)
            if (i != peer && peers_[i][0] && !std::strcmp(peers_[i], authenticatedPuid)) return false;
        if (peers_[peer][0] && (std::strcmp(peers_[peer], authenticatedPuid) || peerTokens_[peer] != msg.token)) {
            Fail(now); broken_ = true; result_ = Result::SessionLost; return false;
        }
        std::memcpy(peers_[peer], authenticatedPuid, std::strlen(authenticatedPuid) + 1);
        peerTokens_[peer] = msg.token; Initialize(); return true;
    }
    bool Welcome(std::uint32_t peer, const Message& msg) noexcept {
        if (context_.host || peer != context_.hostPeer || msg.token != localToken_ || msg.revision ||
            msg.state.enabled || Busy() || revision_ || (sharedEpoch_ && sharedEpoch_ != msg.epoch)) return false;
        // Welcome can precede other peers' Hello messages. It remains disabled,
        // and Ready waits until the entire authenticated identity set matches.
        bool self = false, host = false;
        for (std::size_t i = 0; i < msg.state.count; ++i) {
            self = self || !std::strcmp(msg.state.entries[i].puid, local_);
            host = host || !std::strcmp(msg.state.entries[i].puid, peers_[peer]);
        }
        if (!self || !host || msg.state.count != context_.peerCount + 1) return false;
        state_ = msg.state; sharedEpoch_ = msg.epoch; Initialize(); return true;
    }
    void SendHello(std::uint64_t now) noexcept {
        lastHello_ = now ? now : 1;
        Message msg{}; msg.kind = Kind::Hello; msg.token = localToken_; std::memcpy(msg.puid, local_, 65);
        for (std::uint32_t i = 1; i <= context_.peerCount; ++i)
            if (!Send(i, msg)) result_ = Result::SendFailed;
    }
    Message Control(Kind kind) const noexcept {
        Message msg{}; msg.kind = kind; msg.epoch = sharedEpoch_;
        msg.revision = revision_; msg.token = transaction_ ? transaction_ : 1; return msg;
    }
    bool Send(std::uint32_t peer, const Message& msg) noexcept {
        std::uint8_t bytes[kMaxWireBytes]{};
        const std::size_t n = wire::Encode(msg, bytes, sizeof(bytes));
        return n && hooks_.Send && hooks_.Send(hooks_.user, peer, bytes, n);
    }
    bool Broadcast(Message msg) noexcept {
        bool ok = true;
        for (std::uint32_t i = 1; i <= context_.peerCount; ++i) if (!Send(i, msg)) ok = false;
        return ok;
    }
    static bool SetTeam(State& state, const char* puid, std::uint8_t team) noexcept {
        for (std::size_t i = 0; i < state.count; ++i)
            if (!std::strcmp(state.entries[i].puid, puid)) { state.entries[i].team = team; return true; }
        return false;
    }
    bool Request(Kind kind, std::uint8_t value, std::uint64_t now) noexcept {
        if (!Ready()) { result_ = Result::NotReady; return false; }
        if (Busy()) { result_ = Result::Busy; return false; }
        if (context_.host) {
            State candidate = state_;
            if (kind == Kind::RequestMode) candidate.enabled = value != 0;
            else if (!SetTeam(candidate, local_, value)) return false;
            return Begin(candidate, now);
        }
        Message msg = Control(kind); msg.token = NextToken(); msg.arg = value;
        const bool ok = Send(context_.hostPeer, msg);
        requestPending_ = ok; deadline_ = now + kTimeoutMs;
        result_ = ok ? Result::Pending : Result::SendFailed; return ok;
    }
    bool Begin(const State& candidate, std::uint64_t now) noexcept {
        if (!ValidateRoster(candidate) || revision_ >= UINT32_MAX - 1) { result_ = Result::ProtocolError; return false; }
        if (SameState(candidate, state_)) { result_ = Result::Complete; return true; }
        if (hooks_.Prepare && !hooks_.Prepare(hooks_.user, candidate)) { result_ = Result::PrepareFailed; return false; }
        previous_ = state_; previousRevision_ = revision_; pending_ = candidate; saved_ = true;
        transaction_ = NextToken(); phase_ = Phase::Preparing; deadline_ = now + kTimeoutMs; result_ = Result::Pending;
        std::memset(acknowledgements_, 0, sizeof(acknowledgements_));
        Message msg = Control(Kind::Prepare); msg.revision = revision_ + 1; msg.state = candidate;
        if (!Broadcast(msg)) { Abort(Result::SendFailed, now); return false; }
        if (!context_.peerCount) Advance(now);
        return result_ == Result::Pending || result_ == Result::Complete;
    }
    bool Matches(const Message& msg) const noexcept {
        return saved_ && msg.token == transaction_ && msg.revision == previousRevision_ + 1;
    }
    bool AllAcknowledged() const noexcept {
        for (std::uint32_t i = 1; i <= context_.peerCount; ++i) if (!acknowledgements_[i]) return false;
        return true;
    }
    void Advance(std::uint64_t now) noexcept {
        if (phase_ == Phase::Preparing) {
            if (hooks_.Apply && !hooks_.Apply(hooks_.user, pending_)) { Abort(Result::ApplyFailed, now); return; }
            state_ = pending_; phase_ = Phase::Applying; deadline_ = now + kTimeoutMs;
            std::memset(acknowledgements_, 0, sizeof(acknowledgements_));
            Message msg = Control(Kind::Apply); msg.revision = previousRevision_ + 1;
            if (!Broadcast(msg)) { Abort(Result::SendFailed, now); return; }
            if (context_.peerCount) return;
        }
        if (phase_ == Phase::Applying) {
            Message msg = Control(Kind::Final); msg.revision = previousRevision_ + 1;
            exposedRevision_ = msg.revision;
            if (!Broadcast(msg)) { Abort(Result::SendFailed, now); return; }
            revision_ = msg.revision; committedToken_ = transaction_; phase_ = Phase::None; result_ = Result::Complete;
            UpdateLease(now);
        }
    }
    bool Rollback(Result reason, std::uint64_t now) noexcept {
        if (previous_.enabled && (!leaseArmed_ || LeaseExpired(now))) {
            State disabled = previous_; disabled.enabled = false;
            const bool ok = Baseline(disabled);
            state_ = disabled; revision_ = previousRevision_; phase_ = Phase::None; saved_ = requestPending_ = false;
            result_ = ok ? Result::Faulted : Result::ApplyFailed;
            return false; // never reactivate an enabled state whose quorum expired
        }
        const bool ok = !hooks_.Apply || hooks_.Apply(hooks_.user, previous_);
        state_ = previous_; revision_ = previousRevision_; phase_ = Phase::None; saved_ = requestPending_ = false;
        if (!state_.enabled) StopLease();
        result_ = ok ? reason : Result::ApplyFailed;
        if (!ok) {
            State disabled = previous_; disabled.enabled = false;
            Baseline(disabled);
            state_ = disabled;
        }
        return ok;
    }
    void Abort(Result reason, std::uint64_t now) noexcept {
        if (!saved_) return;
        Message msg = Control(Kind::Abort); msg.revision = previousRevision_ + 1;
        const bool sent = Broadcast(msg); const bool restored = Rollback(reason, now);
        if (!sent || !restored) FailSafe();
    }
    void FailSafe() noexcept {
        State disabled = state_; disabled.enabled = false;
        const bool ok = Baseline(disabled);
        state_ = disabled; phase_ = Phase::None; saved_ = requestPending_ = false;
        StopLease();
        // A partially sent Final may have advanced one peer even when Abort
        // also failed. Safety must advance beyond every exposed final revision.
        if (exposedRevision_ > revision_) revision_ = exposedRevision_;
        if (revision_ == UINT32_MAX) { broken_ = true; result_ = Result::ProtocolError; return; }
        ++revision_; transaction_ = NextToken(); committedToken_ = transaction_;
        Message msg = Control(Kind::FailSafe); msg.state = state_; const bool sent = Broadcast(msg);
        result_ = ok && sent ? Result::Faulted : !ok ? Result::ApplyFailed : Result::SendFailed;
        if (!sent) broken_ = true;
    }
};
} // namespace pvp_protocol
