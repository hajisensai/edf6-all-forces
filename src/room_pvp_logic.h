// The room's PvP setup (docs/plan-2026-10-25-pvp-factions.md §3), the pure side: tests/room_pvp_test.cpp runs it.
//
// In the room every member says what they want and the host fixes the match before it starts:
//   - each member publishes their pick (EDF6Coop member attribute EDF6X_PVPSEL: faction, role, unit);
//   - the host publishes the match's settings (EDF6X_PVPSET: on / off, the relation preset or its own table);
//   - when the host starts the mission the host first publishes the lock (EDF6X_PVPLOCK): a serial, the settings and
//     every member's pick as the host read them (an unset or unusable pick: that member's default), and holds the start
//     until every member published the lock's serial and hash back (EDF6X_PVPACK). A pick or a member changing while it
//     waits makes a new lock (the next serial), so what starts is what every member saw.
//   - in the mission the host applies the lock through the PvP protocol's one transaction (pvp.h QueuePvpMatch): every
//     machine gets the host's table, never its own reading of attributes that may arrive seconds apart.
// Values are printable ASCII (coop_member_attribute_api.h), versioned by pvp_protocol::kVersion: a member whose values
// are of another version is not on this build, and the lock never completes for it (the room refuses older builds
// before that: mod_room.h).
#pragma once
#include "pvp_match.h"
#include "pvp_protocol.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace crew::roompvp {
namespace pm=pvp_match;
constexpr const char* kSelKey="EDF6X_PVPSEL";
constexpr const char* kSetKey="EDF6X_PVPSET";
constexpr const char* kLockKey="EDF6X_PVPLOCK";
constexpr const char* kAckKey="EDF6X_PVPACK";
constexpr std::size_t kValueMost=1000;   // EDF6COOP_MEMBER_ATTRIBUTE_MAX_VALUE + 1
constexpr std::size_t kMaxMembers=pvp_protocol::kMaxMembers;

// --- the host's settings ---
struct Settings { bool enabled=false; pm::Relations relations=pvp_protocol::DefaultRelations(); };
inline bool SameSettings(const Settings& a,const Settings& b) noexcept { return a.enabled==b.enabled && a.relations.Same(b.relations); }

namespace detail {
inline char Hex(unsigned v) noexcept { return "0123456789ABCDEF"[v&15]; }
inline int Unhex(char c) noexcept { return c>='0' && c<='9' ? c-'0' : c>='A' && c<='F' ? c-'A'+10 : -1; }
// The relation table as 64 hex digits (4 relations a byte, pvp_protocol.h's packing), or "-" for a preset's own.
inline void PutRelations(const pm::Relations& r,char* out) noexcept {
    if(r.preset!=static_cast<std::uint8_t>(pm::Preset::custom)){out[0]='-';out[1]=0;return;}
    constexpr int kF=pm::kMaxFactions;
    for(int byte=0;byte<kF*kF/4;++byte) {
        unsigned v=0;
        for(int k=0;k<4;++k){const int i=byte*4+k;v|=static_cast<unsigned>(r.rel[i/kF][i%kF]&3)<<(k*2);}
        out[byte*2]=Hex(v>>4);out[byte*2+1]=Hex(v);
    }
    out[kF*kF/2]=0;
}
inline bool GetRelations(int preset,const char* text,std::size_t n,pm::Relations* r) noexcept {
    if(preset<0 || preset>=pm::kPresetCount)return false;
    if(preset!=static_cast<int>(pm::Preset::custom)) {
        if(n!=1 || text[0]!='-')return false;
        r->FromPreset(static_cast<pm::Preset>(preset));return true;
    }
    constexpr int kF=pm::kMaxFactions;
    if(n!=static_cast<std::size_t>(kF*kF/2))return false;
    r->preset=static_cast<std::uint8_t>(preset);
    for(int byte=0;byte<kF*kF/4;++byte) {
        const int hi=Unhex(text[byte*2]),lo=Unhex(text[byte*2+1]);
        if(hi<0 || lo<0)return false;
        const unsigned v=static_cast<unsigned>(hi*16+lo);
        for(int k=0;k<4;++k){const int i=byte*4+k;r->rel[i/kF][i%kF]=static_cast<std::uint8_t>((v>>(k*2))&3);}
    }
    return r->Valid();
}
// The ';'-separated fields of a value; false when there are more than `most`.
inline int Split(const char* text,const char** at,std::size_t* len,int most) noexcept {
    int n=0;const char* start=text;
    for(const char* p=text;;++p) {
        if(*p==';' || !*p) {
            if(n==most)return -1;
            at[n]=start;len[n]=static_cast<std::size_t>(p-start);++n;
            if(!*p)break;
            start=p+1;
        }
    }
    return n;
}
// A decimal field, all of it, within [lo, hi].
inline bool Number(const char* at,std::size_t n,long long lo,long long hi,long long* out) noexcept {
    if(!n || n>19)return false;
    long long v=0;
    for(std::size_t i=0;i<n;++i){if(at[i]<'0' || at[i]>'9')return false;v=v*10+(at[i]-'0');}
    if(v<lo || v>hi)return false;
    *out=v;return true;
}
inline bool Version(const char* at,std::size_t n) noexcept {
    long long v=0;return Number(at,n,0,255,&v) && v==pvp_protocol::kVersion;
}
inline bool PickFields(const char* const* at,const std::size_t* len,pm::Pick* p) noexcept {
    long long f=0,r=0,u=0;
    if(!Number(at[0],len[0],0,pm::kMaxFactions-1,&f) || !Number(at[1],len[1],0,pm::kRoleCount-1,&r) ||
       !Number(at[2],len[2],0,0xFFFF,&u))return false;
    const pm::Pick pick{static_cast<std::uint8_t>(f),static_cast<std::uint8_t>(r),static_cast<std::uint16_t>(u)};
    if(!pm::PickValid(pick))return false;
    *p=pick;   // written only when it is a pick
    return true;
}
inline std::uint32_t Fnv(const char* text) noexcept {
    std::uint32_t h=2166136261u;
    for(const char* p=text;*p;++p)h=(h^static_cast<unsigned char>(*p))*16777619u;
    return h;
}
}  // namespace detail

