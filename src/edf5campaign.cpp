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
// online mode sharing one content id, TestRangeContent (0: not installed), owned through the same hook; its rows (the
// range, then the PvP presets) are all opened by the mission select's hook, as none of them ever ends. So is EDF4.1's
// story (plan P6), slotted after the range so no installed pack's id moves, and its two DLC mission packs right after
// it: EDF41Content .. +2 (0: not installed).
// Since 2026-10-18 the ids are fixed (mission_packs.h: 65..71, whatever else the machine has installed: a room's DLC
// is the id) and the plugin owns exactly the packs EDF6VehicleCrew.packs.ini lists in a run the ini names. A pack with
// placeholder rows (a mission whose resources this machine lacks, kept so every row is the definition's) is owned only
// while the mission select's hook (InstallPortedMissions) can keep those rows locked. A guest refused at the lobby's
// join (0x8EC659) for one of our ids is told which pack this machine lacks.
// Another plugin may have redirected a site first (EDF6Coop logs the lobby join's 0x8EC659 and loads before us, by
// name): its call is chained, ours asking it instead of 0xD92B0. A site redirected inside EDF.dll is another build.
#include "crew.h"
#include "memory.h"
#include "edf/host.h"
#include "debug_spawn.h"
#include "unlock_ported.h"
#include "pack_notice.h"
#include <cstdint>
#include <cstring>
#include <array>
#include <cwchar>
#include <intrin.h>
#include <iterator>
#include <string>
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
bool missionsReady=false;             // InstallPortedMissions: the mission select's hook (placeholder rows locked)
bool saveHookReady=false;             // airstrike.cpp's save-load hook (0xDC550): each pack's save marked / moved
OwnedFn prior[std::size(kSites)]{};   // what each site called before us: 0xD92B0, or another plugin's hook of it

// The ini key naming `id`'s run (EDF5CampaignContent's three, TestRangeContent, EDF41Content's three): its fixed
// first id, 0 for an id that is none of ours.
constexpr int RunOf(int id) noexcept {
    return id>=packs::kEdf5 && id<packs::kEdf5+kPacks ? packs::kEdf5 : id==packs::kRange ? packs::kRange
         : id>=packs::kEdf41 && id<packs::kEdf41+kPacks41 ? packs::kEdf41 : 0;
}
static_assert(RunOf(65)==65 && RunOf(67)==65 && RunOf(68)==68 && RunOf(69)==69 && RunOf(71)==69 && !RunOf(72)
              && !RunOf(64) && !RunOf(3) && !RunOf(0) && packs::kEdf5+kPacks==packs::kRange && packs::kRange+1==packs::kEdf41
              && packs::kEdf41+kPacks41==packs::kBase+packs::kCount);
// `id`'s installed pack: listed in EDF6VehicleCrew.packs.ini, its run's ini key set (an older installer's id is 0
// there: plugin.cpp PackContent). nullptr: not ours, or not installed.
const packs::Pack* Installed(const Config& cfg,int id) noexcept {
    const int run=RunOf(id);
    const int key=run==packs::kEdf5 ? cfg.edf5CampaignContent : run==packs::kRange ? cfg.testRangeContent
                : run==packs::kEdf41 ? cfg.edf41Content : 0;
    return key==run && run ? cfg.missionPacks.Find(id) : nullptr;
}
// Owned by us: installed, a pack with placeholder rows only while the mission select keeps them locked (without
// that hook a placeholder cleared before its resources went missing would be selectable: a crash), and only while
// the save-load hook marks its saves with their layout (MigrateMissionPackSave). An unmarked save played and written
// back with the rows now could no longer be told from one with an older install's rows: the next load with the hook
// in would move rows that are already right.
bool Ours(const Config& cfg,int id) noexcept {
    const packs::Pack* pack=Installed(cfg,id);
    return pack && (missionsReady || !pack->MissingCount()) && saveHookReady;
}

