// src/soldier_identity_logic.h: All Forces' classes 4 / 5 (clones of AssultSoldier's vtable) known by their RTTI pointer.
#include "soldier_identity_logic.h"
#include <cstdio>

namespace {
int failures=0;
void Expect(bool ok,const char* what) {
    if(!ok){std::printf("FAIL %s\n",what);++failures;}
}
}  // namespace

int main() {
    namespace si=crew::soldier_identity;
    constexpr std::uintptr_t base=0x7FF600000000ull;
    constexpr std::uintptr_t col=base+si::kAssultSoldierCol;
    constexpr std::uintptr_t clone=0x1D0000123450ull;   // a plugin block's vptr (&block[1])

    static_assert(si::AssultSoldierClone(base,clone,col,true),"a clone keeping AssultSoldier's RTTI pointer");
    Expect(si::AssultSoldierClone(base,clone,col,true),"a cloned vtable with AssultSoldier's locator is a class 4 / 5 soldier");
    Expect(!si::AssultSoldierClone(base,clone,col,false),"without the load's check none is");
    Expect(!si::AssultSoldierClone(base,base+si::kAssultSoldierVtable,col,true),"the stock AssultSoldier vtable is IsSoldierClass's, not a clone");
    Expect(!si::AssultSoldierClone(base,clone,base+0x1C7D5A8,true),"a clone of PaleWing's (another locator) is not");
    Expect(!si::AssultSoldierClone(base,clone,si::kAssultSoldierCol,true),"the locator's RVA unrelocated is not its address");
    Expect(!si::AssultSoldierClone(base,0,col,true),"no vptr");
    Expect(!si::AssultSoldierClone(0,clone,si::kAssultSoldierCol,true),"no base");

    std::uint32_t locator[6]={1,0,0,si::kAssultSoldierTd,0x1C7B870,si::kAssultSoldierCol};
    Expect(si::AssultSoldierLocator(locator),"AssultSoldier's x64 locator");
    locator[0]=0;
    Expect(!si::AssultSoldierLocator(locator),"an x86 (signature 0) locator is not");
    locator[0]=1;locator[1]=8;
    Expect(!si::AssultSoldierLocator(locator),"a secondary vtable's locator (offset 8) is not");
    locator[1]=0;locator[3]=0x2027058;
    Expect(!si::AssultSoldierLocator(locator),"one naming PaleWing's type descriptor is not");
    locator[3]=si::kAssultSoldierTd;locator[5]=0x1C7D5A8;
    Expect(!si::AssultSoldierLocator(locator),"one whose self RVA is another's is not");

    Expect(sizeof(si::kStockVtables)/sizeof(si::kStockVtables[0])==4 && si::kStockVtables[0]==si::kAssultSoldierVtable,
           "the stock four, AssultSoldier's first");
    if(!failures)std::printf("soldier_identity: ok\n");
    return failures ? 1 : 0;
}
