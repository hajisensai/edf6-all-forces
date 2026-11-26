// A jet engine's lever online (jet_lever_net.cpp, docs/feature-wt-sound.md 「联机」; the user's rule, 2026-10-20: 「所有功能都要
// 支持联机」): pure, no game, no transport (tests/jet_lever_net_test.cpp runs it).
// Why: a registered jet (a stock mission's NPC-flown aircraft, a player's jet) is run by one machine (online_authority.h
// RunsHere); on every other its copy moves by native replication, with no flight of the plugin's, so no lever: its engine
// sound and exhaust had only what that machine sees of it (a guess from its speed, the afterburner at >=240 m/s). The
// machine that runs it knows the lever (its NPC flight's, wing_engine.h; its player's throttle) and sends it; every other
// machine's engine sound, afterburner and exhaust read it. An unregistered copy (the plugin's call jets) runs on every
// machine and has its own lever: none is sent for it.
// Wire (plugin_net.h channel AFJL, version 1): a state message (unreliable, the newest of a sender's key kept), 44 bytes:
// magic, version, the jet's reference (net_object_ref.h: its NetId), the lever quantized to one byte, flags. Sent when the
// byte moves by kStep (at most every kGapMs), at once when the afterburner or a flag changes, and every kHeartbeatMs
// regardless (a lost state, a member who joined: the next one stands for it); a lever not heard of for kSilentMs is gone.
#pragma once
#include "wing_engine.h"
#include "wire.h"
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace crew::jetlever {
constexpr std::uint32_t kMagic=0x4C4A4641u;   // "AFJL"
constexpr std::uint32_t kVersion=1;
constexpr std::size_t kWire=44;
constexpr int kStep=6;                        // of 255 (2.4%): a smaller move waits for the heartbeat
constexpr std::uint64_t kGapMs=100;           // a moving lever: at most 10 a second
constexpr std::uint64_t kHeartbeatMs=1000;    // a still one: once a second
constexpr std::uint64_t kSilentMs=3500;       // three heartbeats lost: the jet's engine is not heard of
static_assert(kHeartbeatMs*3<kSilentMs,"a state lost twice is not silence");
// Flags: how the engine is heard (jetsound_state.h State).
constexpr std::uint8_t kRotor=1,kParked=2,kOff=4,kFlags=7;   // off: nothing flies it on its machine (no one at the lever)

// The afterburner: the NPC wing's and the player's lever alike (wing_engine.h).
inline bool Burner(float lever) noexcept { return jet::wing::Burner(lever); }
inline float Lever(std::uint8_t q) noexcept { return static_cast<float>(q)/255.0f; }
// The byte of a lever (0..1, clamped; not finite: 0), its afterburner kept: a lever over kBurnerAt is a byte over it.
inline std::uint8_t Quantize(float lever) noexcept {
    if(!std::isfinite(lever) || lever<=0.0f)return 0;
    if(lever>=1.0f)return 255;
    int q=static_cast<int>(std::lround(lever*255.0f));
    if(Burner(lever) && !Burner(Lever(static_cast<std::uint8_t>(q))))++q;
    if(!Burner(lever) && Burner(Lever(static_cast<std::uint8_t>(q))))--q;
    return static_cast<std::uint8_t>(q);
}

struct Msg {
    unsigned char jet[32]{};   // its reference (net_object_ref.h)
    std::uint8_t lever=0;      // Quantize
    std::uint8_t flags=0;
};
// What a machine last sent of one jet.
struct Pace {
    bool sent=false;
    std::uint8_t lever=0,flags=0;
    std::uint64_t at=0;
};
// The next state goes out at `now` (ms): never sent, its afterburner or flags changed, the byte moved kStep and kGapMs
// passed, or the heartbeat is due.
inline bool Due(const Pace& p,std::uint8_t lever,std::uint8_t flags,std::uint64_t now) noexcept {
    if(!p.sent || flags!=p.flags || Burner(Lever(lever))!=Burner(Lever(p.lever)))return true;
    const int moved=lever>p.lever ? lever-p.lever : p.lever-lever;
    return now-p.at>=kHeartbeatMs || (moved>=kStep && now-p.at>=kGapMs);
}
inline void Sent(Pace& p,std::uint8_t lever,std::uint8_t flags,std::uint64_t now) noexcept { p={true,lever,flags,now}; }
// A lever heard at `heard` still stands at `now`.
constexpr bool Fresh(std::uint64_t heard,std::uint64_t now) noexcept { return heard && now>=heard && now-heard<kSilentMs; }

namespace detail {
constexpr std::size_t kUsed=4+4+32+1+1;
static_assert(kUsed<=kWire,"the message fits");
}
inline bool Owned(const void* bytes,std::size_t size) noexcept { return wire::HeadWord(bytes,size,0)==kMagic; }
// Every one of the channel's messages is a state (plugin_net.h takesState).
inline bool TakesState(const void* bytes,std::size_t size) noexcept { return Owned(bytes,size); }
// False: no jet named (its reference not a NetId: a player is never a jet), flags not ours.
inline bool Encode(const Msg& m,void* bytes,std::size_t size) noexcept {
    if(!bytes || size!=kWire || !netref::IsNative(m.jet) || (m.flags&~kFlags))return false;
    wire::Writer w(bytes,size);
    w.U32(kMagic).U32(kVersion).Ref(m.jet,false).U8(m.lever).U8(m.flags);
    return w.Ok() && w.Used()==detail::kUsed;
}
// False: not AFJL, the wrong size or version, no NetId, flags not ours.
inline bool Decode(const void* bytes,std::size_t size,Msg& out) noexcept {
    if(!Owned(bytes,size) || size!=kWire)return false;
    wire::Reader r(bytes,size);
    Msg m{};
    r.Skip(4);
    if(r.U32()!=kVersion)return false;
    r.Ref(m.jet,false);m.lever=r.U8();m.flags=r.U8();
    if(!r.Ok() || !netref::IsNative(m.jet) || (m.flags&~kFlags))return false;
    out=m;
    return true;
}
}  // namespace crew::jetlever
