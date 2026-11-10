// The Andryusha's incendiary cluster rockets without the game (src/andryusha.cpp fuses and splits them, tools/andryusha_check.cpp
// checks this; docs/andryusha-re.md; test site #37). No EDF.dll in here.
//  - A carrier is the Katyusha's rocket (GrenadeBullet01, tools/make_katyusha.py ANDRYUSHA_ROCKETS) told apart by its
//    AmmoHitSizeAdjust: the weapon's SGO writes kMarkImpactBits, the plugin turns the weapon's copy into kMarkAirburstBits and
//    back (the rider's fuse switch), and each round carries the one it was fired with (its core copies it at birth).
//  - Impact fuse: the carrier bursts where it hits (the stock round's own blast) and its bomblets leave there, thrown out
//    round the point (ImpactAims). Airburst (近炸) fuse: on the way down the carrier bursts kBurstHeight m over the ground
//    under it (AirburstDue; the plugin sets the stock round's burst-on-expiry and ends its life) and its bomblets fall onto
//    a disc under it (AirburstAims).
//  - A bomblet is two rounds on one arc (jet_bay.cpp AndryushaBomblet): a small blast (GrenadeBullet01) and the incendiary
//    (NapalmBullet01: its FlameBullet02 ground fire burns on where it lands), so it bursts and then burns.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

namespace crew {
namespace andryusha {
constexpr float kPi=3.14159265358979f;

// The marks (AmmoHitSizeAdjust's bits; tools/make_katyusha.py ANDRYUSHA_MARK_IMPACT / _AIRBURST, tools/selftest.py holds the
// two copies equal). Both are the Katyusha's contact factor (1.5 m / AmmoSize 1.34 = 1.1194) with its low 12 bits replaced:
// the contact sphere grows by 0.07 mm. No stock weapon's AmmoHitSizeAdjust has these bits (the stock ones near 1 are 1.0
// exactly), the Katyusha's is 0x3F8F4880, the barrages' 0x3F801000..0x3F801FFF (barrage.cpp).
constexpr std::uint32_t kMarkImpactBits=0x3F8F4A36u,kMarkAirburstBits=0x3F8F4A37u;
enum class Fuse : std::uint8_t { none, impact, airburst };
constexpr Fuse FuseOf(std::uint32_t bits) noexcept {
    return bits==kMarkImpactBits ? Fuse::impact : bits==kMarkAirburstBits ? Fuse::airburst : Fuse::none;
}
constexpr std::uint32_t MarkOf(Fuse f) noexcept { return f==Fuse::airburst ? kMarkAirburstBits : kMarkImpactBits; }
constexpr Fuse Toggled(Fuse f) noexcept { return f==Fuse::airburst ? Fuse::impact : Fuse::airburst; }

// The airburst: kBurstHeight m over the ground under the carrier, on the way down, never in its first kArmFrames (it climbs
// then, and a rocket fired level from a hillside must not burst over the slope it leaves).
constexpr float kBurstHeight=35.0f;
constexpr int kArmFrames=60;
// Whether a carrier `height` m over the ground under it, falling at `vy` m/s (up positive), `age` frames old, bursts this
// frame: it is low enough now, or would pass under the height within this frame. A negative height (no ground found under
// it within the probe) never bursts: it keeps its impact fuse.
constexpr bool AirburstDue(float height,float vy,int age) noexcept {
    return age>=kArmFrames && vy<0.0f && height>=0.0f && height+vy/60.0f<=kBurstHeight;
}
// The probe's length under a carrier: the burst height and two frames of its fall.
constexpr float ProbeDepth(float vy) noexcept { return kBurstHeight+(vy<0.0f ? -vy : 0.0f)/30.0f+5.0f; }

// Its bomblets.
constexpr int kBomblets=8;
constexpr float kImpactLift=2.0f;          // m: an impact's bomblets leave this far over the burst point
constexpr float kImpactRingMin=10.0f,kImpactRingMax=26.0f;   // m: where an impact throws them, round the point
constexpr float kAirburstSpread=30.0f;     // m: the disc an airburst drops them on, round the ground under it
constexpr float kBlastShare=0.35f;         // a bomblet's blast, of the carrier's damage
constexpr float kFireShare=0.12f;          // its fire's, of the carrier's damage (the fire's hits repeat while it burns)

// A seeded stream (splitmix64), the same on every machine for the same seed.
struct Rng {
    std::uint64_t s;
    std::uint64_t Next() noexcept {
        std::uint64_t z=(s+=0x9E3779B97F4A7C15ull);
        z=(z^(z>>30))*0xBF58476D1CE4E5B9ull;z=(z^(z>>27))*0x94D049BB133111EBull;
        return z^(z>>31);
    }
    float U() noexcept { return static_cast<float>(Next()>>40)/16777216.0f; }   // [0, 1)
};
// A burst's seed from where it is (to the centimetre): two machines bursting the same round at the same point split it
// alike.
inline std::uint64_t SeedAt(const float* at) noexcept {
    std::uint64_t s=0x5A17A5D11ull;
    for(int c=0;c<3;++c){const auto v=static_cast<std::int64_t>(std::floor(at[c]*100.0f));s=s*0x100000001B3ull^static_cast<std::uint64_t>(v);}
    return s;
}

// The ground's height at (x, z) for an aim (the caller's map ray, from well over `y`); `y` when it finds none.
using GroundFn=float(*)(void* ctx,float x,float z,float y);

// An impact's bomblets: they leave kImpactLift m over `at` and fall on a ring round it, evenly round, each its own
// distance and a little off its even angle. `from` gets where they leave, `aims` (kBomblets) where they land.
inline void ImpactAims(const float* at,std::uint64_t seed,GroundFn ground,void* ctx,float* from,float (*aims)[3]) noexcept {
    Rng r{seed};
    from[0]=at[0];from[1]=at[1]+kImpactLift;from[2]=at[2];
    const float turn=r.U()*2.0f*kPi;
    for(int i=0;i<kBomblets;++i) {
        const float a=turn+(static_cast<float>(i)+0.35f*(r.U()-0.5f))*2.0f*kPi/static_cast<float>(kBomblets);
        const float d=kImpactRingMin+(kImpactRingMax-kImpactRingMin)*r.U();
        const float x=at[0]+std::cos(a)*d,z=at[2]+std::sin(a)*d;
        aims[i][0]=x;aims[i][1]=ground ? ground(ctx,x,z,at[1]) : at[1];aims[i][2]=z;
    }
}
// An airburst's bomblets: they leave the burst point `at` and fall on a disc of kAirburstSpread m round `below` (the
// ground under it), spread evenly over the disc (the sunflower: each its own ring and angle).
inline void AirburstAims(const float* at,const float* below,std::uint64_t seed,GroundFn ground,void* ctx,float* from,
                         float (*aims)[3]) noexcept {
    Rng r{seed};
    std::memcpy(from,at,12);
    const float turn=r.U()*2.0f*kPi,golden=kPi*(3.0f-std::sqrt(5.0f));
    for(int i=0;i<kBomblets;++i) {
        const float d=kAirburstSpread*std::sqrt((static_cast<float>(i)+0.5f)/static_cast<float>(kBomblets));
        const float a=turn+golden*static_cast<float>(i);
        const float x=below[0]+std::cos(a)*d,z=below[2]+std::sin(a)*d;
        aims[i][0]=x;aims[i][1]=ground ? ground(ctx,x,z,below[1]) : below[1];aims[i][2]=z;
    }
}
}  // namespace andryusha
}  // namespace crew
