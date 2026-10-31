// The ultimate strike's online sharing (src/ultimate.cpp; the user, 2026-10-20: 「所有功能都要支持联机」): pure, no game,
// no transport (tests/ultimate_net_test.cpp runs it).
// Why a show is one message and not its events: the show is a function of its frame, centre, ground planes and seed
// (src/ultimate_plan.h), so every machine plays the whole of it itself (its circles, sky, sounds, particles, rounds) from
// the same numbers. Its damage counts on the caller's machine alone: every machine fires the show's rounds by its own copy
// of the caller, and jet_bay.cpp ShellMake zeroes a round's damage where online_authority.h ShotCounts says it does not
// count (a registered player of another machine: Authority is (net & kNetRemote)==0, false there).
// The caller's machine sends `start` when its show begins, `keep` every kKeepMs while it plays (a member who joins late,
// or lost the start, picks the show up from it; a newly joined peer is sent one at once) and `end` when it is over or
// stopped. Each carries the frame the caller has played, so a receiver starts (or catches up) where the caller is,
// through ultimate_show.h Seek, never replaying the frames it missed.
// Wire: fixed size, little-endian (wire.h), plugin_net.h channel ULTS, support_protocol.h kExtUltimate: sent only to a
// peer that announced the bit, another version dropped by the registry.
//   magic, version, kind (byte 8), origin @12, serial @16, frame @20, seed @24 (two words, low first), damage @32,
//   at[3] @36, plane[3] @48, caller[32] @60; zero to kWire.
// Version 2 (2026-10-31): the caller is its requester ID (net_object_ref.h: its mission slot, the same on every machine; a stock mission numbers the players' NetIds per machine).
// Version 3 (2026-10-31): the caller is its object reference (net_object_ref.h: its slot, the tag at bytes 20..23),
// taken when the show starts and sent with every message of it, its caller fallen or not (before, a caller that fell sent
// no keep and no end, and a receiver dropped a fallen caller's messages); a receiver that does not have the caller (left,
// deleted) plays the show's looks without its rounds.
#pragma once
#include "ultimate_plan.h"
#include "wire.h"
#include <cstddef>
#include <cstdint>

namespace crew::ultimate_net {
constexpr std::uint32_t kMagic=0x53544C55u;   // "ULTS"
constexpr std::uint32_t kVersion=3;
constexpr std::size_t kWire=96;
constexpr std::uint64_t kKeepMs=1000;          // a playing show's keep goes out this often
constexpr float kMostDamage=1.0e9f;            // a weapon's damage past this is not a weapon's

enum class Kind : std::uint8_t { start=1, keep=2, end=3 };
struct Msg {
    Kind kind=Kind::start;
    std::uint32_t origin=0;          // the caller's machine's run (plugin_net::NewOrigin): a new run, new shows
    std::uint32_t serial=0;          // the show's in that run (never 0)
    std::int32_t frame=0;            // the show frame the caller had played when it sent this
    std::uint64_t seed=0;
    float damage=0.0f;               // the weapon's (dealt on the caller's machine only)
    float at[3]{};                   // the centre, already on the ground
    float plane[ultimate::kGroundCircles]{};   // the ground circles' heights (ultimate.cpp CirclePlane), the caller's
    unsigned char caller[32]{};      // the calling player's reference (net_object_ref.h), taken when its show started
};

namespace detail {
constexpr std::size_t kKindAt=8,kOrigin=12,kSerial=16,kFrame=20,kSeed=24,kDamage=32,kAt=36,kPlane=48,kCaller=60,kUsed=92;
static_assert(kSerial==kOrigin+4 && kFrame==kSerial+4 && kSeed==kFrame+4 && kDamage==kSeed+8 && kAt==kDamage+4 &&
              kPlane==kAt+12 && kCaller==kPlane+4*ultimate::kGroundCircles && kUsed==kCaller+32 && kUsed<=kWire,"the layout");
inline bool KindOk(Kind k) noexcept { return k==Kind::start || k==Kind::keep || k==Kind::end; }
// What a message says must be a show's: a frame of the show (an end's up to its last), a damage a weapon has, ground
// planes where CirclePlane puts them (over the centre, at most kPlaneMost), a caller and a key.
inline bool Valid(const Msg& m) noexcept {
    if(!KindOk(m.kind) || !m.origin || !m.serial || m.frame<0 || m.frame>ultimate::kAfterEnd)return false;
    if(!(m.damage>=0.0f) || m.damage>kMostDamage || !netref::IsPlayer(m.caller))return false;
    for(float p:m.plane)if(!(p>=m.at[1]-0.01f) || !(p<=m.at[1]+ultimate::kPlaneMost+0.01f))return false;
    return true;
}
}  // namespace detail

inline bool Owned(const void* bytes,std::size_t size) noexcept { return wire::HeadWord(bytes,size,0)==kMagic; }

// False: a message that would not decode (see Decode), or no room.
inline bool Encode(const Msg& m,void* bytes,std::size_t size) noexcept {
    if(!bytes || size!=kWire || !detail::Valid(m))return false;
    wire::Writer w(bytes,size);
    w.U32(kMagic).U32(kVersion).U8(static_cast<std::uint8_t>(m.kind)).Seek(detail::kOrigin).U32(m.origin).U32(m.serial)
     .U32(static_cast<std::uint32_t>(m.frame)).U32(static_cast<std::uint32_t>(m.seed)).U32(static_cast<std::uint32_t>(m.seed>>32))
     .F(m.damage).V3(m.at);
    for(float p:m.plane)w.F(p);
    w.Ref(m.caller,false,true);
    return w.Ok() && w.Used()==detail::kUsed;
}

// False: not ULTS, the wrong size, another version, an unknown kind, a number not finite or out of its bounds, a frame
// outside the show, a caller that is no player's reference, no origin or serial.
inline bool Decode(const void* bytes,std::size_t size,Msg& out) noexcept {
    if(!Owned(bytes,size) || size!=kWire)return false;
    wire::Reader r(bytes,size);
    Msg m{};
    r.Skip(4);
    if(r.U32()!=kVersion)return false;
    m.kind=static_cast<Kind>(r.U8());
    r.Seek(detail::kOrigin);
    m.origin=r.U32();m.serial=r.U32();
    m.frame=static_cast<std::int32_t>(r.U32());
    const std::uint64_t lo=r.U32(),hi=r.U32();
    m.seed=lo|(hi<<32);
    m.damage=r.F(kMostDamage);
    r.V3(m.at);
    for(float& p:m.plane)p=r.F();
    r.Ref(m.caller,false,true);
    if(!r.Ok() || !detail::Valid(m))return false;
    out=m;
    return true;
}
}  // namespace crew::ultimate_net
