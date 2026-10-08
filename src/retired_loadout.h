// Compatibility boundary for old installer data, not a weapon capability guess.
// These files belonged only to this plugin and no longer describe a supported mount.
#pragma once
#include "crew.h"
#include <cwchar>

namespace crew {
inline bool RetiredLoadout(const unsigned char* weapon) noexcept {
    constexpr wchar_t oldCoax[]=L"EDF6VC_COAX_MG.SGO";
    std::size_t length=0;
    const wchar_t* const file=WeaponFile(weapon,&length);
    return file && length==sizeof(oldCoax)/sizeof(oldCoax[0])-1 &&
           _wcsnicmp(file,oldCoax,length)==0;
}

// Called on both the selection and shot paths. Native shots can enter on a
// replay thread, so the one-time diagnostic is shared and atomically claimed.
inline void ReportRetiredLoadout() noexcept {
    static volatile LONG reported=0;
    if(InterlockedCompareExchange(&reported,1,0)==0)
        Log("STORES: obsolete EDF6VC_COAX_MG.SGO blocked; rerun the installer to regenerate vehicle requests");
}
}  // namespace crew
