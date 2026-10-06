// The Air Raider call's pick carried in its own message (airstrike.cpp; docs/online-re.md sections 1 and 9).
// Online, the caller's machine sends message 9 (the confirm state 0x6A9270: heading, target, seed, sequence) and every
// other machine replays the call from it; nothing in it said which call the caller's picker turned the weapon into, so
// the others flew the weapon's own. The heading goes out as its exact 4 bytes (0x12B57E0: a 0xC2 tag and the float), so
// its 12 lowest mantissa bits can carry the pick: 5 bits of the pick + 1 (0: "each its own"), 7 of a check over the rest
// of the float. The heading moves by at most 2^-11 of itself (0.09 degrees at pi), on every machine alike, since every
// machine builds the call's matrix from the value sent. A heading no modded machine wrote (no check) decodes to kNoMark.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

namespace crew {
namespace callnet {
constexpr int kNoMark=-2;      // no pick in it: the call is replayed as before
constexpr int kOwnCall=-1;     // the caller's picker said "each its own"
constexpr int kMostPicks=31;   // 5 bits less the "each its own" code
constexpr std::uint32_t kLowMask=0xFFFu,kCheckMask=0x7Fu;

constexpr std::uint32_t Check(std::uint32_t high,std::uint32_t code) noexcept {
    std::uint32_t x=(high*0x9E3779B1u)^(code*0x85EBCA6Bu)^0x5A17C0DEu;
    x^=x>>15;x*=0x2C1B3C6Du;x^=x>>12;
    return x&kCheckMask;
}

inline std::uint32_t Bits(float f) noexcept { std::uint32_t u;std::memcpy(&u,&f,4);return u; }
inline float Float(std::uint32_t u) noexcept { float f;std::memcpy(&f,&u,4);return f; }

// `heading` with `pick` (kOwnCall or 0..kMostPicks-1) in it; a heading that is not finite, or a pick out of range, as it is.
inline float Encode(float heading,int pick) noexcept {
    if(!std::isfinite(heading) || pick<kOwnCall || pick>=kMostPicks)return heading;
    const std::uint32_t u=Bits(heading),high=u&~kLowMask,code=static_cast<std::uint32_t>(pick+1);
    return Float(high|(code<<7)|Check(high>>12,code));
}

// The pick in `heading`: kNoMark, kOwnCall or the pick.
inline int Decode(float heading) noexcept {
    if(!std::isfinite(heading))return kNoMark;
    const std::uint32_t u=Bits(heading),high=u&~kLowMask,code=(u>>7)&0x1Fu;
    if((u&kCheckMask)!=Check(high>>12,code))return kNoMark;
    return static_cast<int>(code)-1;
}
}  // namespace callnet
}  // namespace crew
