// The room pre-sync's logic (src/room_sync.h; docs/feature-2026-10-12-room-loadout-menu.md 阶段 6), free of the game and
// of Windows: tests/room_sync_test.cpp drives it offline against a fake EDF6Coop member attribute table. src/room_sync.cpp
// only wires the real things into Env (the files in Mods/OBJECT, the pending list, the background generator, the config).
//
// What travels (EDF6CoopGetMemberAttributeApi v1, src/coop_member_attribute_api.h; values unauthenticated, printable
// ASCII, at most 999 bytes):
//   EDF6X_VCVARIANTS  the host's list: "1;<hash>;<listed>;<parts>;<tokens>"
//   EDF6X_VCVAR1..3   its further parts when the tokens do not fit one value: "<hash>;<tokens>"
//   EDF6X_VCHAVE      every member's readiness: "1;<hash>;<missing>;<state>"
// <hash> is 16 upper-case hex digits: FNV-1a 64 of the listed tokens joined by ',' (ListHash), so a reader knows the parts
// belong together and that a member's readiness is about the list the host has now. <tokens> are ','-separated, sorted,
// unique, each one generated variant file in short form, never its name:
//   a soldier   one letter, the kind (support_call.h SupportWeapon: a = rifle ... n = fshotgun; upper case = the leader's
//               template), then the primary and the secondary colour, each 6 upper-case hex digits or X (stock):
//               "a1E3A8AX" = EDF6VC_NPC_RIFLE_1E3A8A_X.SGO
//   a vehicle   one digit, the body (support_loadout.h VehicleBody 1 tank ... 5 multirole), then the loadout's 64-bit
//               variant (LoadoutVariant, applied bit clear) in 16 upper-case hex digits:
//               "14000000000000C81" = EDF6VC_LO_TANK_4000000000000C81.SGO
// A reader turns a token into a VariantRef, the VariantRef into its file name through support_loadout.h's own naming
// (SupportLookFile / VehicleVariantFile: the one place the grammar lives), and keeps it only when that name parses back
// (ParseVariantFile) to the same VariantRef and the token re-encodes to the same text: anything else (a path, another
// name, a malformed colour, a pylon a body cannot carry) refuses the whole list. A name is only ever a file to generate.
// The pylon weapons a vehicle carries (EDF6VC_<STORE>_<n>.SGO) are not listed: the generator writes them with the vehicle.
// Up to kListParts values (16 distinct extension keys a process, Coop's limit; this uses five), about 270 tokens; a longer
// set is listed as far as it fits and the host says how many are left out (those still go through the pending list).
// <state>: r ready, g generating (or about to), f the generator failed, n no generator installed, w no list to check yet.
#pragma once
#include "coop_member_attribute_api.h"
#include "hudtext.h"
#include "room_sync.h"
#include "support_loadout.h"
#include "support_variants.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <initializer_list>

