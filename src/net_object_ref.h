#pragma once
// An object's reference on the wire (every plugin channel, plugin_net.h): what one machine sends so another finds the same
// object in its own copy of the world. Pure: no game, no transport (tests/net_object_ref_test.cpp runs it).
// Why two kinds: a stock (AngelScript) mission registers its objects under its event threads' own counts (1D94C0), the
// same sequence on every machine for the enemies and the NPCs, so their native NetIds agree; the players' do not (each
// machine registers its own player first), so the host's player had another NetId on a guest (2026-10-31: the guests
// dropped the host's vehicle syncs, barrages, plans). So:
//   - a player is its mission player slot (eos::User+0x48 behind the soldier's +0x1ED0, the index
//     EDF6Coop_ResolveMissionPlayerPuid maps to a PUID), the same on every machine: the slot as a little-endian word at
//     bytes 0..3, kPlayerTag at bytes 20..23, every other byte zero;
//   - anything else is its native NetId (support_soldier.h ReadNativeObjectId): 32 bytes, nonzero, bytes 20..23 zero
//     (ReadNativeObjectId copies 0..19 and 24..31 into a zeroed buffer: the NetId's padding, never identity).
// The tag sits where a native NetId is always zero, so no NetId ever reads as a player (before 2026-10-31 the player's
// magic sat at bytes 0..3, which a NetId's own bytes could in principle have matched).
// All zero: no object (Kind::none). Anything else (a tag that is not kPlayerTag, a player slot past kPlayerSlots, a
// player with other bytes set) is invalid, and a decoder that meets one fails its message.
// The game-thread side (finding the object, making one's reference): net_object_resolve.h.
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace crew::netref {
constexpr std::size_t kBytes=32;
constexpr int kPlayerSlots=1024;                    // EDF6Coop's user slots
constexpr std::uint32_t kPlayerTag=0x52504641u;     // "AFPR", at bytes 20..23
constexpr std::size_t kTagAt=20;

enum class Kind : std::uint8_t { none, player, native, invalid };

namespace detail {
inline std::uint32_t Word(const unsigned char* p) noexcept {
    return static_cast<std::uint32_t>(p[0])|(static_cast<std::uint32_t>(p[1])<<8)|(static_cast<std::uint32_t>(p[2])<<16)|
        (static_cast<std::uint32_t>(p[3])<<24);
}
inline void PutWord(unsigned char* p,std::uint32_t v) noexcept {
    for(int i=0;i<4;++i)p[i]=static_cast<unsigned char>(v>>(8*i));
}
}  // namespace detail

// Mission player `slot`'s reference; false (and `out` zero) for a slot outside [0, kPlayerSlots).
inline bool MakePlayer(int slot,unsigned char out[kBytes]) noexcept {
    if(!out)return false;
    std::memset(out,0,kBytes);
    if(slot<0 || slot>=kPlayerSlots)return false;
    detail::PutWord(out,static_cast<std::uint32_t>(slot));
    detail::PutWord(out+kTagAt,kPlayerTag);
    return true;
}
// A native NetId's reference (a copy of it); false (and `out` zero) for one all zero or with bytes 20..23 set.
inline bool MakeNative(const unsigned char* id,unsigned char out[kBytes]) noexcept {
    if(!out)return false;
    std::memset(out,0,kBytes);
    if(!id)return false;
    unsigned any=0;
    for(std::size_t i=0;i<kBytes;++i) {
        if(i>=kTagAt && i<kTagAt+4 && id[i])return false;
        any|=id[i];
    }
    if(!any)return false;
    std::memcpy(out,id,kBytes);
    return true;
}
// What `ref` names; `slot` (optional) the player's slot for Kind::player, -1 otherwise. A null `ref` is Kind::none.
inline Kind Classify(const unsigned char* ref,int* slot=nullptr) noexcept {
    if(slot)*slot=-1;
    if(!ref)return Kind::none;
    const std::uint32_t tag=detail::Word(ref+kTagAt);
    unsigned rest=0;
    for(std::size_t i=0;i<kBytes;++i)if(i<kTagAt || i>=kTagAt+4)rest|=ref[i];
    if(!tag)return rest ? Kind::native : Kind::none;
    if(tag!=kPlayerTag)return Kind::invalid;
    const std::uint32_t value=detail::Word(ref);
    for(std::size_t i=4;i<kTagAt;++i)if(ref[i])return Kind::invalid;
    for(std::size_t i=kTagAt+4;i<kBytes;++i)if(ref[i])return Kind::invalid;
    if(value>=static_cast<std::uint32_t>(kPlayerSlots))return Kind::invalid;
    if(slot)*slot=static_cast<int>(value);
    return Kind::player;
}
inline bool IsPlayer(const unsigned char* ref,int* slot=nullptr) noexcept { return Classify(ref,slot)==Kind::player; }
inline bool IsNative(const unsigned char* ref) noexcept { return Classify(ref)==Kind::native; }
inline bool IsNone(const unsigned char* ref) noexcept { return Classify(ref)==Kind::none; }
// A reference a message may carry: a player or a native NetId; Kind::none only when `allowNone`; only a player when
// `playerOnly` (a caller, a grabber, a requester: whoever speaks for a machine is its player).
inline bool Valid(const unsigned char* ref,bool allowNone,bool playerOnly=false) noexcept {
    switch(Classify(ref)) {
    case Kind::none: return allowNone;
    case Kind::player: return true;
    case Kind::native: return !playerOnly;
    case Kind::invalid: break;
    }
    return false;
}
// Only a native NetId (or none, when `allowNone`): an NPC unit of a map command, a colonist whose gun fell.
inline bool ValidNative(const unsigned char* ref,bool allowNone) noexcept {
    const Kind k=Classify(ref);
    return k==Kind::native || (allowNone && k==Kind::none);
}
inline bool Same(const unsigned char* a,const unsigned char* b) noexcept {
    return a && b && !std::memcmp(a,b,kBytes);
}
}  // namespace crew::netref
