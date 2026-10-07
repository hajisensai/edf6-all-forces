// The EDF5 campaign's rows left out of the story's end and clear ratio (tools/make_edf5_campaign.py, docs/mission-list-re.md).
// The campaign appends EDF5's missions to the offline mission list. Six places in EDF.dll take the list's row count as
// "the story": the last row is the one whose clear plays the ending, the one whose clear unlocks every difficulty, and
// the clear ratio counts every row (numerator and denominator in one loop). With the rows appended, M152 would no
// longer play the ending and a save at 100% would read 57%. Here those six calls get the row count capped at the rows
// before the first EDF5 one (ini EDF5CampaignRows, written by the installer; 0 = no cap). The accessor itself
// (0xE0650) stays as it is: the mission select, ProceedNextMission and loading need the real count. Lists no longer
// than the cap (the DLC lists, the online list, the offline list without the campaign) are untouched.
#include "crew.h"
#include "memory.h"
#include <cstdint>
#include <cstring>
#include <iterator>

namespace crew {
namespace {
using RowsFn=int(__fastcall*)(std::uintptr_t);

// EDF.dll TimeDateStamp 0x678CCB46.
constexpr unsigned kRows=0xE0650;   // (list): the rows of a mission list (its table's child count)
// mov rdx,[rcx+0xF0]; test rdx,rdx; jne +3
const unsigned char kRowsSig[]={0x48,0x8B,0x91,0xF0,0x00,0x00,0x00,0x48,0x85,0xD2,0x75,0x03};
constexpr unsigned kSites[]={
    0x70EDB7,   // IsLastMission (0x70EDA0): current == count-1, the ending
    0xDF9B8,    // the all-clear check (0xDF990): the last row cleared by any class unlocks every difficulty
    0xDCD3B,    // the clear ratio's denominator (0xDC9E0)
    0xDD108,    // its numerator: the loop over the rows (0xDC9E0)
    0xD8586,    // the per-difficulty clear counts (0xD8550)
    0x7480F6,   // the online percentage
};

constexpr int Capped(int rows,int cap) noexcept { return cap>0 && rows>cap ? cap : rows; }
static_assert(Capped(282,147)==147 && Capped(147,147)==147 && Capped(40,147)==40 && Capped(282,0)==282);

int __fastcall RowsHook(std::uintptr_t list) {
    return Capped(reinterpret_cast<RowsFn>(image+kRows)(list),Cfg().edf5CampaignRows);
}

bool CallsRows(unsigned site) noexcept {
    const unsigned char* p=image+site;
    if(!Readable(p,5) || p[0]!=0xE8)return false;
    std::int32_t rel;
    std::memcpy(&rel,p+1,4);
    return p+5+rel==image+kRows;
}
}  // namespace

// All or nothing: the ending at the last EDF5 row but the ratio over the story (or the reverse) is worse than stock.
bool InstallEdf5Campaign() noexcept {
    if(!Matches(kRows,kRowsSig,sizeof(kRowsSig))){Log("HOOK edf5 campaign=0 (EDF+%#x does not match)",kRows);return false;}
    for(unsigned site:kSites)
        if(!CallsRows(site)){Log("HOOK edf5 campaign=0 (EDF+%#x is no call to EDF+%#x)",site,kRows);return false;}
    int done=0;
    for(unsigned site:kSites) {
        bool changed=false;
        if(RedirectCall(image+site,image+kRows,reinterpret_cast<void*>(&RowsHook),changed))++done;
        else Log("EDF5 call site %#x %s",site,changed ? "half patched" : "not patched");
    }
    Log("HOOK edf5 campaign=%d (%d/%d row-count calls capped at EDF5CampaignRows=%d)",done==static_cast<int>(std::size(kSites)),
        done,static_cast<int>(std::size(kSites)),Cfg().edf5CampaignRows);
    return done==static_cast<int>(std::size(kSites));
}
}  // namespace crew
