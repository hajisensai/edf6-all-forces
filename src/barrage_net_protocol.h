// The long-range barrages' online sharing (barrage.cpp; the user, 2026-10-26: 「所有插件支援改成m里面消耗pt召唤」): pure, no
// game, no transport (tests/barrage_net_test.cpp runs it).
// A barrage is a function of its kind, its point and its heading (barrage_plan.h Seed / Plan; the guided rockets' aims are
// each machine's own view of the enemies), so the caller's machine sends that once, when it starts (from the map's bar:
// airstrike.cpp SupportStrikeAt), and every peer plans and fires its own copy on its copy of the caller. Its damage counts
// on the caller's machine alone (jet_bay.cpp ShellMake: OnlineShotCounts). Before 2026-10-26 the stock radio call carried
// the point and heading to every machine; the call is no weapon any more.
// A barrage lasts seconds: a peer that joins during one, or loses its message, does not see it (no keep).
// Wire: fixed size, little-endian (wire.h), plugin_net.h channel BRGS, support_protocol.h kExtBarrage:
//   magic, version, kind (byte 8), origin @12, serial @16, damage @20, at[3] @24, heading[3] @36, caller[32] @48; zero to kWire.
// Version 2 (2026-10-31): the caller is its requester ID (net_object_ref.h: its mission slot, the same on every machine; a stock mission numbers the players' NetIds per machine);
// version 1 sent the NetId, and a guest dropped the host's barrages.
// Version 3 (2026-10-31): the caller is its object reference (net_object_ref.h: its slot, the tag at bytes 20..23), a
// fallen caller's too (its barrage was told by nobody before: its slot came from a live player only).
#pragma once
#include "barrage_plan.h"
#include "wire.h"
#include <cstddef>
#include <cstdint>

namespace crew::barrage_net {
constexpr std::uint32_t kMagic=0x53475242u;   // "BRGS"
constexpr std::uint32_t kVersion=3;
constexpr std::size_t kWire=80;
constexpr float kMostDamage=1.0e9f;

struct Msg {
    int kind=0;                      // barrage_plan.h Kind
    std::uint32_t origin=0;          // the caller's machine's run (plugin_net::NewOrigin)
    std::uint32_t serial=0;          // the barrage's in that run (never 0)
    float damage=0.0f;               // a round's (dealt on the caller's machine only)
    float at[3]{};                   // the point called, as the caller's machine planned from it
    float heading[3]{};              // ...and the heading
    unsigned char caller[32]{};      // the calling player's reference (net_object_ref.h)
};

namespace detail {
constexpr std::size_t kKindAt=8,kOrigin=12,kSerial=16,kDamage=20,kAt=24,kHeading=36,kCaller=48,kUsed=80;
static_assert(kSerial==kOrigin+4 && kDamage==kSerial+4 && kAt==kDamage+4 && kHeading==kAt+12 && kCaller==kHeading+12 &&
              kUsed==kCaller+32 && kUsed<=kWire,"the layout");
inline bool Valid(const Msg& m) noexcept {
    if(m.kind<0 || m.kind>=barrage::kKinds || !m.origin || !m.serial)return false;
    if(!(m.damage>=0.0f) || m.damage>kMostDamage || !netref::IsPlayer(m.caller))return false;
    return std::fabs(m.heading[0])+std::fabs(m.heading[2])>1e-4f;
}
}  // namespace detail

inline bool Owned(const void* bytes,std::size_t size) noexcept { return wire::HeadWord(bytes,size,0)==kMagic; }

// False: a message that would not decode (see Decode), or no room.
inline bool Encode(const Msg& m,void* bytes,std::size_t size) noexcept {
    if(!bytes || size!=kWire || !detail::Valid(m))return false;
    wire::Writer w(bytes,size);
    w.U32(kMagic).U32(kVersion).U8(static_cast<std::uint8_t>(m.kind)).Seek(detail::kOrigin).U32(m.origin).U32(m.serial)
     .F(m.damage).V3(m.at).V3(m.heading).Ref(m.caller,false,true);
    return w.Ok() && w.Used()==detail::kUsed;
}

// False: not BRGS, the wrong size, another version, an unknown kind, a number not finite or out of its bounds, no heading,
// a caller that is no player's reference, no origin or serial.
inline bool Decode(const void* bytes,std::size_t size,Msg& out) noexcept {
    if(!Owned(bytes,size) || size!=kWire)return false;
    wire::Reader r(bytes,size);
    Msg m{};
    r.Skip(4);
    if(r.U32()!=kVersion)return false;
    m.kind=r.U8();
    r.Seek(detail::kOrigin);
    m.origin=r.U32();m.serial=r.U32();
    m.damage=r.F(kMostDamage);
    r.V3(m.at);r.V3(m.heading,2.0f);
    r.Ref(m.caller,false,true);
    if(!r.Ok() || !detail::Valid(m))return false;
    out=m;
    return true;
}

// The barrages heard (their origin and serial), so a message that came twice starts one barrage.
struct Heard {
    static constexpr int kMost=32;
    std::uint32_t origin[kMost]{},serial[kMost]{};
    int next=0;
    // True the first time a key is seen (then remembered); false again.
    bool First(std::uint32_t o,std::uint32_t s) noexcept {
        for(int i=0;i<kMost;++i)if(origin[i]==o && serial[i]==s)return false;
        origin[next]=o;serial[next]=s;next=(next+1)%kMost;
        return true;
    }
    void Clear() noexcept { *this=Heard{}; }
};
}  // namespace crew::barrage_net
