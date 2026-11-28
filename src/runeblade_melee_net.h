// The RuneBlade's arms online (docs/newclass-re/runeblade-melee-plan.md §4): plugin_net.h channel "runeblade melee", magic
// 'ZWRM'. Pure (wire.h, little-endian, one size for every kind); tests/runeblade_melee_test.cpp round-trips it.
// The machine of the RuneBlade's player runs the arms (runeblade_melee_logic.h Step) and deals their damage (the plugin's
// rule for a player's own rounds, online_authority.h ShotCounts: Shooter::localPlayer counts on that player's machine and no
// other, as floatgun.cpp's guns and jet_bay.cpp's shells); it tells every peer what it struck, so each shows the same hits
// with damage 0. The player's motion (the dash, the falls) is the soldier's own, carried by EDF6Coop's player sync.
//   strike (owner -> every peer):  magic, version, kind 1 @8, arm @9, effect @10, player @12 (its player reference),
//                                  points @44, waves @46, every @48, delay @50, stagger @52 (u16 each), from @56, dir @68,
//                                  length @80, radius @84, grow @88, damage @92 (f32 each)
//   ring   (owner -> every peer):  kind 2, arm @9 (stars), player @12, from @56 (where it was thrown), yaw @80 (radians):
//                                  each peer flies the same ring (RingAt) round that player's copy, shown only
//   bolt   (owner -> every peer):  kind 3, arm @9, effect @10, player @12, from @56, dir @68, length @80 (its speed, metres
//                                  a frame), radius @84 (its size), damage @92: one round, fired with damage 0 there
#pragma once
#include "runeblade_melee_logic.h"
#include "wire.h"
#include <cstddef>
#include <cstdint>

namespace crew::zwrb {

constexpr std::uint32_t kMagic=0x4D52575Au;   // "ZWRM"
constexpr std::uint32_t kVersion=1;
constexpr std::size_t kWire=96;
constexpr float kMostSpeed=50.0f,kMostYaw=100.0f;

enum class Kind : std::uint8_t { strike=1,ring=2,bolt=3 };
struct Msg {
    Kind kind=Kind::strike;
    unsigned char player[32]{};
    runeblade::Strike s{};   // ring: s.arm, s.from; bolt: arm, effect, from, dir, length (speed), radius (size), damage
    float yaw=0.0f;          // ring
};

namespace detail {
constexpr std::size_t kKindAt=8,kArm=9,kEffect=10,kPlayer=12,kPoints=44,kFrom=56,kDir=68,kLength=80,kRadius=84,kGrow=88,
                      kDamage=92;
static_assert(kDamage+4==kWire,"the layout");
inline bool Valid(const Msg& m) noexcept {
    if(!netref::IsPlayer(m.player))return false;
    const auto arm=static_cast<std::uint8_t>(m.s.arm);
    if(!arm || arm>=runeblade::kArmsMost)return false;
    switch(m.kind) {
    case Kind::strike: return runeblade::Valid(m.s);
    case Kind::ring: return m.s.arm==runeblade::Arm::stars && std::isfinite(m.yaw) && std::fabs(m.yaw)<=kMostYaw;
    case Kind::bolt: {
        const float dl=runeblade::Len(m.s.dir);
        return std::isfinite(dl) && dl>0.9f && dl<1.1f && m.s.length>0.0f && m.s.length<=kMostSpeed && m.s.radius>0.0f &&
               m.s.radius<=runeblade::kMostRadius && m.s.damage>=0.0f && std::isfinite(m.s.damage);
    }
    }
    return false;
}
inline const float* P(const runeblade::V3& v) noexcept { return &v.x; }
}  // namespace detail

inline bool Owned(const void* bytes,std::size_t size) noexcept { return wire::HeadWord(bytes,size,0)==kMagic; }

inline bool Encode(const Msg& m,void* bytes,std::size_t size) noexcept {
    if(!bytes || size!=kWire || !detail::Valid(m))return false;
    const runeblade::Strike& s=m.s;
    const float from[3]={s.from.x,s.from.y,s.from.z},dir[3]={s.dir.x,s.dir.y,s.dir.z};
    wire::Writer w(bytes,size);
    w.U32(kMagic).U32(kVersion).U8(static_cast<std::uint8_t>(m.kind)).U8(static_cast<std::uint8_t>(s.arm))
     .U8(static_cast<std::uint8_t>(s.effect));
    w.Seek(detail::kPlayer).Ref(m.player,false,true);
    w.U16(s.points).U16(s.waves).U16(s.every).U16(s.delay).U16(s.stagger);
    w.Seek(detail::kFrom).V3(from).V3(dir);
    w.F(m.kind==Kind::ring ? m.yaw : s.length).F(s.radius).F(s.grow).F(s.damage);
    return w.Ok();
}

// False: not ZWRM, another version, an unknown kind, the wrong size, a field out of its bounds.
inline bool Decode(const void* bytes,std::size_t size,Msg& out) noexcept {
    if(!Owned(bytes,size) || size!=kWire)return false;
    wire::Reader r(bytes,size);
    Msg m{};
    r.Skip(4);
    if(r.U32()!=kVersion)return false;
    const std::uint8_t kind=r.U8();
    if(kind<1 || kind>3)return false;
    m.kind=static_cast<Kind>(kind);
    m.s.arm=static_cast<runeblade::Arm>(r.U8());
    m.s.effect=static_cast<runeblade::Effect>(r.U8());
    r.Seek(detail::kPlayer);r.Ref(m.player,false,true);
    m.s.points=r.U16();m.s.waves=r.U16();m.s.every=r.U16();m.s.delay=r.U16();m.s.stagger=r.U16();
    r.Seek(detail::kFrom);
    float from[3],dir[3];
    r.V3(from);r.V3(dir,2.0f);
    m.s.from={from[0],from[1],from[2]};m.s.dir={dir[0],dir[1],dir[2]};
    const float length=r.F();
    m.s.radius=r.F();m.s.grow=r.F();m.s.damage=r.F();
    if(m.kind==Kind::ring)m.yaw=length;else m.s.length=length;
    if(!r.Ok() || !detail::Valid(m))return false;
    out=m;
    return true;
}

}  // namespace crew::zwrb
