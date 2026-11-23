// A room member's support loadout as one line of printable ASCII (the room pre-sync's EDF6X_VCCFG value, room_sync_logic.h),
// and back. In an online room every member's own configuration (EDF6VehicleCrew.ini [VehicleCrew], edited in the room's
// loadout menu) applies to the supports that member calls (2026-11-21, the player: 「要能在房间里面用界面调npc、挂载等」):
// each member publishes it, every machine generates the variant files of every member's configuration, and the host plans
// a member's request with that member's configuration (support_dispatch.cpp PlanForRequester). Free of the game and of
// Windows but for ParseSupportConfig (support_config.cpp); tests/member_config_test.cpp runs it offline.
//
// The line: entries joined by '|', each "<code>=<value>", only the settings that differ from the defaults, in this order:
//   Q / L / T / D / R   SupportSquadWeapon / SupportSquadLeaderWeapon / SupportPlatoonWeapons / SupportVehicleCrewWeapon /
//                       SupportAircraftCrewWeapon (the ini's English weapon names; T three of them, ',' between)
//   N<i>                SupportAircraftCount_<key of catalog entry i> (1..kSupportAircraftMost)
//   P<i>                SupportPreset_<key>   (room_menu.h SerialisePreset: the ini's own spelling)
//   V<i>                SupportVehicle_<key>  (SerialiseVehicle; only an entry whose body the key names)
//   C                   SupportCrewColour (every crew's default), M SupportCrewColour_MISSION, C<i> SupportCrewColour_<key>
// <i> is the catalog index in decimal (no leading zero): every member of a room runs the same build (mod_room.h's room
// protocol), so the indexes agree. Never in it: SupportDisabled (whether a support may be called is the host's rule, as
// before) and SupportVehicle_OWN_* (a player's own summons travel in EDF6X_VCPVEH, player_vehicle_logic.h).
// A reader takes a line only when every entry is a known code, none twice, ParseSupportConfig (the ini's own parser,
// with the entries' seats) reports no problem, and the configuration it gives encodes back to the very same line: anything
// else is refused whole (that member's calls come with the defaults). Entries past the budget are left out whole, the
// last ones first; what is left still reads back as itself (a prefix of the same order).
#pragma once
#include "room_menu.h"
#include "support_call.h"
#include "support_config.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace crew::memberconfig {

inline constexpr int kEntriesMost=5+4*kSupportConfigUnits+2;   // weapons, N/P/V/C per entry, C and M
inline constexpr std::size_t kEntryMost=640;                    // one entry's bytes (a 15-soldier preset fits)

namespace detail {
// `text` (wide) as printable ASCII into out at `at`; false when a character is not one or it does not fit.
inline bool PutAscii(char* out,std::size_t capacity,std::size_t& at,const wchar_t* text) noexcept {
    for(;*text;++text) {
        if(*text<0x20 || *text>0x7E || *text==L'|' || *text==L'=' || *text==L';' || at+1>=capacity)return false;
        out[at++]=static_cast<char>(*text);
    }
    out[at]=0;
    return true;
}
struct Writer {
    char* out;std::size_t capacity,used=0;
    int written=0,leftOut=0;
    bool full=false;
    // One entry "<code>=<value>": appended whole, or counted as left out (and so is every later one: the line stays a
    // prefix of the full order, which a reader encodes back to itself).
    void Entry(const char* code,const wchar_t* value) noexcept {
        if(!value || !*value)return;
        char entry[kEntryMost];std::size_t n=0;
        const int head=std::snprintf(entry,sizeof(entry),"%s=",code);
        n=head>0 ? static_cast<std::size_t>(head) : 0;
        if(!head || !PutAscii(entry,sizeof(entry),n,value)){++leftOut;return;}   // never written by this build's menus
        const std::size_t need=n+(used ? 1 : 0);
        if(full || used+need+1>capacity){full=true;++leftOut;return;}
        if(used)out[used++]='|';
        std::memcpy(out+used,entry,n);used+=n;out[used]=0;++written;
    }
};
}  // namespace detail