namespace crew::roomsync {

inline constexpr char kKeyList[]="EDF6X_VCVARIANTS";
inline constexpr char kKeyHave[]="EDF6X_VCHAVE";
inline constexpr int kListParts=4;                    // EDF6X_VCVARIANTS + EDF6X_VCVAR1..3
inline constexpr std::size_t kValueMost=EDF6COOP_MEMBER_ATTRIBUTE_MAX_VALUE;
inline constexpr int kSetMost=512;                    // 37 catalog entries x 12 soldiers, the vehicles
static_assert(kSetMost<=kHostVariantsMost,"the mission preload holds a whole host list (support_variants.h)");
// A client generates at most this many files a room (each one a name the host's validated list gave): a host cannot make
// a member write more than this per room, whatever it lists or how often it changes its list. The rest come stock.
inline constexpr int kNotesPerRoom=128;
inline constexpr int kMembersMost=64;                 // Coop lists at most Epic's 64
inline constexpr std::uint64_t kTickMs=1000;
inline constexpr std::uint64_t kStartGapMs=5000;      // between two generator starts
inline constexpr int kAttemptsMost=2;                 // per missing set
inline constexpr int kLinesMost=8;

// The part key i (0 = kKeyList).
inline void PartKey(int i,char* out,std::size_t capacity) noexcept {
    if(i<=0)std::snprintf(out,capacity,"%s",kKeyList);
    else std::snprintf(out,capacity,"EDF6X_VCVAR%d",i);
}

// --- One generated variant file, by what it is ---
struct VariantRef {
    bool vehicle=false;
    std::uint8_t sub=0;        // a soldier: SupportWeapon; a vehicle: VehicleBody
    bool leader=false;         // a soldier: the leader's template
    std::uint64_t bits=0;      // LookVariant / LoadoutVariant, kVariantApplied clear
};
inline bool operator==(const VariantRef& a,const VariantRef& b) noexcept {
    return a.vehicle==b.vehicle && a.sub==b.sub && a.leader==b.leader && a.bits==b.bits;
}
inline bool Before(const VariantRef& a,const VariantRef& b) noexcept {
    if(a.vehicle!=b.vehicle)return !a.vehicle;
    if(a.sub!=b.sub)return a.sub<b.sub;
    if(a.leader!=b.leader)return !a.leader;
    return a.bits<b.bits;
}

// Its file name (upper case, SupportLookFile / VehicleVariantFile); 0 when it is no file those can name.
inline std::size_t VariantFileOf(const VariantRef& r,wchar_t* out,std::size_t capacity) noexcept {
    if(out && capacity)out[0]=0;
    if(r.bits&kVariantApplied)return 0;
    if(r.vehicle) {
        VehicleLoadout l;
        if(!LoadoutOfVariant(r.bits,static_cast<VehicleBody>(r.sub),&l) || LoadoutVariant(l)!=r.bits)return 0;
        return VehicleVariantFile(l,out,capacity);
    }
    SupportLook look;
    if(r.sub>=kSupportWeaponCount || !LookOfVariant(r.bits,&look) || LookVariant(look)!=r.bits)return 0;
    return SupportLookFile(static_cast<SupportWeapon>(r.sub),r.leader,look,out,capacity);
}

namespace detail {
inline int HexDigit(wchar_t c) noexcept { return c>=L'0' && c<=L'9' ? c-L'0' : c>=L'A' && c<=L'F' ? c-L'A'+10 : -1; }
inline bool HexOf(const wchar_t* s,int digits,std::uint64_t* out) noexcept {
    std::uint64_t v=0;
    for(int i=0;i<digits;++i){const int d=HexDigit(s[i]);if(d<0)return false;v=(v<<4)|static_cast<unsigned>(d);}
    *out=v;return true;
}
// The upper-case word at s (A-Z) up to the next '_' : its length.
inline std::size_t Word(const wchar_t* s) noexcept {
    std::size_t n=0;while(s[n]>=L'A' && s[n]<=L'Z')++n;
    return n;
}
inline bool SameWord(const wchar_t* s,std::size_t n,const wchar_t* lower) noexcept {
    std::size_t i=0;
    for(;i<n && lower[i];++i)if(static_cast<wchar_t>(std::towupper(lower[i]))!=s[i])return false;
    return i==n && !lower[i];
}
}  // namespace detail

// A file name (exactly as SupportLookFile / VehicleVariantFile write it, upper case, no path) back into what it is.
// Anything else is refused: the name is rebuilt from the result and must come out the same, character for character.
inline bool ParseVariantFile(const wchar_t* name,VariantRef* out) noexcept {
    using namespace detail;
    if(!name)return false;
    const std::size_t length=std::wcslen(name);
    if(!length || length>=96)return false;
    for(std::size_t i=0;i<length;++i) {
        const wchar_t c=name[i];
        if(!((c>=L'A' && c<=L'Z') || (c>=L'0' && c<=L'9') || c==L'_' || c==L'.'))return false;
    }
    VariantRef r;
    if(!std::wcsncmp(name,L"EDF6VC_NPC_",11)) {
        const wchar_t* s=name+11;
        const std::size_t n=Word(s);
        int kind=-1;
        for(int k=0;k<kSupportWeaponCount;++k)if(SameWord(s,n,kSupportKindNames[k].name))kind=k;
        if(kind<0)return false;
        s+=n;
        r.sub=static_cast<std::uint8_t>(kind);
        if(!std::wcsncmp(s,L"_L_",3)){r.leader=true;s+=2;}
        SupportLook look;
        for(std::int32_t* colour:{&look.primary,&look.secondary}) {
            if(*s!=L'_')return false;
            ++s;
            if(*s==L'X'){++s;continue;}
            std::uint64_t v=0;
            if(!HexOf(s,6,&v))return false;
            *colour=static_cast<std::int32_t>(v);s+=6;
        }
        if(std::wcscmp(s,L".SGO") || !look.On())return false;
        r.bits=LookVariant(look);
    } else if(!std::wcsncmp(name,L"EDF6VC_LO_",10)) {
        const wchar_t* s=name+10;
        const std::size_t n=Word(s);
        const VehicleBodyRow* body=nullptr;
        for(const auto& row:kVehicleBodies)if(SameWord(s,n,row.name))body=&row;
        if(!body)return false;
        s+=n;
        std::uint64_t v=0;
        if(*s!=L'_' || !HexOf(s+1,16,&v) || std::wcscmp(s+17,L".SGO"))return false;
        r.vehicle=true;r.sub=static_cast<std::uint8_t>(body->body);r.bits=v;
    } else {
        return false;
    }
    wchar_t again[96];
    if(!VariantFileOf(r,again,96) || std::wcscmp(again,name))return false;
    if(out)*out=r;
    return true;
}

// --- Tokens ---
// The short form (above) of `r` into out (NUL-terminated); its length, 0 when `r` is no variant or it does not fit.
inline std::size_t TokenOf(const VariantRef& r,char* out,std::size_t capacity) noexcept {
    wchar_t file[96];
    if(!out || !capacity || !VariantFileOf(r,file,96))return 0;
    char t[24];int n=0;
    const char* hex="0123456789ABCDEF";
    if(r.vehicle) {
        t[n++]=static_cast<char>('0'+r.sub);
        for(int s=60;s>=0;s-=4)t[n++]=hex[(r.bits>>s)&0xF];
    } else {
        t[n++]=static_cast<char>((r.leader ? 'A' : 'a')+r.sub);
        SupportLook look;LookOfVariant(r.bits,&look);
        for(const std::int32_t colour:{look.primary,look.secondary}) {
            if(colour==kStockColour){t[n++]='X';continue;}
            for(int s=20;s>=0;s-=4)t[n++]=hex[(static_cast<std::uint32_t>(colour)>>s)&0xF];
        }
    }
    t[n]=0;
    if(static_cast<std::size_t>(n)+1>capacity){out[0]=0;return 0;}
    std::memcpy(out,t,static_cast<std::size_t>(n)+1);
    return static_cast<std::size_t>(n);
}
// One token (s, n characters, no separator) back; false for anything TokenOf would not have written.
inline bool ParseToken(const char* s,std::size_t n,VariantRef* out) noexcept {
    if(!s || !n || n>20)return false;
    VariantRef r;
    const auto hex=[](char c) noexcept { return c>='0' && c<='9' ? c-'0' : c>='A' && c<='F' ? c-'A'+10 : -1; };
    if(s[0]>='1' && s[0]<='5') {
        if(n!=17)return false;
        r.vehicle=true;r.sub=static_cast<std::uint8_t>(s[0]-'0');
        for(std::size_t i=1;i<17;++i){const int d=hex(s[i]);if(d<0)return false;r.bits=(r.bits<<4)|static_cast<unsigned>(d);}
    } else if((s[0]>='a' && s[0]<'a'+kSupportWeaponCount) || (s[0]>='A' && s[0]<'A'+kSupportWeaponCount)) {
        r.leader=s[0]<'a';r.sub=static_cast<std::uint8_t>(s[0]-(r.leader ? 'A' : 'a'));
        SupportLook look;std::size_t i=1;
        for(std::int32_t* colour:{&look.primary,&look.secondary}) {
            if(i<n && s[i]=='X'){++i;continue;}
            if(i+6>n)return false;
            std::int32_t v=0;
            for(int k=0;k<6;++k){const int d=hex(s[i+k]);if(d<0)return false;v=(v<<4)|d;}
            *colour=v;i+=6;
        }
        if(i!=n || !look.On())return false;
        r.bits=LookVariant(look);
    } else {
        return false;
    }
    char again[24];
    if(TokenOf(r,again,sizeof(again))!=n || std::memcmp(again,s,n))return false;
    if(out)*out=r;
    return true;
}

// --- A set of variants: sorted, unique ---
struct VariantSet {
    int count=0;
    VariantRef ref[kSetMost];
    // False when full (the variant left out).
    bool Add(const VariantRef& r) noexcept {
        int at=0;
        while(at<count && Before(ref[at],r))++at;
        if(at<count && ref[at]==r)return true;
        if(count>=kSetMost)return false;
        for(int i=count;i>at;--i)ref[i]=ref[i-1];
        ref[at]=r;++count;
        return true;
    }
    bool Has(const VariantRef& r) const noexcept {
        for(int i=0;i<count;++i)if(ref[i]==r)return true;
        return false;
    }
};
inline std::uint64_t Fnv(std::uint64_t h,const char* s,std::size_t n) noexcept {
    for(std::size_t i=0;i<n;++i){h^=static_cast<unsigned char>(s[i]);h*=1099511628211ull;}
    return h;
}
inline constexpr std::uint64_t kFnvBasis=1469598103934665603ull;
// The hash of the first `n` variants of `set` (ListHash of the tokens joined by ',').
inline std::uint64_t ListHash(const VariantSet& set,int n) noexcept {
    std::uint64_t h=kFnvBasis;
    for(int i=0;i<n && i<set.count;++i) {
        char t[24];const std::size_t len=TokenOf(set.ref[i],t,sizeof(t));
        if(i)h=Fnv(h,",",1);
        h=Fnv(h,t,len);
    }
    return h;
}

// --- The host's list: encode / decode ---
struct ListValues {
    int parts=0;                       // values written (1..kListParts); keys PartKey(0..parts-1)
    int listed=0;                      // variants in them (the first `listed` of the set)
    std::uint64_t hash=0;              // ListHash(set, listed)
    char value[kListParts][kValueMost+1]{};
};
// Header room: "1;" + 16 + ";" + up to 3 digits + ";" + 1 digit + ";": 25 bytes.
inline constexpr std::size_t kHeaderMost=25,kPartHeader=17;
inline void EncodeList(const VariantSet& set,ListValues* out) noexcept {
    ListValues& v=*out;
    v=ListValues{};
    // Lay the tokens out first (each part's room), then write the header with the counts.
    int partOf[kSetMost];
    std::size_t used[kListParts]{};
    int part=0,listed=0;
    for(int i=0;i<set.count;++i) {
        char t[24];const std::size_t len=TokenOf(set.ref[i],t,sizeof(t));
        if(!len)break;
        const std::size_t room=kValueMost-(part ? kPartHeader : kHeaderMost);
        const std::size_t need=len+(used[part] ? 1 : 0);
        if(used[part]+need>room) {
            if(part+1>=kListParts)break;
            ++part;
            if(len>kValueMost-kPartHeader)break;
            used[part]=len;
        } else {
            used[part]+=need;
        }
        partOf[i]=part;++listed;
    }
    v.listed=listed;
    v.parts=listed ? partOf[listed-1]+1 : 1;
    v.hash=ListHash(set,listed);
    char hash[17];std::snprintf(hash,sizeof(hash),"%016llX",static_cast<unsigned long long>(v.hash));
    std::snprintf(v.value[0],sizeof(v.value[0]),"1;%s;%d;%d;",hash,listed,v.parts);
    for(int p=1;p<v.parts;++p)std::snprintf(v.value[p],sizeof(v.value[p]),"%s;",hash);
    bool first[kListParts]={true,true,true,true};
    for(int i=0;i<listed;++i) {
        char* dst=v.value[partOf[i]];
        std::size_t at=std::strlen(dst);
        if(!first[partOf[i]])dst[at++]=',';
        first[partOf[i]]=false;
        TokenOf(set.ref[i],dst+at,kValueMost+1-at);
    }
}

enum class ListState : std::uint8_t { none, waiting, bad, ok };
struct ListHeader { std::uint64_t hash=0; int listed=0,parts=0; const char* tokens=nullptr; };
inline bool ParseHash(const char* s,std::uint64_t* out) noexcept {
    std::uint64_t v=0;
    for(int i=0;i<16;++i) {
        const char c=s[i];
        const int d=c>='0' && c<='9' ? c-'0' : c>='A' && c<='F' ? c-'A'+10 : -1;
        if(d<0)return false;
        v=(v<<4)|static_cast<unsigned>(d);
    }
    *out=v;return true;
}
inline bool ParseCount(const char*& s,int most,int* out) noexcept {
    int v=0,digits=0;
    while(*s>='0' && *s<='9'){v=v*10+(*s-'0');++s;if(++digits>4)return false;}
    if(!digits || v>most || (digits>1 && s[-digits]=='0'))return false;
    *out=v;return true;
}
// "1;<hash>;<listed>;<parts>;<tokens>".
inline bool ParseListHeader(const char* value,ListHeader* out) noexcept {
    if(!value || std::strncmp(value,"1;",2))return false;
    ListHeader h;
    const char* s=value+2;
    if(!ParseHash(s,&h.hash) || s[16]!=';')return false;
    s+=17;
    if(!ParseCount(s,kSetMost,&h.listed) || *s++!=';' || !ParseCount(s,kListParts,&h.parts) || *s++!=';' || h.parts<1)return false;
    h.tokens=s;
    *out=h;return true;
}
// "<hash>;<tokens>" of the list `hash`: its tokens, nullptr when it is another list's (or not one).
inline const char* PartTokens(const char* value,std::uint64_t hash) noexcept {
    std::uint64_t h=0;
    if(!value || std::strlen(value)<17 || !ParseHash(value,&h) || value[16]!=';' || h!=hash)return nullptr;
    return value+17;
}
// Adds the ','-separated tokens to `set`; false on the first malformed one (or an empty token, or more than kSetMost).
inline bool AddTokens(const char* tokens,VariantSet* set,int* added) noexcept {
    if(!tokens || !*tokens)return true;
    const char* s=tokens;
    for(;;) {
        const char* end=s;while(*end && *end!=',')++end;
        VariantRef r;
        if(!ParseToken(s,static_cast<std::size_t>(end-s),&r) || set->Has(r) || !set->Add(r))return false;
        ++*added;
        if(!*end)return true;
        s=end+1;
    }
}
// DecodeList's work; `set` may be left partly filled on a failure.
inline ListState DecodeListInto(const char* const* value,int have,VariantSet* set,std::uint64_t* hash) noexcept {
    if(have<1 || !value[0])return ListState::none;
    ListHeader h;
    if(!ParseListHeader(value[0],&h))return ListState::bad;
    int added=0;
    if(!AddTokens(h.tokens,set,&added))return ListState::bad;
    for(int p=1;p<h.parts;++p) {
        if(p>=have || !value[p])return ListState::waiting;
        const char* tokens=PartTokens(value[p],h.hash);
        if(!tokens) {
            std::uint64_t other=0;   // another list's part (the host changed it; the rest is on its way) or junk
            return std::strlen(value[p])>=17 && ParseHash(value[p],&other) && value[p][16]==';' ? ListState::waiting : ListState::bad;
        }
        if(!AddTokens(tokens,set,&added))return ListState::bad;
    }
    if(added!=h.listed || ListHash(*set,set->count)!=h.hash)return ListState::bad;
    *hash=h.hash;
    return ListState::ok;
}
// The host's values (value[i] of PartKey(i), nullptr where it has none) into `set`: ok only when every part is there,
// of the same list, every token valid, the count and the hash as the header says. Anything else: `set` empty.
inline ListState DecodeList(const char* const* value,int have,VariantSet* set,std::uint64_t* hash) noexcept {
    set->count=0;*hash=0;
    const ListState state=DecodeListInto(value,have,set,hash);
    if(state!=ListState::ok)set->count=0;
    return state;
}

// --- Readiness: "1;<hash>;<missing>;<state>" ---
enum class Ready : char { ready='r', generating='g', failed='f', noGenerator='n', noList='w' };
struct Have { std::uint64_t hash=0; int missing=0; Ready state=Ready::noList; };
inline void EncodeHave(const Have& h,char* out,std::size_t capacity) noexcept {
    std::snprintf(out,capacity,"1;%016llX;%d;%c",static_cast<unsigned long long>(h.hash),h.missing,static_cast<char>(h.state));
}
inline bool ParseHave(const char* value,Have* out) noexcept {
    if(!value || std::strncmp(value,"1;",2))return false;
    Have h;
    const char* s=value+2;
    if(!ParseHash(s,&h.hash) || s[16]!=';')return false;
    s+=17;
    if(!ParseCount(s,kSetMost,&h.missing) || *s++!=';')return false;
    switch(*s) {
        case 'r':case 'g':case 'f':case 'n':case 'w':h.state=static_cast<Ready>(*s);break;
        default:return false;
    }
    // ready exactly when nothing is missing; no list: nothing counted
    if(s[1] || (h.state==Ready::noList ? h.missing!=0 : (h.state==Ready::ready)!=(h.missing==0)))return false;
    *out=h;return true;
}

// --- The generator, as the sync sees it (loadout_file.h GenerateState) ---
enum class Gen : std::uint8_t { idle, running, ok, failed, missing };
struct GenStatus { Gen state=Gen::idle; wchar_t message[200]{}; };
// One attempt per missing set (at most kAttemptsMost: a generator that ended ok while the set is still missing is
// tried once more), kStartGapMs apart, never while one runs or none is installed.
struct GenPlan {
    std::uint64_t setHash=0,lastStart=0;
    int attempts=0;
    bool started=false;
};
inline bool ShouldStart(GenPlan& p,std::uint64_t missingHash,int missing,Gen state,std::uint64_t now) noexcept {
    if(missing<=0 || state==Gen::running || state==Gen::missing)return false;
    if(p.setHash!=missingHash){p.setHash=missingHash;p.attempts=0;}
    if(p.attempts>=kAttemptsMost || (p.attempts && state==Gen::failed))return false;
    if(p.started && now-p.lastStart<kStartGapMs)return false;
    return true;
}
inline void Started(GenPlan& p,std::uint64_t now) noexcept { ++p.attempts;p.started=true;p.lastStart=now; }
// This machine's state for its readiness value.
inline Ready OwnReady(const GenPlan& p,std::uint64_t missingHash,int missing,Gen state) noexcept {
    if(missing<=0)return Ready::ready;
    if(state==Gen::running)return Ready::generating;
    if(state==Gen::missing)return Ready::noGenerator;
    const bool spent=p.setHash==missingHash && (p.attempts>=kAttemptsMost || (p.attempts && state==Gen::failed));
    return spent ? Ready::failed : Ready::generating;
}

// --- What the menu says ---
enum class MemberStatus : std::uint8_t { none, stale, ready, missing };
struct MemberReady {
    std::uint32_t ordinal=0;
    bool isHost=false,isSelf=false;
    MemberStatus status=MemberStatus::none;
    int missing=0;
    Ready state=Ready::noList;
};
inline MemberReady ReadyOf(const EDF6CoopMemberValue& m,std::uint64_t listHash) noexcept {
    MemberReady r;
    r.ordinal=m.ordinal;r.isHost=m.isHost!=0;r.isSelf=m.isSelf!=0;
    Have h;
    if(!m.hasValue || !ParseHave(m.value,&h))return r;
    r.missing=h.missing;r.state=h.state;
    r.status=h.state==Ready::noList || h.hash!=listHash ? MemberStatus::stale : h.state==Ready::ready ? MemberStatus::ready : MemberStatus::missing;
    return r;
}

struct Situation {
    bool api=false,inRoom=false,isHost=false;
    int members=0;                  // the room's, this machine included
    ListState list=ListState::none; // the host's list (this machine's own when it is the host)
    int listed=0,leftOut=0;         // variants listed / the host's that did not fit
    int ownMissing=0;
    int ownCapped=0;                // of those, over kNotesPerRoom: never generated in this room
    Ready own=Ready::noList;
    const wchar_t* genMessage=L"";  // the generator's last message
    int others=0;
    MemberReady other[kMembersMost];
};
struct LineSpec { hudtext::Tx tx; int a=0,b=0; const wchar_t* s=nullptr; bool warn=false; };

inline SyncState StateOf(const Situation& s) noexcept {
    if(!s.api)return SyncState::unavailable;
    if(!s.inRoom || s.members<=1)return SyncState::alone;
    if(s.list!=ListState::ok || s.own!=Ready::ready)return SyncState::waiting;
    for(int i=0;i<s.others;++i)if(s.other[i].status!=MemberStatus::ready)return SyncState::waiting;
    return SyncState::ready;
}
// The lines, problems first; at most `capacity`.
inline int SelectLines(const Situation& s,LineSpec* out,int capacity) noexcept {
    using hudtext::Tx;
    int n=0;
    const auto put=[&](Tx tx,bool warn,int a=0,int b=0,const wchar_t* text=nullptr) noexcept {
        if(n<capacity){out[n]=LineSpec{tx,a,b,text,warn};++n;}
    };
    if(!s.api){put(Tx::roomSyncCoopOld,true);return n;}
    if(!s.inRoom)return n;
    switch(s.list) {
        case ListState::none:if(!s.isHost)put(Tx::roomSyncHostOld,true);break;
        case ListState::bad:put(Tx::roomSyncHostBad,true);break;
        case ListState::waiting:put(Tx::roomSyncHostWaiting,false);break;
        case ListState::ok:break;
    }
    if(s.leftOut)put(Tx::roomSyncLeftOut,true,s.leftOut);
    if(s.list==ListState::ok && s.ownCapped)put(Tx::roomSyncSelfCapped,true,s.ownCapped,kNotesPerRoom);
    if(s.list==ListState::ok && s.ownMissing>s.ownCapped) {
        switch(s.own) {
            case Ready::failed:put(Tx::roomSyncSelfFailed,true,s.ownMissing,0,s.genMessage && *s.genMessage ? s.genMessage : L"-");break;
            case Ready::noGenerator:put(Tx::roomSyncSelfNoGenerator,true,s.ownMissing);break;
            case Ready::generating:put(Tx::roomSyncSelfGenerating,false,s.ownMissing);break;
            default:break;
        }
    }
    for(int i=0;i<s.others;++i) {
        const MemberReady& m=s.other[i];
        const int who=static_cast<int>(m.ordinal)+1;
        switch(m.status) {
            case MemberStatus::none:put(Tx::roomSyncMemberOld,true,who);break;
            case MemberStatus::stale:if(s.list==ListState::ok)put(Tx::roomSyncMemberSyncing,false,who);break;
            case MemberStatus::missing:
                put(m.state==Ready::failed || m.state==Ready::noGenerator ? Tx::roomSyncMemberFailed : Tx::roomSyncMemberGenerating,
                    m.state==Ready::failed || m.state==Ready::noGenerator,who,m.missing);
                break;
            case MemberStatus::ready:break;
        }
    }
    if(StateOf(s)==SyncState::ready)put(Tx::roomSyncAllReady,false,s.listed);
    else if(StateOf(s)==SyncState::alone && s.list==ListState::ok && s.own==Ready::ready)put(Tx::roomSyncSelfReady,false,s.listed);
    return n;
}
inline void FormatLine(const LineSpec& l,hudtext::Lang lang,SyncLine* out) noexcept {
    const wchar_t* format=hudtext::Tr(l.tx,lang);
    switch(l.tx) {   // each text's arguments (tools/hudtext_check.cpp keeps every language's the same)
        case hudtext::Tx::roomSyncSelfFailed:_snwprintf_s(out->text,_TRUNCATE,format,l.a,l.s ? l.s : L"");break;
        case hudtext::Tx::roomSyncMemberGenerating:case hudtext::Tx::roomSyncMemberFailed:case hudtext::Tx::roomSyncSelfCapped:
            _snwprintf_s(out->text,_TRUNCATE,format,l.a,l.b);break;
        default:_snwprintf_s(out->text,_TRUNCATE,format,l.a);break;
    }
    out->warn=l.warn;
}

// --- The sync itself, one per process (src/room_sync.cpp) or per fake member (the test) ---
struct Env {
    void* context=nullptr;
    // Mods/OBJECT/<file> there.
    bool (*fileThere)(void* context,const wchar_t* file)=nullptr;
    // A file the host's list wants and this machine lacks: support_variants.h NoteMissingVariant (the pending list).
    void (*noteMissing)(void* context,const wchar_t* file)=nullptr;
    // loadout_store.h StartVariantGeneration / VariantGenerationStatus.
    bool (*startGeneration)(void* context)=nullptr;
    GenStatus (*generation)(void* context)=nullptr;
    // The host's own list: every file the current configuration can need (support_variants.h ForEachConfiguredVariant).
    void (*configured)(void* context,void (*each)(void* set,const wchar_t* file),void* set)=nullptr;
    void (*log)(void* context,const char* text)=nullptr;
};

class Engine {
public:
    // At most once a kTickMs (the room's frames call it every frame).
    void Tick(const EDF6CoopMemberAttributeApi* api,std::uint64_t now,const Env& env) noexcept {
        if(ticked_ && now-lastTick_<kTickMs)return;
        ticked_=true;lastTick_=now;
        Situation& s=situation_;
        s=Situation{};
        s.api=api!=nullptr;
        if(!api){lines_=SelectLines(s,line_,kLinesMost);return;}
        EDF6CoopRoomState room{};room.size=sizeof(room);
        s.inRoom=api->room(&room) && room.inRoom;
        if(!s.inRoom) {
            if(anyPublished_)Leave(api);
            lines_=SelectLines(s,line_,kLinesMost);
            return;
        }
        s.isHost=room.isHost!=0;s.members=static_cast<int>(room.memberCount);
        if(s.isHost)HostList(api,env);
        else ReadHostList(api);
        s.list=list_;s.listed=target_.count;s.leftOut=leftOut_;
        // The list the next mission's plans come from: a client's validated one; none on the host or without a list.
        if(!s.isHost && list_==ListState::ok){hostList_=target_;hostListHash_=listHash_;hostListOk_=true;}
        else ForgetHostList();
        Readiness(api,now,env);
        ReadMembers(api);
        lines_=SelectLines(s,line_,kLinesMost);
    }
    // The room is gone: every key this machine published withdrawn, the state forgotten.
    void Leave(const EDF6CoopMemberAttributeApi* api) noexcept {
        if(api) {
            for(int p=0;p<kListParts;++p)if(listPublished_[p][0]){char key[33];PartKey(p,key,sizeof(key));api->clear(key);}
            if(havePublished_[0])api->clear(kKeyHave);
        }
        Reset();
    }
    // The host's list last validated in a room (a client's; empty: none, or this machine the host): the mission that
    // starts from that room preloads its files (support_variants.h PreloadSupportVariants) and tells the host it has
    // exactly those (its hello's filter). Kept when the room is left (a mission starting leaves it); replaced by the next
    // tick in a room, dropped by ForgetHostList.
    const VariantSet& HostList() const noexcept { return hostList_; }
    std::uint64_t HostListHash() const noexcept { return hostListOk_ ? hostListHash_ : 0; }
    bool HasHostList() const noexcept { return hostListOk_; }
    void ForgetHostList() noexcept { hostListOk_=false;hostListHash_=0;hostList_.count=0; }
    SyncState State() const noexcept { return StateOf(situation_); }
    int Lines(SyncLine* out,int capacity,hudtext::Lang lang) const noexcept {
        int n=0;
        for(;out && n<capacity && n<lines_;++n)FormatLine(line_[n],lang,&out[n]);
        return n;
    }
    const Situation& Now() const noexcept { return situation_; }
    const VariantSet& Target() const noexcept { return target_; }
private:
    // Everything a room leaves behind, but the tick clock and the situation the caller is filling.
    void Reset() noexcept {
        anyPublished_=false;listHash_=0;loggedHash_=0;list_=ListState::none;leftOut_=0;unnamed_=0;full_=0;cappedLogged_=0;
        own_.count=0;decoded_.count=0;target_.count=0;noted_.count=0;
        plan_=GenPlan{};
        for(auto& v:listPublished_)v[0]=0;
        havePublished_[0]=0;genMessage_[0]=0;
    }
    void Log(const Env& env,const char* text) noexcept { if(env.log)env.log(env.context,text); }
    bool Publish(const EDF6CoopMemberAttributeApi* api,const char* key,const char* value,char* cache) noexcept {
        if(!std::strcmp(cache,value))return true;
        if(!api->publish(key,value))return false;   // Coop's glue not up yet: tried again next tick
        std::snprintf(cache,kValueMost+1,"%s",value);anyPublished_=true;
        return true;
    }
    void Withdraw(const EDF6CoopMemberAttributeApi* api,int part) noexcept {
        if(!listPublished_[part][0])return;
        char key[33];PartKey(part,key,sizeof(key));
        if(api->clear(key))listPublished_[part][0]=0;
    }
    static void AddConfigured(void* context,const wchar_t* file) noexcept {
        auto* self=static_cast<Engine*>(context);
        VariantRef r;
        if(!ParseVariantFile(file,&r)){++self->unnamed_;return;}
        if(!self->own_.Add(r))++self->full_;
    }
    void HostList(const EDF6CoopMemberAttributeApi* api,const Env& env) noexcept {
        own_.count=0;unnamed_=0;full_=0;
        if(env.configured)env.configured(env.context,&Engine::AddConfigured,this);
        EncodeList(own_,&values_);
        for(int p=0;p<values_.parts;++p) {
            char key[33];PartKey(p,key,sizeof(key));
            Publish(api,key,values_.value[p],listPublished_[p]);
        }
        for(int p=values_.parts;p<kListParts;++p)Withdraw(api,p);
        target_.count=0;
        for(int i=0;i<values_.listed;++i)target_.Add(own_.ref[i]);
        listHash_=values_.hash;list_=ListState::ok;
        leftOut_=own_.count-values_.listed+full_;
        if(values_.hash!=loggedHash_) {
            loggedHash_=values_.hash;
            char text[160];
            std::snprintf(text,sizeof(text),"ROOMSYNC host list %016llX: %d variant files in %d value(s), %d left out, %d unnamed",
                          static_cast<unsigned long long>(values_.hash),values_.listed,values_.parts,leftOut_,unnamed_);
            Log(env,text);
        }
    }
    struct HostValue { char value[kValueMost+1]; bool has; };
    static void TakeHost(void* context,const EDF6CoopMemberValue* m) noexcept {
        auto* out=static_cast<HostValue*>(context);
        if(!m || !m->isHost || m->isSelf || !m->hasValue || !m->value)return;
        if(m->valueBytes>kValueMost)return;
        std::snprintf(out->value,sizeof(out->value),"%s",m->value);out->has=true;
    }
    void ReadHostList(const EDF6CoopMemberAttributeApi* api) noexcept {
        // No longer the host (it moved): this machine's list withdrawn.
        for(int p=0;p<kListParts;++p)Withdraw(api,p);
        static HostValue part[kListParts];   // UI thread only (src/room_sync.h); the test ticks one engine at a time
        const char* value[kListParts]{};
        int have=0;
        part[0]=HostValue{};
        api->snapshot(kKeyList,&Engine::TakeHost,&part[0],nullptr);
        if(part[0].has) {
            value[0]=part[0].value;have=1;
            ListHeader h;
            if(ParseListHeader(part[0].value,&h))
                for(int p=1;p<h.parts;++p) {
                    char key[33];PartKey(p,key,sizeof(key));
                    part[p]=HostValue{};
                    api->snapshot(key,&Engine::TakeHost,&part[p],nullptr);
                    value[p]=part[p].has ? part[p].value : nullptr;have=p+1;
                }
        }
        std::uint64_t hash=0;
        const ListState state=DecodeList(value,have,&decoded_,&hash);
        // A list being replaced part by part: keep checking against the last complete one meanwhile.
        if(state==ListState::waiting && list_==ListState::ok)return;
        list_=state;leftOut_=0;
        if(state==ListState::ok){target_=decoded_;listHash_=hash;}
        else {target_.count=0;listHash_=0;}
    }
    void Readiness(const EDF6CoopMemberAttributeApi* api,std::uint64_t now,const Env& env) noexcept {
        Situation& s=situation_;
        Have have;
        if(list_==ListState::ok) {
            // `fixable`: the missing files the generator can make (the host's own ini names its own; a client's are the
            // ones on its pending list: at most kNotesPerRoom a room); the rest (`capped`) stay missing, stock.
            int missing=0,capped=0;std::uint64_t missingHash=kFnvBasis;
            for(int i=0;i<target_.count;++i) {
                wchar_t file[96];
                if(!VariantFileOf(target_.ref[i],file,96))continue;
                if(env.fileThere && env.fileThere(env.context,file))continue;
                ++missing;
                // The host's own list is its own ini: the generator reads that. A client's goes on the pending list
                // (the generator reads it too, and the installer's menus as before), never more than kNotesPerRoom.
                if(!s.isHost && !noted_.Has(target_.ref[i])) {
                    if(noted_.count>=kNotesPerRoom){++capped;continue;}
                    noted_.Add(target_.ref[i]);
                    if(env.noteMissing)env.noteMissing(env.context,file);
                }
                char t[24];missingHash=Fnv(Fnv(missingHash,",",1),t,TokenOf(target_.ref[i],t,sizeof(t)));
            }
            if(capped && capped!=cappedLogged_) {
                cappedLogged_=capped;
                char text[128];
                std::snprintf(text,sizeof(text),"ROOMSYNC %d variant file(s) over this room's limit of %d: not generated, stock",
                              capped,kNotesPerRoom);
                Log(env,text);
            }
            const int fixable=missing-capped;
            const GenStatus gen=env.generation ? env.generation(env.context) : GenStatus{};
            if(ShouldStart(plan_,missingHash,fixable,gen.state,now)) {
                Started(plan_,now);
                const bool ok=env.startGeneration && env.startGeneration(env.context);
                char text[96];
                std::snprintf(text,sizeof(text),"ROOMSYNC %d variant file(s) missing: generator %s",missing,ok ? "started" : "not started");
                Log(env,text);
            }
            const GenStatus after=env.generation ? env.generation(env.context) : gen;
            have.hash=listHash_;have.missing=missing;
            have.state=fixable>0 || !missing ? OwnReady(plan_,missingHash,fixable,after.state) : Ready::failed;
            s.ownCapped=capped;
            std::memcpy(genMessage_,after.message,sizeof(genMessage_));
            genMessage_[sizeof(genMessage_)/sizeof(genMessage_[0])-1]=0;
        }
        s.ownMissing=have.missing;s.own=have.state;s.genMessage=genMessage_;
        char value[48];EncodeHave(have,value,sizeof(value));
        Publish(api,kKeyHave,value,havePublished_);
    }
    static void TakeMember(void* context,const EDF6CoopMemberValue* m) noexcept {
        auto* self=static_cast<Engine*>(context);
        Situation& s=self->situation_;
        if(!m || m->isSelf || s.others>=kMembersMost)return;
        s.other[s.others++]=ReadyOf(*m,self->listHash_);
    }
    void ReadMembers(const EDF6CoopMemberAttributeApi* api) noexcept {
        api->snapshot(kKeyHave,&Engine::TakeMember,this,nullptr);
    }

    bool ticked_=false,anyPublished_=false;
    std::uint64_t lastTick_=0,listHash_=0,loggedHash_=0;
    ListState list_=ListState::none;
    int leftOut_=0,unnamed_=0,full_=0,cappedLogged_=0;
    VariantSet own_,decoded_,target_,noted_;
    VariantSet hostList_;               // HostList(): not cleared by Reset (a mission start leaves the room)
    std::uint64_t hostListHash_=0;
    bool hostListOk_=false;
    ListValues values_;
    GenPlan plan_;
    char listPublished_[kListParts][kValueMost+1]{};
    char havePublished_[kValueMost+1]{};
    wchar_t genMessage_[200]{};
    Situation situation_;
    LineSpec line_[kLinesMost];
    int lines_=0;
};

}  // namespace crew::roomsync
