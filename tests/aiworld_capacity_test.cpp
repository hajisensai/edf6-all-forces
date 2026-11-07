// ai::World's agent capacity sites (src/aiworld_capacity_logic.h) offline: each site rewrites only its 4-byte field,
// from the stock value its kind has to the patched one; the sizes, the zero loop and the id bound agree with each other.
// cmake --build build --target aiworld_capacity_test && build\aiworld_capacity_test.exe
#include "../src/aiworld_capacity_logic.h"
#include <cstdio>
#include <cstring>

namespace {
using namespace crew::aiworld;
int failures=0,checks=0;
void Expect(bool ok,const char* what) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}
std::uint32_t Field(const std::uint8_t* b,unsigned at) {
    return static_cast<std::uint32_t>(b[at])|static_cast<std::uint32_t>(b[at+1])<<8|static_cast<std::uint32_t>(b[at+2])<<16|
        static_cast<std::uint32_t>(b[at+3])<<24;
}
}  // namespace

int main() {
    static_assert(kAgentCapacity==0xFFFE,"every uint16 id but the stock 0xFFFF");
    static_assert(kWorldSize==0x82188,"0x2180 + 0x10001 slots");
    unsigned kinds[5]={};
    for(const Site& s:kSites) {
        Expect(s.size>=5 && s.size<=12 && s.field+4u<=s.size,"the field lies inside one instruction");
        Expect(!std::memcmp(s.stock,s.patched,s.field),"bytes before the field unchanged");
        Expect(!std::memcmp(s.stock+s.field+4,s.patched+s.field+4,s.size-s.field-4u),"bytes after the field unchanged");
        const std::uint32_t was=Field(s.stock,s.field),now=Field(s.patched,s.field);
        ++kinds[static_cast<int>(s.kind)];
        switch(s.kind) {
        case Kind::WorldSize: Expect(was==kStockWorldSize && now==kWorldSize,"World size: 0x2180 -> the larger World"); break;
        case Kind::ZeroBase: Expect(was==kStockSlots && now==kTable,"zero loop base: +0x80 -> the table"); break;
        case Kind::ZeroCount: Expect(was*8+1==kStockSlotCount && now==kZeroIterations,"zero loop: 0x401 -> every table slot"); break;
        case Kind::Slot: Expect(was==kStockSlots && now==kTable,"slot access: +0x80 -> the table"); break;
        case Kind::IdBound: Expect(was==kStockIdBound && now==kIdBound,"free-id fill: 1..0x400 -> 1..0xFFFE"); break;
        }
    }
    Expect(kinds[static_cast<int>(Kind::WorldSize)]==3,"allocation and both destructor branches");
    Expect(kinds[static_cast<int>(Kind::ZeroBase)]==1 && kinds[static_cast<int>(Kind::ZeroCount)]==1,"one zero loop");
    Expect(kinds[static_cast<int>(Kind::Slot)]==11,"eleven slot accesses");
    Expect(kinds[static_cast<int>(Kind::IdBound)]==1,"one id bound");
    Expect(kSites[kSiteCount-1].kind==Kind::IdBound,"the id bound is written last");
    Expect(kSites[0].kind==Kind::WorldSize,"the allocation grows first");
    for(std::size_t i=0;i<kSiteCount;++i)for(std::size_t j=i+1;j<kSiteCount;++j)
        Expect(kSites[i].rva+kSites[i].size<=kSites[j].rva || kSites[j].rva+kSites[j].size<=kSites[i].rva,"sites do not overlap");
    // The table starts 8-aligned right after the stock World and ends at the allocation's end.
    Expect(kTable%8==0 && kTable==kStockWorldSize && kTable+kTableSlots*8==kWorldSize,"table placement");
    std::printf("aiworld_capacity: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
