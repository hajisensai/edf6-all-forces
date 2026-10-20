#include "gravity_net_protocol.h"
#include "wire.h"
#include <cmath>
#include <cstring>

namespace crew::gravity_net {
namespace {
// GRVT: magic, version, kind (byte 8), origin, grab; grabber[32]; target[32]; at[3]; rayA[3]; rayB[3]; goal[3]; vel[3];
// reserved to kGrabWire (zero sent, ignored read).
constexpr std::size_t kGOrigin=12,kGGrab=16,kGGrabber=20,kGTarget=52,kGAt=84,kGRayA=96,kGRayB=108,kGGoal=120,kGVel=132,kGUsed=144;
static_assert(kGUsed<=kGrabWire,"a grab fits its wire size");
// FGUN: magic, version, kind (byte 8), serial (u16 at 10), index (byte 12), origin; who[32]; at[3]; leftMs.
constexpr std::size_t kUSerial=10,kUIndex=12,kUOrigin=16,kUWho=20,kUAt=52,kULeft=64,kUUsed=68;
static_assert(kUUsed<=kGunWire,"a gun message fits its wire size");
// The fields go in this order (wire.h writes and reads them in sequence from each Seek).
static_assert(kGGrab==kGOrigin+4 && kGGrabber==kGGrab+4 && kGTarget==kGGrabber+32 && kGAt==kGTarget+32 && kGRayA==kGAt+12 &&
              kGRayB==kGRayA+12 && kGGoal==kGRayB+12 && kGVel==kGGoal+12 && kGUsed==kGVel+12,"the grab's layout");
static_assert(kUIndex==kUSerial+2 && kUWho==kUOrigin+4 && kUAt==kUWho+32 && kULeft==kUAt+12 && kUUsed==kULeft+4,"the gun's layout");

bool Owned(const void* bytes,std::size_t size,std::uint32_t magic) noexcept { return wire::HeadWord(bytes,size,0)==magic && size>=4; }
bool Kind(GrabKind k) noexcept { return k==GrabKind::hold || k==GrabKind::thrown || k==GrabKind::letGo; }
bool Kind(GunKind k) noexcept { return k==GunKind::drop || k==GunKind::pick || k==GunKind::owner || k==GunKind::gone; }
}  // namespace

bool OwnedGrab(const void* bytes,std::size_t size) noexcept { return Owned(bytes,size,kGrabMagic); }
bool OwnedGun(const void* bytes,std::size_t size) noexcept { return Owned(bytes,size,kGunMagic); }

bool Encode(const Grab& g,void* bytes,std::size_t size) noexcept {
    if(!bytes || size!=kGrabWire || !Kind(g.kind))return false;
    wire::Writer w(bytes,size);
    w.U32(kGrabMagic).U32(kVersion).U8(static_cast<std::uint8_t>(g.kind)).Seek(kGOrigin).U32(g.origin).U32(g.grab)
     .Id(g.grabber).Id(g.target).V3(g.at).V3(g.rayA).V3(g.rayB).V3(g.goal).V3(g.vel);
    return w.Ok() && w.Used()==kGUsed;
}

bool Decode(const void* bytes,std::size_t size,Grab& out) noexcept {
    if(!OwnedGrab(bytes,size) || size!=kGrabWire)return false;
    wire::Reader r(bytes,size);
    Grab g{};
    r.Skip(4);
    if(r.U32()!=kVersion)return false;
    g.kind=static_cast<GrabKind>(r.U8());
    r.Seek(kGOrigin);g.origin=r.U32();g.grab=r.U32();
    r.Id(g.grabber);r.Id(g.target);
    r.V3(g.at);r.V3(g.rayA);r.V3(g.rayB);r.V3(g.goal);r.V3(g.vel);
    if(!r.Ok() || !Kind(g.kind))return false;
    out=g;
    return true;
}

bool Encode(const Gun& g,void* bytes,std::size_t size) noexcept {
    if(!bytes || size!=kGunWire || g.leftMs>kMostLeftMs || !Kind(g.kind))return false;
    wire::Writer w(bytes,size);
    w.U32(kGunMagic).U32(kVersion).U8(static_cast<std::uint8_t>(g.kind)).Seek(kUSerial).U16(g.serial).U8(g.index)
     .Seek(kUOrigin).U32(g.origin).Id(g.who).V3(g.at).U32(g.leftMs);
    return w.Ok() && w.Used()==kUUsed;
}

bool Decode(const void* bytes,std::size_t size,Gun& out) noexcept {
    if(!OwnedGun(bytes,size) || size!=kGunWire)return false;
    wire::Reader r(bytes,size);
    Gun g{};
    r.Skip(4);
    if(r.U32()!=kVersion)return false;
    g.kind=static_cast<GunKind>(r.U8());
    r.Seek(kUSerial);g.serial=r.U16();g.index=r.U8();
    r.Seek(kUOrigin);g.origin=r.U32();
    r.Id(g.who);r.V3(g.at);g.leftMs=r.U32();
    if(!r.Ok() || !Kind(g.kind) || g.leftMs>kMostLeftMs)return false;
    out=g;
    return true;
}

GrabEvent GrabInbox::Receive(std::uint32_t peer,const Grab& g,std::uint64_t now) noexcept {
    if(!peer || peer>kMaxPeers)return GrabEvent::none;
    RemoteGrab& r=peer_[peer];
    Done& d=done_[peer];
    const bool same=r.active && r.last.origin==g.origin && r.last.grab==g.grab;
    // An ended grab, or an older one of the same run: nothing (the transport is ordered; this guards a replay).
    if(d.any && d.origin==g.origin && static_cast<std::int32_t>(g.grab-d.grab)<=0)return GrabEvent::none;
    if(!same && r.active && r.last.origin==g.origin && static_cast<std::int32_t>(g.grab-r.last.grab)<0)return GrabEvent::none;
    if(g.kind==GrabKind::hold) {
        r.active=true;r.heardAt=now;r.last=g;
        return same ? GrabEvent::held : GrabEvent::started;
    }
    r.active=false;r.last=g;r.heardAt=now;
    d=Done{g.origin,g.grab,true};
    if(g.kind==GrabKind::letGo)return same ? GrabEvent::letGo : GrabEvent::none;
    return GrabEvent::thrown;
}

bool GrabInbox::Holding(std::uint32_t peer,std::uint64_t now) noexcept {
    if(!peer || peer>kMaxPeers)return false;
    RemoteGrab& r=peer_[peer];
    if(r.active && now-r.heardAt>kHoldSilentMs)r.active=false;
    return r.active;
}
}  // namespace crew::gravity_net