// The lobby's join (0x8EC659 in 0x8EC270, the room list's "join") refused one of our ids: say which pack, and why
// this machine does not have it. The game itself says nothing there for a content id past 2: its only message on
// that branch (Lobby_WarnWeaponEquip, 0x8EC6BB..0x8EC7B0, then the join anyway through 0x8E7060) needs the content's
// name from 0xE3F80, which has none past 2 (0xE3FA4), so the join just does not happen: this dialog is the only one.
void ExplainRefusedPack(const Config& cfg,int id) noexcept {
    wchar_t text[512]{};
    const packs::Pack* pack=Installed(cfg,id);
    const bool select=missionsReady || !pack || !pack->MissingCount();
    if(pack && !select)std::swprintf(text,std::size(text),L"房间的任务包是「%ls」。本机装了它，但它有 %d 关缺资源，而锁住这些关的选关钩子没有装上"
        L"（日志 HOOK ported missions=0），所以不能使用这个任务包。\n\n请把日志发给作者。",packs::Name(id),pack->MissingCount());
    else if(pack)std::swprintf(text,std::size(text),L"房间的任务包是「%ls」。本机装了它，但给任务包存档换算关号的读档钩子没有装上"
        L"（日志 CALLS ownership），所以不能使用这个任务包。\n\n请把日志发给作者。",packs::Name(id));
    else packs::Explain(id,packs::Admission{packs::Verdict::kNotInstalled},nullptr,text,std::size(text));
    const char* const reason=!pack ? packs::Reason(packs::Verdict::kNotInstalled) : !select ? "pack-placeholders-unguarded"
                           : "pack-save-hook-missing";
    Log("ROOM join refused: reason=%s content=%d (the room's mission pack is not usable here)",reason,id);
    char key[64]{};
    std::snprintf(key,sizeof(key),"room-pack;%d;%s",id,reason);
    notice::Post(key,L"全军出击：无法加入联机房间",text,0);
}

