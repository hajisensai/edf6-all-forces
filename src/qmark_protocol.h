// The Q mark's online sharing (qmark.cpp; the user, 2026-10-10: "这个q会在队友那边显示"): pure, no game, no transport.
// Every machine sends each peer its own player's marks as one state (an enemy and a point on the ground), whenever it
// changes and again every kKeepMs (kEnemyKeepMs while an enemy is marked: the enemy moves); the receiver keeps each
// peer's last state and drops it kSilentMs after the peer goes quiet (it left, its plugin stopped). A state, not events:
// a lost or late message is mended by the next one, a peer that joins late gets the marks with the next keepalive.
// Wire: kWireSize bytes, little-endian, over the EDF6Coop extension transport the support and command messages use
// (support_net.cpp routes it by its magic before theirs). Version 1. Capability bits in `caps`: kCapEnemy (it sends and
// shows enemy marks), kCapPoint (point marks); a receiver masks off bits it does not know, a newer version is owned and
// ignored. A peer without this plugin version (no QMRK) drops the bytes: its support parser rejects the magic.
// The enemy goes by its native network ID (support_soldier.h ReadNativeObjectId, the one the map commands' focus uses:
// the same object on every machine); an all-zero ID: one without it, shown at the position sent. The marker is the
// sending player's own native ID (resolved and checked against the authenticated sender on the receiving machine).
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace crew::qmark_net {
constexpr std::uint32_t kMagic=0x4B524D51u;   // "QMRK"
constexpr std::uint32_t kVersion=1;
// kCapLetGo: `letGo` counts the marks its player let go (the key again on the same enemy), so the receiver tells a let-go
// (its cue) from a mark gone with its enemy (silent).
constexpr std::uint32_t kCapEnemy=1u,kCapPoint=2u,kCapLetGo=4u,kCapabilities=kCapEnemy|kCapPoint|kCapLetGo;
constexpr std::size_t kWireSize=128;
constexpr std::uint32_t kMaxPeers=1024;
constexpr std::uint64_t kKeepMs=1000,kEnemyKeepMs=250,kMinGapMs=50,kSilentMs=3500;
constexpr std::uint32_t kMaxPointMs=600000;   // QMarkPointSec's top

struct State {
    std::uint32_t origin=0;          // the sender's run (a new one: its sequence starts over)
    std::uint32_t sequence=0;
    std::uint32_t caps=kCapabilities;
    unsigned char marker[32]{};      // the marking player's native ID (zero: not known)
    bool enemy=false;
    unsigned char target[32]{};      // the enemy's native ID (zero: none, shown at enemyAt)
    float enemyAt[3]{};
    bool point=false;
    float pointAt[3]{};
    std::uint32_t pointLeftMs=0;     // how long the point mark stays from now
    std::uint32_t letGo=0;           // how many marks its player has let go this run (kCapLetGo)
};
bool Owned(const void* bytes,std::size_t size) noexcept;           // the QMRK magic (any version)
bool Encode(const State&,void* bytes,std::size_t size) noexcept;
// False: not QMRK, the wrong size, a newer version, an ID not canonical, a coordinate not finite, a point past kMaxPointMs.
bool Decode(const void* bytes,std::size_t size,State& out) noexcept;
bool SameMarks(const State& a,const State& b) noexcept;            // the same enemy and the same point (not positions)

// What came of a state for its sender's marks, for the HUD's and the sound's sake: a new mark; one let go by its
// player; one gone otherwise (its enemy dead or removed, its point's time up).
enum class Change { none, marked, letGo, cleared };

// The sender: when this machine's state goes out.
class Outbox {
public:
    explicit Outbox(std::uint32_t origin=1) noexcept : origin_(origin) {}
    // Due: the marks (or the let-go count) changed since the last sent, or the keepalive is due; never twice within kMinGapMs.
    bool Due(const State& now,std::uint64_t at) const noexcept;
    // The state as sent (origin, sequence filled in), remembered.
    State Stamp(const State& now,std::uint64_t at) noexcept;
    void Reset(std::uint32_t origin) noexcept { *this=Outbox(origin); }
private:
    std::uint32_t origin_=1,sequence_=0;
    bool sent_=false;
    std::uint64_t sentAt_=0;
    State last_{};
};

// The receiver: each peer's last state.
struct Remote {
    bool heard=false;
    std::uint64_t heardAt=0,pointUntil=0;
    State state{};
};
class Inbox {
public:
    // A peer's state: taken unless it is older than the last one of the same run (a late message). `change`: marked when a
    // new enemy or a new point came, letGo when its player let one go, cleared when one went otherwise. False: not taken.
    bool Receive(std::uint32_t peer,const State& s,std::uint64_t now,Change* change=nullptr) noexcept;
    // The peer's marks as of `now`: nothing once it is silent kSilentMs; its point only until its time.
    bool Enemy(std::uint32_t peer,std::uint64_t now) const noexcept;
    bool Point(std::uint32_t peer,std::uint64_t now) const noexcept;
    bool Capable(std::uint32_t peer,std::uint64_t now,std::uint32_t caps) const noexcept;
    const Remote& Peer(std::uint32_t peer) const noexcept { return peer_[peer<=kMaxPeers ? peer : 0]; }
    void Forget(std::uint32_t peer) noexcept { if(peer<=kMaxPeers)peer_[peer]=Remote{}; }
    void Reset() noexcept { peer_.fill(Remote{}); }
private:
    std::array<Remote,kMaxPeers+1> peer_{};
};

}  // namespace crew::qmark_net
