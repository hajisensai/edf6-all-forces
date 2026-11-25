// The room pre-sync's logic (src/room_sync.h; docs/feature-2026-10-12-room-loadout-menu.md 阶段 6, reworked 2026-11-21:
// docs/feedback-2026-11-21-worldmap-room.md), free of the game and of Windows: tests/room_sync_test.cpp drives it offline
// against a fake EDF6Coop member attribute table. src/room_sync.cpp only wires the real things into Env (the files in
// Mods/OBJECT, the pending list, the background generator, the configuration and its line, member_config.h).
//
// In an online room every member's own support loadout applies to the supports that member calls (the host plans a
// member's request with that member's configuration, support_dispatch.cpp PlanForRequester). So every member publishes
// its configuration, and every machine makes the variant files of every member's configuration before the mission.
//
// What travels (EDF6CoopGetMemberAttributeApi v1, src/coop_member_attribute_api.h; values unauthenticated, printable
// ASCII, at most 999 bytes):
//   EDF6X_VCCFG       every member's own configuration: "1;<hash>;<parts>;<line>"
//   EDF6X_VCCFG1..3   its further parts when the line does not fit one value: "<hash>;<line continued>"
//   EDF6X_VCHAVE      every member's readiness: "1;<hash>;<missing>;<state>"
//   EDF6X_VCPVEH      every member's own player loadouts (player_vehicle_logic.h: the jets it summons itself), "1;<tokens>":
//                     every other member generates their files too, and preloads them for the mission
// <line> is member_config.h's (Env::ownConfig writes it, Env::memberVariants reads one: this file only carries it), its
// <hash> 16 upper-case hex digits, FNV-1a 64 of the whole line, so a reader knows the parts belong together. The parts
// are cut anywhere (the line is put back together before it is read). The whole configuration, headers included, stays
// within kConfigBudget (Coop's budget: EDF6COOP_MEMBER_ATTRIBUTE_MAX_TOTAL_VALUE 4096 bytes of every value this process
// publishes); entries that do not fit are left out by the writer and said on a status line.
// Every machine derives the files of the room: its own configuration's (Env::configured, its own ini, which the generator
// reads), and every other member's line's (Env::memberVariants: refused whole when it does not read, that member said on
// a line). The union, sorted, is the room's set (Target); its hash (ListHash over the tokens below) is what a member's
// readiness names, so a member that sees the same configurations reports ready for the same set.
// Tokens (the hash's spelling, and EDF6X_VCPVEH's vehicles), each one generated variant file in short form:
//   a soldier   one letter, the kind (support_call.h SupportWeapon: a = rifle ... n = fshotgun; upper case = the leader's
//               template), then the primary and the secondary colour, each 6 upper-case hex digits or X (stock):
//               "a1E3A8AX" = EDF6VC_NPC_RIFLE_1E3A8A_X.SGO; a backpack weapon (npc_pack_table.h) adds '+' and its code in
//               2 upper-case hex digits: "aXX+01" = EDF6VC_NPC_RIFLE_X_X_AHANDGRENADE01.SGO.
//   a vehicle   one character, '0' + the body (support_loadout.h VehicleBody 1 tank ... 5 multirole; 6 .. 10, ':', a
//               player's own summons), then the loadout's 64-bit variant (LoadoutVariant, applied bit clear) in 16
//               upper-case hex digits: "14000000000000C81" = EDF6VC_LO_TANK_4000000000000C81.SGO
// A file name only ever reaches the pending list after it parses back (ParseVariantFile) to what it names.
// Coop sends extension values at most once every 2 s, merged (the last value wins), and other members read them a beat
// or more later: nothing here assumes a published value is visible at once. A member's line being replaced part by part
// reads as waiting; its last complete line stands meanwhile. A member's readiness of another set, or none yet, reads as
// "checking" until the current one arrives.
// <state>: r ready, g generating (or about to), f the generator failed, n no generator installed, w no set to check yet.
#pragma once
#include "coop_member_attribute_api.h"
#include "host_settings_logic.h"
#include "hudtext.h"
#include "player_vehicle_logic.h"
#include "room_sync.h"
#include "support_loadout.h"
#include "support_variants.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <initializer_list>
#include <memory>
#include <new>

