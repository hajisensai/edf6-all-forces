// hknpEventDispatcher's indexes widened to uint32 (docs/havok-props-re.md, havok_props_logic.h): 137 same-length
// rewrites inside the dispatcher's own functions. Nothing of the plugin runs afterwards; the game's code makes, uses
// and frees every dispatcher.
#include "havok_props.h"
#include "havok_props_logic.h"
#include "crew.h"

namespace crew {
namespace {
using namespace havokprops;

bool PatchSites() noexcept {
    for(std::size_t i=0;i<kSiteCount;++i) {
        const Site& s=kSites[i];
        if(edf::PatchCode(image+s.rva,s.stock,s.patched,s.size))continue;
        // Undo in reverse: a dispatcher whose readers and writers disagree on the index width corrupts its lists.
        for(std::size_t j=i;j-->0;)edf::PatchCode(image+kSites[j].rva,kSites[j].patched,kSites[j].stock,kSites[j].size);
        Log("HAVOKPROPS refused: site %#x could not be written (stock uint16 indexes)",s.rva);
        return false;
    }
    return true;
}
}  // namespace

bool InstallHavokProps() noexcept {
    __try {
        for(const Site& s:kSites)
            if(!Matches(s.rva,s.stock,s.size)){Log("HAVOKPROPS refused: unexpected code at %#x (stock uint16 indexes)",s.rva);return false;}
        if(!Matches(kReserveU16,kReserveU16Bytes,kReserveSize) || !Matches(kReserveU32,kReserveU32Bytes,kReserveSize)) {
            Log("HAVOKPROPS refused: unexpected hkArray reserve code (stock uint16 indexes)");
            return false;
        }
        // The sites move fields (+0x3C, +0x64, node +4) into bytes a stock dispatcher leaves uninitialized: one made
        // before the patch would be corrupted by it. None can exist before the application's init.
        if(At<void*>(image,kFsManager) || At<void*>(image,kPhysicsSystem)) {
            Log("HAVOKPROPS refused: the application's init has run (a stock event dispatcher may exist)");
            return false;
        }
        if(!PatchSites())return false;
        Log("HAVOKPROPS event dispatcher indexes uint32 (stock uint16) sites=%u",static_cast<unsigned>(kSiteCount));
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}  // namespace crew
