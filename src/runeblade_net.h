// The Rune Blade online (docs/newclass-re/runeblade-plan.md §2.2): plugin_net.h channel "runeblade", magic 'ZWRB'. Pure
// (wire.h, little-endian, one fixed size); tests/runeblade_test.cpp round-trips it.
// The authority is the machine the Rune Blade plays on (its ether is a movement resource the game simulates there, as it
// does the jump: plan §2.3); every other machine takes only that player's own word (plugin_net.h SenderIsPlayer).
//   state (BroadcastState, key StateStream::runeBlade + the local player's index): magic, version, kind 1 @8, player @12
//         (a player reference), ether @44 (u16, hundredths), flags @46 (bit 0 depleted, bit 1 gliding), serial @47 (the
//         sender's count of states, wraps)
//   step  (Broadcast, reliable):  kind 2, player @12, serial @44 (u8: the sender's count of steps), cue @45 (1 takeoff,
//         2 air step)
//   stage (Broadcast, reliable):  kind 3, player @12, weapon tag @44 (u8, RB-MELEE's), stage @45 (u8 < combo::kMostStages)
// Unused bytes zero; a message whose unused bytes are not is refused (a newer layout is the next version).
#pragma once
#include "runeblade_combo_logic.h"
#include "runeblade_ether_logic.h"
#include "wire.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace crew::runeblade::net {

constexpr std::uint32_t kMagic=0x4252575Au;   // "ZWRB"
constexpr std::uint32_t kVersion=1;
constexpr std::size_t kWire=48;
constexpr std::uint8_t kDepleted=1,kGliding=2,kFlags=3;
constexpr std::uint64_t kGapMs=100,kHeartbeatMs=1000;
constexpr float kMostEther=600.0f;            // hundredths in a u16

enum class Kind : std::uint8_t { state=1,step=2,stage=3 };
struct Msg {
    Kind kind=Kind::state;
    unsigned char player[32]{};
    float ether=0.0f;          // state
    std::uint8_t flags=0;      // state
    std::uint8_t serial=0;     // state, step
    std::uint8_t cue=0;        // step (runeblade::Cue)
    std::uint8_t tag=0;        // stage
    std::uint8_t stage=0;      // stage
};

namespace detail {
constexpr std::size_t kKindAt=8,kPlayerAt=12,kBodyAt=44;
inline std::uint16_t Hundredths(float e) noexcept {
    if(!std::isfinite(e) || e<=0.0f)return 0;
    if(e>=kMostEther)return static_cast<std::uint16_t>(kMostEther*100.0f);
    return static_cast<std::uint16_t>(std::lround(e*100.0f));
}
inline bool Valid(const Msg& m) noexcept {
    if(!netref::IsPlayer(m.player))return false;
    switch(m.kind) {
    case Kind::state: return std::isfinite(m.ether) && m.ether>=0.0f && m.ether<=kMostEther && !(m.flags&~kFlags);
    case Kind::step: return m.cue==static_cast<std::uint8_t>(Cue::takeoff) || m.cue==static_cast<std::uint8_t>(Cue::step);
    case Kind::stage: return m.stage<combo::kMostStages;
    }
    return false;
}
}  // namespace detail

inline bool Owned(const void* bytes,std::size_t size) noexcept { return wire::HeadWord(bytes,size,0)==kMagic; }
// Only the state kind comes as a state message (plugin_net.h takesState).
inline bool TakesState(const void* bytes,std::size_t size) noexcept {
    return Owned(bytes,size) && size==kWire && static_cast<const unsigned char*>(bytes)[detail::kKindAt]==
           static_cast<unsigned char>(Kind::state);
}

inline bool Encode(const Msg& m,void* bytes,std::size_t size) noexcept {
    if(!bytes || size!=kWire || !detail::Valid(m))return false;
    wire::Writer w(bytes,size);
    w.U32(kMagic).U32(kVersion).U8(static_cast<std::uint8_t>(m.kind)).Skip(3).Ref(m.player,false,true);
    switch(m.kind) {
    case Kind::state: w.U16(detail::Hundredths(m.ether)).U8(m.flags).U8(m.serial); break;
    case Kind::step: w.U8(m.serial).U8(m.cue); break;
    case Kind::stage: w.U8(m.tag).U8(m.stage); break;
    }
    return w.Ok();
}
inline bool Decode(const void* bytes,std::size_t size,Msg& out) noexcept {
    if(!Owned(bytes,size) || size!=kWire)return false;
    const auto* b=static_cast<const unsigned char*>(bytes);
    wire::Reader r(bytes,size);
    Msg m{};
    r.Skip(4);
    if(r.U32()!=kVersion)return false;
    const std::uint8_t kind=r.U8();
    if(kind<1 || kind>3)return false;
    m.kind=static_cast<Kind>(kind);
    for(std::size_t i=9;i<detail::kPlayerAt;++i)if(b[i])return false;
    r.Skip(3);
    r.Ref(m.player,false,true);
    std::size_t used=detail::kBodyAt;
    switch(m.kind) {
    case Kind::state: m.ether=static_cast<float>(r.U16())/100.0f;m.flags=r.U8();m.serial=r.U8();used+=4; break;
    case Kind::step: m.serial=r.U8();m.cue=r.U8();used+=2; break;
    case Kind::stage: m.tag=r.U8();m.stage=r.U8();used+=2; break;
    }
    for(std::size_t i=used;i<kWire;++i)if(b[i])return false;
    if(!r.Ok() || !detail::Valid(m))return false;
    out=m;
    return true;
}

// The state's pacing (as jet_lever_protocol.h): sent when never sent, the flags changed, the ether moved a whole unit
// and kGapMs passed, or the heartbeat is due.
struct Pace { bool sent=false; std::uint16_t ether=0; std::uint8_t flags=0; std::uint64_t at=0; };
inline bool Due(const Pace& p,float ether,std::uint8_t flags,std::uint64_t now) noexcept {
    const std::uint16_t e=detail::Hundredths(ether);
    if(!p.sent || flags!=p.flags)return true;
    const int moved=e>p.ether ? e-p.ether : p.ether-e;
    return now-p.at>=kHeartbeatMs || (moved>=100 && now-p.at>=kGapMs);
}
inline void Sent(Pace& p,float ether,std::uint8_t flags,std::uint64_t now) noexcept {
    p={true,detail::Hundredths(ether),flags,now};
}

}  // namespace crew::runeblade::net
