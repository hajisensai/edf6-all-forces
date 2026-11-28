// The Vehicler's logistics points online (docs/newclass-re/vehicler.md §3.2): plugin_net.h channel "vehicler logistics",
// magic 'ZWLG'. Pure (wire.h, little-endian, fixed sizes a kind); tests/vehicler_logistics_test.cpp round-trips it.
// The host keeps the book (vehicler_logistics_logic.h Ledger); the machine that settles a kill (the victim's authority,
// docs/newclass-re/vehicler.md §3.1) and is not the host reports it, the host books it once by the victim and answers.
//   credit (settling guest -> host): magic, version, kind 1 @8, victim @12 (a NetId reference: a player is no kill of
//                                    theirs), player @44 (a player reference: whose kill), points @76 (1..kMostCredit)
//   ack    (host -> that guest):     magic, version, kind 3 @8, victim @12, the rest zero (kCreditWire bytes): the
//                                    guest stops sending it again
//   state  (host -> every guest):    magic, version, kind 2 @8, serial @12, gauge @16, count @20, rows @24 (kRowWire a
//                                    row: slot u16, points u16, held u16, flags u16: bit 0 a resupply of that player's on
//                                    its way), kRowsMost rows (the unused zero)
//   handed (host -> every guest):    magic, version, kind 4 @8, hull @12 (the delivered vehicle's NetId), courier @44 (the
//                                    soldier who drove it in), flags @76 (bit 0: the courier now follows the owner): the
//                                    vehicle is its owner's now (zw_vehicle_dispatch.cpp)
//   retired (host -> every guest):   magic, version, kind 5 @8, hull @12 (the replaced vehicle's NetId), courier @44, flags
//                                    @76 zero: the courier drove the old vehicle off the field; every machine deletes its
//                                    copy of both (a native deletion is no message of the game's)
// The resupply's call is no message of this channel: it is the support transaction catalog 1019 (zw_vehicle_dispatch.h),
// the requester the authenticated transport's player; the host holds the price there (the ledger's hooks).
#pragma once
#include "vehicler_logistics_logic.h"
#include "wire.h"
#include <cstddef>
#include <cstdint>

