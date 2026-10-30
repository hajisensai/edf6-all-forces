// The map's support pt shared by a team (the user, 2026-10-27: 「pt 改成全队互通」): pure, no game, no transport
// (tests/support_points_test.cpp drives it offline; support_points.cpp feeds it the game and plugin_net.h).
// The host keeps one pool a team (Pools: rows 0..6 of the team manager, every team's score read and charged on the host
// alone, PvP's factions each their own). A guest keeps the host's last word (Mirror) and its own changes the host has not
// confirmed yet: a call it paid is shown spent at once, sent to the host (a change: its team, its pt, a serial of this
// run of the guest's) and dropped from the guest's list once a host's state confirms that serial. The host takes a
// guest's changes in order, each once (Peer), and sends each guest the whole state with its last serial taken.
// Two machines spending the same pt at once: both calls stand and the pool stops at 0 (no call is taken back).
// The order a host meets a guest's changes in: the EDF6Coop extension route is ReliableOrdered only within one route
// generation (coop_extension_api.h), and a send can fail outright (SendHost false: the channel not ready a moment, a
// generation change cancels what was pending). A change that failed waits for its resend (kChangeAgainMs) while a newer
// one may go at once, so the host can meet a run's later change first. Each change therefore carries the oldest serial
// of its run the guest still holds unconfirmed (base): a host meeting a run for the first time starts it there (never at
// whatever change came first, which would skip the older ones for good), and a run whose older changes another host
// confirmed (the host moved) goes on past them.
// Wire: plugin_net.h channel SPTS (wire.h, little-endian, fixed kWire bytes): every machine of a room protocol 5 room
// (mod_room.h) has it, so no feature bit. Version 2 (2026-10-30): the change's base.
//   state  (host -> guest): magic, version, kind 1 @8, origin @12 (the guest run's whose serial is confirmed), taken @16
//                           (its last serial taken), most @20, scale @24, points[7] @28
//   change (guest -> host): magic, version, kind 2 @8, origin @12 (this run of the guest's), serial @16, team @20, pt @24,
//                           base @28 (the run's oldest serial not confirmed yet: 1..serial)
#pragma once
#include "support_points_logic.h"
#include "wire.h"
#include <cstddef>
#include <cstdint>