namespace crew::roomsync {

inline constexpr char kKeyConfig[]="EDF6X_VCCFG";
inline constexpr char kKeyHave[]="EDF6X_VCHAVE";
inline constexpr int kConfigParts=4;                  // EDF6X_VCCFG + EDF6X_VCCFG1..3
inline constexpr std::size_t kValueMost=EDF6COOP_MEMBER_ATTRIBUTE_MAX_VALUE;
inline constexpr int kSetMost=512;                    // the room's set; excess is said
static_assert(kSetMost<=kHostVariantsMost,"the mission preload holds the room's whole set (support_variants.h)");
// A machine generates at most this many files a room for other members' configurations (each one a name a validated
// line gave): no member can make another write more than this per room, whatever it publishes or how often it changes
// it. The rest come stock.
inline constexpr int kNotesPerRoom=128;
inline constexpr int kMembersMost=64;                 // Coop lists at most Epic's 64
inline constexpr std::uint64_t kTickMs=1000;
inline constexpr std::uint64_t kStartGapMs=5000;      // between two generator starts
inline constexpr int kAttemptsMost=2;                 // per missing set
inline constexpr int kLinesMost=8;
inline constexpr std::size_t kHaveMost=40;            // "1;<16 hex>;<up to 4 digits>;<c>": 24 bytes, with room
// Configuration parts share Coop's budget with readiness, the player's stores, and the room word.
inline constexpr std::size_t kConfigBudget=EDF6COOP_MEMBER_ATTRIBUTE_MAX_TOTAL_VALUE-kHaveMost-
                                           playerveh::kValueMost-hostcfg::kWordMost;
static_assert(kConfigBudget+kHaveMost+playerveh::kValueMost+hostcfg::kWordMost<=EDF6COOP_MEMBER_ATTRIBUTE_MAX_TOTAL_VALUE,
              "the configuration, readiness, own player loadouts and the room word fit Coop's budget");
// "1;" + 16 + ";" + 1 digit + ";": 21 bytes; a further part's "<16 hex>;": 17.
inline constexpr std::size_t kConfigHeader=21,kPartHeader=17;
// The longest line: the budget less every header.
inline constexpr std::size_t kConfigLineMost=kConfigBudget-kConfigHeader-(kConfigParts-1)*kPartHeader;
static_assert(kConfigLineMost<=(kValueMost-kConfigHeader)+(kConfigParts-1)*(kValueMost-kPartHeader),"the parts can carry it");
inline constexpr int kPlayerSetMost=kMembersMost*kPlayerVehicleCount;   // every other member's player loadouts
static_assert(kPlayerSetMost<=kSetMost,"a VariantSet holds them");

// The part key i (0 = kKeyConfig).
inline void PartKey(int i,char* out,std::size_t capacity) noexcept {
    if(i<=0)std::snprintf(out,capacity,"%s",kKeyConfig);
    else std::snprintf(out,capacity,"%s%d",kKeyConfig,i);
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
        if(*s==L'_') {   // the backpack weapon's WEAPONTABLE name
            const wchar_t* a=++s;
            while((*s>=L'A' && *s<=L'Z') || (*s>=L'0' && *s<=L'9'))++s;
            const PackWeapon* pack=PackNamed(a,s);
            if(!pack)return false;
            look.pack=pack->code;
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
        if(look.pack){t[n++]='+';t[n++]=hex[look.pack>>4];t[n++]=hex[look.pack&0xF];}
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
    if(s[0]>='1' && s[0]<='0'+kVehicleBodyLast) {   // '1' tank .. '5' multirole, '6'..':' a player's own (support_loadout.h)
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
        if(i+3==n && s[i]=='+') {
            const int hi=hex(s[i+1]),lo=hex(s[i+2]);
            if(hi<0 || lo<0)return false;
            look.pack=static_cast<std::uint8_t>(hi*16+lo);i+=3;
            if(!PackOf(look.pack))return false;
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
inline bool SameSet(const VariantSet& a,const VariantSet& b) noexcept {
    if(a.count!=b.count)return false;
    for(int i=0;i<a.count;++i)if(!(a.ref[i]==b.ref[i]))return false;
    return true;
}
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
inline bool Printable(const char* s,std::size_t n) noexcept {
    for(std::size_t i=0;i<n;++i)if(s[i]<0x20 || s[i]>0x7E)return false;
    return true;
}

// --- A member's configuration line: encode / decode ---
struct ConfigValues {
    int parts=0;                       // values written (1..kConfigParts); keys PartKey(0..parts-1)
    std::uint64_t hash=0;              // Fnv of the whole line
    char value[kConfigParts][kValueMost+1]{};
};
// `line` (printable ASCII, at most kConfigLineMost bytes: Env::ownConfig is given that room) into its values. False for
// a line that is not one (its values left empty).
inline bool EncodeConfig(const char* line,ConfigValues* out) noexcept {
    ConfigValues& v=*out;
    v=ConfigValues{};
    const std::size_t n=line ? std::strlen(line) : 0;
    if(n>kConfigLineMost || (n && !Printable(line,n)))return false;
    v.hash=Fnv(kFnvBasis,line ? line : "",n);
    char hash[17];std::snprintf(hash,sizeof(hash),"%016llX",static_cast<unsigned long long>(v.hash));
    const std::size_t first=kValueMost-kConfigHeader,rest=kValueMost-kPartHeader;
    v.parts=n<=first ? 1 : 1+static_cast<int>((n-first+rest-1)/rest);
    if(v.parts>kConfigParts){v=ConfigValues{};return false;}
    std::size_t at=0;
    for(int p=0;p<v.parts;++p) {
        const std::size_t head=p ? kPartHeader : kConfigHeader;
        std::size_t take=n-at;
        if(take>kValueMost-head)take=kValueMost-head;
        if(p)std::snprintf(v.value[p],sizeof(v.value[p]),"%s;",hash);
        else std::snprintf(v.value[p],sizeof(v.value[p]),"1;%s;%d;",hash,v.parts);
        const std::size_t w=std::strlen(v.value[p]);
        std::memcpy(v.value[p]+w,line+at,take);v.value[p][w+take]=0;
        at+=take;
    }
    return at==n;
}

enum class ListState : std::uint8_t { none, waiting, bad, ok };
// A member's values (value[i] of PartKey(i), nullptr where it has none) back into its line (out: kConfigLineMost+1
// bytes): ok only when every part is there, of the same line, the bytes printable and the hash as the header says;
// waiting while a part is missing or still another line's; bad for anything else. `out` is empty unless ok.
inline ListState DecodeConfig(const char* const* value,int have,char* out,std::uint64_t* hash) noexcept {
    out[0]=0;*hash=0;
    if(have<1 || !value[0])return ListState::none;
    const char* s=value[0];
    std::uint64_t h=0;int parts=0;
    if(std::strncmp(s,"1;",2) || std::strlen(s)<kConfigHeader || !ParseHash(s+2,&h) || s[18]!=';')return ListState::bad;
    s+=19;
    if(!ParseCount(s,kConfigParts,&parts) || *s++!=';' || parts<1)return ListState::bad;
    std::size_t used=0;
    const auto add=[&](const char* text) noexcept {
        const std::size_t n=std::strlen(text);
        if(used+n>kConfigLineMost || !Printable(text,n))return false;
        std::memcpy(out+used,text,n);used+=n;out[used]=0;
        return true;
    };
    if(!add(s)){out[0]=0;return ListState::bad;}
    for(int p=1;p<parts;++p) {
        if(p>=have || !value[p]){out[0]=0;return ListState::waiting;}
        std::uint64_t other=0;
        if(std::strlen(value[p])<kPartHeader || !ParseHash(value[p],&other) || value[p][16]!=';'){out[0]=0;return ListState::bad;}
        if(other!=h){out[0]=0;return ListState::waiting;}   // another line's part: the rest is on its way
        if(!add(value[p]+kPartHeader)){out[0]=0;return ListState::bad;}
    }
    if(Fnv(kFnvBasis,out,used)!=h){out[0]=0;return ListState::bad;}
    *hash=h;
    return ListState::ok;
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
    std::uint32_t refused=0;        // a publish Coop refused this tick: its lastError (0 when it has none to give)
    bool anyRefused=false;
    int members=0;                  // the room's, this machine included
    ListState list=ListState::none; // the room's set: ok, or waiting while a member's configuration is on its way
    int listed=0,leftOut=0;         // variants in the room's set / that did not fit it
    int configLeftOut=0;            // this machine's configuration entries that did not fit its values
    int badCount=0;                 // members whose configuration does not read (their calls come with the defaults)
    std::uint32_t bad[kMembersMost]{};   // their ordinals
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
// The host's mission start (room_pvp.cpp StartGate) against the room's files: go; held while they are on their way (a
// member's loadout, this PC's or a member's generation, a member re-checking the list); failed (held too, the lines say
// why) while this PC's or a member's generation failed / has no generator, a member has no pre-sync or the room's set
// does not read. Not held for what the lines already say comes stock / with the defaults: this PC's files over the
// room's cap (roomSyncSelfCapped), a member's loadout that does not read (roomSyncMemberConfigBad).
enum class StartHold : std::uint8_t { go, waiting, failed };
inline StartHold HoldOf(const Situation& s) noexcept {
    if(StateOf(s)!=SyncState::waiting)return StartHold::go;   // no Coop, alone, every member ready
    bool waiting=false,failed=false;
    if(s.list==ListState::bad)failed=true;
    else if(s.list!=ListState::ok)waiting=true;
    else switch(s.own) {
        case Ready::ready:break;
        case Ready::failed:if(s.ownMissing>s.ownCapped)failed=true;break;   // only the capped missing: they come stock
        case Ready::noGenerator:failed=true;break;
        case Ready::generating:case Ready::noList:waiting=true;break;
    }
    for(int i=0;i<s.others;++i) {
        const MemberReady& m=s.other[i];
        switch(m.status) {
            case MemberStatus::ready:break;
            case MemberStatus::none:(s.list==ListState::ok ? failed : waiting)=true;break;   // (just joined: nothing yet)
            case MemberStatus::stale:waiting=true;break;
            case MemberStatus::missing:
                if(m.state==Ready::failed || m.state==Ready::noGenerator)failed=true;else waiting=true;
                break;
        }
    }
    return failed ? StartHold::failed : waiting ? StartHold::waiting : StartHold::go;
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
    if(s.anyRefused)put(Tx::roomSyncPublishRefused,true,static_cast<int>(s.refused));
    if(s.configLeftOut)put(Tx::roomSyncConfigLeftOut,true,s.configLeftOut);
    for(int i=0;i<s.badCount;++i)put(Tx::roomSyncMemberConfigBad,true,static_cast<int>(s.bad[i])+1);
    if(s.list==ListState::waiting)put(Tx::roomSyncConfigWaiting,false);
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
        case hudtext::Tx::roomSyncConfigWaiting:_snwprintf_s(out->text,_TRUNCATE,L"%ls",format);break;
        default:_snwprintf_s(out->text,_TRUNCATE,format,l.a);break;
    }
    out->warn=l.warn;
}

// --- The sync itself, one per process (src/room_sync.cpp) or per fake member (the test) ---
struct Env {
    void* context=nullptr;
    // Mods/OBJECT/<file> there.
    bool (*fileThere)(void* context,const wchar_t* file)=nullptr;
    // A file another member's configuration wants and this machine lacks: support_variants.h NoteMissingVariant.
    void (*noteMissing)(void* context,const wchar_t* file)=nullptr;
    // loadout_store.h StartVariantGeneration / VariantGenerationStatus.
    bool (*startGeneration)(void* context)=nullptr;
    GenStatus (*generation)(void* context)=nullptr;
    // This machine's own files: every one its configuration can need (support_variants.h ForEachConfiguredVariant).
    void (*configured)(void* context,void (*each)(void* set,const wchar_t* file),void* set)=nullptr;
    // This machine's configuration as its line (member_config.h Encode) into out (capacity: kConfigLineMost+1); its
    // length; *leftOut: the entries that did not fit.
    std::size_t (*ownConfig)(void* context,char* out,std::size_t capacity,int* leftOut)=nullptr;
    // Another member's line read (member_config.h Decode): each file its configuration can need; false when the line
    // does not read (nothing given then).
    bool (*memberVariants)(void* context,const char* line,void (*each)(void* set,const wchar_t* file),void* set)=nullptr;
    // This machine's own player loadouts as its EDF6X_VCPVEH value (player_vehicle_logic.h OwnValue); false: none.
    bool (*ownPlayer)(void* context,char* value,std::size_t capacity)=nullptr;
    void (*log)(void* context,const char* text)=nullptr;
};

// Another member's configuration line as the last room published it, for the host's plans (room_sync.h
// RoomMemberConfig) and the mission's preload.
struct MemberLine {
    char id[EDF6COOP_MEMBER_ID_SIZE]{};
    std::uint32_t ordinal=0;
    bool ok=false;                  // read: `line` is its configuration
    std::uint64_t hash=0;
    char line[kConfigLineMost+1]{};
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
        EDF6CoopRoomState room{};room.size=sizeof(room);   // at least EDF6COOP_ROOM_STATE_V1_SIZE; Coop writes what it knows
        s.inRoom=api->room(&room) && room.inRoom;
        if(!s.inRoom) {
            if(anyPublished_)Leave(api);
            lines_=SelectLines(s,line_,kLinesMost);
            return;
        }
        s.isHost=room.isHost!=0;s.members=static_cast<int>(room.memberCount);
        PlayerLoadouts(api,env);
        OwnConfig(api,env);
        Configs(api,env);
        s.list=list_;s.listed=target_.count;s.leftOut=leftOut_;s.configLeftOut=configLeftOut_;
        // The set the next mission preloads: the room's, once it is complete.
        if(list_==ListState::ok){roomList_=target_;roomListHash_=listHash_;roomListOk_=true;}
        Readiness(api,now,env);
        ReadMembers(api);
        lines_=SelectLines(s,line_,kLinesMost);
    }
    // The room is gone: every key this machine published withdrawn, the state forgotten (not RoomList / the members'
    // lines: the mission starting from that room needs them).
    void Leave(const EDF6CoopMemberAttributeApi* api) noexcept {
        if(api) {
            for(int p=0;p<kConfigParts;++p)if(configPublished_[p][0]){char key[33];PartKey(p,key,sizeof(key));api->clear(key);}
            if(havePublished_[0])api->clear(kKeyHave);
            if(playerPublished_[0])api->clear(playerveh::kKey);
        }
        Reset();
    }
    // The room's set last complete in a room (every member's configuration's files, this machine's own included): the
    // mission that starts from that room preloads its files (support_variants.h PreloadSupportVariants) and tells the
    // host it has exactly those (its hello's filter). Kept when the room is left (a mission starting leaves it);
    // replaced by the next complete tick in a room, dropped by ForgetRoomList.
    const VariantSet& RoomList() const noexcept { return roomList_; }
    std::uint64_t RoomListHash() const noexcept { return roomListOk_ ? roomListHash_ : 0; }
    bool HasRoomList() const noexcept { return roomListOk_; }
    void ForgetRoomList() noexcept {
        roomListOk_=false;roomListHash_=0;roomList_.count=0;
        if(lines()){for(int i=0;i<kMembersMost;++i)members_[i]=MemberLine{};}
        ++membersVersion_;
    }
    // The other members' lines as the last room published them (kept when the room is left, as RoomList), copied into
    // out[0..capacity) (the rest of out cleared): room_sync.cpp keeps them for the host's plans.
    void CopyMembers(MemberLine* out,int capacity) const noexcept {
        for(int i=0;out && i<capacity;++i)out[i]=members_ && i<kMembersMost ? members_[i] : MemberLine{};
    }
    // Bumped each time the members' lines change (0: never set): room_sync.cpp copies them for other threads only then.
    std::uint64_t MembersVersion() const noexcept { return membersVersion_; }
    SyncState State() const noexcept { return StateOf(situation_); }
    StartHold Hold() const noexcept { return HoldOf(situation_); }
    // The next Tick runs whatever the time (a preset switched in: the start gate reads the new set's state at once).
    void Nudge() noexcept { ticked_=false; }
    int Lines(SyncLine* out,int capacity,hudtext::Lang lang) const noexcept {
        int n=0;
        for(;out && n<capacity && n<lines_;++n)FormatLine(line_[n],lang,&out[n]);
        return n;
    }
    const Situation& Now() const noexcept { return situation_; }
    // Every other member's player loadouts the last room published (validated; kept when the room is left, as RoomList):
    // the mission preloads their files (player_vehicle.cpp).
    const VariantSet& MemberPlayerVehicles() const noexcept { return memberPlayers_; }
    // Bumped each time MemberPlayerVehicles() changes (0: never set): room_sync.cpp copies it for other threads only then,
    // not every room frame.
    std::uint64_t MemberPlayersVersion() const noexcept { return memberPlayersVersion_; }
    const VariantSet& Target() const noexcept { return target_; }
private:
    // Everything a room leaves behind, but the tick clock and the situation the caller is filling.
    void Reset() noexcept {
        anyPublished_=false;listHash_=0;loggedHash_=0;loggedOwn_=0;list_=ListState::none;leftOut_=0;configLeftOut_=0;
        unnamed_=0;cappedLogged_=0;refusedLogged_=false;refusedCode_=0;badLogged_=0;
        own_.count=0;target_.count=0;noted_.count=0;
        plan_=GenPlan{};
        for(auto& v:configPublished_)v[0]=0;
        havePublished_[0]=0;genMessage_[0]=0;playerPublished_[0]=0;players_.count=0;
    }
    void Log(const Env& env,const char* text) noexcept { if(env.log)env.log(env.context,text); }
    // The members' lines, on the heap the first time (64 of them, ~240 KB: not on a test's stack).
    MemberLine* lines() noexcept {
        if(!members_)members_.reset(new(std::nothrow) MemberLine[kMembersMost]);
        return members_.get();
    }
    // Coop keeps a value published before its lobby glue runs and sends it later (1); 0 is a refusal (lastError says
    // why: the budget, the key limit...): said on a line, logged once per new reason, tried again next tick.
    bool Publish(const EDF6CoopMemberAttributeApi* api,const char* key,const char* value,char* cache,const Env& env) noexcept {
        if(!std::strcmp(cache,value))return true;
        if(!api->publish(key,value)) {
            const std::uint32_t code=EDF6COOP_MA_HAS(api,lastError) && api->lastError ? api->lastError() : 0;
            situation_.anyRefused=true;situation_.refused=code;
            if(!refusedLogged_ || refusedCode_!=code) {
                refusedLogged_=true;refusedCode_=code;
                char text[128];
                std::snprintf(text,sizeof(text),"ROOMSYNC EDF6Coop refused %s (%u bytes): error %u",key,
                              static_cast<unsigned>(std::strlen(value)),static_cast<unsigned>(code));
                Log(env,text);
            }
            return false;
        }
        std::snprintf(cache,kValueMost+1,"%s",value);anyPublished_=true;
        return true;
    }
    void Withdraw(const EDF6CoopMemberAttributeApi* api,int part) noexcept {
        if(!configPublished_[part][0])return;
        char key[33];PartKey(part,key,sizeof(key));
        if(api->clear(key))configPublished_[part][0]=0;
    }
    // A file of a configuration into a set (`own_` or `others_`): a name it cannot be is counted, never kept.
    struct Collect { Engine* self; VariantSet* set; int full; };
    static void AddFile(void* context,const wchar_t* file) noexcept {
        auto* c=static_cast<Collect*>(context);
        VariantRef r;
        if(!ParseVariantFile(file,&r)){++c->self->unnamed_;return;}
        if(!c->set->Add(r))++c->full;
    }
    // This machine's configuration line published (Env::ownConfig), the parts it no longer needs withdrawn first (their
    // bytes leave Coop's budget).
    void OwnConfig(const EDF6CoopMemberAttributeApi* api,const Env& env) noexcept {
        static char line[kConfigLineMost+1];   // UI thread only (src/room_sync.h); the test ticks one engine at a time
        line[0]=0;int leftOut=0;
        if(env.ownConfig)env.ownConfig(env.context,line,sizeof(line),&leftOut);
        line[kConfigLineMost]=0;
        if(!EncodeConfig(line,&values_)){line[0]=0;++leftOut;EncodeConfig(line,&values_);}
        configLeftOut_=leftOut;
        for(int p=values_.parts;p<kConfigParts;++p)Withdraw(api,p);
        // The further parts first, the header last: a reader that sees the new header finds its parts already there
        // (or reads waiting, never a mix).
        for(int p=values_.parts-1;p>=0;--p) {
            char key[33];PartKey(p,key,sizeof(key));
            Publish(api,key,values_.value[p],configPublished_[p],env);
        }
        if(values_.hash!=loggedOwn_) {
            loggedOwn_=values_.hash;
            char text[160];
            std::snprintf(text,sizeof(text),"ROOMSYNC own configuration %016llX: %u bytes in %d value(s), %d entries left out",
                          static_cast<unsigned long long>(values_.hash),static_cast<unsigned>(std::strlen(line)),values_.parts,leftOut);
            Log(env,text);
        }
    }
    // Every member's parts, by member, as Coop has them now.
    struct Parts {
        char id[EDF6COOP_MEMBER_ID_SIZE];
        std::uint32_t ordinal;
        bool self,has[kConfigParts];
        char value[kConfigParts][kValueMost+1];
    };
    struct PartsTable { int count=0;int part=0;Parts row[kMembersMost]; };
    static void TakePart(void* context,const EDF6CoopMemberValue* m) noexcept {
        auto* t=static_cast<PartsTable*>(context);
        if(!m || !m->memberId)return;
        Parts* row=nullptr;
        for(int i=0;i<t->count;++i)if(!std::strcmp(t->row[i].id,m->memberId))row=&t->row[i];
        if(!row) {
            if(t->count>=kMembersMost)return;
            row=&t->row[t->count++];
            std::snprintf(row->id,sizeof(row->id),"%s",m->memberId);
            row->ordinal=m->ordinal;row->self=m->isSelf!=0;
            for(bool& h:row->has)h=false;
        }
        if(m->hasValue && m->value && m->valueBytes<=kValueMost) {
            std::snprintf(row->value[t->part],sizeof(row->value[t->part]),"%s",m->value);row->has[t->part]=true;
        }
    }
    // Every other member's line read; the room's set: this machine's own files and every readable line's.
    void Configs(const EDF6CoopMemberAttributeApi* api,const Env& env) noexcept {
        Situation& s=situation_;
        MemberLine* kept=lines();
        static PartsTable table;   // UI thread only, as OwnConfig's line
        table.count=0;
        for(int p=0;p<kConfigParts;++p) {
            char key[33];PartKey(p,key,sizeof(key));
            table.part=p;
            api->snapshot(key,&Engine::TakePart,&table,nullptr);
        }
        own_.count=0;others_.count=0;unnamed_=0;
        Collect ownSet{this,&own_,0},otherSet{this,&others_,0};
        if(env.configured)env.configured(env.context,&Engine::AddFile,&ownSet);
        bool waiting=false,changed=false;
        static MemberLine next[kMembersMost];   // UI thread only
        int nextCount=0;
        static char line[kConfigLineMost+1];
        for(int i=0;i<table.count;++i) {
            const Parts& row=table.row[i];
            if(row.self)continue;
            const char* value[kConfigParts]{};
            for(int p=0;p<kConfigParts;++p)value[p]=row.has[p] ? row.value[p] : nullptr;
            std::uint64_t hash=0;
            ListState state=DecodeConfig(value,kConfigParts,line,&hash);
            const MemberLine* before=nullptr;
            for(int k=0;kept && k<kMembersMost;++k)if(kept[k].id[0] && !std::strcmp(kept[k].id,row.id))before=&kept[k];
            MemberLine m;
            std::snprintf(m.id,sizeof(m.id),"%s",row.id);m.ordinal=row.ordinal;
            if(state==ListState::waiting && before && before->ok) {
                m=*before;state=ListState::ok;   // its last complete line stands meanwhile
            } else if(state==ListState::ok) {
                m.hash=hash;
                std::snprintf(m.line,sizeof(m.line),"%s",line);
            }
            // Read (a refused line gives nothing): its files join the room's set.
            if(state==ListState::ok)m.ok=env.memberVariants && env.memberVariants(env.context,m.line,&Engine::AddFile,&otherSet);
            if(state==ListState::waiting)waiting=true;
            if(((state==ListState::ok && !m.ok) || state==ListState::bad) && s.badCount<kMembersMost)s.bad[s.badCount++]=row.ordinal;
            if(!before || before->ok!=m.ok || before->hash!=m.hash)changed=true;
            if(nextCount<kMembersMost)next[nextCount++]=m;
        }
        // Members gone from the room are forgotten with it (the next mission's room is this one's members).
        int keptCount=0;
        for(int k=0;kept && k<kMembersMost;++k)keptCount+=kept[k].id[0]!=0;
        if(keptCount!=nextCount)changed=true;
        if(kept && changed) {
            for(int k=0;k<kMembersMost;++k)kept[k]=k<nextCount ? next[k] : MemberLine{};
            ++membersVersion_;
        }
        if(s.badCount && s.badCount!=badLogged_) {
            badLogged_=s.badCount;
            char text[96];std::snprintf(text,sizeof(text),"ROOMSYNC %d member(s)' configuration does not read: their calls use the defaults",s.badCount);
            Log(env,text);
        }
        // The room's set: own first (its files are this machine's own ini's), then the others'.
        VariantSet next2;
        int full=ownSet.full+otherSet.full;
        for(int i=0;i<own_.count;++i)if(!next2.Add(own_.ref[i]))++full;
        for(int i=0;i<others_.count;++i)if(!next2.Add(others_.ref[i]))++full;
        // A line being replaced part by part, with none complete before: wait for it, keep the last set meanwhile.
        if(waiting && list_==ListState::ok)return;
        list_=waiting ? ListState::waiting : ListState::ok;
        if(list_!=ListState::ok){target_.count=0;listHash_=0;leftOut_=0;return;}
        target_=next2;leftOut_=full;listHash_=ListHash(target_,target_.count);
        if(listHash_!=loggedHash_) {
            loggedHash_=listHash_;
            char text[160];
            std::snprintf(text,sizeof(text),"ROOMSYNC room set %016llX: %d variant files (%d own, %d of %d other member(s)), %d left out, %d unnamed",
                          static_cast<unsigned long long>(listHash_),target_.count,own_.count,others_.count,nextCount,leftOut_,unnamed_);
            Log(env,text);
        }
    }
    // This machine's own player loadouts published (withdrawn when there are none); every other member's read and
    // validated (a member's bad value is skipped whole, never half-read) into players_.
    static void TakePlayer(void* context,const EDF6CoopMemberValue* m) noexcept {
        auto* self=static_cast<Engine*>(context);
        if(!m || m->isSelf || !m->hasValue || !m->value || m->valueBytes>playerveh::kValueMost)return;
        playerveh::Variant v[kPlayerVehicleCount];int n=0;
        if(!playerveh::ParseValue(m->value,v,&n)){++self->playersBad_;return;}
        for(int i=0;i<n;++i){VariantRef r;r.vehicle=true;r.sub=static_cast<std::uint8_t>(v[i].body);r.bits=v[i].bits;self->players_.Add(r);}
    }
    void PlayerLoadouts(const EDF6CoopMemberAttributeApi* api,const Env& env) noexcept {
        char value[playerveh::kValueMost+1]{};
        if(env.ownPlayer && env.ownPlayer(env.context,value,sizeof(value)))Publish(api,playerveh::kKey,value,playerPublished_,env);
        else if(playerPublished_[0] && api->clear(playerveh::kKey))playerPublished_[0]=0;
        players_.count=0;playersBad_=0;
        api->snapshot(playerveh::kKey,&Engine::TakePlayer,this,nullptr);
        if(!SameSet(memberPlayers_,players_)){memberPlayers_=players_;++memberPlayersVersion_;}
        if(playersBad_ && !playersBadLogged_) {
            playersBadLogged_=true;
            char text[96];std::snprintf(text,sizeof(text),"ROOMSYNC %d member(s)' player loadouts do not read: ignored",playersBad_);
            Log(env,text);
        }
    }
    void Readiness(const EDF6CoopMemberAttributeApi* api,std::uint64_t now,const Env& env) noexcept {
        Situation& s=situation_;
        Have have;
        if(list_==ListState::ok) {
            // `fixable`: the missing files the generator can make (this machine's own ini names its own; the others' go
            // on its pending list: at most kNotesPerRoom a room); the rest (`capped`) stay missing, stock.
            int missing=0,capped=0;std::uint64_t missingHash=kFnvBasis;
            // The room's set, then every other member's player loadouts (EDF6X_VCPVEH): their files are generated the
            // same way (pending list, kNotesPerRoom), but only the room set's count in this machine's readiness.
            int listMissing=0;
            for(int i=0;i<target_.count+players_.count;++i) {
                const VariantRef& ref=i<target_.count ? target_.ref[i] : players_.ref[i-target_.count];
                if(i>=target_.count && target_.Has(ref))continue;
                wchar_t file[96];
                if(!VariantFileOf(ref,file,96))continue;
                if(env.fileThere && env.fileThere(env.context,file))continue;
                ++missing;
                if(i<target_.count)++listMissing;
                // This machine's own configuration is its own ini: the generator reads that. Another member's goes on
                // the pending list (the generator reads it too, and the installer's menus as before), never more than
                // kNotesPerRoom a room.
                if(!own_.Has(ref) && !noted_.Has(ref)) {
                    if(noted_.count>=kNotesPerRoom){++capped;continue;}
                    noted_.Add(ref);
                    if(env.noteMissing)env.noteMissing(env.context,file);
                }
                char t[24];missingHash=Fnv(Fnv(missingHash,",",1),t,TokenOf(ref,t,sizeof(t)));
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
            have.hash=listHash_;have.missing=listMissing;
            have.state=!listMissing ? Ready::ready : fixable>0 ? OwnReady(plan_,missingHash,fixable,after.state) : Ready::failed;
            s.ownCapped=capped;
            std::memcpy(genMessage_,after.message,sizeof(genMessage_));
            genMessage_[sizeof(genMessage_)/sizeof(genMessage_[0])-1]=0;
        }
        s.ownMissing=have.missing;s.own=have.state;s.genMessage=genMessage_;
        char value[kHaveMost+8];EncodeHave(have,value,sizeof(value));
        Publish(api,kKeyHave,value,havePublished_,env);
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
    std::uint64_t lastTick_=0,listHash_=0,loggedHash_=0,loggedOwn_=0;
    ListState list_=ListState::none;
    int leftOut_=0,configLeftOut_=0,unnamed_=0,cappedLogged_=0,badLogged_=0;
    bool refusedLogged_=false;
    std::uint32_t refusedCode_=0;
    VariantSet own_,others_,target_,noted_;
    VariantSet roomList_;               // RoomList(): not cleared by Reset (a mission start leaves the room)
    std::uint64_t roomListHash_=0;
    bool roomListOk_=false;
    std::unique_ptr<MemberLine[]> members_;   // the other members' lines (kept, as roomList_)
    std::uint64_t membersVersion_=0;
    VariantSet players_,memberPlayers_; // the other members' player loadouts: this tick's, and the last room's (kept)
    std::uint64_t memberPlayersVersion_=0;
    int playersBad_=0;bool playersBadLogged_=false;
    char playerPublished_[playerveh::kValueMost+1]{};
    ConfigValues values_;
    GenPlan plan_;
    char configPublished_[kConfigParts][kValueMost+1]{};
    char havePublished_[kValueMost+1]{};
    wchar_t genMessage_[200]{};
    Situation situation_;
    LineSpec line_[kLinesMost];
    int lines_=0;
};

}  // namespace crew::roomsync