namespace crew::zwlog {

constexpr std::uint32_t kMagic=0x474C575Au;   // "ZWLG"
constexpr std::uint32_t kVersion=1;
constexpr std::size_t kCreditWire=80,kRowWire=8,kStateWire=24+kRowWire*kRowsMost;

enum class Kind : std::uint8_t { credit=1,state=2,ack=3,handed=4,retired=5 };
constexpr unsigned kRowTransit=1u,kHandedFollows=1u;
struct Msg {
    Kind kind=Kind::state;
    unsigned char victim[32]{};   // credit / ack; handed / retired: the hull
    unsigned char player[32]{};   // credit; handed / retired: the courier
    unsigned points=0;            // credit; handed / retired: the flags
    std::uint32_t serial=0;       // state
    unsigned gauge=0;             // state
    int count=0;                  // state
    MirrorRow rows[kRowsMost]{};  // state
};

namespace detail {
constexpr std::size_t kKindAt=8,kVictim=12,kPlayer=44,kPoints=76,kSerial=12,kGauge=16,kCount=20,kRows=24;
static_assert(kPoints+4==kCreditWire && kRows+kRowWire*kRowsMost==kStateWire,"the layout");
inline bool Valid(const Msg& m) noexcept {
    switch(m.kind) {
    case Kind::credit:
        return netref::ValidNative(m.victim,false) && netref::IsPlayer(m.player) && m.points>=1 && m.points<=kMostCredit;
    case Kind::ack: return netref::ValidNative(m.victim,false);
    case Kind::handed: return netref::ValidNative(m.victim,false) && netref::ValidNative(m.player,false) && m.points<=kHandedFollows;
    case Kind::retired: return netref::ValidNative(m.victim,false) && netref::ValidNative(m.player,false) && !m.points;
    case Kind::state:
        if(m.gauge>kMostGauge || m.count<0 || m.count>kRowsMost)return false;
        for(int i=0;i<m.count;++i) {
            const MirrorRow& r=m.rows[i];
            if(r.slot<0 || r.slot>=kSlotsMost || r.points+r.held>m.gauge || (r.flags&~kRowTransit))return false;
            for(int j=0;j<i;++j)if(m.rows[j].slot==r.slot)return false;
        }
        return true;
    }
    return false;
}
}  // namespace detail

constexpr std::size_t WireOf(Kind k) noexcept { return k==Kind::state ? kStateWire : kCreditWire; }
inline bool Owned(const void* bytes,std::size_t size) noexcept { return wire::HeadWord(bytes,size,0)==kMagic; }

inline bool Encode(const Msg& m,void* bytes,std::size_t size) noexcept {
    if(!bytes || size!=WireOf(m.kind) || !detail::Valid(m))return false;
    wire::Writer w(bytes,size);
    w.U32(kMagic).U32(kVersion).U8(static_cast<std::uint8_t>(m.kind));
    if(m.kind==Kind::state) {
        w.Seek(detail::kSerial).U32(m.serial).U32(m.gauge).U32(static_cast<std::uint32_t>(m.count));
        for(int i=0;i<m.count;++i)
            w.U16(static_cast<std::uint16_t>(m.rows[i].slot)).U16(static_cast<std::uint16_t>(m.rows[i].points))
             .U16(static_cast<std::uint16_t>(m.rows[i].held)).U16(static_cast<std::uint16_t>(m.rows[i].flags));
    } else {
        w.Seek(detail::kVictim).Ref(m.victim,false);
        if(m.kind==Kind::credit)w.Ref(m.player,false,true).U32(m.points);
        else if(m.kind!=Kind::ack)w.Ref(m.player,false).U32(m.points);
    }
    return w.Ok();
}

// False: not ZWLG, another version, an unknown kind, the wrong size for its kind, a field out of its bounds.
inline bool Decode(const void* bytes,std::size_t size,Msg& out) noexcept {
    if(!Owned(bytes,size) || size<detail::kKindAt+1)return false;
    wire::Reader r(bytes,size);
    Msg m{};
    r.Skip(4);
    if(r.U32()!=kVersion)return false;
    const std::uint8_t kind=r.U8();
    if(kind<1 || kind>5)return false;
    m.kind=static_cast<Kind>(kind);
    if(size!=WireOf(m.kind))return false;
    if(m.kind==Kind::state) {
        r.Seek(detail::kSerial);
        m.serial=r.U32();m.gauge=r.U32();
        const std::uint32_t count=r.U32();
        if(count>static_cast<std::uint32_t>(kRowsMost))return false;
        m.count=static_cast<int>(count);
        for(int i=0;i<m.count;++i) {
            m.rows[i].slot=r.U16();m.rows[i].points=r.U16();m.rows[i].held=r.U16();
            m.rows[i].flags=r.U16();
        }
    } else {
        r.Seek(detail::kVictim);
        r.Ref(m.victim,false);
        if(m.kind==Kind::credit){r.Ref(m.player,false,true);m.points=r.U32();}
        else if(m.kind!=Kind::ack){r.Ref(m.player,false);m.points=r.U32();}
    }
    if(!r.Ok() || !detail::Valid(m))return false;
    out=m;
    return true;
}

// The host's book as a state message.
inline Msg StateOf(const Ledger& l) noexcept {
    Msg m;
    m.kind=Kind::state;m.serial=l.serial;m.gauge=l.gauge;
    for(const Row& r:l.rows) {
        if(r.slot<0 || m.count>=kRowsMost)continue;
        m.rows[m.count++]=MirrorRow{r.slot,r.points,r.held,r.transit ? kRowTransit : 0u};
    }
    return m;
}
// A guest hears the host's state.
inline void Hear(Mirror& mirror,const Msg& m) noexcept {
    if(m.kind!=Kind::state)return;
    mirror.heard=true;mirror.serial=m.serial;mirror.gauge=m.gauge;mirror.count=m.count;
    for(int i=0;i<kRowsMost;++i)mirror.rows[i]=i<m.count ? m.rows[i] : MirrorRow{};
}
// A guest that hosts now starts from the host's last word (the objects seen are not known: a kill reported twice across
// the move may be booked twice, as any machine that never heard the first report would).
inline void Adopt(Ledger& l,const Mirror& mirror) {
    for(Row& r:l.rows)r=Row{};
    l.gauge=mirror.gauge ? mirror.gauge : l.gauge;
    for(int i=0;i<mirror.count && i<kRowsMost;++i) {
        Row& r=l.rows[i];
        r.slot=mirror.rows[i].slot;
        r.points=mirror.rows[i].points+mirror.rows[i].held;   // a call the old host held is not this one's: given back
    }
    ++l.serial;
}
}  // namespace crew::zwlog
