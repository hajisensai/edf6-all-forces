// The EDF5 campaign's three mission packs owned (tools/make_edf5_campaign.py, docs/mission-list-re.md §7).
// The installer appends EDF5's story and its two DLCs to DefaultPackage/config.sgo's ModeList as offline modes of
// their own, content ids EDF5CampaignContent .. +2 (after every id in the list; EDF.dll knows 0..2). The mission-pack
// dialog lists every offline mode and enables the ones whose content the player owns: 0xD92B0(mgr, id) looks the id
// up in mgr+0xE0, a std::set rebuilt as {0} and the content ids of the platform's add-ons (0xDBB50, 0xDA600,
// 0xDBD40). Ours are no platform add-on, so the dialog would show them greyed out with "not purchased". Every call
// to 0xD92B0 (7, each named in kSites; tools/selftest.py edf5_campaign_plugin_sites checks there are no others)
// goes through OwnedHook: the native answer, and ours owned besides. Nothing else changes: the packs' lists, saves,
// endings (none: MAINSCRIPT plays them for contents 0..2) and the story's statistics (content 0 only) are the
// game's own handling of a mode. EDF5CampaignContent=0 (no packs installed): the native answer alone.
// The test range (testrange/gen.py) is a mission pack of its own too, no longer laid over RM015: an offline and an
// online mode sharing one content id, TestRangeContent (0: not installed), owned through the same hook.
// Another plugin may have redirected a site first (EDF6Coop logs the lobby join's 0x8EC659 and loads before us, by
// name): its call is chained, ours asking it instead of 0xD92B0. A site redirected inside EDF.dll is another build.
#include "crew.h"
#include "memory.h"
#include "edf/host.h"
#include <cstdint>
#include <cstring>
#include <array>
#include <iterator>
#include <utility>

namespace crew {
namespace {
using OwnedFn=bool(__fastcall*)(std::uintptr_t,int);

// EDF.dll TimeDateStamp 0x678CCB46.
constexpr unsigned kOwned=0xD92B0;   // (mgr, content id): whether the player owns the content
constexpr int kPacks=3;              // EDF5's story, DLC1, DLC2: content ids EDF5CampaignContent .. +2
// mov r8,[rcx+0xE0]; mov rcx,r8; mov rax,[r8+8]: the owned set's head and root
const unsigned char kOwnedSig[]={0x4C,0x8B,0x81,0xE0,0x00,0x00,0x00,0x49,0x8B,0xC8,0x49,0x8B,0x40,0x08};
constexpr unsigned kSites[]={
    0x8BEF48,   // the mission-pack dialog's list (0x8BEE40): enabled or "not purchased"
    0x8B5188,   // (0x8B4280)
    0x8EC659,   // the lobby's room join (0x8EC270)
    0x8F64A1,   // (0x8F6460)
    0x8FD963,   // (0x8FD8F0)
    0x90069B,   // the lobby's rooms (0x9005D0)
    0x91A655,   // (0x91A600)
};
bool campaignReady=false;
OwnedFn prior[std::size(kSites)]{};   // what each site called before us: 0xD92B0, or another plugin's hook of it

// first: the EDF5 packs' first id, range: the test range's id (each 0 when not installed).
constexpr bool Ours(int id,int first,int range) noexcept {
    return (first>0 && id>=first && id<first+kPacks) || (range>0 && id==range);
}
static_assert(Ours(3,3,0) && Ours(5,3,0) && !Ours(6,3,0) && !Ours(2,3,0) && !Ours(0,0,0) && !Ours(3,0,0));
static_assert(Ours(7,3,7) && !Ours(6,3,7) && Ours(4,0,4) && !Ours(0,0,0) && !Ours(5,0,4));

// One hook per site, each asking what that site called before (so a chained plugin still sees every call).
template<std::size_t I> bool __fastcall OwnedHook(std::uintptr_t mgr,int id) {
    if(prior[I](mgr,id))return true;
    return campaignReady && Ours(id,Cfg().edf5CampaignContent,Cfg().testRangeContent);
}
template<std::size_t... I> constexpr auto Hooks(std::index_sequence<I...>) { return std::array<OwnedFn,sizeof...(I)>{&OwnedHook<I>...}; }
constexpr auto kHooks=Hooks(std::make_index_sequence<std::size(kSites)>{});

// The rel32 call's target at `site`, nullptr when it is no call.
unsigned char* CallTarget(unsigned site) noexcept {
    unsigned char* p=image+site;
    if(!Readable(p,5) || p[0]!=0xE8)return nullptr;
    std::int32_t rel;
    std::memcpy(&rel,p+1,4);
    return p+5+rel;
}
// The native lookup, or another plugin's redirect of it (outside EDF.dll); not a different call inside it.
bool Chainable(const unsigned char* target) noexcept {
    return target && (target==image+kOwned || target<image || target>=image+edf::kImageSize);
}
}  // namespace

// All or nothing: a pack enabled in the dialog but refused where another call asks is worse than stock.
bool InstallEdf5Campaign() noexcept {
    // Once: after a partial install our own thunks would look like another plugin's redirect (chained to itself).
    static bool tried=false;
    if(tried)return campaignReady;
    tried=true;
    if(!Matches(kOwned,kOwnedSig,sizeof(kOwnedSig))){Log("HOOK edf5 campaign=0 (EDF+%#x does not match)",kOwned);return false;}
    unsigned char* targets[std::size(kSites)]{};
    for(std::size_t i=0;i<std::size(kSites);++i) {
        targets[i]=CallTarget(kSites[i]);
        if(!Chainable(targets[i])){Log("HOOK edf5 campaign=0 (EDF+%#x is no call to EDF+%#x)",kSites[i],kOwned);return false;}
    }
    int done=0;
    for(std::size_t i=0;i<std::size(kSites);++i) {
        prior[i]=reinterpret_cast<OwnedFn>(targets[i]);
        if(targets[i]!=image+kOwned)Log("EDF5 call site %#x already redirected (another plugin): chained",kSites[i]);
        bool changed=false;
        if(RedirectCall(image+kSites[i],targets[i],reinterpret_cast<void*>(kHooks[i]),changed))++done;
        else Log("EDF5 call site %#x %s",kSites[i],changed ? "half patched" : "not patched");
    }
    // A near-thunk allocation/protection failure can occur after earlier calls were redirected. Keep those
    // redirected calls on the native answer until every call is installed, including a half-written call.
    campaignReady=done==static_cast<int>(std::size(kSites));
    Log("HOOK edf5 campaign=%d (%d/%d calls: the mission packs' content %d..%d owned, test range %d)",campaignReady,
        done,static_cast<int>(std::size(kSites)),Cfg().edf5CampaignContent,Cfg().edf5CampaignContent+kPacks-1,
        Cfg().testRangeContent);
    return campaignReady;
}
}  // namespace crew
