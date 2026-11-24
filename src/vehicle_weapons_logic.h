#pragma once
// The weapon SGOs a vehicle's SGO names (its vehicle_setup / mission_setup `app:/weapon/....sgo` strings), which the
// game's resource preload of the vehicle does not take with it: the plugin queues them with their vehicle
// (vehicle_weapons.h), so the vehicle is made without reading them on the game thread (docs/feature-2026-11-24-
// preload-set.md; the SYNC LOAD probe saw 41 of them, 4.1 s, in one test range run). No game state here: the tests'.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <string_view>

namespace crew::vehicle_weapons {

// A weapon file's name at most (the game's own are under 64 characters).
inline constexpr std::size_t kPathMost=128;

constexpr wchar_t Lower(wchar_t c) noexcept { return c>=L'A' && c<=L'Z' ? static_cast<wchar_t>(c-L'A'+L'a') : c; }
constexpr bool Same(std::wstring_view a,std::wstring_view b) noexcept {
    if(a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i)if(Lower(a[i])!=Lower(b[i]))return false;
    return true;
}
constexpr bool StartsWith(std::wstring_view s,std::wstring_view head) noexcept {
    return s.size()>=head.size() && Same(s.substr(0,head.size()),head);
}
constexpr bool EndsWith(std::wstring_view s,std::wstring_view tail) noexcept {
    return s.size()>=tail.size() && Same(s.substr(s.size()-tail.size()),tail);
}

inline constexpr std::wstring_view kObjectDir=L"app:/object/",kWeaponDir=L"app:/weapon/",kSgo=L".sgo";

// An object path the plugin preloads (app:/object/NAME.sgo, any case) -> NAME; empty when it is not one.
constexpr std::wstring_view ObjectName(std::wstring_view path) noexcept {
    if(!StartsWith(path,kObjectDir) || !EndsWith(path,kSgo) || path.size()<=kObjectDir.size()+kSgo.size())return {};
    return path.substr(kObjectDir.size(),path.size()-kObjectDir.size()-kSgo.size());
}
constexpr bool WeaponPath(std::wstring_view s) noexcept {
    return s.size()<kPathMost && s.size()>kWeaponDir.size()+kSgo.size() && StartsWith(s,kWeaponDir) && EndsWith(s,kSgo);
}

// The stock vehicles' lists (src/vehicle_weapons.inc, tools/gen_vehicle_weapons.py from Root.cpk): a stock SGO is in
// Root.cpk, which the plugin does not read. `weapons`: each NUL-ended, the list ended by an empty one.
struct StockRow { const wchar_t* name; const wchar_t* weapons; };
inline constexpr StockRow kStock[]={
#include "vehicle_weapons.inc"
};
// NAME's row (a stock vehicle the plugin preloads), or null.
inline const StockRow* Stock(std::wstring_view name) noexcept {
    for(const auto& r:kStock)if(Same(name,r.name))return &r;
    return nullptr;
}
// Each weapon of a row's list, to `put(std::wstring_view)`; their count.
template<class Put> int EachListed(const wchar_t* list,Put&& put) {
    int n=0;
    for(const wchar_t* w=list;w && *w;w+=std::wcslen(w)+1,++n)put(std::wstring_view{w});
    return n;
}

// (`put` gets a view of a buffer of its own: copy what is kept.)
// Each `app:/weapon/....sgo` string (NUL-ended UTF-16LE, any case; an SGO's string pool, pylib/sgo.py) of an SGO's
// bytes, to `put(std::wstring_view)`, as often as it is there (the caller drops repeats); their count. A string is
// taken at any even or odd offset: the scan does not rely on the pool's alignment.
template<class Put> int ScanSgo(const unsigned char* data,std::size_t size,Put&& put) {
    if(!data)return 0;
    int n=0;
    const auto at=[&](std::size_t i) noexcept { return static_cast<wchar_t>(data[i]|(data[i+1]<<8)); };
    for(std::size_t i=0;i+2*kWeaponDir.size()<=size;++i) {
        if(Lower(at(i))!=L'a')continue;
        bool head=true;
        for(std::size_t k=0;k<kWeaponDir.size() && head;++k)head=Lower(at(i+2*k))==kWeaponDir[k];
        if(!head)continue;
        wchar_t text[kPathMost];std::size_t len=0;
        std::size_t j=i;
        for(;j+1<size && len<kPathMost;j+=2) {
            const wchar_t c=at(j);
            if(!c)break;
            text[len++]=c;
        }
        if(j+1<size && len<kPathMost && WeaponPath({text,len})){put(std::wstring_view{text,len});++n;i=j+1;}
    }
    return n;
}

// A vehicle NAME's weapons to `put`: from its SGO's bytes when it is a Mods file (`modsSgo`, `size` non-zero: the
// plugin's, the range's, EDF4.1's, the variants'; a Mods copy of a stock vehicle too, as the game loads that), else from
// its stock row. Their count; -1: neither (a stock vehicle tools/gen_vehicle_weapons.py does not know).
template<class Put> int WeaponsOf(std::wstring_view name,const unsigned char* modsSgo,std::size_t size,Put&& put) {
    if(modsSgo && size)return ScanSgo(modsSgo,size,put);
    const StockRow* row=Stock(name);
    return row ? EachListed(row->weapons,put) : -1;
}

// The weapon files queued this mission, each once (any case).
struct Queued {
    static constexpr int kMost=512;
    wchar_t path[kMost][kPathMost]{};
    int count=0;
    bool Has(std::wstring_view w) const noexcept {
        for(int i=0;i<count;++i)if(Same(w,path[i]))return true;
        return false;
    }
    // Remembers `w` (false: already there). Past kMost it is not remembered: queued again, which the game's own
    // preload takes as a repeat.
    bool Add(std::wstring_view w) noexcept {
        if(w.size()>=kPathMost || Has(w))return false;
        if(count<kMost){for(std::size_t i=0;i<w.size();++i)path[count][i]=w[i];path[count][w.size()]=0;++count;}
        return true;
    }
};

}  // namespace crew::vehicle_weapons
