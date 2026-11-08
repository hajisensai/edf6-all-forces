// ai::World's agent capacity (docs/aiworld-re.md, aiworld_capacity_logic.h): the agent table moves to the end of a
// larger World allocation and the free-id fill runs to 0xFFFE. Only immediates and displacements change, so the
// game's own code still makes, zeroes, locks (+0x58) and frees each World; nothing of the plugin runs per agent.
#include "aiworld_capacity.h"
#include "aiworld_capacity_logic.h"
#include "crew.h"

namespace crew {
namespace {
using namespace aiworld;

bool PatchSites() noexcept {
    for(std::size_t i=0;i<kSiteCount;++i) {
        const Site& s=kSites[i];
        if(edf::PatchCode(image+s.rva,s.stock,s.patched,s.size))continue;
        // Undo in reverse: a table moved for some accesses and not others would split readers from writers.
        for(std::size_t j=i;j-->0;)edf::PatchCode(image+kSites[j].rva,kSites[j].patched,kSites[j].stock,kSites[j].size);
        Log("AIWORLD refused: site %#x could not be written (stock 1024 agents)",s.rva);
        return false;
    }
    return true;
}
}  // namespace

bool InstallAiWorldCapacity() noexcept {
    __try {
        for(const Site& s:kSites)
            if(!Matches(s.rva,s.stock,s.size)){Log("AIWORLD refused: unexpected code at %#x (stock 1024 agents)",s.rva);return false;}
        if(!PatchSites())return false;
        Log("AIWORLD agents=%u (stock %u) world=%#x bytes sites=%u",kAgentCapacity,kStockIdBound-1,kWorldSize,
            static_cast<unsigned>(kSiteCount));
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}  // namespace crew
