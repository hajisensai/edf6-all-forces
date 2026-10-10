#include "qmark_protocol.h"
#include <cmath>
#include <cstring>

namespace crew::qmark_net {
namespace {
// Layout: magic, version, caps, origin, sequence, flags (bit 0 enemy, bit 1 point); marker[32]; target[32]; enemyAt[3];
// pointAt[3]; pointLeftMs; letGo (kCapLetGo); reserved to kWireSize (zero sent, ignored read: room for a compatible
// addition).
constexpr std::size_t kMarker=24,kTarget=56,kEnemyAt=88,kPointAt=100,kPointLeft=112,kLetGo=116,kUsed=120;
static_assert(kUsed<=kWireSize,"the state fits its wire size");
constexpr std::uint32_t kFlagEnemy=1u,kFlagPoint=2u;

void Put32(unsigned char* p,std::uint32_t v) noexcept { for(int i=0;i<4;++i)p[i]=static_cast<unsigned char>(v>>(8*i)); }
std::uint32_t Get32(const unsigned char* p) noexcept {
    std::uint32_t v=0;
    for(int i=0;i<4;++i)v|=static_cast<std::uint32_t>(p[i])<<(8*i);
    return v;
}
void PutF(unsigned char* p,float f) noexcept { std::uint32_t v;std::memcpy(&v,&f,4);Put32(p,v); }
float GetF(const unsigned char* p) noexcept { const std::uint32_t v=Get32(p);float f;std::memcpy(&f,&v,4);return f; }
bool Zero(const unsigned char* id) noexcept { for(int i=0;i<32;++i)if(id[i])return false;return true; }
// A native ID as command_identity.cpp takes it (bytes 20..23 zero), or all zero (none).
bool IdOk(const unsigned char* id) noexcept { for(int i=20;i<24;++i)if(id[i])return false;return true; }
bool Finite(const float* v) noexcept { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }
bool Near(const float* a,const float* b) noexcept {   // the same point: within a metre (a new press elsewhere is not)
    const float d[3]={a[0]-b[0],a[1]-b[1],a[2]-b[2]};
    return d[0]*d[0]+d[1]*d[1]+d[2]*d[2]<1.0f;
}
// Sequence `a` after `b` (round the 32-bit wrap).
bool After(std::uint32_t a,std::uint32_t b) noexcept { return static_cast<std::int32_t>(a-b)>0; }
}  // namespace

bool Owned(const void* bytes,std::size_t size) noexcept {
    return bytes && size>=4 && Get32(static_cast<const unsigned char*>(bytes))==kMagic;
}

bool Encode(const State& s,void* bytes,std::size_t size) noexcept {
    if(!bytes || size!=kWireSize || !IdOk(s.marker) || !IdOk(s.target) || s.pointLeftMs>kMaxPointMs ||
       !Finite(s.enemyAt) || !Finite(s.pointAt))return false;
    auto* p=static_cast<unsigned char*>(bytes);
    std::memset(p,0,size);
    Put32(p,kMagic);Put32(p+4,kVersion);Put32(p+8,s.caps&kCapabilities);Put32(p+12,s.origin);Put32(p+16,s.sequence);
    Put32(p+20,(s.enemy ? kFlagEnemy : 0u)|(s.point ? kFlagPoint : 0u));
    std::memcpy(p+kMarker,s.marker,32);
    if(s.enemy)std::memcpy(p+kTarget,s.target,32);
    for(int i=0;i<3;++i){PutF(p+kEnemyAt+4*i,s.enemy ? s.enemyAt[i] : 0.0f);PutF(p+kPointAt+4*i,s.point ? s.pointAt[i] : 0.0f);}
    Put32(p+kPointLeft,s.point ? s.pointLeftMs : 0u);
    Put32(p+kLetGo,s.letGo);
    return true;
}

bool Decode(const void* bytes,std::size_t size,State& out) noexcept {
    if(!Owned(bytes,size) || size!=kWireSize)return false;
    const auto* p=static_cast<const unsigned char*>(bytes);
    if(Get32(p+4)!=kVersion)return false;
    State s{};
    s.caps=Get32(p+8)&kCapabilities;   // bits this version does not know: ignored
    s.origin=Get32(p+12);s.sequence=Get32(p+16);
    const std::uint32_t flags=Get32(p+20);
    std::memcpy(s.marker,p+kMarker,32);std::memcpy(s.target,p+kTarget,32);
    for(int i=0;i<3;++i){s.enemyAt[i]=GetF(p+kEnemyAt+4*i);s.pointAt[i]=GetF(p+kPointAt+4*i);}
    s.pointLeftMs=Get32(p+kPointLeft);
    s.letGo=(s.caps&kCapLetGo) ? Get32(p+kLetGo) : 0u;
    s.enemy=(flags&kFlagEnemy)!=0 && (s.caps&kCapEnemy)!=0;
    s.point=(flags&kFlagPoint)!=0 && (s.caps&kCapPoint)!=0;
    if(!IdOk(s.marker) || !IdOk(s.target) || !Finite(s.enemyAt) || !Finite(s.pointAt) || s.pointLeftMs>kMaxPointMs)return false;
    if(!s.enemy)std::memset(s.target,0,32);
    if(!s.point)s.pointLeftMs=0;
    out=s;
    return true;
}

bool SameMarks(const State& a,const State& b) noexcept {
    if(a.enemy!=b.enemy || a.point!=b.point)return false;
    if(a.enemy && std::memcmp(a.target,b.target,32))return false;
    if(a.enemy && Zero(a.target) && !Near(a.enemyAt,b.enemyAt))return false;   // no ID: the position is its identity
    return !a.point || Near(a.pointAt,b.pointAt);
}

bool Outbox::Due(const State& now,std::uint64_t at) const noexcept {
    if(sent_ && at-sentAt_<kMinGapMs)return false;
    if(!sent_ || !SameMarks(now,last_) || now.letGo!=last_.letGo)return true;   // a let-go goes at once too (its cue)
    return at-sentAt_>=(now.enemy ? kEnemyKeepMs : kKeepMs);
}

State Outbox::Stamp(const State& now,std::uint64_t at) noexcept {
    State s=now;
    s.origin=origin_;s.sequence=++sequence_;
    last_=s;sent_=true;sentAt_=at;
    return s;
}

bool Inbox::Receive(std::uint32_t peer,const State& s,std::uint64_t now,Change* change) noexcept {
    if(change)*change=Change::none;
    if(!peer || peer>kMaxPeers)return false;
    Remote& r=peer_[peer];
    const bool live=r.heard && now-r.heardAt<kSilentMs;
    // The same run: only a later sequence. Another run (the sender restarted) or a silent peer: taken as it comes.
    if(live && s.origin==r.state.origin && !After(s.sequence,r.state.sequence))return false;
    const bool hadEnemy=live && r.state.enemy,hadPoint=live && r.pointUntil>now;
    const State prev=r.state;
    r.state=s;r.heard=true;r.heardAt=now;
    r.pointUntil=s.point ? now+s.pointLeftMs : 0;
    if(change) {
        // The same enemy: the same ID (or, with none, the same place); the same point: within a metre.
        const bool sameEnemy=hadEnemy && !std::memcmp(prev.target,s.target,32) && (!Zero(s.target) || Near(prev.enemyAt,s.enemyAt));
        const bool enemyNew=s.enemy && !sameEnemy;
        const bool pointNew=s.point && !(hadPoint && Near(prev.pointAt,s.pointAt));
        // Let go by its player: its count of let-goes moved on within the same run (a lost message's let-go still counts;
        // a mark gone with its enemy, dead or removed, moves nothing).
        const bool letGo=live && s.origin==prev.origin && (s.caps&kCapLetGo) && s.letGo!=prev.letGo;
        if(enemyNew || pointNew)*change=Change::marked;
        else if(letGo)*change=Change::letGo;
        else if((hadEnemy && !s.enemy) || (hadPoint && !s.point))*change=Change::cleared;
    }
    return true;
}

bool Inbox::Enemy(std::uint32_t peer,std::uint64_t now) const noexcept {
    const Remote& r=Peer(peer);
    return peer && r.heard && now-r.heardAt<kSilentMs && r.state.enemy;
}
bool Inbox::Point(std::uint32_t peer,std::uint64_t now) const noexcept {
    const Remote& r=Peer(peer);
    return peer && r.heard && now-r.heardAt<kSilentMs && r.state.point && now<r.pointUntil;
}
bool Inbox::Capable(std::uint32_t peer,std::uint64_t now,std::uint32_t caps) const noexcept {
    const Remote& r=Peer(peer);
    return peer && r.heard && now-r.heardAt<kSilentMs && (r.state.caps&caps)==caps;
}
}  // namespace crew::qmark_net