// EDF6X_PVPSEL: "<version>;<faction>;<role>;<unit>".
inline void EncodePick(const pm::Pick& p,char* out,std::size_t n) noexcept {
    std::snprintf(out,n,"%u;%u;%u;%u",static_cast<unsigned>(pvp_protocol::kVersion),static_cast<unsigned>(p.faction),
                  static_cast<unsigned>(p.role),static_cast<unsigned>(p.unit));
}
inline bool DecodePick(const char* text,pm::Pick* out) noexcept {
    const char* at[5];std::size_t len[5];
    if(!text || detail::Split(text,at,len,5)!=4 || !detail::Version(at[0],len[0]))return false;
    return detail::PickFields(at+1,len+1,out);
}
// EDF6X_PVPSET: "<version>;<on 0/1>;<preset>;<relations>".
inline void EncodeSettings(const Settings& s,char* out,std::size_t n) noexcept {
    char rel[pm::kMaxFactions*pm::kMaxFactions/2+1];
    detail::PutRelations(s.relations,rel);
    std::snprintf(out,n,"%u;%d;%u;%s",static_cast<unsigned>(pvp_protocol::kVersion),s.enabled ? 1 : 0,
                  static_cast<unsigned>(s.relations.preset),rel);
}
inline bool DecodeSettings(const char* text,Settings* out) noexcept {
    const char* at[5];std::size_t len[5];long long on=0,preset=0;
    if(!text || detail::Split(text,at,len,5)!=4 || !detail::Version(at[0],len[0]) ||
       !detail::Number(at[1],len[1],0,1,&on) || !detail::Number(at[2],len[2],0,pm::kPresetCount-1,&preset))return false;
    Settings s;s.enabled=on!=0;
    if(!detail::GetRelations(static_cast<int>(preset),at[3],len[3],&s.relations))return false;
    *out=s;return true;
}

