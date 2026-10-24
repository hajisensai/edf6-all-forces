// The loadouts of the jets a player summons with a vehicle request (2026-10-25, the user: "换载具要能换自己召唤的啊，
// 不只是npc的载具"; docs/feature-2026-10-25-room-menu-review.md §4): pure, no game, no Windows, no transport
// (tests/player_vehicle_test.cpp runs several machines of it against each other).
//
// The game makes such a vehicle on every machine of the room by itself: the request's transporter (0x5E5070) configures
// its container (0x5E8B40) with the vehicle's SGO path, and the container, once landed, makes it from that path
// (CreateObject at 0x5E8FDC) and registers it under the network ID it derives (docs/airdrop-vehicle-re.md §1, H). A
// loaded jet is another SGO (EDF6VC_LO_<BODY>_<variant>.SGO, support_loadout.h VehicleVariantFile, built on the request's
// twin), so the path is swapped where the container makes it: and every machine must swap it the same way, or the one
// network vehicle has other weapons on each machine. Hence:
//   the room       every member publishes its own player loadouts (Coop member attribute kKey, OwnValue): every machine
//                  generates and, at the mission's start, preloads every member's files it can (room_sync.h);
//   the mission    every machine tells the room which of those files it preloaded (Msg::have, exact names, no filter);
//                  the requester's machine alone decides at its own request: the loaded jet only when its own file is
//                  preloaded and every peer of the room announced the channel and said it has that file too, else the
//                  stock twin (Decide); it tells everyone (Msg::decide) for that request, keyed by the requester's
//                  native ID, the body and the request's number for that body (seq: each machine counts the configures
//                  of that player's requests of that body in order, the game replicates them in order); a machine whose
//                  container lands without the decision makes the stock twin, as the requester does when in doubt.
//   a newly joined machine is sent the haves and each local player's next seq (Msg::sync), so its count lines up.
// What can still make two machines differ: a decide that arrives after that machine's container has landed (a request
// lands well over ten seconds after it is made; the channel is reliable and ordered), a machine that joins while a
// container is in the air. Both are logged where they happen (Engine::Land: "late" / "unknown").
#pragma once
#include "support_loadout.h"
#include "wire.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace crew::playerveh {

inline constexpr char kKey[]="EDF6X_VCPVEH";           // the room's member attribute: "1;<tokens>"
inline constexpr std::size_t kValueMost=128;           // 5 tokens of 17 characters, their commas and the header
inline constexpr int kPeersMost=64;
inline constexpr int kHaveMost=100;                    // the variants a have names (kMaxPayload 1024: 16 + 9 x 100)
inline constexpr int kDecisionsMost=128;
inline constexpr int kRequestersMost=64;

// One loaded jet: its body (a player's: support_loadout.h IsPlayerBody) and LoadoutVariant (applied bit clear).
struct Variant { VehicleBody body=VehicleBody::none; std::uint64_t bits=0; };
constexpr bool Same(const Variant& a,const Variant& b) noexcept { return a.body==b.body && a.bits==b.bits; }
// A valid one: a player's body and bits that decode for it (the parser's rules, the weight included).
inline bool Valid(const Variant& v) noexcept {
    VehicleLoadout l;
    return IsPlayerBody(v.body) && v.bits && !(v.bits&kVariantApplied) && LoadoutOfVariant(v.bits,v.body,&l) && LoadoutVariant(l)==v.bits;
}

