// The Air Raider call's pick carried in its own message (airstrike.cpp; docs/online-re.md sections 1, 9 and 10).
// Online, the caller's machine sends message 9 (the confirm state 0x6A9270: heading, target, seed, sequence) and every
// other machine replays the call from it; nothing in it said which call the caller's picker turned the weapon into, so
// the others flew the weapon's own. The seed goes out as its exact 8 bytes (0x12B5690) and the receiver keeps them at
// weapon +0x1958 until the next call (0x6A8724), so the seed's high 32 bits carry the pick: a fixed 26-bit mark and 6
// bits of the pick + 1 (0: "each its own"). The low 32 bits stay the weapon's own random state; the caller keeps the seed
// it sent (+0xBC8), so every machine's replay starts from the same value as before.
// Only a call received from another machine is ever decoded (airstrike.cpp CallOf: online, the owner another machine's).
// A seed nobody marked (another version, an unmodded caller) carries the mark by chance once in 2^26 (1.5e-8) calls.
#pragma once
#include <cstdint>

namespace crew {
namespace callnet {
constexpr int kNoMark=-2;      // no pick in it: the call is replayed as before
constexpr int kOwnCall=-1;     // the caller's picker said "each its own"
constexpr int kMostPicks=62;   // 6 bits less the "each its own" code, less one spare
constexpr std::uint64_t kMark=0x2ED6C0Full;   // 26 bits
constexpr unsigned kMarkShift=38,kCodeShift=32;

// `seed` with `pick` (kOwnCall or 0..kMostPicks-1) in its high 32 bits; a pick out of range: `seed` as it is.
constexpr std::uint64_t Encode(std::uint64_t seed,int pick) noexcept {
    if(pick<kOwnCall || pick>=kMostPicks)return seed;
    return (kMark<<kMarkShift)|(static_cast<std::uint64_t>(pick+1)<<kCodeShift)|(seed&0xFFFFFFFFull);
}

// The pick in a received `seed`: kNoMark, kOwnCall or the pick.
constexpr int Decode(std::uint64_t seed) noexcept {
    if((seed>>kMarkShift)!=kMark)return kNoMark;
    const int code=static_cast<int>((seed>>kCodeShift)&0x3Full);
    return code<=kMostPicks ? code-1 : kNoMark;
}
}  // namespace callnet
}  // namespace crew
