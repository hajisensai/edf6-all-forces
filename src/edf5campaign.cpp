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
// online mode sharing one content id, TestRangeContent (0: not installed), owned through the same hook. So is EDF4.1's
// story (plan P6), slotted after the range so no installed pack's id moves, and its two DLC mission packs right after
// it: EDF41Content .. +2 (0: not installed).
// Another plugin may have redirected a site first (EDF6Coop logs the lobby join's 0x8EC659 and loads before us, by
// name): its call is chained, ours asking it instead of 0xD92B0. A site redirected inside EDF.dll is another build.
#include "crew.h"
#include "memory.h"
#include "edf/host.h"
#include "debug_spawn.h"
#include "unlock_ported.h"
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
constexpr int kPacks41=3;            // EDF4.1's story, Mission Pack 1, Mission Pack 2: EDF41Content .. +2
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

// first: the EDF5 packs' first id, range: the test range's id, edf41: the first of EDF4.1's packs' ids (its story's;
// its two DLC packs follow) (each 0 when not installed).
constexpr bool Ours(int id,int first,int range,int edf41=0) noexcept {
    return (first>0 && id>=first && id<first+kPacks) || (range>0 && id==range) || (edf41>0 && id>=edf41 && id<edf41+kPacks41);
}
static_assert(Ours(3,3,0) && Ours(5,3,0) && !Ours(6,3,0) && !Ours(2,3,0) && !Ours(0,0,0) && !Ours(3,0,0));
static_assert(Ours(7,3,7) && !Ours(6,3,7) && Ours(4,0,4) && !Ours(0,0,0) && !Ours(5,0,4));
static_assert(Ours(7,3,6,7) && Ours(8,3,6,7) && Ours(9,3,6,7) && !Ours(10,3,6,7) && Ours(7,0,0,7) && !Ours(0,3,6,0)
              && !Ours(7,3,6,0));