// --- The room: "1;<token>,<token>..." (room_sync_logic.h's vehicle token: '0'+body, then 16 upper-case hex digits) ---
inline std::size_t TokenOf(const Variant& v,char* out,std::size_t capacity) noexcept {
    if(!Valid(v) || !out || capacity<18)return 0;
    const char* hex="0123456789ABCDEF";
    out[0]=static_cast<char>('0'+static_cast<int>(v.body));
    for(int i=0;i<16;++i)out[1+i]=hex[(v.bits>>(60-4*i))&0xF];
    out[17]=0;
    return 17;
}
// This machine's own loadouts (one per kPlayerVehicles entry; body none: stock) as its value. False: nothing to say.
inline bool OwnValue(const VehicleLoadout* own,int count,char* out,std::size_t capacity) noexcept {
    if(!out || capacity<3)return false;
    std::snprintf(out,capacity,"1;");
    std::size_t n=2;int listed=0;
    for(int i=0;own && i<count;++i) {
        const Variant v{own[i].body,LoadoutVariant(own[i])};
        char t[20];
        if(!own[i].On() || !own[i].count || !TokenOf(v,t,sizeof(t)))continue;   // guns alone is the stock twin's guns: nothing
        if(n+(listed ? 1 : 0)+17+1>capacity)return false;
        if(listed)out[n++]=',';
        std::memcpy(out+n,t,17);n+=17;out[n]=0;
        ++listed;
    }
    return listed>0;
}
// A member's value (untrusted) into `out` (at most kPlayerVehicleCount, one per body). False: not one (nothing kept).
inline bool ParseValue(const char* value,Variant* out,int* count) noexcept {
    if(count)*count=0;
    if(!value || std::strncmp(value,"1;",2))return false;
    const char* s=value+2;
    Variant got[kPlayerVehicleCount];int n=0;
    while(*s) {
        const char* end=s;
        while(*end && *end!=',')++end;
        if(end-s!=17 || n>=kPlayerVehicleCount)return false;
        Variant v;
        const int body=s[0]-'0';
        if(body<1 || body>kVehicleBodyLast)return false;
        v.body=static_cast<VehicleBody>(body);
        for(int i=1;i<17;++i) {
            const char c=s[i];
            const int d=c>='0' && c<='9' ? c-'0' : c>='A' && c<='F' ? c-'A'+10 : -1;
            if(d<0)return false;
            v.bits=(v.bits<<4)|static_cast<unsigned>(d);
        }
        char again[20];
        if(!Valid(v) || !TokenOf(v,again,sizeof(again)) || std::memcmp(again,s,17))return false;
        for(int k=0;k<n;++k)if(got[k].body==v.body)return false;   // one loadout a body
        got[n++]=v;
        s=*end ? end+1 : end;
        if(*end && !*s)return false;   // a trailing comma
    }
    if(!n)return false;
    for(int i=0;i<n && out;++i)out[i]=got[i];
    if(count)*count=n;
    return true;
}

