// The mission's variant preload set and the hello's filter (support_variants.h), free of the game and of Windows file
// calls: tests/support_variants_test.cpp drives it offline. support_variants.cpp feeds it the configuration, the room's
// host list and the pending list, and preloads what it names.
#pragma once
#include "room_sync_logic.h"
#include "support_loadout.h"
#include "support_variants.h"
#include <cstddef>
#include <cstring>
#include <cwchar>
#include <cwctype>

namespace crew::variants {

inline constexpr std::size_t kNameMost=96;

// Variant file names (upper case, each one ParseVariantFile accepts: never a path), unique, in the order added.
template<int Most>
struct NameSet {
    int count=0;
    wchar_t name[Most][kNameMost]{};
    bool Has(const wchar_t* file) const noexcept {
        for(int i=0;i<count;++i)if(!_wcsicmp(name[i],file))return true;
        return false;
    }
    // False when it is no variant file name or the set is full (already there: true).
    bool Add(const wchar_t* file) noexcept {
        if(!file)return false;
        wchar_t upper[kNameMost];
        std::size_t n=0;
        for(;file[n] && n+1<kNameMost;++n)upper[n]=static_cast<wchar_t>(std::towupper(file[n]));
        if(file[n])return false;
        upper[n]=0;
        if(!roomsync::ParseVariantFile(upper,nullptr))return false;
        if(Has(upper))return true;
        if(count>=Most)return false;
        std::memcpy(name[count],upper,(n+1)*sizeof(wchar_t));
        ++count;
        return true;
    }
};
using MissionSet=NameSet<kSupportVariantsMost>;
using HostSet=NameSet<kHostVariantsMost>;

// The newest `most` variant names of the pending list's text (UTF-8 / ASCII lines, CRLF or LF; anything that is not a
// variant file name skipped), oldest first, to `each`. Returns how many.
template<class Each>
int PendingNames(const char* text,std::size_t size,int most,Each&& each) {
    if(!text || most<=0)return 0;
    // Walk back over the lines, keeping the newest `most` names' spans, then give them oldest first.
    struct Span { std::size_t at,length; };
    static thread_local Span spans[kPendingPreloadMost];
    if(most>kPendingPreloadMost)most=kPendingPreloadMost;
    int found=0;
    std::size_t end=size;
    while(end>0 && found<most) {
        std::size_t start=end;
        while(start>0 && text[start-1]!='\n')--start;
        std::size_t a=start,b=end;
        while(b>a && (text[b-1]=='\n' || text[b-1]=='\r' || text[b-1]==' ' || text[b-1]=='\t'))--b;
        while(a<b && (text[a]==' ' || text[a]=='\t' || static_cast<unsigned char>(text[a])==0xEF ||
                      static_cast<unsigned char>(text[a])==0xBB || static_cast<unsigned char>(text[a])==0xBF))++a;
        if(b>a && b-a<kNameMost) {
            wchar_t name[kNameMost];
            bool ascii=true;
            for(std::size_t i=a;i<b;++i){const auto c=static_cast<unsigned char>(text[i]);ascii&=c<0x80;name[i-a]=static_cast<wchar_t>(std::towupper(c));}
            name[b-a]=0;
            if(ascii && roomsync::ParseVariantFile(name,nullptr)) {
                bool again=false;   // a name listed twice counts once (its newest line)
                for(int i=0;i<found && !again;++i)
                    again=spans[i].length==b-a && !_strnicmp(text+spans[i].at,text+a,b-a);
                if(!again)spans[found++]=Span{a,b-a};
            }
        }
        end=start>0 ? start-1 : 0;
        if(start==0)break;
    }
    for(int i=found-1;i>=0;--i) {
        wchar_t name[kNameMost];
        for(std::size_t k=0;k<spans[i].length;++k)name[k]=static_cast<wchar_t>(std::towupper(static_cast<unsigned char>(text[spans[i].at+k])));
        name[spans[i].length]=0;
        each(name);
    }
    return found;
}

// The mission's set: the configuration's names, the host's list's, the pending list's newest. Each part is capped by its
// own bound (kConfiguredVariantsMost, kHostVariantsMost, kPendingPreloadMost) so the union always fits; the counts of
// names a part gave beyond its bound (none with the plugin's own sources) come back in *dropped.
struct MissionParts { int configured=0,host=0,pending=0,dropped=0; };
template<class Configured,class Host>
MissionParts MissionVariants(MissionSet* out,Configured&& configured,Host&& host,const char* pending,std::size_t pendingSize) {
    MissionParts parts;
    out->count=0;
    const auto put=[&](const wchar_t* file,int& part,int most) {
        if(part>=most){++parts.dropped;return;}
        const int before=out->count;
        if(out->Add(file) && out->count>before)++part;
    };
    configured([&](const wchar_t* file){put(file,parts.configured,kConfiguredVariantsMost);});
    host([&](const wchar_t* file){put(file,parts.host,kHostVariantsMost);});
    PendingNames(pending,pendingSize,kPendingPreloadMost,[&](const wchar_t* file){put(file,parts.pending,kPendingPreloadMost);});
    return parts;
}

// The hello's filter (support_loadout.h BloomAdd, 256 bits, 3 probes): the files ready here that the host can ask for.
// With the room's host list (`host` non-null and non-empty): ready ∩ host list; else every ready file.
template<int Most>
void HelloBloom(const NameSet<Most>& ready,const HostSet* host,unsigned char* bloom) noexcept {
    std::memset(bloom,0,kVariantBloomBytes);
    const bool restrict=host && host->count>0;
    for(int i=0;i<ready.count;++i)
        if(!restrict || host->Has(ready.name[i]))BloomAdd(bloom,VariantHash(ready.name[i]));
}

}  // namespace crew::variants