// One hook per site, each asking what that site called before (so a chained plugin still sees every call).
template<std::size_t I> bool __fastcall OwnedHook(std::uintptr_t mgr,int id) {
    const bool previous=prior[I](mgr,id);
    const auto& cfg=Cfg();
    const bool mod=campaignReady && Ours(id,cfg.edf5CampaignContent,cfg.testRangeContent,cfg.edf41Content);
    const bool allowed=previous || mod;
    // Only the lobby's join check: the pack-list sites also run while drawing the menu.
    // Coop's inner hook logs its native answer before ours has added MOD ownership; that is not this final result.
    if constexpr(I==2)Log("ROOM content check site=%#x id=%d prior=%d mod=%d allowed=%d hooks_ready=%d EDF5=%d..%d range=%d EDF41=%d..%d",
        kSites[I],id,previous,mod,allowed,campaignReady,cfg.edf5CampaignContent,
        cfg.edf5CampaignContent>0 ? cfg.edf5CampaignContent+kPacks-1 : 0,cfg.testRangeContent,
        cfg.edf41Content,cfg.edf41Content>0 ? cfg.edf41Content+kPacks41-1 : 0);
    return allowed;
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
    Log("HOOK edf5 campaign=%d (%d/%d calls: the mission packs' content %d..%d owned, test range %d, EDF4.1 %d..%d)",
        campaignReady,done,static_cast<int>(std::size(kSites)),Cfg().edf5CampaignContent,
        Cfg().edf5CampaignContent+kPacks-1,Cfg().testRangeContent,Cfg().edf41Content,
        Cfg().edf41Content>0 ? Cfg().edf41Content+kPacks41-1 : 0);
    return campaignReady;
}
namespace {
// ---- PortedMissionsUnlocked: the mission select of the ported packs (see the file comment) ----
constexpr unsigned kGameStatus=0x20B2890;   // GameDataMgr* (the GS): modes at +0x20 (count u32 +0x30), current +0x38
constexpr unsigned kListScan=0x8A21C3;      // the mission select's first clear check: call 0x8A4C00(row 0)
constexpr unsigned kCleared=0x8A4C00;       // bool (row): any class cleared it on any difficulty (the mode's save)
constexpr unsigned kField5=0xE0CA0;         // int (cfg = GS+0x130, row): the list's field 5 (1: a branch row)
// 0x8A20F3: the function's own read of the current mode's content id (mov rdx,[GS]; ... mov r12d,[rcx+rax+0x74]).
const unsigned char kContentSig[]={0x48,0x8B,0x15,0x96,0x07,0x81,0x01,0x8B,0x42,0x48,0x89,0x44,0x24,0x68,0x8B,0x4A,0x38,
    0x48,0x8B,0x42,0x20,0x48,0x8B,0x0C,0xC8,0x48,0x8B,0x41,0x10,0x48,0x63,0x48,0x08,0x44,0x8B,0x64,0x01,0x74};
// 0x8A2160: the rows ([rsp+0x48], 0x14 bytes each): row 0 open, the rest numbered and hidden.
const unsigned char kRowsSig[]={0x48,0x8B,0x44,0x24,0x48,0xC7,0x40,0x08,0x01,0x00,0x00,0x00,0xBA,0x01,0x00,0x00,0x00,0x3B,
    0xDA,0x76,0x2C,0xB9,0x14,0x00,0x00,0x00,0x66,0x0F,0x1F,0x44,0x00,0x00,0x48,0x8B,0x44,0x24,0x48,0x89,0x14,0x01,0x48,
    0x8B,0x44,0x24,0x48,0xC7,0x44,0x01,0x08,0x04,0x00,0x00,0x00,0xFF,0xC2,0x48,0x8D,0x49,0x14,0x3B,0xD3,0x72,0xE1,0xEB,0x08};
// 0x8A21A9..0x8A21C3: the scan's start, row 0 (r14d) into ecx; no push between the prologue and the call.
const unsigned char kScanSig[]={0x4D,0x8B,0xE8,0x45,0x8B,0xF8,0xBF,0xFF,0xFF,0xFF,0xFF,0x89,0x7C,0x24,0x60,0x45,0x8B,0xF0,
    0x0F,0x1F,0x44,0x00,0x00,0x41,0x8B,0xCE};
constexpr unsigned kRowsAt=0x8A2160,kContentAt=0x8A20F3,kScanAt=0x8A21A9;
constexpr std::size_t kModes=0x20,kModeCount=0x30,kModeNow=0x38,kModeNode=0x10,kModeContent=0x74,kModeCfg=0x130;
using ClearedFn=bool(__fastcall*)(unsigned);
using Field5Fn=int(__fastcall*)(void*,unsigned);
ClearedFn priorCleared=nullptr;   // 0x8A4C00, or another plugin's redirect of this call
Field5Fn field5=nullptr;
bool missionsReady=false;
int lastOpened=-2,lastContent=-2;   // the last opening logged (one line per change, not per opening)

// The current mode's content id (as 0x8A20F3 reads it), -1 when there is none to read.
int CurrentContent(unsigned char*& gs) noexcept {
    gs=Readable(image+kGameStatus,8) ? At<unsigned char*>(image,kGameStatus) : nullptr;
    if(!gs || !Readable(gs,kModeCfg))return -1;
    const auto count=At<std::uint32_t>(gs,kModeCount);
    const auto now=At<std::uint32_t>(gs,kModeNow);
    unsigned char* const* modes=At<unsigned char* const*>(gs,kModes);
    if(now>=count || !modes || !Readable(modes+now,8))return -1;
    unsigned char* const mode=modes[now];
    if(!mode || !Readable(mode+kModeNode,8))return -1;
    unsigned char* const node=At<unsigned char*>(mode,kModeNode);
    if(!node || !Readable(node+8,4))return -1;
    const auto at=At<std::int32_t>(node,8);
    if(!Readable(node+at+kModeContent,4))return -1;
    return At<std::int32_t>(node+at,kModeContent);
}

// The rows' vector: {begin, end, capacity} at the caller's rsp+0x48.
void OpenRows(unsigned char* vec) noexcept {
    const auto& cfg=Cfg();
    if(!cfg.portedMissionsUnlocked || !vec || !Readable(vec,0x18))return;
    unsigned char* gs=nullptr;
    const int content=CurrentContent(gs);
    if(!ported::StoryPack(content,cfg.edf5CampaignContent,cfg.edf41Content))return;
    auto* const begin=At<ported::ListRow*>(vec,0);
    auto* const end=At<ported::ListRow*>(vec,8);
    int opened=-1;
    if(begin && end>begin) {
        const auto bytes=static_cast<std::size_t>(reinterpret_cast<unsigned char*>(end)-reinterpret_cast<unsigned char*>(begin));
        if(bytes%sizeof(ported::ListRow)==0 && Readable(begin,bytes,true))
            opened=ported::OpenAll(begin,bytes/sizeof(ported::ListRow),[gs](unsigned row){ return field5(gs+kModeCfg,row); });
    }
    if(opened!=lastOpened || content!=lastContent) {
        if(opened>=0)Log("PORTED missions unlocked: content %d, %d rows opened",content,opened);
        else Log("PORTED missions: content %d, the rows are not the stock list's: left stock",content);
    }
    lastOpened=opened;lastContent=content;
}

bool __fastcall ClearedHook(unsigned row,unsigned char* vec) {
    if(row==0) {
        __try { OpenRows(vec); } __except(EXCEPTION_EXECUTE_HANDLER){ Log("PORTED missions: opening the rows failed (stock)"); }
    }
    return priorCleared(row);
}
}  // namespace