// `c`'s loadout (catalog entries 0..units-1) into out (NUL-terminated, at most capacity-1 bytes). Returns its length;
// *leftOut (may be null) gets the entries that did not fit. An empty line is a configuration of nothing but defaults.
inline std::size_t Encode(const SupportConfig& c,int units,char* out,std::size_t capacity,int* leftOut=nullptr) noexcept {
    if(leftOut)*leftOut=0;
    if(!out || !capacity)return 0;
    out[0]=0;
    if(units>kSupportConfigUnits)units=kSupportConfigUnits;
    const SupportConfig stock{};
    detail::Writer w{out,capacity};
    const auto weapon=[&](const char* code,SupportWeapon now,SupportWeapon def) noexcept {
        if(now!=def)w.Entry(code,SupportWeaponName(now));
    };
    weapon("Q",c.squad,stock.squad);
    weapon("L",c.leader,stock.leader);
    if(c.platoon[0]!=stock.platoon[0] || c.platoon[1]!=stock.platoon[1] || c.platoon[2]!=stock.platoon[2]) {
        wchar_t three[64];
        _snwprintf_s(three,_TRUNCATE,L"%ls,%ls,%ls",SupportWeaponName(c.platoon[0]),SupportWeaponName(c.platoon[1]),
                     SupportWeaponName(c.platoon[2]));
        w.Entry("T",three);
    }
    weapon("D",c.vehicleCrew,stock.vehicleCrew);
    weapon("R",c.aircraftCrew,stock.aircraftCrew);
    char code[8];wchar_t value[kEntryMost];
    for(int i=0;i<units;++i) {
        if(!SupportCallKey(i))continue;
        if(c.aircraft[i]){std::snprintf(code,sizeof(code),"N%d",i);_snwprintf_s(value,_TRUNCATE,L"%d",c.aircraft[i]);w.Entry(code,value);}
    }
    for(int i=0;i<units;++i) {
        if(!SupportCallKey(i) || c.preset[i].count<=0)continue;
        std::snprintf(code,sizeof(code),"P%d",i);
        if(roommenu::SerialisePreset(c.preset[i],value,kEntryMost))w.Entry(code,value);else ++w.leftOut;
    }
    for(int i=0;i<units;++i) {
        const wchar_t* key=SupportCallKey(i);
        const VehicleLoadout& l=c.vehicle[i];
        if(!key || !l.On() || l.body!=VehicleBodyOfKey(key))continue;
        std::snprintf(code,sizeof(code),"V%d",i);
        if(roommenu::SerialiseVehicle(l,value,kEntryMost))w.Entry(code,value);else ++w.leftOut;
    }
    if(c.crewDefault.set && roommenu::SerialiseCrew(c.crewDefault,value,kEntryMost))w.Entry("C",value);
    if(c.crewMission.set && roommenu::SerialiseCrew(c.crewMission,value,kEntryMost))w.Entry("M",value);
    for(int i=0;i<units;++i) {
        if(!SupportCallKey(i) || !c.crew[i].set)continue;
        std::snprintf(code,sizeof(code),"C%d",i);
        if(roommenu::SerialiseCrew(c.crew[i],value,kEntryMost))w.Entry(code,value);
    }
    if(leftOut)*leftOut=w.leftOut;
    return w.used;
}