// --- The mission's messages (plugin_net.h channel PVEH, support_protocol.h kExtPlayerVehicle) ---
inline constexpr std::uint32_t kMagic=0x48455650u;   // "PVEH"
inline constexpr std::uint32_t kVersion=1;
inline constexpr std::size_t kHead=16;                // magic, version, kind @8, origin @12
inline constexpr std::size_t kDecideSize=kHead+8+4+4+wire::kIdBytes;   // body @16, seq @20, bits @24, requester @32
inline constexpr std::size_t kSyncSize=kHead+8+wire::kIdBytes;          // body @16, next seq @20, requester @24
inline constexpr std::size_t kMostSize=kHead+4+9*kHaveMost;              // a have: count @16, then 9 bytes a variant
enum class Kind : std::uint8_t { have=1, decide=2, sync=3 };
struct Msg {
    Kind kind=Kind::have;
    std::uint32_t origin=0;                // the sender's run (plugin_net::NewOrigin)
    int count=0;Variant have[kHaveMost]{};   // have
    Variant variant{};                     // decide: bits 0 = the stock twin; sync: body alone
    std::uint32_t seq=0;                   // decide: the request's number; sync: the next one
    unsigned char requester[wire::kIdBytes]{};
};
inline bool Owned(const void* bytes,std::size_t size) noexcept { return wire::HeadWord(bytes,size,0)==kMagic; }
// The message's size (0: it would not decode).
inline std::size_t Encode(const Msg& m,void* bytes,std::size_t capacity) noexcept {
    std::size_t size=0;
    switch(m.kind) {
    case Kind::have: if(m.count<0 || m.count>kHaveMost)return 0; size=kHead+4+9*static_cast<std::size_t>(m.count); break;
    case Kind::decide: size=kDecideSize; break;
    case Kind::sync: size=kSyncSize; break;
    default: return 0;
    }
    if(!bytes || capacity<size || !m.origin)return 0;
    wire::Writer w(bytes,size);
    w.U32(kMagic).U32(kVersion).U8(static_cast<std::uint8_t>(m.kind)).Seek(12).U32(m.origin);
    if(m.kind==Kind::have) {
        w.U32(static_cast<std::uint32_t>(m.count));
        for(int i=0;i<m.count;++i) {
            if(!Valid(m.have[i]))return 0;
            w.U8(static_cast<std::uint8_t>(m.have[i].body)).U32(static_cast<std::uint32_t>(m.have[i].bits)).U32(static_cast<std::uint32_t>(m.have[i].bits>>32));
        }
    } else if(m.kind==Kind::decide) {
        if(!IsPlayerBody(m.variant.body) || (m.variant.bits && !Valid(m.variant)) || wire::ZeroId(m.requester))return 0;
        w.U8(static_cast<std::uint8_t>(m.variant.body)).Seek(20).U32(m.seq)
         .U32(static_cast<std::uint32_t>(m.variant.bits)).U32(static_cast<std::uint32_t>(m.variant.bits>>32)).Id(m.requester);
    } else {
        if(!IsPlayerBody(m.variant.body) || wire::ZeroId(m.requester))return 0;
        w.U8(static_cast<std::uint8_t>(m.variant.body)).Seek(20).U32(m.seq).Id(m.requester);
    }
    return w.Ok() && w.Used()==size ? size : 0;
}
// False: not PVEH, another version, a size its kind does not have, a variant that is no player's loaded jet, no origin,
// no requester or one not canonical.
inline bool Decode(const void* bytes,std::size_t size,Msg& out) noexcept {
    if(!Owned(bytes,size) || size<kHead)return false;
    wire::Reader r(bytes,size);
    Msg m{};
    r.Skip(4);
    if(r.U32()!=kVersion)return false;
    m.kind=static_cast<Kind>(r.U8());
    r.Seek(12);m.origin=r.U32();
    if(!m.origin)return false;
    const auto bits=[&]() noexcept { const std::uint64_t lo=r.U32();return lo|(static_cast<std::uint64_t>(r.U32())<<32); };
    switch(m.kind) {
    case Kind::have: {
        const std::uint32_t n=r.U32();
        if(n>static_cast<std::uint32_t>(kHaveMost) || size!=kHead+4+9*static_cast<std::size_t>(n))return false;
        m.count=static_cast<int>(n);
        for(int i=0;i<m.count;++i) {
            m.have[i].body=static_cast<VehicleBody>(r.U8());m.have[i].bits=bits();
            if(!Valid(m.have[i]))return false;
        }
        break;
    }
    case Kind::decide:
        if(size!=kDecideSize)return false;
        m.variant.body=static_cast<VehicleBody>(r.U8());r.Seek(20);m.seq=r.U32();m.variant.bits=bits();r.Id(m.requester);
        if(!IsPlayerBody(m.variant.body) || (m.variant.bits && !Valid(m.variant)) || wire::ZeroId(m.requester))return false;
        break;
    case Kind::sync:
        if(size!=kSyncSize)return false;
        m.variant.body=static_cast<VehicleBody>(r.U8());r.Seek(20);m.seq=r.U32();r.Id(m.requester);
        if(!IsPlayerBody(m.variant.body) || wire::ZeroId(m.requester))return false;
        break;
    default: return false;
    }
    if(!r.Ok())return false;
    out=m;
    return true;
}

// --- One machine's state for a mission (player_vehicle.cpp holds one; the test, one per fake machine) ---
struct Requester { unsigned char id[wire::kIdBytes]{}; };
inline bool SameId(const unsigned char* a,const unsigned char* b) noexcept { return !std::memcmp(a,b,wire::kIdBytes); }