// --- the lock ---
struct LockMember { char id[65]{}; pm::Pick pick{}; };
struct Lock {
    std::uint32_t serial=0;   // 0: none
    Settings settings{};
    int count=0;
    LockMember members[kMaxMembers]{};
};
// EDF6X_PVPLOCK: "<version>;<serial>;<on>;<preset>;<relations>;<count>;<id>,<f>,<r>,<u>;..." (members in order).
inline bool EncodeLock(const Lock& l,char* out,std::size_t n) noexcept {
    if(!out || n<2 || l.count<0 || l.count>static_cast<int>(kMaxMembers))return false;
    char settings[pm::kMaxFactions*pm::kMaxFactions/2+16];
    char rel[pm::kMaxFactions*pm::kMaxFactions/2+1];
    detail::PutRelations(l.settings.relations,rel);
    std::snprintf(settings,sizeof(settings),"%d;%u;%s",l.settings.enabled ? 1 : 0,
                  static_cast<unsigned>(l.settings.relations.preset),rel);
    int w=std::snprintf(out,n,"%u;%u;%s;%d",static_cast<unsigned>(pvp_protocol::kVersion),l.serial,settings,l.count);
    if(w<0 || static_cast<std::size_t>(w)>=n)return false;
    for(int i=0;i<l.count;++i) {
        const auto& m=l.members[i];
        const int add=std::snprintf(out+w,n-static_cast<std::size_t>(w),";%s,%u,%u,%u",m.id,static_cast<unsigned>(m.pick.faction),
                                    static_cast<unsigned>(m.pick.role),static_cast<unsigned>(m.pick.unit));
        if(add<0 || static_cast<std::size_t>(w+add)>=n)return false;
        w+=add;
    }
    return true;
}
inline bool DecodeLock(const char* text,Lock* out) noexcept {
    constexpr int kMost=6+static_cast<int>(kMaxMembers)+1;
    const char* at[kMost];std::size_t len[kMost];
    const int fields=text ? detail::Split(text,at,len,kMost) : -1;
    long long serial=0,on=0,preset=0,count=0;
    if(fields<6 || !detail::Version(at[0],len[0]) || !detail::Number(at[1],len[1],1,0xFFFFFFFFll,&serial) ||
       !detail::Number(at[2],len[2],0,1,&on) || !detail::Number(at[3],len[3],0,pm::kPresetCount-1,&preset) ||
       !detail::Number(at[5],len[5],1,static_cast<long long>(kMaxMembers),&count) || fields!=6+count)return false;
    Lock l;l.serial=static_cast<std::uint32_t>(serial);l.settings.enabled=on!=0;l.count=static_cast<int>(count);
    if(!detail::GetRelations(static_cast<int>(preset),at[4],len[4],&l.settings.relations))return false;
    for(int i=0;i<l.count;++i) {
        const char* f=at[6+i];const std::size_t fn=len[6+i];
        const char* part[4];std::size_t plen[4];int parts=0;const char* start=f;
        for(std::size_t k=0;k<=fn;++k)if(k==fn || f[k]==',') {
            if(parts==4)return false;
            part[parts]=start;plen[parts]=static_cast<std::size_t>(f+k-start);++parts;start=f+k+1;
        }
        if(parts!=4 || !plen[0] || plen[0]>64)return false;
        for(std::size_t k=0;k<plen[0];++k)if(part[0][k]<33 || part[0][k]>126 || part[0][k]==',' || part[0][k]==';')return false;
        std::memcpy(l.members[i].id,part[0],plen[0]);l.members[i].id[plen[0]]=0;
        if(!detail::PickFields(part+1,plen+1,&l.members[i].pick))return false;
        for(int j=0;j<i;++j)if(!std::strcmp(l.members[j].id,l.members[i].id))return false;
    }
    *out=l;return true;
}
// The lock's identity a member acknowledges: its serial and the hash of its whole text.
inline std::uint32_t LockHash(const Lock& l) noexcept {
    char text[kValueMost];
    return EncodeLock(l,text,sizeof(text)) ? detail::Fnv(text) : 0;
}
// EDF6X_PVPACK: "<version>;<serial>;<hash hex>".
inline void EncodeAck(const Lock& l,char* out,std::size_t n) noexcept {
    std::snprintf(out,n,"%u;%u;%08X",static_cast<unsigned>(pvp_protocol::kVersion),l.serial,LockHash(l));
}
inline bool AckMatches(const char* text,const Lock& l) noexcept {
    char want[64];EncodeAck(l,want,sizeof(want));
    return text && l.serial && !std::strcmp(text,want);
}

// A member as the host reads the room: its id, whether it is the host, its pick if it published a usable one.
struct Member { char id[65]{}; bool host=false,hasPick=false; pm::Pick pick{}; };
// The pick a member without one plays: the first EDF faction, a soldier.
inline pm::Pick DefaultPick() noexcept { return {0,static_cast<std::uint8_t>(pm::Role::soldier),pm::kNoUnit}; }
// The lock the host would publish now (serial left to the caller).
inline Lock BuildLock(const Settings& s,const Member* members,int count) noexcept {
    Lock l;l.settings=s;
    for(int i=0;i<count && l.count<static_cast<int>(kMaxMembers);++i) {
        auto& m=l.members[l.count++];
        std::memcpy(m.id,members[i].id,sizeof(m.id));m.id[64]=0;
        m.pick=members[i].hasPick && pm::PickValid(members[i].pick) ? members[i].pick : DefaultPick();
    }
    return l;
}
// The same match (serial aside): what a member would play has not changed.
inline bool SameMatch(const Lock& a,const Lock& b) noexcept {
    if(a.count!=b.count || !SameSettings(a.settings,b.settings))return false;
    for(int i=0;i<a.count;++i) {
        const auto &x=a.members[i],&y=b.members[i];
        if(std::strcmp(x.id,y.id) || x.pick.faction!=y.pick.faction || x.pick.role!=y.pick.role || x.pick.unit!=y.pick.unit)return false;
    }
    return true;
}
// The protocol state the lock asks for (pvp.h QueuePvpMatch: the host's mission transaction).
inline pvp_protocol::State MatchState(const Lock& l) noexcept {
    pvp_protocol::State s;s.enabled=l.settings.enabled;s.relations=l.settings.relations;
    s.count=static_cast<std::uint8_t>(l.count);
    for(int i=0;i<l.count;++i){std::memcpy(s.entries[i].puid,l.members[i].id,65);pvp_protocol::SetPick(s.entries[i],l.members[i].pick);}
    return s;
}

// The host's start gate: what holding the start found this frame.
enum class Gate : std::uint8_t { open, locking, waiting };
// `acks[i]`: member i's EDF6X_PVPACK value (nullptr none); the host's own counts as acknowledged.
inline bool AllAcknowledged(const Lock& l,const Member* members,const char* const* acks,int count) noexcept {
    if(!l.serial || count!=l.count)return false;
    for(int i=0;i<count;++i)if(!members[i].host && !AckMatches(acks[i],l))return false;
    return true;
}
}  // namespace crew::roompvp
