// The Vehicler's logistics points (docs/newclass-re/vehicler-plan.md §3.1, §3.2, §5 M4): pure, no game, no transport
// (tests/vehicler_logistics_test.cpp drives it offline; vehicler_logistics.cpp feeds it the damage hook's kills and the
// plugin channel, vehicler_logistics_net.h the wire).
//  - A kill is worth Credit(victim): an enemy's body 1 point, a part of one (a weak spot that dies on its own) or anything
//    not an enemy nothing. Room for tiers by the enemy's size is in VictimInfo (not used yet: the numbers are tuned later).
//  - Whose kill (Attribute): a Vehicler player on foot is his own; a vehicle's is the rider of the seat holding the weapon
//    that fired (none when that rider is no Vehicler player), the driver seat's rider when the weapon is not known or no
//    seat holds it; anything else (an NPC, an enemy, the world) is nobody's.
//  - The host keeps every Vehicler's points (Ledger, one row a mission player slot); the gauge caps points + held, so a
//    refund always fits and a kill past the cap is not kept. Each killed object is booked once (by its NetRef: a guest's
//    report that comes twice, two machines reporting one kill).
//  - A resupply's price is held (Hold: taken from the points, kept apart under a ticket) while the host plans it, then
//    settled (Settle: spent) or given back (Refund). One hold a player at a time: a second call while one is held is
//    refused (one resupply on its way a player, §3.3).
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <unordered_set>

namespace crew::zwlog {

constexpr int kRowsMost=16;                // the Vehicler players a mission keeps points for (a room's players at most)
constexpr unsigned kMostCredit=8;          // a kill's most points (a tier's room)
constexpr unsigned kMostGauge=1000;        // a gauge past this is garbage (the wire's bound)
constexpr int kSlotsMost=1024;             // net_object_ref.h kPlayerSlots: a mission player slot is below it
constexpr int kSeatsMost=16;               // common/edf/seat.h kMaxSeats

// ---- a kill's worth ----
struct VictimInfo {
    bool enemy=false;   // of a team hostile to the players
    bool part=false;    // a weak spot / part that dies as a whole of its own (damagestats.h WeakPartObject)
    float size=0.0f;    // the enemy's size (m), for tiers later; 0 unknown
};
constexpr unsigned Credit(const VictimInfo& v) noexcept { return v.enemy && !v.part ? 1u : 0u; }

// ---- whose kill ----
// The shooter as the settling machine saw it. `self`: a soldier's own key (its mission slot when it is a Vehicler player,
// -1 for anyone else). A vehicle's: its seats' riders' keys (the same rule each), and the seat whose holders have the
// weapon that fired (-1 unknown / none).
struct Shooter {
    bool vehicle=false;
    int self=-1;
    int seats=0;
    int rider[kSeatsMost]{};
    int weaponSeat=-1;
};
constexpr int Attribute(const Shooter& s) noexcept {
    if(!s.vehicle)return s.self>=0 && s.self<kSlotsMost ? s.self : -1;
    if(s.seats<=0 || s.seats>kSeatsMost)return -1;
    const int seat=s.weaponSeat>=0 && s.weaponSeat<s.seats ? s.weaponSeat : 0;
    const int key=s.rider[seat];
    return key>=0 && key<kSlotsMost ? key : -1;
}

// ---- the killed objects seen (one book each) ----
// A 64-bit FNV-1a of the victim's 32-byte reference (net_object_ref.h): two references the same only when the objects are.
inline std::uint64_t VictimKey(const unsigned char* ref) noexcept {
    std::uint64_t h=0xCBF29CE484222325ull;
    for(int i=0;i<32;++i){h^=ref[i];h*=0x100000001B3ull;}
    return h;
}

// ---- the host's book ----
struct Row {
    int slot=-1;              // the mission player slot (-1: a free row)
    unsigned points=0;        // what may be spent
    unsigned held=0;          // the price of the call being planned (0: none)
    std::uint64_t ticket=0;   // its ticket (0: none)
    bool transit=false;       // a resupply of this player's on its way (zw_vehicle_dispatch.h ZwDeliverPending, the host's)
};
enum class Booked : std::uint8_t {
    kept,        // the points added (maybe fewer than given: the gauge's cap)
    capped,      // the gauge full: nothing added (the kill still booked)
    seen,        // that object was booked before
    nothing,     // worth no point / no player
    full,        // no row left for a new player
};
struct Ledger {
    Row rows[kRowsMost]{};
    unsigned gauge=0;
    std::uint64_t nextTicket=0;
    std::uint32_t serial=0;   // changes with every change of a row (the state sent again)
    std::unordered_set<std::uint64_t> seen;

