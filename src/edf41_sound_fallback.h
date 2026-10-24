#pragma once
// Missing optional 4.1 audio is resolved to reviewed finite resident cues.
// Keep the real source bank first; a fallback never creates an unowned loop.
#include <cstddef>
#include <cwchar>

namespace crew { namespace edf41sound {
struct FallbackCue { const wchar_t* source; const wchar_t* target; };
inline constexpr FallbackCue kFallbackCues[]={
#include "edf41_sound_fallbacks.inc"
};

inline const wchar_t* FallbackName(const wchar_t* name) noexcept {
    if(!name)return nullptr;
    std::size_t first=0,last=sizeof(kFallbackCues)/sizeof(kFallbackCues[0]);
    while(first<last) {
        const std::size_t middle=first+(last-first)/2;
        const int order=std::wcscmp(name,kFallbackCues[middle].source);
        if(!order)return kFallbackCues[middle].target;
        if(order<0)last=middle;
        else first=middle+1;
    }
    return nullptr;
}

template<class Stock,class Imported> bool LookupCue(const wchar_t* name,Stock stock,Imported imported) {
    if(stock(name) || imported(name))return true;
    const wchar_t* fallback=FallbackName(name);
    return fallback && stock(fallback);
}
} }