class Engine {
public:
    // A mission: this machine's own loadouts (one per kPlayerVehicles entry) and the loaded jets it preloaded (every
    // member's it had files of). Everything of the last mission forgotten.
    void Begin(const VehicleLoadout* own,int ownCount,const Variant* preloaded,int preloadedCount) noexcept {
        Clear();
        for(int i=0;i<kPlayerVehicleCount && own && i<ownCount;++i)
            if(own[i].On() && own[i].count && IsPlayerBody(own[i].body))own_[i]=Variant{own[i].body,LoadoutVariant(own[i])};
        for(int i=0;preloaded && i<preloadedCount && preloadedCount_<kHaveMost;++i)
            if(Valid(preloaded[i]) && !Preloaded(preloaded[i]))preloaded_[preloadedCount_++]=preloaded[i];
    }
    // This machine's have message (its preloaded loaded jets).
    Msg Have(std::uint32_t origin) const noexcept {
        Msg m;m.kind=Kind::have;m.origin=origin;m.count=preloadedCount_;
        for(int i=0;i<preloadedCount_;++i)m.have[i]=preloaded_[i];
        return m;
    }
    bool Preloaded(const Variant& v) const noexcept {
        for(int i=0;i<preloadedCount_;++i)if(Same(preloaded_[i],v))return true;
        return false;
    }
    // The room: its peers (1..count) and which of them announced the channel. A change of room forgets the haves heard
    // (the indexes mean other machines now): they come again (every machine re-sends its own when the room changes).
    void Room(std::uint32_t generation,int count,const bool* hasFeature) noexcept {
        if(generation!=generation_){generation_=generation;for(auto& p:peer_)p=Peer{};}
        peers_=count<0 ? 0 : count>kPeersMost ? kPeersMost : count;
        for(int i=0;i<peers_;++i)peer_[i].feature=hasFeature && hasFeature[i];
    }
    void Heard(int peer,const Msg& have) noexcept {
        if(peer<1 || peer>kPeersMost || have.kind!=Kind::have)return;
        Peer& p=peer_[peer-1];
        p.heard=true;p.count=have.count;
        for(int i=0;i<have.count;++i)p.have[i]=have.have[i];
    }
    // This machine's player requested `body`: the decision (bits 0 = the stock twin) and the message telling the room.
    // `id`: the requester's native ID. Online: the loaded jet only when every peer announced the channel and has it.
    Msg Decide(const unsigned char* id,VehicleBody body,std::uint32_t origin,bool online) noexcept {
        Msg m;m.kind=Kind::decide;m.origin=origin;m.variant.body=body;
        std::memcpy(m.requester,id,wire::kIdBytes);
        Counter& c=CounterOf(id,body);
        m.seq=c.next++;
        const int at=PlayerVehicleOfBody(body);
        Variant want=at>=0 ? own_[at] : Variant{};
        if(want.bits && !Preloaded(want))want.bits=0;
        for(int i=0;online && want.bits && i<peers_;++i)
            if(!peer_[i].feature || !peer_[i].heard || !PeerHas(peer_[i],want))want.bits=0;
        m.variant.bits=want.bits;
        Remember(id,body,m.seq,want.bits);
        return m;
    }
    // The room's number of another machine's player's next request of `body` (each configure here counts one).
    std::uint32_t Count(const unsigned char* id,VehicleBody body) noexcept { return CounterOf(id,body).next++; }
    // A newly joined machine: each local player's next number per body, so its counts line up.
    int Syncs(const unsigned char* id,std::uint32_t origin,Msg* out,int capacity) noexcept {
        int n=0;
        for(int i=0;i<kPlayerVehicleCount && n<capacity;++i) {
            Counter& c=CounterOf(id,kPlayerVehicles[i].body);
            Msg m;m.kind=Kind::sync;m.origin=origin;m.variant.body=kPlayerVehicles[i].body;m.seq=c.next;
            std::memcpy(m.requester,id,wire::kIdBytes);
            out[n++]=m;
        }
        return n;
    }
    // A decide / sync another machine sent for its own player (the sender check is the caller's: SenderIsPlayer).
    void Received(const Msg& m) noexcept {
        if(m.kind==Kind::decide)Remember(m.requester,m.variant.body,m.seq,m.variant.bits);
        else if(m.kind==Kind::sync) {
            Counter& c=CounterOf(m.requester,m.variant.body);
            if(c.next<m.seq)c.next=m.seq;
        }
    }
    // A container landed: what it makes. `found`: false when no decision is known (the stock twin; logged by the
    // caller). A decided loaded jet this machine did not preload: the stock twin too (`missing` true; never happens
    // when the have it sent was true).
    struct Landing { std::uint64_t bits=0; bool found=false,missing=false; };
    Landing Land(const unsigned char* id,VehicleBody body,std::uint32_t seq) const noexcept {
        Landing l;
        for(int i=0;i<decisions_;++i) {
            const Decision& d=decision_[i];
            if(d.body!=body || d.seq!=seq || !SameId(d.id,id))continue;
            l.found=true;l.bits=d.bits;
            if(d.bits && !Preloaded(Variant{body,d.bits})){l.missing=true;l.bits=0;}
            return l;
        }
        return l;
    }
    // Everything forgotten (no temporary: the state is large).
    void Clear() noexcept {
        for(auto& v:own_)v=Variant{};
        preloadedCount_=0;generation_=0;peers_=0;
        for(auto& p:peer_){p.feature=false;p.heard=false;p.count=0;}
        for(auto& c:counter_){c.used=false;c.next=0;}
        decisions_=0;next_=0;
    }
private:
    struct Peer { bool feature=false,heard=false; int count=0; Variant have[kHaveMost]{}; };
    struct Counter { unsigned char id[wire::kIdBytes]{}; VehicleBody body=VehicleBody::none; std::uint32_t next=0; bool used=false; };
    struct Decision { unsigned char id[wire::kIdBytes]{}; VehicleBody body=VehicleBody::none; std::uint32_t seq=0; std::uint64_t bits=0; };
    static bool PeerHas(const Peer& p,const Variant& v) noexcept {
        for(int i=0;i<p.count;++i)if(Same(p.have[i],v))return true;
        return false;
    }
    Counter& CounterOf(const unsigned char* id,VehicleBody body) noexcept {
        for(auto& c:counter_)if(c.used && c.body==body && SameId(c.id,id))return c;
        for(auto& c:counter_)if(!c.used){c.used=true;c.body=body;std::memcpy(c.id,id,wire::kIdBytes);return c;}
        return counter_[0];   // more requesters than kRequestersMost x bodies: the first counter shared (never in a room)
    }
    void Remember(const unsigned char* id,VehicleBody body,std::uint32_t seq,std::uint64_t bits) noexcept {
        for(int i=0;i<decisions_;++i) {
            Decision& d=decision_[i];
            if(d.body==body && d.seq==seq && SameId(d.id,id)){d.bits=bits;return;}
        }
        Decision& d=decision_[decisions_<kDecisionsMost ? decisions_++ : (next_++%kDecisionsMost)];
        std::memcpy(d.id,id,wire::kIdBytes);d.body=body;d.seq=seq;d.bits=bits;
    }
    Variant own_[kPlayerVehicleCount]{};
    Variant preloaded_[kHaveMost]{};int preloadedCount_=0;
    std::uint32_t generation_=0;int peers_=0;
    Peer peer_[kPeersMost]{};
    Counter counter_[kRequestersMost*kPlayerVehicleCount]{};
    Decision decision_[kDecisionsMost]{};int decisions_=0;unsigned next_=0;
};

