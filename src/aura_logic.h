// The aura vehicles (aura.cpp, docs/feature-aura-vehicles.md): the command, repair and ammo trucks. Pure: no game, no
// transport (tests/aura_logic_test.cpp runs it).
//  - An aura stands while its truck is whole and either someone (a player or an NPC) is in its driver's seat, or it is
//    parked (slower than the park speed).
//  - A friend within an aura's radius (the same team) takes its effect: the command truck's attack bonus (the native
//    per-frame dealt multiplier obj+0x390, a max: docs/feature-aura-vehicles.md), the repair truck's share of max HP a
//    second, the ammo truck's share of each magazine a second.
//  - One kind never stacks: of several auras of a kind over one friend, the strongest stands (the native support
//    devices' rule, SupportUnitBullet01 0x2AC7E0's max). The three kinds together all apply.
// Online (aura_net.cpp): the host decides which auras stand (Sources) and sends them, AFAU (a state message, every
// kHeartbeatMs and at once when they change); every machine applies them to the friends it owns (aura.cpp).
#pragma once
#include "wire.h"
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace crew::aura {
enum class Kind : std::uint8_t { command=0, repair=1, ammo=2 };
constexpr int kKinds=3;
constexpr int kMaxSources=16;            // auras of a mission at once (more: the first kMaxSources found)

// An aura's marker bone in its truck's model (tools/make_aura_vehicles.py MARKERS): the game's bone lookup (body506.cpp
// BoneRecord506) compares 16 characters, and each is told apart within them.
constexpr const wchar_t* kMarkers[kKinds]={L"edf6vc_aura_cmd",L"edf6vc_aura_rep",L"edf6vc_aura_amo"};

// One aura standing: its kind, where, how far, how strong (attack: the bonus, 0.3 = +30%; repair / ammo: the share of
// max a second), whose side.
struct Source {
    Kind kind=Kind::command;
    float pos[3]{};
    float radius=0.0f;
    float strength=0.0f;
    std::int32_t team=0;
};

// What reaches one friend: each kind's strongest (0: none), and which kinds (bit 1<<kind).
struct Effect {
    float attack=0.0f,repair=0.0f,ammo=0.0f;
    std::uint8_t kinds=0;
};

constexpr bool ValidKind(std::uint8_t k) noexcept { return k<kKinds; }

// The aura stands: whole, and driven (someone in seat 0) or parked (its speed, m/s, at most `parkSpeed`).
constexpr bool Active(bool dead,bool driven,float speed,float parkSpeed) noexcept {
    return !dead && (driven || (speed>=0.0f && speed<=parkSpeed));
}

inline float Dist2(const float* a,const float* b) noexcept {
    const float d[3]={a[0]-b[0],a[1]-b[1],a[2]-b[2]};
    return d[0]*d[0]+d[1]*d[1]+d[2]*d[2];
}

// The effect over a friend at `pos` of team `team`: per kind the strongest source of that team within its radius.
inline Effect Select(const Source* s,int n,const float* pos,std::int32_t team) noexcept {
    Effect e{};
    if(!s || !pos)return e;
    for(int i=0;i<n && i<kMaxSources;++i) {
        const Source& x=s[i];
        if(x.team!=team || !(x.radius>0.0f) || !(x.strength>0.0f) || !ValidKind(static_cast<std::uint8_t>(x.kind)))continue;
        if(Dist2(x.pos,pos)>x.radius*x.radius)continue;
        float& slot=x.kind==Kind::command ? e.attack : x.kind==Kind::repair ? e.repair : e.ammo;
        if(x.strength>slot)slot=x.strength;
        e.kinds=static_cast<std::uint8_t>(e.kinds|(1u<<static_cast<unsigned>(x.kind)));
    }
    return e;
}

// The native dealt multiplier with the attack bonus in (the game resets it to 1 each frame and every support device
// takes the max of its own and what is there, 0x2AC8CA): never lowered.
inline float AttackMultiplier(float current,float bonus) noexcept {
    const float want=1.0f+(bonus>0.0f ? bonus : 0.0f);
    return std::isfinite(current) && current>want ? current : want;
}

// HP after `dt` s of a repair share (of `most` a second), within [least, most]; what was given in `*given`.
inline float Heal(float hp,float least,float most,float share,float dt,float* given) noexcept {
    if(given)*given=0.0f;
    if(!std::isfinite(hp) || !std::isfinite(most) || !(most>0.0f) || !(share>0.0f) || !(dt>0.0f) || hp>=most)return hp;
    float next=hp+most*share*dt;
    if(next>most)next=most;
    if(std::isfinite(least) && next<least)next=least;
    if(given)*given=next-hp;
    return next;
}

// A magazine of `capacity` rounds with `left` in it after `dt` s of an ammo share (of the capacity a second); `carry`
// keeps the fraction of a round owed between frames (a 1-round rocket fills after 1/share s, not never).
inline std::int32_t Refill(std::int32_t left,std::int32_t capacity,float share,float dt,float& carry) noexcept {
    if(capacity<=0 || left>=capacity || !(share>0.0f) || !(dt>0.0f)){carry=0.0f;return left<0 ? 0 : left;}
    if(left<0)left=0;
    carry+=static_cast<float>(capacity)*share*dt;
    if(!std::isfinite(carry) || carry<0.0f)carry=0.0f;
    const float whole=std::floor(carry);
    if(whole<1.0f)return left;
    carry-=whole;
    const std::int64_t next=static_cast<std::int64_t>(left)+static_cast<std::int64_t>(whole);
    if(next>=capacity){carry=0.0f;return capacity;}
    return static_cast<std::int32_t>(next);
}

// A weapon the ammo truck fills: not reloaded by points (ReloadType 2, the Air Raider's calls: free airstrikes).
constexpr bool Refillable(std::int32_t reloadType) noexcept { return reloadType!=2; }

// --- the wire (plugin_net.h channel AFAU, version 1): a state message from the host, fixed size ---
// magic, version, count, then kMaxSources of {kind u8, team i32, radius f32, strength f32, pos 3 x f32} (unused zero).
constexpr std::uint32_t kMagic=0x55414641u;   // "AFAU"
constexpr std::uint32_t kVersion=1;
constexpr std::size_t kSourceWire=1+4+4+4+12;
constexpr std::size_t kWire=4+4+1+kSourceWire*kMaxSources;
constexpr std::uint64_t kGapMs=100;           // a change goes out at most 10 a second
constexpr std::uint64_t kHeartbeatMs=1000;    // the same list once a second (a lost state, a member who joined)
constexpr std::uint64_t kSilentMs=3500;       // three heartbeats lost: no aura stands here
static_assert(kHeartbeatMs*3<kSilentMs,"a state lost twice is not silence");
constexpr float kMostRadius=1000.0f,kMostStrength=10.0f;

struct Msg {
    int count=0;
    Source sources[kMaxSources]{};
};

inline bool Owned(const void* bytes,std::size_t size) noexcept { return wire::HeadWord(bytes,size,0)==kMagic; }
inline bool TakesState(const void* bytes,std::size_t size) noexcept { return Owned(bytes,size); }

inline bool SourceOk(const Source& s) noexcept {
    return ValidKind(static_cast<std::uint8_t>(s.kind)) && std::isfinite(s.radius) && s.radius>=0.0f && s.radius<=kMostRadius &&
           std::isfinite(s.strength) && s.strength>=0.0f && s.strength<=kMostStrength &&
           std::isfinite(s.pos[0]) && std::isfinite(s.pos[1]) && std::isfinite(s.pos[2]);
}

inline bool Encode(const Msg& m,void* bytes,std::size_t size) noexcept {
    if(!bytes || size!=kWire || m.count<0 || m.count>kMaxSources)return false;
    for(int i=0;i<m.count;++i)if(!SourceOk(m.sources[i]))return false;
    wire::Writer w(bytes,size);
    w.U32(kMagic).U32(kVersion).U8(static_cast<std::uint8_t>(m.count));
    for(int i=0;i<kMaxSources;++i) {
        const Source z{};
        const Source& s=i<m.count ? m.sources[i] : z;
        w.U8(static_cast<std::uint8_t>(s.kind)).U32(static_cast<std::uint32_t>(s.team)).F(s.radius).F(s.strength).V3(s.pos);
    }
    return w.Ok() && w.Used()==kWire;
}

inline bool Decode(const void* bytes,std::size_t size,Msg& out) noexcept {
    if(!Owned(bytes,size) || size!=kWire)return false;
    wire::Reader r(bytes,size);
    r.Skip(4);
    if(r.U32()!=kVersion)return false;
    Msg m{};
    const std::uint8_t count=r.U8();
    if(count>kMaxSources)return false;
    m.count=count;
    for(int i=0;i<kMaxSources;++i) {
        Source s{};
        const std::uint8_t kind=r.U8();
        s.team=static_cast<std::int32_t>(r.U32());
        s.radius=r.F(kMostRadius);s.strength=r.F(kMostStrength);r.V3(s.pos);
        if(i>=m.count)continue;
        if(!ValidKind(kind))return false;
        s.kind=static_cast<Kind>(kind);
        if(!SourceOk(s))return false;
        m.sources[i]=s;
    }
    if(!r.Ok())return false;
    out=m;
    return true;
}

// Two lists the same to the receiver (positions within 0.5 m, values within 1%): no new state needed before the heartbeat.
inline bool Same(const Msg& a,const Msg& b) noexcept {
    if(a.count!=b.count)return false;
    for(int i=0;i<a.count;++i) {
        const Source& x=a.sources[i];const Source& y=b.sources[i];
        if(x.kind!=y.kind || x.team!=y.team || Dist2(x.pos,y.pos)>0.25f ||
           std::fabs(x.radius-y.radius)>0.01f*(x.radius>1.0f ? x.radius : 1.0f) ||
           std::fabs(x.strength-y.strength)>0.01f*(x.strength>0.01f ? x.strength : 0.01f))return false;
    }
    return true;
}
// The next state goes out at `now`: never sent, the list changed and kGapMs passed, or the heartbeat is due.
inline bool Due(bool sent,const Msg& last,std::uint64_t lastAt,const Msg& now,std::uint64_t at) noexcept {
    if(!sent)return true;
    if(at-lastAt>=kHeartbeatMs)return true;
    return !Same(last,now) && at-lastAt>=kGapMs;
}
constexpr bool Fresh(std::uint64_t heard,std::uint64_t now) noexcept { return heard && now>=heard && now-heard<kSilentMs; }
}  // namespace crew::aura