namespace crew::support_points {

constexpr std::uint32_t kMagic=0x53545053u;   // "SPTS"
constexpr std::uint32_t kVersion=2;
constexpr std::size_t kWire=56;
constexpr std::int32_t kMostPt=1000000;       // plugin.cpp's bound of SupportPtMost; a change or a pool past it is garbage
constexpr float kMostScale=100.0f;            // plugin.cpp's bound of SupportPtScale

enum class Kind : std::uint8_t { state=1,change=2 };
struct Msg {
    Kind kind=Kind::state;
    std::uint32_t origin=0;
    std::uint32_t serial=0;               // state: the last taken; change: its own
    std::int32_t most=0;float scale=0.0f; // state
    std::int32_t points[kTeams]{};        // state
    std::int32_t team=-1,pt=0;            // change: negative spent, positive given back
    std::uint32_t base=0;                 // change: the oldest serial of its run not confirmed yet (it or an older one)
};

namespace detail {
constexpr std::size_t kKindAt=8,kOrigin=12,kSerial=16,kMost=20,kTeam=20,kScale=24,kPt=24,kPoints=28,kBase=28,
    kUsed=kPoints+4*kTeams;
static_assert(kUsed==kWire,"the layout");
inline bool Valid(const Msg& m) noexcept {
    if(m.kind==Kind::state) {
        if(m.most<0 || m.most>kMostPt || !(m.scale>=0.0f) || m.scale>kMostScale)return false;
        for(std::int32_t p:m.points)if(p<0 || p>m.most)return false;
        return true;
    }
    return m.kind==Kind::change && m.origin && m.serial && m.base && m.base<=m.serial && m.team>=0 && m.team<kTeams &&
           m.pt!=0 && m.pt>=-kMostPt && m.pt<=kMostPt;
}
}  // namespace detail

inline bool Owned(const void* bytes,std::size_t size) noexcept { return wire::HeadWord(bytes,size,0)==kMagic; }

inline bool Encode(const Msg& m,void* bytes,std::size_t size) noexcept {
    if(!bytes || size!=kWire || !detail::Valid(m))return false;
    wire::Writer w(bytes,size);
    w.U32(kMagic).U32(kVersion).U8(static_cast<std::uint8_t>(m.kind)).Seek(detail::kOrigin).U32(m.origin).U32(m.serial);
    if(m.kind==Kind::state) {
        w.U32(static_cast<std::uint32_t>(m.most)).F(m.scale);
        for(std::int32_t p:m.points)w.U32(static_cast<std::uint32_t>(p));
    } else w.U32(static_cast<std::uint32_t>(m.team)).U32(static_cast<std::uint32_t>(m.pt)).U32(m.base);   // base @kBase
    return w.Ok();
}

// False: not SPTS, the wrong size, another version, an unknown kind, a number out of its bounds.
inline bool Decode(const void* bytes,std::size_t size,Msg& out) noexcept {
    if(!Owned(bytes,size) || size!=kWire)return false;
    wire::Reader r(bytes,size);
    Msg m{};
    r.Skip(4);
    if(r.U32()!=kVersion)return false;
    m.kind=static_cast<Kind>(r.U8());
    r.Seek(detail::kOrigin);
    m.origin=r.U32();m.serial=r.U32();
    if(m.kind==Kind::state) {
        m.most=static_cast<std::int32_t>(r.U32());m.scale=r.F(kMostScale);
        for(std::int32_t& p:m.points)p=static_cast<std::int32_t>(r.U32());
    } else {
        m.team=static_cast<std::int32_t>(r.U32());m.pt=static_cast<std::int32_t>(r.U32());m.base=r.U32();
    }
    if(!r.Ok() || !detail::Valid(m))return false;
    out=m;
    return true;
}

// The host's (offline: this machine's) pools, one a team.
struct Pools {
    Pool team[kTeams];
    void Start(const Settings& s) noexcept { for(Pool& p:team)p.Start(s); }
    // SupportPtMost lowered during a mission (the ini read again): every pool brought down to it, as no pool past the
    // most is sent (detail::Valid). True: one was over.
    bool Cap(const Settings& s) noexcept {
        const std::int32_t most=s.most<0 ? 0 : s.most;
        bool over=false;
        for(Pool& p:team)if(p.points>most){p.points=most;over=true;}
        return over;
    }
    // A frame's read of team `t`'s score.
    std::int32_t Take(std::int32_t t,float score,const Settings& s) noexcept {
        return t>=0 && t<kTeams ? team[t].Take(t,score,s) : 0;
    }
    // A guest's change: a spend stops at 0 (another machine spent the same pt meanwhile), a refund at the most.
    void Apply(std::int32_t t,std::int32_t pt,const Settings& s) noexcept {
        if(t<0 || t>=kTeams || !pt)return;
        Pool& p=team[t];
        if(pt>0){p.Refund(pt,s);return;}
        const std::int32_t take=-pt<p.points ? -pt : p.points;
        p.points-=take;p.spent+=-pt;
    }
};

// What the host knows of one guest: the run (origin) and the last serial of it taken. Kept by the guest's PUID, not
// its peer index (a roster change renumbers the peers; a change taken before must not be taken again when it comes
// once more).
struct Peer {
    std::uint32_t origin=0,taken=0;
    // True: `m` is this guest's next change (taken in order, each once). A run not seen yet begins at its oldest change
    // not confirmed (m.base: the change that came first may be a later one, see the top); a run whose changes before
    // m.base another host confirmed goes on from there.
    bool Next(const Msg& m) noexcept {
        if(!m.base || m.base>m.serial)return false;
        if(m.origin!=origin){origin=m.origin;taken=m.base-1;}
        else if(m.base>taken+1)taken=m.base-1;
        if(m.serial!=taken+1)return false;
        taken=m.serial;
        return true;
    }
};

// A guest's: the host's last state and this run's changes it has not confirmed.
struct Change { std::uint32_t serial=0; std::int32_t team=-1,pt=0; std::uint64_t sentAt=0; };
constexpr int kChangesMost=16;
struct Mirror {
    bool heard=false;                 // a host's state came this mission
    std::int32_t points[kTeams]{};
    std::int32_t most=0;float scale=1.0f;
    std::uint32_t origin=0,next=1;
    Change changes[kChangesMost]{};
    int count=0;
    // A call's price held while the call is asked (Hold): shown spent, and a change's room kept for it, so that paying
    // for the call once it is taken (Commit) cannot fail. None: heldPt 0.
    std::int32_t heldTeam=-1,heldPt=0;