// One hook per site, each asking what that site called before (so a chained plugin still sees every call).
template<std::size_t I> bool __fastcall OwnedHook(std::uintptr_t mgr,int id) {
    const bool previous=prior[I](mgr,id);
    const auto& cfg=Cfg();
    const bool mod=campaignReady && Ours(cfg,id);
    const bool allowed=previous || mod;
    // Only the lobby's join check: the pack-list sites also run while drawing the menu.
    // Coop's inner hook logs its native answer before ours has added MOD ownership; that is not this final result.
    if constexpr(I==2) {
        char owned[64]{};
        std::size_t n=0;
        for(int p=packs::kBase;p<packs::kBase+packs::kCount && n+4<sizeof(owned);++p)
            if(Ours(cfg,p))n+=static_cast<std::size_t>(std::snprintf(owned+n,sizeof(owned)-n,"%s%d",n ? "," : "",p));
        Log("ROOM content check site=%#x id=%d prior=%d mod=%d allowed=%d hooks_ready=%d EDF5=%d..%d range=%d EDF41=%d..%d packs=%s",
            kSites[I],id,previous,mod,allowed,campaignReady,cfg.edf5CampaignContent,
            cfg.edf5CampaignContent>0 ? cfg.edf5CampaignContent+kPacks-1 : 0,cfg.testRangeContent,
            cfg.edf41Content,cfg.edf41Content>0 ? cfg.edf41Content+kPacks41-1 : 0,n ? owned : "none");
        if(!allowed && campaignReady && packs::Reserved(id))ExplainRefusedPack(cfg,id);
    }
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

// The installed pack of a room's content id, for the room's identity (mod_room.cpp): nullptr when it is not ours or
// not installed here.
const packs::Pack* InstalledMissionPack(std::int64_t id) noexcept {
    return packs::Reserved(id) ? Installed(Cfg(),static_cast<int>(id)) : nullptr;
}

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
int lastOpened=-2,lastContent=-2;   // the last opening logged (one line per change, not per opening)
int lastLocked=-2,lastLockedContent=-2;
// The pack of the scan under way whose placeholder rows were locked at its row 0 (nullptr: none): its rows never
// count as cleared (ClearedHook). Points into a published Config, never freed (plugin.cpp).
const packs::Pack* scanPack=nullptr;

// The current mode's content id (as 0x8A20F3 reads it), -1 when there is none to read.
int ContentOf(unsigned char* gs) noexcept {
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
int CurrentContent(unsigned char*& gs) noexcept {
    gs=Readable(image+kGameStatus,8) ? At<unsigned char*>(image,kGameStatus) : nullptr;
    return ContentOf(gs);
}

// The rows' vector: {begin, end, capacity} at the caller's rsp+0x48. PortedMissionsUnlocked opens a story pack's rows;
// in any of our packs the placeholder rows are then locked (shown, not selectable).
void OpenRows(unsigned char* vec) noexcept {
    scanPack=nullptr;
    const auto& cfg=Cfg();
    if(!vec || !Readable(vec,0x18))return;
    unsigned char* gs=nullptr;
    const int content=CurrentContent(gs);
    const packs::Pack* const pack=Installed(cfg,content);
    // The test range's pack always: its rows (the range, then the PvP presets, tools/make_edf5_campaign.py PVP_ROWS)
    // never end, so none would ever open the next.
    const bool unlock=(cfg.portedMissionsUnlocked && ported::StoryPack(content,cfg.edf5CampaignContent,cfg.edf41Content))
        || (cfg.testRangeContent>0 && content==cfg.testRangeContent);
    const bool lock=pack && pack->MissingCount()>0;
    if(!unlock && !lock)return;
    auto* const begin=At<ported::ListRow*>(vec,0);
    auto* const end=At<ported::ListRow*>(vec,8);
    std::size_t n=0;
    if(begin && end>begin) {
        const auto bytes=static_cast<std::size_t>(reinterpret_cast<unsigned char*>(end)-reinterpret_cast<unsigned char*>(begin));
        if(bytes%sizeof(ported::ListRow)==0 && Readable(begin,bytes,true))n=bytes/sizeof(ported::ListRow);
    }
    if(unlock) {
        const int opened=n ? ported::OpenAll(begin,n,[gs](unsigned row){ return field5(gs+kModeCfg,row); }) : -1;
        if(opened!=lastOpened || content!=lastContent) {
            if(opened>=0)Log("PORTED missions unlocked: content %d, %d rows opened",content,opened);
            else Log("PORTED missions: content %d, the rows are not the stock list's: left stock",content);
        }
        lastOpened=opened;lastContent=content;
    }
    if(lock) {
        // After the opening: a placeholder row is never selectable, PortedMissionsUnlocked or not.
        const int locked=n ? packs::LockPlaceholders(begin,n,*pack) : -1;
        if(locked>=0)scanPack=pack;
        if(locked!=lastLocked || content!=lastLockedContent)
            Log("PACK placeholders: content %d (%s), %d of %d rows locked (the list has %zu)",content,pack->tag,locked,
                pack->MissingCount(),n);
        lastLocked=locked;lastLockedContent=content;
    }
}

bool __fastcall ClearedHook(unsigned row,unsigned char* vec) {
    if(row==0) {
        __try { OpenRows(vec); } __except(EXCEPTION_EXECUTE_HANDLER){ scanPack=nullptr;Log("PORTED missions: opening the rows failed (stock)"); }
    }
    const bool cleared=priorCleared(row);
    // A placeholder never counts as cleared: one cleared before this machine lost its resources would be shown
    // cleared (0, selectable) and load a mission that is not there. Its successors are the row after it already.
    return cleared && !(scanPack && scanPack->Missing(static_cast<int>(row)));
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
int GameContentId() noexcept {
    unsigned char* gs=nullptr;
    return CurrentContent(gs);
}

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
// After a mode's save was loaded (airstrike.cpp ScriptUnlockDlcHook, the 0x70FF87 entry only: the script's SetMode,
// LoadSaveData_ModeSelect, then UnlockDownloadContents, MAINSCRIPT.AS 842..852): one of our packs' save, at
// GS+0x57A4, marked with its layout and moved to the rows now once (mission_packs.h MigrateModeSave). Before the game
// writes it again, so the next save has the new rows and the marker. A save whose layout cannot be told is left as it
// is and the player told, once a session per pack.
namespace { int lastMigration=-1,lastMigrationContent=-1; }
void SetMissionPackSaveHook(bool ready) noexcept { saveHookReady=ready; }
void MigrateMissionPackSave(unsigned char* gs) noexcept {
    const int content=ContentOf(gs);
    const packs::Pack* const pack=Installed(Cfg(),content);
    if(!pack || !Readable(gs+packs::kModeSave,packs::kModeSaveSize,true))return;
    unsigned char* const save=gs+packs::kModeSave;
    std::uint32_t marker=0;
    std::memcpy(&marker,save+packs::kMarker,4);
    const int high=packs::HighestUsedRow(save);
    const packs::Migration m=packs::MigrateModeSave(save,*pack);
    if(m!=packs::Migration::kAlready && (static_cast<int>(m)!=lastMigration || content!=lastMigrationContent || m==packs::Migration::kRemapped))
        Log("PACK save: content %d (%s) %s (marker %08x -> layout %08x, highest row %d, stable rows %d, older layouts %d)",
            content,pack->tag,packs::MigrationName(m),marker,pack->layout,high,pack->stableRows,pack->fromCount);
    lastMigration=static_cast<int>(m);lastMigrationContent=content;
    if(m!=packs::Migration::kForeign && m!=packs::Migration::kUndetermined)return;
    wchar_t text[640]{};
    if(m==packs::Migration::kUndetermined)
        std::swprintf(text,std::size(text),L"「%ls」的存档是 2026-10-18 以前的全军出击写的。那时缺资源的关会从列表里删掉，后面的关整体前移；"
            L"这份存档用到了第 %d 关以后，而本机没有当时安装的任务列表记录（或记录与存档对不上），无法确定它当时缺了哪几关。\n\n"
            L"存档没有被改动，也没有打上新布局的标记：第 %d 关以后的通关记录可能错位。如果当时安装器没有跳过这个任务包的任何关，记录是对的。",
            packs::Name(content),pack->stableRows+1,pack->stableRows+1);
    else
        std::swprintf(text,std::size(text),L"「%ls」的存档是另一版本的全军出击写的（关卡布局 %08x，本版是 %08x），本版不知道怎样换算它的关号。\n\n"
            L"存档没有被改动：通关记录可能与关卡对不上。请换回写这份存档的版本，或更新到最新版本的全军出击。",
            packs::Name(content),marker,pack->layout);
    char key[48]{};
    std::snprintf(key,sizeof(key),"pack-save;%d;%s",content,packs::MigrationName(m));
    notice::Post(key,L"全军出击：任务包存档",text,notice::kOnce);
}

namespace {
// ---- EDF4.1's HQ background (tools/edf41_menu_bg.py, docs/mission-list-re.md 字段 7) ----
// lyt_bg.sgo's HUiBG (constructor 0x8BE0E0) keeps its four layouts BG_BASE .. BG_BASE_D as weak_ptrs at +0x770 and
// shows the list's member 7-th (0x8BE6E0, from the UI message 0x14 through 0x8BE6B0) by each one's +0x1F8. The
// installer adds a fifth layout, BG_BASE_EDF41 (4.1's Menu_BG01 reprojected), first in the tree: after the stock
// choice, an EDF4.1 pack's mode shows it and hides the four; any other mode hides it (and the stock four cover it
// anyway). Found by name through the layout's own child lookup (0x839FD0, as the constructor finds the four), each
// time the theme is set; not there (not installed): nothing changes. Purely local UI: nothing goes over the network,
// host and guest each pick it from their own current mode (the room's).
constexpr unsigned kBgThemeCall=0x8BE6B6;   // in 0x8BE6B0: call 0x8BE6E0 (rcx = the HUiBG, edx = the theme)
constexpr unsigned kBgTheme=0x8BE6E0;
constexpr unsigned kChildByName=0x839FD0;   // (layout, shared_ptr* out, const std::wstring* name) -> out
constexpr std::size_t kBgLayouts=0x770,kShown=0x1F8;
const unsigned char kBgThemeCallerSig[]={0x48,0x83,0xEC,0x28,0x8B,0x12,0xE8,0x25,0x00,0x00,0x00,0x32,0xC0,0x48,0x83,0xC4,
    0x28,0xC3};
const unsigned char kBgThemeSig[]={0x85,0xD2,0x0F,0x88,0xDE,0x00,0x00,0x00,0x41,0x56,0x48,0x83,0xEC,0x20,0x48,0x89,0x6C,
    0x24,0x38,0x48,0x8B,0xA9,0x78,0x07,0x00,0x00};
const unsigned char kChildByNameSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x40,0x48,
    0x8B,0xDA,0x48,0x8B,0xF1,0x48,0x8D,0x54,0x24,0x20,0xE8};
using BgThemeFn=void(__fastcall*)(unsigned char*,int);
struct SharedRef { unsigned char* ptr; unsigned char* ctrl; };   // MSVC shared_ptr / weak_ptr: object, control block
using ChildByNameFn=SharedRef*(__fastcall*)(unsigned char*,SharedRef*,const std::wstring*);
BgThemeFn priorBgTheme=nullptr;
ChildByNameFn childByName=nullptr;
bool menuBgReady=false;
int lastBgState=-1;   // the last switch logged: 1 shown, 0 hidden, 2 not installed

// MSVC _Ref_count_base: uses at +8, weaks at +0xC; slot 0 destroys the object, slot 1 the block.
void Release(SharedRef& r) noexcept {
    if(!r.ctrl)return;
    auto* const uses=reinterpret_cast<volatile long*>(r.ctrl+8);
    auto* const weaks=reinterpret_cast<volatile long*>(r.ctrl+0xC);
    using Fn=void(__fastcall*)(unsigned char*);
    Fn* const vtable=*reinterpret_cast<Fn**>(r.ctrl);
    if(_InterlockedDecrement(uses)==0) {
        vtable[0](r.ctrl);
        if(_InterlockedDecrement(weaks)==0)vtable[1](r.ctrl);
    }
    r={};
}

void ApplyEdf41Bg(unsigned char* bg) noexcept {
    static const std::wstring name=L"BG_BASE_EDF41";   // tools/edf41_menu_bg.py LAYOUT
    SharedRef child{};
    childByName(bg,&child,&name);
    if(!child.ptr || !Readable(child.ptr+kShown,1)){Release(child);if(lastBgState!=2)Log("EDF41 HQ background: not installed (no %ls)",name.c_str());lastBgState=2;return;}
    unsigned char* gs=nullptr;
    const bool edf41=ported::Edf41Pack(CurrentContent(gs),Cfg().edf41Content);
    child.ptr[kShown]=edf41 ? 1 : 0;
    if(edf41) {   // the four stock layouts hidden (the alive ones: a weak_ptr's uses count > 0)
        auto* const begin=At<SharedRef*>(bg,kBgLayouts);
        auto* const end=At<SharedRef*>(bg,kBgLayouts+8);
        for(SharedRef* w=begin;w && w<end && Readable(w,sizeof(SharedRef));++w)
            if(w->ptr && w->ctrl && *reinterpret_cast<volatile long*>(w->ctrl+8)>0)w->ptr[kShown]=0;
    }
    Release(child);
    if(lastBgState!=static_cast<int>(edf41)) {
        Log("EDF41 HQ background %s (content %d, EDF41 %d..%d)",edf41 ? "shown" : "hidden",CurrentContent(gs),
            Cfg().edf41Content,Cfg().edf41Content>0 ? Cfg().edf41Content+kPacks41-1 : 0);
        lastBgState=static_cast<int>(edf41);
    }
}

void __fastcall BgThemeHook(unsigned char* bg,int theme) {
    priorBgTheme(bg,theme);
    __try { ApplyEdf41Bg(bg); } __except(EXCEPTION_EXECUTE_HANDLER){ Log("EDF41 HQ background: the switch failed (stock)"); }
}
}  // namespace

// The HUiBG theme call redirected (BgThemeHook): with no fifth layout installed it does exactly the stock call.
bool InstallEdf41MenuBg() noexcept {
    static bool tried=false;
    if(tried)return menuBgReady;
    tried=true;
    if(!Matches(kBgThemeCall-6,kBgThemeCallerSig,sizeof(kBgThemeCallerSig)) || !Matches(kBgTheme,kBgThemeSig,sizeof(kBgThemeSig))
       || !Matches(kChildByName,kChildByNameSig,sizeof(kChildByNameSig))) {
        Log("HOOK edf41 HQ background=0 (the background code is not the one read)");
        return false;
    }
    unsigned char* const target=CallTarget(kBgThemeCall);
    if(!target || !(target==image+kBgTheme || target<image || target>=image+edf::kImageSize)) {
        Log("HOOK edf41 HQ background=0 (EDF+%#x is no call to EDF+%#x)",kBgThemeCall,kBgTheme);
        return false;
    }
    if(target!=image+kBgTheme)Log("EDF41 HQ background call %#x already redirected (another plugin): chained",kBgThemeCall);
    priorBgTheme=reinterpret_cast<BgThemeFn>(target);
    childByName=reinterpret_cast<ChildByNameFn>(image+kChildByName);
    bool changed=false;
    menuBgReady=RedirectCall(image+kBgThemeCall,target,reinterpret_cast<void*>(&BgThemeHook),changed);
    Log("HOOK edf41 HQ background=%d%s",menuBgReady,menuBgReady ? "" : changed ? " half patched" : " not patched");
    return menuBgReady;
}
}  // namespace crew
