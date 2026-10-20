#include "gravity_net_protocol.h"
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

void Put32(unsigned char* p,std::uint32_t v) noexcept { for(int i=0;i<4;++i)p[i]=static_cast<unsigned char>(v>>(8*i)); }
std::uint32_t Get32(const unsigned char* p) noexcept {
    std::uint32_t v=0;
    for(int i=0;i<4;++i)v|=static_cast<std::uint32_t>(p[i])<<(8*i);
    return v;
}
void PutF(unsigned char* p,float f) noexcept { std::uint32_t v;std::memcpy(&v,&f,4);Put32(p,v); }
float GetF(const unsigned char* p) noexcept { const std::uint32_t v=Get32(p);float f;std::memcpy(&f,&v,4);return f; }
void PutV(unsigned char* p,const float* v) noexcept { for(int i=0;i<3;++i)PutF(p+4*i,v[i]); }
void GetV(const unsigned char* p,float* v) noexcept { for(int i=0;i<3;++i)v[i]=GetF(p+4*i); }
// A native ID as command_identity.cpp takes it (bytes 20..23 zero), or all zero (none).
bool IdOk(const unsigned char* id) noexcept { for(int i=20;i<24;++i)if(id[i])return false;return true; }
bool Finite(const float* v) noexcept { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }
bool Owned(const void* bytes,std::size_t size,std::uint32_t magic) noexcept {
    return bytes && size>=4 && Get32(static_cast<const unsigned char*>(bytes))==magic;
}
bool GrabFinite(const Grab& g) noexcept {
    return Finite(g.at) && Finite(g.rayA) && Finite(g.rayB) && Finite(g.goal) && Finite(g.vel);
}
}  // namespace

bool OwnedGrab(const void* bytes,std::size_t size) noexcept { return Owned(bytes,size,kGrabMagic); }
bool OwnedGun(const void* bytes,std::size_t size) noexcept { return Owned(bytes,size,kGunMagic); }

bool Encode(const Grab& g,void* bytes,std::size_t size) noexcept {
    if(!bytes || size!=kGrabWire || !IdOk(g.grabber) || !IdOk(g.target) || !GrabFinite(g))return false;
    if(g.kind!=GrabKind::hold && g.kind!=GrabKind::thrown && g.kind!=GrabKind::letGo)return false;
    auto* p=static_cast<unsigned char*>(bytes);
    std::memset(p,0,size);
    Put32(p,kGrabMagic);Put32(p+4,kVersion);p[8]=static_cast<unsigned char>(g.kind);
    Put32(p+kGOrigin,g.origin);Put32(p+kGGrab,g.grab);
    std::memcpy(p+kGGrabber,g.grabber,32);std::memcpy(p+kGTarget,g.target,32);
    PutV(p+kGAt,g.at);PutV(p+kGRayA,g.rayA);PutV(p+kGRayB,g.rayB);PutV(p+kGGoal,g.goal);PutV(p+kGVel,g.vel);
    return true;
}

bool Decode(const void* bytes,std::size_t size,Grab& out) noexcept {
    if(!OwnedGrab(bytes,size) || size!=kGrabWire)return false;
    const auto* p=static_cast<const unsigned char*>(bytes);
    if(Get32(p+4)!=kVersion)return false;
    Grab g{};
    g.kind=static_cast<GrabKind>(p[8]);
    if(g.kind!=GrabKind::hold && g.kind!=GrabKind::thrown && g.kind!=GrabKind::letGo)return false;
    g.origin=Get32(p+kGOrigin);g.grab=Get32(p+kGGrab);
    std::memcpy(g.grabber,p+kGGrabber,32);std::memcpy(g.target,p+kGTarget,32);
    GetV(p+kGAt,g.at);GetV(p+kGRayA,g.rayA);GetV(p+kGRayB,g.rayB);GetV(p+kGGoal,g.goal);GetV(p+kGVel,g.vel);
    if(!IdOk(g.grabber) || !IdOk(g.target) || !GrabFinite(g))return false;
    out=g;
    return true;
}

bool Encode(const Gun& g,void* bytes,std::size_t size) noexcept {
    if(!bytes || size!=kGunWire || !IdOk(g.who) || !Finite(g.at) || g.leftMs>kMostLeftMs)return false;
    if(g.kind!=GunKind::drop && g.kind!=GunKind::pick && g.kind!=GunKind::owner && g.kind!=GunKind::gone)return false;
    auto* p=static_cast<unsigned char*>(bytes);
    std::memset(p,0,size);
    Put32(p,kGunMagic);Put32(p+4,kVersion);p[8]=static_cast<unsigned char>(g.kind);
    p[kUSerial]=static_cast<unsigned char>(g.serial);p[kUSerial+1]=static_cast<unsigned char>(g.serial>>8);
    p[kUIndex]=g.index;
    Put32(p+kUOrigin,g.origin);
    std::memcpy(p+kUWho,g.who,32);
    PutV(p+kUAt,g.at);
    Put32(p+kULeft,g.leftMs);
    return true;
}

bool Decode(const void* bytes,std::size_t size,Gun& out) noexcept {
    if(!OwnedGun(bytes,size) || size!=kGunWire)return false;
    const auto* p=static_cast<const unsigned char*>(bytes);
    if(Get32(p+4)!=kVersion)return false;
    Gun g{};
    g.kind=static_cast<GunKind>(p[8]);
    if(g.kind!=GunKind::drop && g.kind!=GunKind::pick && g.kind!=GunKind::owner && g.kind!=GunKind::gone)return false;
    g.serial=static_cast<std::uint16_t>(p[kUSerial]|(p[kUSerial+1]<<8));
    g.index=p[kUIndex];
    g.origin=Get32(p+kUOrigin);
    std::memcpy(g.who,p+kUWho,32);
    GetV(p+kUAt,g.at);
    g.leftMs=Get32(p+kULeft);
    if(!IdOk(g.who) || !Finite(g.at) || g.leftMs>kMostLeftMs)return false;
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