    void Start(const Settings& s,std::uint32_t runOrigin) noexcept {
        *this=Mirror{};
        origin=runOrigin;most=s.most;scale=s.scale;
        const std::int32_t start=s.start<0 ? 0 : s.start>s.most ? s.most : s.start;
        for(std::int32_t& p:points)p=start;
    }
    // Team `t`'s pool as this guest sees it: the host's word and its own changes since, within 0..most.
    std::int32_t Shown(std::int32_t t) const noexcept {
        if(t<0 || t>=kTeams)return 0;
        std::int64_t v=points[t];
        for(int i=0;i<count;++i)if(changes[i].team==t)v+=changes[i].pt;
        if(heldPt>0 && heldTeam==t)v-=heldPt;
        return v<0 ? 0 : v>most ? most : static_cast<std::int32_t>(v);
    }
    // The changes that may still be kept: the room a held price keeps is not another change's.
    int Room() const noexcept { return kChangesMost-count-(heldPt>0 ? 1 : 0); }
    // A change of this guest's, kept until confirmed; the message to send. False: no room (nothing kept).
    bool Keep(std::int32_t t,std::int32_t pt,std::uint64_t now,Msg* out) noexcept {
        if(t<0 || t>=kTeams || !pt || Room()<=0 || !origin)return false;
        changes[count++]={next,t,pt,now};
        *out=ChangeMsg(changes[count-1]);
        ++next;
        return true;
    }
    // A call's `price` of team `t` held (one at a time). False (nothing held): the team's pool shows less, no room for
    // its change (kChangesMost unconfirmed), or one is held already.
    bool Hold(std::int32_t t,std::int32_t price) noexcept {
        if(t<0 || t>=kTeams || price<=0 || heldPt>0 || !origin || Room()<=0 || Shown(t)<price)return false;
        heldTeam=t;heldPt=price;
        return true;
    }
    // The held price spent: its change kept (in the room held for it) and its message. False: none held.
    bool Commit(std::uint64_t now,Msg* out) noexcept {
        if(heldPt<=0)return false;
        const std::int32_t t=heldTeam,pt=-heldPt;
        heldTeam=-1;heldPt=0;
        return Keep(t,pt,now,out);
    }
    void Drop() noexcept { heldTeam=-1;heldPt=0; }   // the call not asked: nothing spent, nothing sent
    Msg ChangeMsg(const Change& c) const noexcept {
        Msg m;m.kind=Kind::change;m.origin=origin;m.serial=c.serial;m.team=c.team;m.pt=c.pt;
        m.base=count>0 ? changes[0].serial : c.serial;   // the changes are kept oldest first
        return m;
    }
    // A host's state: its pools, and this run's changes up to `serial` dropped (another run's confirmation drops none).
    void Hear(const Msg& m) noexcept {
        if(m.kind!=Kind::state)return;
        heard=true;most=m.most;scale=m.scale;
        for(int t=0;t<kTeams;++t)points[t]=m.points[t];
        if(m.origin!=origin)return;
        int kept=0;
        for(int i=0;i<count;++i)if(changes[i].serial>m.serial)changes[kept++]=changes[i];
        count=kept;
    }
};

}  // namespace crew::support_points
