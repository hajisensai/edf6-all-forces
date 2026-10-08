// The EDF5 campaign's rows left out of the story's end, clear ratio and progress (tools/make_edf5_campaign.py,
// docs/mission-list-re.md §「EDF5 战役」). The campaign appends EDF5's missions to the offline mission list. EDF.dll
// reads the list's row count as "the story" in two ways:
// - as its length (7 of the 27 calls to the row-count accessor 0xE0650): the last row's clear plays the ending, sets
//   the story-complete flag and unlocks every difficulty, and the clear ratio counts every row. Those calls get the
//   count capped at the rows before the first EDF5 one (ini EDF5CampaignRows, written by the installer; 0 = no cap);
// - as the scale of the story's progress, current row / (rows - 1) (0xD7B60), by which its 4 callers interpolate the
//   mode's per-difficulty ranges (config.sgo ModeList: Easy 5..15 ... Inferno 160..280, the drop level by all
//   appearances). Appended rows would compress EDF6's own missions to about half. Those 4 calls get EDF6's rows over
//   EDF6's rows (as stock), and the EDF5 rows their own place in the campaign (0 at its first mission, 1 at its last).
// The accessor itself stays as it is: the mission select, ProceedNextMission, the save's loops and loading need the
// real count (the other 20 calls, each named in tools/selftest.py edf5_campaign_plugin_sites). Lists no longer than
// the cap (the DLC lists, the online list, the offline list without the campaign) are untouched.
#include "crew.h"
#include "memory.h"
#include <cstdint>
#include <cstring>
#include <iterator>

namespace crew {
namespace {
using RowsFn=int(__fastcall*)(std::uintptr_t);
using ProgressFn=float(__fastcall*)(std::uintptr_t);

// EDF.dll TimeDateStamp 0x678CCB46.
constexpr unsigned kRows=0xE0650;       // (list): the rows of a mission list (its table's child count)
constexpr unsigned kProgress=0xD7B60;   // (mgr): mgr+0x48 (current row) / (rows of mgr+0x130 - 1), 0 for 1 row or none
constexpr std::uintptr_t kList=0x130;   // mgr+: the current mode's mission list
constexpr std::uintptr_t kCurrent=0x48; // mgr+: the current row
// mov rdx,[rcx+0xF0]; test rdx,rdx; jne +3
const unsigned char kRowsSig[]={0x48,0x8B,0x91,0xF0,0x00,0x00,0x00,0x48,0x85,0xD2,0x75,0x03};
// mov [rsp+8],rbx; push rdi; sub rsp,0x30; mov rbx,rcx
const unsigned char kProgressSig[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x30,0x48,0x8B,0xD9};
bool campaignReady=false;
constexpr unsigned kRowSites[]={
    0x70EDB7,   // IsLastMission (0x70EDA0): current == count-1, the ending
    0xDF9B8,    // the all-clear check (0xDF990): the last row cleared by any class unlocks every difficulty
    0x92EBD,    // clears applied from a record: the last row among them sets the story-complete flag (+0x300)
    0xDCD3B,    // the clear ratio's denominator (0xDC9E0)
    0xDD108,    // its numerator: the loop over the rows (0xDC9E0)
    0xD8586,    // the per-difficulty clear counts (0xD8550)
    0x7480F6,   // the online percentage
};
constexpr unsigned kProgressSites[]={0xD7B25,0xD7C77,0xD8140,0xD8860};

constexpr int Capped(int rows,int cap) noexcept { return cap>0 && rows>cap ? cap : rows; }
// The story's progress at row `current` of `rows`, the first `cap` of them EDF6's (cap > 0, rows > cap).
constexpr float Progress(std::uint32_t current,int rows,int cap) noexcept {
    if(current<static_cast<std::uint32_t>(cap))return cap>1 ? static_cast<float>(current)/static_cast<float>(cap-1) : 0.0f;
    const int campaign=rows-cap;
    return campaign>1 ? static_cast<float>(current-static_cast<std::uint32_t>(cap))/static_cast<float>(campaign-1) : 0.0f;
}
static_assert(Capped(282,147)==147 && Capped(147,147)==147 && Capped(40,147)==40 && Capped(282,0)==282);
static_assert(Progress(0,282,147)==0.0f && Progress(146,282,147)==1.0f && Progress(73,282,147)==0.5f);
static_assert(Progress(147,282,147)==0.0f && Progress(281,282,147)==1.0f && Progress(147,148,147)==0.0f);

int __fastcall RowsHook(std::uintptr_t list) {
    const int rows=reinterpret_cast<RowsFn>(image+kRows)(list);
    return campaignReady ? Capped(rows,Cfg().edf5CampaignRows) : rows;
}

float __fastcall ProgressHook(std::uintptr_t mgr) {
    const int cap=Cfg().edf5CampaignRows;
    const int rows=reinterpret_cast<RowsFn>(image+kRows)(mgr+kList);
    if(!campaignReady || cap<=0 || rows<=cap)return reinterpret_cast<ProgressFn>(image+kProgress)(mgr);
    return Progress(*reinterpret_cast<const std::uint32_t*>(mgr+kCurrent),rows,cap);
}

bool Calls(unsigned site,unsigned target) noexcept {
    const unsigned char* p=image+site;
    if(!Readable(p,5) || p[0]!=0xE8)return false;
    std::int32_t rel;
    std::memcpy(&rel,p+1,4);
    return p+5+rel==image+target;
}

struct Group { unsigned target; const unsigned* sites; std::size_t count; void* hook; };
const Group kGroups[]={
    {kRows,kRowSites,std::size(kRowSites),reinterpret_cast<void*>(&RowsHook)},
    {kProgress,kProgressSites,std::size(kProgressSites),reinterpret_cast<void*>(&ProgressHook)},
};
}  // namespace

// All or nothing: the ending at the last EDF5 row but the ratio over the story (or the reverse) is worse than stock.
bool InstallEdf5Campaign() noexcept {
    if(campaignReady)return true;
    if(!Matches(kRows,kRowsSig,sizeof(kRowsSig))){Log("HOOK edf5 campaign=0 (EDF+%#x does not match)",kRows);return false;}
    if(!Matches(kProgress,kProgressSig,sizeof(kProgressSig))){Log("HOOK edf5 campaign=0 (EDF+%#x does not match)",kProgress);return false;}
    int sites=0;
    for(const auto& g:kGroups) {
        for(std::size_t i=0;i<g.count;++i)
            if(!Calls(g.sites[i],g.target)){Log("HOOK edf5 campaign=0 (EDF+%#x is no call to EDF+%#x)",g.sites[i],g.target);return false;}
        sites+=static_cast<int>(g.count);
    }
    int done=0;
    for(const auto& g:kGroups)
        for(std::size_t i=0;i<g.count;++i) {
            bool changed=false;
            if(RedirectCall(image+g.sites[i],image+g.target,g.hook,changed))++done;
            else Log("EDF5 call site %#x %s",g.sites[i],changed ? "half patched" : "not patched");
        }
    // A near-thunk allocation/protection failure can occur after earlier calls were redirected. Keep those
    // redirected calls on the original behavior until every call is installed, including a half-written call.
    campaignReady=done==sites;
    Log("HOOK edf5 campaign=%d (%d/%d calls: the story's length and progress over EDF5CampaignRows=%d rows)",campaignReady,done,sites,
        Cfg().edf5CampaignRows);
    return campaignReady;
}
}  // namespace crew
