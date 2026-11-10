// The squads that follow a player get in and out with that player, and the seats are handed out by distance -- the pure
// part (src/npcai.cpp drives it; tools/ride_along_check.cpp checks it offline). No game, no Windows.
// Test site #45 (2026-11-10, the maintainer: 「45 做」; docs/feedback-2026-11-10-wheel-follow.md):
//  - 「玩家登上载具时，跟随的 NPC 自动上车坐乘客座，玩家下车时 NPC 自动跟随下车」: a recruited squad with no order of its own
//    (it marches round its player: mapcmd Order::none) is sent into the vehicle its player has just got into -- the
//    passenger seats first, then the gunners'; never the driver's (the player chose the vehicle and who drives it). Those
//    with no seat keep walking after the player as before. When the player gets out (or into another vehicle), the
//    members seated in that vehicle get out with them (only that vehicle's: a member the map put in another stays).
//    While the player stays in, the squad is offered the seats again every kRetryMs (a seat given up, a member that
//    came late), until a map order is given to the squad: from then on it is the player's (quiet) until the player's
//    vehicle changes again.
//  - 「运兵车和运输直升机与 NPC 协同 … NPC 不抢载具、自动分配」 (its least part, docs/plan-2026-11-10-transport-coop.md): a
//    squad's members get the seats nearest them, not the first free seat in member order; the seats a member of another
//    squad is walking to stay reserved for it (npcai.cpp Reserved) and are not offered.
//  - 「首次情境提示 … 在图标位置显示「按 M 键进行指挥」」: the first time the player on foot comes near a squad the map can
//    command (one to recruit, or the player's own), a line over its head says so for kHintMs; then never again (the ini's
//    CommandHintShown=1, written once).
#pragma once
#include <cstdint>

namespace ridealong {
// The seat roles (npcai.cpp SeatPriority; mapcmd_logic.h kSeatDriver ...).
inline constexpr int kDriver=0,kGunner=1,kPassenger=2;
inline constexpr int kSeatsMost=16;     // common/edf/seat.h kMaxSeats
inline constexpr int kMembersMost=16;   // npcai.cpp kMaxSquad
// The order the roles are filled in: the map's BOARD the driver first (the vehicle goes), the gunners, the passengers;
// riding along with the player the passengers, then the gunners, never the driver.
inline constexpr int kBoardRoles[]={kDriver,kGunner,kPassenger};
inline constexpr int kAlongRoles[]={kPassenger,kGunner};

// A seat on offer: its role, and whether it may be given (empty, not reserved for someone else's walk).
struct Seat { int role=kPassenger; bool open=false; };
// What the caller knows of member m and seat k: whether m may take k (on foot, its class fits the seat) and how far it
// has to walk to k (m).
struct Offer {
    int members=0,seats=0;
    Seat seat[kSeatsMost]{};
    bool fits[kMembersMost][kSeatsMost]{};
    float distance[kMembersMost][kSeatsMost]{};
};
// Hands out the seats: role by role in `roles` order, each time the free pair (member, seat) with the shortest walk
// (ties: the lower member, then the lower seat), until no member left without a seat fits a free seat of that role.
// `out[m]`: the seat member m is given, -1 none. Returns how many are given a seat.
inline int Assign(const Offer& o,const int* roles,int roleCount,int* out) noexcept {
    const int members=o.members<kMembersMost ? (o.members>0 ? o.members : 0) : kMembersMost;
    const int seats=o.seats<kSeatsMost ? (o.seats>0 ? o.seats : 0) : kSeatsMost;
    bool taken[kSeatsMost]{};
    for(int m=0;m<members;++m)out[m]=-1;
    int given=0;
    for(int r=0;r<roleCount;++r) {
        for(;;) {
            int bm=-1,bk=-1;float bd=0.0f;
            for(int m=0;m<members;++m) {
                if(out[m]>=0)continue;
                for(int k=0;k<seats;++k) {
                    if(taken[k] || !o.seat[k].open || o.seat[k].role!=roles[r] || !o.fits[m][k])continue;
                    const float d=o.distance[m][k];
                    if(!(d>=0.0f))continue;   // NaN or negative: no way there
                    if(bm<0 || d<bd){bm=m;bk=k;bd=d;}
                }
            }
            if(bm<0)break;
            out[bm]=bk;taken[bk]=true;++given;
        }
    }
    return given;
}

// --- Riding along with the player ---
inline constexpr std::uint64_t kRetryMs=3000;
// A squad's state, kept by the squad (npcai.cpp Squad::along). Identities are compared only (the vehicles' addresses).
struct Follow {
    const void* seen=nullptr;    // the vehicle its player rode at the last frame (nullptr: on foot)
    const void* along=nullptr;   // the vehicle the squad was sent into with its player, still riding it
    bool quiet=false;            // a map order since: no more offers into `along`, no getting out with the player
    std::uint64_t lastTry=0;
};
// board: offer the squad the seats of the player's vehicle; leave: the members seated in `left` get out; both: leave
// first, then board.
enum class Act : std::uint8_t { none, board, leave, leaveThenBoard };
struct Step { Act act=Act::none; const void* left=nullptr; };
// A frame. `eligible`: the squad follows its player with no order of its own (recruited, Order::none, not scripted) and
// riding along is on; `now`: the vehicle its player rides now (nullptr on foot).
inline Step Advance(Follow& f,bool eligible,const void* now,std::uint64_t ms,std::uint64_t retry=kRetryMs) noexcept {
    Step s{};
    if(!eligible){f=Follow{};f.seen=now;return s;}   // an order, a dismissal: nothing of the old ride is kept
    if(now!=f.seen) {
        const void* left=f.along && f.along!=now && !f.quiet ? f.along : nullptr;
        f.seen=now;f.quiet=false;f.along=now;f.lastTry=ms;
        s.left=left;
        s.act=left && now ? Act::leaveThenBoard : left ? Act::leave : now ? Act::board : Act::none;
        return s;
    }
    if(now && f.along==now && !f.quiet && ms-f.lastTry>=retry){f.lastTry=ms;s.act=Act::board;}
    return s;
}
// A map order taken by the squad (any: BOARD, DISMOUNT, a point...): the ride is the player's to direct from now on.
inline void Ordered(Follow& f) noexcept { f.quiet=true; }

// --- The first-time hint ---
inline constexpr std::uint64_t kHintMs=10000;
inline constexpr float kHintNear=25.0f;   // m: a commandable squad's top this near the player on foot
struct Hint { bool done=false,showing=false; std::uint64_t since=0; const void* who=nullptr; };
// A frame: `candidate` the nearest commandable squad's top within kHintNear (nullptr none). True while the hint shows
// (over `h.who`); `finished` set on the frame it ends for good (the caller writes CommandHintShown=1 then).
inline bool HintStep(Hint& h,const void* candidate,std::uint64_t ms,bool* finished,std::uint64_t showMs=kHintMs) noexcept {
    if(finished)*finished=false;
    if(h.done)return false;
    if(!h.showing) {
        if(!candidate)return false;
        h.showing=true;h.since=ms;h.who=candidate;
        return true;
    }
    if(ms-h.since>=showMs) {
        h.showing=false;h.done=true;h.who=nullptr;
        if(finished)*finished=true;
        return false;
    }
    return true;
}
}  // namespace ridealong