    // A new mission: every row and the objects seen dropped; `most` the gauge (points + held at most).
    void Start(unsigned most) {
        for(Row& r:rows)r=Row{};
        seen.clear();
        gauge=most>kMostGauge ? kMostGauge : most;
        ++serial;
    }
    const Row* Find(int slot) const noexcept {
        for(const Row& r:rows)if(r.slot>=0 && r.slot==slot)return &r;
        return nullptr;
    }
    Row* Find(int slot) noexcept { return const_cast<Row*>(static_cast<const Ledger*>(this)->Find(slot)); }
    Row* Add(int slot) noexcept {
        if(slot<0 || slot>=kSlotsMost)return nullptr;
        if(Row* r=Find(slot))return r;
        for(Row& r:rows)if(r.slot<0){r=Row{};r.slot=slot;++serial;return &r;}
        return nullptr;
    }
    unsigned Points(int slot) const noexcept { const Row* r=Find(slot);return r ? r->points : 0u; }
    // A kill of `points` for `slot`; `victim` its reference (null: none to know it by, booked as it comes: the hook calls
    // once a death on the machine that settles it). `added` (may be null) gets what was added.
    Booked Book(const unsigned char* victim,int slot,unsigned points,unsigned* added=nullptr) {
        if(added)*added=0;
        if(!points || points>kMostCredit || slot<0 || slot>=kSlotsMost)return Booked::nothing;
        if(victim && !seen.insert(VictimKey(victim)).second)return Booked::seen;
        Row* const r=Add(slot);
        if(!r)return Booked::full;
        const unsigned room=gauge>r->points+r->held ? gauge-r->points-r->held : 0u;
        const unsigned take=points<room ? points : room;
        if(!take)return Booked::capped;
        r->points+=take;++serial;
        if(added)*added=take;
        return Booked::kept;
    }
    // A call of `price` for `slot`: false (nothing taken) with too few points, one held already, or a price of nothing
    // or past the gauge.
    bool Hold(int slot,unsigned price,std::uint64_t* ticket) noexcept {
        if(ticket)*ticket=0;
        Row* const r=Find(slot);
        if(!r || !price || price>gauge || r->held || r->points<price)return false;
        r->points-=price;r->held=price;r->ticket=++nextTicket;++serial;
        if(ticket)*ticket=r->ticket;
        return true;
    }
    Row* OfTicket(std::uint64_t ticket) noexcept {
        if(!ticket)return nullptr;
        for(Row& r:rows)if(r.slot>=0 && r.ticket==ticket)return &r;
        return nullptr;
    }
    // The held price spent / given back. False: no such ticket (settled already, or of a mission before).
    bool Settle(std::uint64_t ticket) noexcept {
        Row* const r=OfTicket(ticket);
        if(!r)return false;
        r->held=0;r->ticket=0;++serial;
        return true;
    }
    bool Refund(std::uint64_t ticket) noexcept {
        Row* const r=OfTicket(ticket);
        if(!r)return false;
        r->points+=r->held;r->held=0;r->ticket=0;++serial;   // points + held never past the gauge: it fits
        return true;
    }
    // Whether `slot` has a resupply on its way (the host's dispatch says): true when that changed.
    bool Transit(int slot,bool on) noexcept {
        Row* const r=Find(slot);
        if(!r || r->transit==on)return false;
        r->transit=on;++serial;return true;
    }
    // The gauge lowered during a mission: every row brought within it (held first kept: it is a call being planned).
    bool Cap(unsigned most) noexcept {
        if(most>kMostGauge)most=kMostGauge;
        gauge=most;
        bool over=false;
        for(Row& r:rows) {
            if(r.slot<0 || r.points+r.held<=most)continue;
            r.points=most>r.held ? most-r.held : 0u;over=true;
        }
        if(over)++serial;
        return over;
    }
};

// ---- a guest's word from the host ----
struct MirrorRow { int slot=-1; unsigned points=0,held=0,flags=0; };
struct Mirror {
    bool heard=false;
    std::uint32_t serial=0;
    unsigned gauge=0;
    int count=0;
    MirrorRow rows[kRowsMost]{};
    void Start() noexcept { *this=Mirror{}; }
    const MirrorRow* Find(int slot) const noexcept {
        for(int i=0;i<count && i<kRowsMost;++i)if(rows[i].slot==slot)return &rows[i];
        return nullptr;
    }
};
}  // namespace crew::zwlog