// The current mode's content id is the test range's (docs/weapon-level-cap-re.md §3: GS+0x38's mode is the one
// GS+0x148 names, which the script's GetContentID 0x70E5D0 reads; a guest's too, once the room's mode is set). Offline
// and online: the range's two modes share its content id.
bool TestRangeMode() noexcept {
    const int range=Cfg().testRangeContent;
    if(range<=0)return false;
    __try {
        unsigned char* gs=nullptr;
        return CurrentContent(gs)==range;
    } __except(EXCEPTION_EXECUTE_HANDLER){ return false; }
}

// The mission select's first clear check through a stub: lea rdx,[rsp+0x50] (the caller's rsp+0x48, past the return
// address); mov rax,ClearedHook; jmp rax. Installed whatever the key (read at each opening); not at all when a piece
// of the code is not the one read.
bool InstallPortedMissions() noexcept {
    static bool tried=false;
    if(tried)return missionsReady;
    tried=true;
    if(!Matches(kScanAt,kScanSig,sizeof(kScanSig)) || !Matches(kRowsAt,kRowsSig,sizeof(kRowsSig))
       || !Matches(kContentAt,kContentSig,sizeof(kContentSig))) {
        Log("HOOK ported missions=0 (the mission select is not the one read)");
        return false;
    }
    unsigned char* const target=CallTarget(kListScan);
    if(!target || !(target==image+kCleared || target<image || target>=image+edf::kImageSize)) {
        Log("HOOK ported missions=0 (EDF+%#x is no call to EDF+%#x)",kListScan,kCleared);
        return false;
    }
    if(target!=image+kCleared)Log("PORTED mission select call %#x already redirected (another plugin): chained",kListScan);
    priorCleared=reinterpret_cast<ClearedFn>(target);
    field5=reinterpret_cast<Field5Fn>(image+kField5);
    unsigned char stub[]={0x48,0x8D,0x54,0x24,0x50,0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    void* const hook=reinterpret_cast<void*>(&ClearedHook);
    std::memcpy(stub+7,&hook,sizeof(hook));
    void* const code=AllocateNearCode(image+kListScan,stub,sizeof(stub));
    bool changed=false;
    missionsReady=code && RedirectCall(image+kListScan,target,code,changed);
    Log("HOOK ported missions=%d%s (PortedMissionsUnlocked=%d, packs EDF5 %d..%d, EDF4.1 %d..%d)",missionsReady,
        missionsReady ? "" : changed ? " half patched" : " not patched",Cfg().portedMissionsUnlocked,
        Cfg().edf5CampaignContent,Cfg().edf5CampaignContent>0 ? Cfg().edf5CampaignContent+kPacks-1 : 0,
        Cfg().edf41Content,Cfg().edf41Content>0 ? Cfg().edf41Content+kPacks41-1 : 0);
    return missionsReady;
}
}  // namespace crew
