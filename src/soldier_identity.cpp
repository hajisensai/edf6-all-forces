#include "soldier_identity.h"
#include "plugin_log.h"
#include "edf_layout.h"

namespace crew {

bool InstallSoldierIdentity() noexcept {
    namespace si=soldier_identity;
    si::checkedBase=0;
    if(!image)return false;
    const unsigned char* const vt=image+si::kAssultSoldierVtable;
    const unsigned char* const col=image+si::kAssultSoldierCol;
    const unsigned char* const td=image+si::kAssultSoldierTd;
    if(!Readable(vt-8,8) || !Readable(col,24) || !Readable(td+si::kTdName,sizeof(si::kAssultSoldierName))) {
        Log("SOLDIER IDENTITY off: AssultSoldier's vtable / locator / type descriptor not readable (classes 4 / 5 not recognised online)");
        return false;
    }
    std::uintptr_t rtti=0;
    std::uint32_t locator[6]{};
    std::memcpy(&rtti,vt-8,8);
    std::memcpy(locator,col,sizeof(locator));
    if(rtti!=reinterpret_cast<std::uintptr_t>(col) || !si::AssultSoldierLocator(locator) ||
       std::memcmp(td+si::kTdName,si::kAssultSoldierName,sizeof(si::kAssultSoldierName))) {
        Log("SOLDIER IDENTITY off: EDF+%X-8 is not AssultSoldier's locator EDF+%X (classes 4 / 5 not recognised online)",
            si::kAssultSoldierVtable,si::kAssultSoldierCol);
        return false;
    }
    si::checkedBase=reinterpret_cast<std::uintptr_t>(image);
    return true;
}

}  // namespace crew