// The CreateObject path of a loaded jet (app:/object/edf6vc_lo_<body>_<variant>.sgo, the lower case the game's paths
// use) into `out`; false when it is none.
inline bool LoadedPath(const Variant& v,wchar_t* out,std::size_t capacity) noexcept {
    VehicleLoadout l;
    if(!Valid(v) || !LoadoutOfVariant(v.bits,v.body,&l))return false;
    wchar_t file[96];
    if(!VehicleVariantFile(l,file,96))return false;
    const int n=std::swprintf(out,capacity,L"app:/object/%ls",file);
    if(n<=0)return false;
    for(wchar_t* c=out;*c;++c)if(*c>=L'A' && *c<=L'Z')*c=static_cast<wchar_t>(*c-L'A'+L'a');
    return true;
}
// Which player vehicle a container's configured path makes (its file name, any case, against kPlayerVehicles' twins);
// -1 none.
inline int TwinOfPath(const wchar_t* path) noexcept {
    if(!path)return -1;
    const wchar_t* name=path;
    for(const wchar_t* c=path;*c;++c)if(*c==L'/' || *c==L'\\')name=c+1;
    for(int i=0;i<kPlayerVehicleCount;++i)if(!_wcsicmp(name,kPlayerVehicles[i].twin))return i;
    return -1;
}

}  // namespace crew::playerveh