namespace detail {
struct Entry { char code; int index; const char* value; std::size_t length; };
struct Entries {
    int count=0;
    Entry e[kEntriesMost];
    int units=0;
};
// "<letter>[<index>]" at s (up to '='); false for anything else.
inline bool ParseCode(const char* s,const char* end,int units,char* letter,int* index) noexcept {
    if(s>=end)return false;
    *letter=*s++;*index=-1;
    const bool indexed=*letter=='N' || *letter=='P' || *letter=='V' || *letter=='C';
    const bool plain=*letter=='Q' || *letter=='L' || *letter=='T' || *letter=='D' || *letter=='R' || *letter=='C' || *letter=='M';
    if(s==end)return plain;
    if(!indexed || (*s=='0' && end-s>1))return false;
    int v=0;
    for(;s<end;++s){if(*s<'0' || *s>'9' || v>=1000)return false;v=v*10+(*s-'0');}
    if(v>=units || !SupportCallKey(v))return false;
    *index=v;return true;
}
inline bool Split(const char* text,int units,Entries* out) noexcept {
    out->count=0;out->units=units;
    if(!text || !*text)return true;
    for(const char* s=text;;) {
        const char* end=s;while(*end && *end!='|')++end;
        const char* eq=s;while(eq<end && *eq!='=')++eq;
        if(eq==end || eq+1==end || out->count>=kEntriesMost)return false;
        Entry e{};
        if(!ParseCode(s,eq,units,&e.code,&e.index))return false;
        for(const char* v=eq+1;v<end;++v)if(*v<0x20 || *v>0x7E || *v=='=' || *v==';')return false;
        e.value=eq+1;e.length=static_cast<std::size_t>(end-eq-1);
        if(e.length>=kEntryMost)return false;
        for(int i=0;i<out->count;++i)if(out->e[i].code==e.code && out->e[i].index==e.index)return false;
        out->e[out->count++]=e;
        if(!*end)return true;
        s=end+1;
    }
}
inline bool StartsWith(const wchar_t* s,const wchar_t* prefix,const wchar_t** rest) noexcept {
    const std::size_t n=std::wcslen(prefix);
    if(_wcsnicmp(s,prefix,n))return false;
    *rest=s+n;return true;
}
inline int IndexOfKey(const Entries& es,const wchar_t* key) noexcept {
    for(int i=0;i<es.units;++i)if(const wchar_t* k=SupportCallKey(i);k && !_wcsicmp(k,key))return i;
    return -1;
}
// The entry ParseSupportConfig's `key` reads: its code and index.
inline bool CodeOfKey(const Entries& es,const wchar_t* key,char* code,int* index) noexcept {
    const wchar_t* rest=nullptr;*index=-1;
    if(!_wcsicmp(key,L"SupportSquadWeapon")){*code='Q';return true;}
    if(!_wcsicmp(key,L"SupportSquadLeaderWeapon")){*code='L';return true;}
    if(!_wcsicmp(key,L"SupportPlatoonWeapons")){*code='T';return true;}
    if(!_wcsicmp(key,L"SupportVehicleCrewWeapon")){*code='D';return true;}
    if(!_wcsicmp(key,L"SupportAircraftCrewWeapon")){*code='R';return true;}
    if(!_wcsicmp(key,L"SupportCrewColour")){*code='C';return true;}
    if(!_wcsicmp(key,L"SupportCrewColour_MISSION")){*code='M';return true;}
    char letter=0;
    if(StartsWith(key,L"SupportAircraftCount_",&rest))letter='N';
    else if(StartsWith(key,L"SupportPreset_",&rest))letter='P';
    else if(StartsWith(key,L"SupportVehicle_",&rest))letter='V';
    else if(StartsWith(key,L"SupportCrewColour_",&rest))letter='C';
    else return false;
    const int i=IndexOfKey(es,rest);
    if(i<0)return false;
    *code=letter;*index=i;return true;
}
inline std::size_t Read(void* context,const wchar_t* key,wchar_t* out,std::size_t capacity) noexcept {
    const auto& es=*static_cast<const Entries*>(context);
    if(!out || !capacity)return 0;
    out[0]=0;
    char code=0;int index=-1;
    if(!key || !CodeOfKey(es,key,&code,&index))return 0;
    for(int i=0;i<es.count;++i) {
        const Entry& e=es.e[i];
        if(e.code!=code || e.index!=index)continue;
        if(e.length+1>capacity)return 0;
        for(std::size_t k=0;k<e.length;++k)out[k]=static_cast<wchar_t>(e.value[k]);
        out[e.length]=0;
        return e.length;
    }
    return 0;
}
}  // namespace detail

// A member's line back into a configuration (*out: the defaults on a refusal). `seatsOf`: the entries' seats
// (SupportCallSeats). True only when the line is exactly what Encode writes for what it says.
inline bool Decode(const char* text,int units,SupportSeatsOf seatsOf,SupportConfig* out) noexcept {
    if(!out)return false;
    *out=SupportConfig{};
    if(units>kSupportConfigUnits)units=kSupportConfigUnits;
    static thread_local detail::Entries entries;   // ~10 KB: off the stack
    if(!text || !detail::Split(text,units,&entries))return false;
    SupportConfig c=ParseSupportConfig(&detail::Read,&entries,&SupportCallKey,units,seatsOf);
    if(c.problems[0])return false;
    static thread_local char again[4096];
    const std::size_t n=Encode(c,units,again,sizeof(again));
    if(n!=std::strlen(text) || std::memcmp(again,text,n))return false;
    *out=c;
    return true;
}

}  // namespace crew::memberconfig
