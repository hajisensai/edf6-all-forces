// The plugins' keys in the game's own key config (docs/plan-2026-10-25-keybinds.md §4, docs/keyconfig-re.md §6..§7).
//
// The keys live in the game's key store (common/edf/keybind.h): slots 0x4F.. no code of the game reads, saved raw with
// the rest of COMMON.CFG. This plugin
//   - seeds each of its slots once from its ini (the store's marker slot says up to which byte; EDF6AutoTurret seeds its
//     own the same way, while this plugin says the pages are offered: EDF6VehicleCrew_KeyConfigPagesV1),
//   - reads its keys from them (OverlayStockKeys: into the published config, so every module's Cfg().xxxKey is the
//     store's), the Q mark from the stock 「标记」 (Spot, action 0x1B),
//   - and offers them as six more pages of the stock key config (pages 11..16, the top list's 「插件」 items): the
//     stock rows, the stock 「按下新键」 dialog and the stock conflict check within a page (a key taken by another row of
//     the page swaps), the stock 「恢复默认」 (the slots' defaults). keyconfig_menu.h has the sites and the stubs.
// All of it only with the pages offered (InstallKeyConfig): without them nothing could change the store's keys, so the
// store is neither seeded nor read and the ini's *Key values stay the keys.
// The keyboard only: the stock pad pages are 11 fixed buttons without the d-pad or Start the plugins' pad buttons use;
// those stay the ini's.
#include "keyconfig.h"
#include "keyconfig_menu.h"
#include "code_signatures.h"
#include "edf/keybind.h"
#include "edf/patch.h"
#include "edf/wstr.h"
#include "game_dialog.h"
#include "hud.h"
#include "hudtext.h"
#include "memory.h"
#include <atomic>
#include <cstring>
#include <iterator>

namespace crew {
void RepublishStockKeys() noexcept;   // plugin.cpp: the published config's keys from the store, when they changed

namespace {
namespace kb=edf::keybind;
using namespace keymenu;
using Signature=CodeSignature;
#include "keyconfig_signatures.inc"

// The label of each slot, in kSlots' order, and each page's name.
constexpr hudtext::Tx kSlotText[]={
    hudtext::Tx::kbFormation,hudtext::Tx::kbPickup,hudtext::Tx::kbHail,hudtext::Tx::kbFirstPerson,hudtext::Tx::kbMap,
    hudtext::Tx::kbDamageStats,hudtext::Tx::kbRoomLoadout,hudtext::Tx::kbChuteCut,
    hudtext::Tx::kbAutoAim,
    hudtext::Tx::kbFreeLook,hudtext::Tx::kbHighCam,hudtext::Tx::kbSightZoom,hudtext::Tx::kbRangeUp,hudtext::Tx::kbRangeDown,
    hudtext::Tx::kbThermal,hudtext::Tx::kbSeatNext,hudtext::Tx::kbSwitch,hudtext::Tx::kbSecondary,hudtext::Tx::kbGunshipGunner,
    hudtext::Tx::kbGatling,
    hudtext::Tx::kbJetBoost,hudtext::Tx::kbJetBrake,hudtext::Tx::kbJetTarget,hudtext::Tx::kbJetFlare,hudtext::Tx::kbJetGear,
    hudtext::Tx::kbDrillLaunch,hudtext::Tx::kbProteusMode,hudtext::Tx::kbProteusShield,hudtext::Tx::kbProteusSalvo,
    hudtext::Tx::kbDnBoost,hudtext::Tx::kbDnDescend,hudtext::Tx::kbDnLeft,hudtext::Tx::kbDnRight,hudtext::Tx::kbDnHangar,
    hudtext::Tx::kbDnAhead,
    hudtext::Tx::kbSzLock,hudtext::Tx::kbSzDash,hudtext::Tx::kbSzDescend,hudtext::Tx::kbSzFunnel,hudtext::Tx::kbSzCannon,
    hudtext::Tx::kbSzMelee,hudtext::Tx::kbSzGuard,
};
static_assert(sizeof(kSlotText)/sizeof(kSlotText[0])==static_cast<std::size_t>(kb::kSlotCount),"a label for every slot");
constexpr hudtext::Tx kPageText[kb::kPages]={hudtext::Tx::kbPageFoot,hudtext::Tx::kbPageVehicle,hudtext::Tx::kbPageAir,
                                              hudtext::Tx::kbPageGround,hudtext::Tx::kbPageDreadnought,hudtext::Tx::kbPageSazabi};

// The config's key of each of this plugin's slots (by the slot's ini name).
struct KeyField { const wchar_t* ini; int Config::* key; };
constexpr KeyField kFields[]={
    {L"NpcFormationCycleKey",&Config::npcFormationKey},{L"NpcPickupKey",&Config::npcPickupKey},
    {L"PlayerJetHailKey",&Config::playerJetHailKey},{L"FirstPersonKey",&Config::firstPersonKey},
    {L"MapKey",&Config::mapKey},{L"DamageStatsKey",&Config::damageStatsKey},
    {L"RoomLoadoutKey",&Config::roomLoadoutKey},{L"PlayerJetChuteCutKey",&Config::playerJetChuteCutKey},
    {L"FreeLookKey",&Config::freeLookKey},{L"HighCamKey",&Config::highCamKey},
    {L"SightZoomKey",&Config::sightZoomKey},{L"SightRangeUpKey",&Config::sightRangeUpKey},
    {L"SightRangeDownKey",&Config::sightRangeDownKey},{L"VehicleThermalKey",&Config::thermalKey},
    {L"SeatNextKey",&Config::seatNextKey},{L"PlayerJetSwitchKey",&Config::playerJetSwitchKey},
    {L"VehicleSecondarySwitchKey",&Config::vehicleSecondarySwitchKey},{L"GunshipGunnerKey",&Config::gunshipGunnerKey},
    {L"GunshipGatlingKey",&Config::gunshipGatlingKey},
    {L"PlayerJetBoostKey",&Config::playerJetBoostKey},{L"PlayerJetBrakeKey",&Config::playerJetBrakeKey},
    {L"PlayerJetTargetKey",&Config::playerJetTargetKey},{L"PlayerJetFlareKey",&Config::playerJetFlareKey},
    {L"PlayerJetGearKey",&Config::playerJetGearKey},{L"DrillLaunchKey",&Config::drillLaunchKey},
    {L"ProteusStanceKey",&Config::proteusModeKey},{L"ProteusShieldKey",&Config::proteusShieldKey},
    {L"ProteusSalvoKey",&Config::proteusSalvoKey},
    {L"DreadnoughtBoostKey",&Config::dreadnoughtBoostKey},{L"DreadnoughtDescendKey",&Config::dreadnoughtDescendKey},
    {L"DreadnoughtStrafeLeftKey",&Config::dreadnoughtStrafeLeftKey},
    {L"DreadnoughtStrafeRightKey",&Config::dreadnoughtStrafeRightKey},
    {L"DreadnoughtHangarKey",&Config::dreadnoughtHangarKey},{L"DreadnoughtAheadKey",&Config::dreadnoughtAheadKey},
    {L"SazabiLockKey",&Config::sazabiLockKey},{L"SazabiDashKey",&Config::sazabiDashKey},
    {L"SazabiDescendKey",&Config::sazabiDescendKey},{L"SazabiFunnelKey",&Config::sazabiFunnelKey},
    {L"SazabiMegaCannonKey",&Config::sazabiCannonKey},{L"SazabiMeleeKey",&Config::sazabiMeleeKey},
    {L"SazabiGuardKey",&Config::sazabiGuardKey},
};
constexpr int kFieldCount=static_cast<int>(sizeof(kFields)/sizeof(kFields[0]));
constexpr bool SameName(const wchar_t* a,const wchar_t* b) noexcept {
    while(*a && *a==*b){++a;++b;}
    return *a==*b;
}
constexpr const KeyField* FieldOf(const wchar_t* ini) noexcept {
    for(const KeyField& f:kFields)if(SameName(f.ini,ini))return &f;
    return nullptr;
}
constexpr bool EveryCrewSlotHasAField() noexcept {
    int crew=0;
    for(const kb::Slot& s:kb::kSlots) {
        if(s.owner!=kb::Owner::crew)continue;
        ++crew;
        if(!FieldOf(s.ini))return false;
    }
    return crew==kFieldCount;
}
static_assert(EveryCrewSlotHasAField(),"each of this plugin's slots is one config key, each listed key one slot");

int Get(const Config& c,const KeyField& f) noexcept { return c.*f.key; }
void Set(Config& c,const KeyField& f,int vk) noexcept {
    c.*f.key=vk;
}

// --- The store
// The key store when built (the option object's, ready flag 1), else nullptr. Under __try (any thread may ask).
unsigned char* KeyStore() noexcept {
    __try {
        if(!image)return nullptr;
        unsigned char* options=At<unsigned char*>(image,kb::kOptions);
        if(!options || !Readable(options+kb::kStore,kb::kStoreSize,true))return nullptr;
        unsigned char* store=options+kb::kStore;
        return At<std::int32_t>(store,kb::kReady)==1 ? store : nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER){return nullptr;}
}
std::atomic<bool> installed{false};   // the pages offered (InstallKeyConfig): the store's slots are the keys
std::atomic<bool> seededLogged{false};
std::atomic<bool> markWanted{true};   // the ini's NpcMarkKey: 0 the custom Q off, else on with the stock 「标记」's key
// The ini's keys as LoadConfig read and validated them (OverlayStockKeys from the ini, before the store's go over them):
// what a slot is seeded with. Index: kFields'.
std::atomic<int> iniKeys[kFieldCount];
std::atomic<bool> iniKeysRead{false};
// This plugin's slots its marker does not cover yet, from the ini; EDF6AutoTurret's own are its business.
void EnsureSeeded(unsigned char* store) noexcept {
    if(kb::Seeded(store,kb::Owner::crew) || !iniKeysRead.load())return;
    const int from=kb::SeededEnd(store,kb::Owner::crew);
    int unmapped=0;
    const int written=kb::Seed(store,kb::Owner::crew,[&](const kb::Slot& s) noexcept {
        const KeyField* f=FieldOf(s.ini);
        const int vk=f ? iniKeys[f-kFields].load() : s.defaultVk;
        if(vk && kb::CodeOfVk(vk)==kb::kNoAssign) {
            Log("KEYCONFIG %ls=0x%X has no key code in the game: the slot left unassigned (set it in the key config)",s.ini,vk);
            ++unmapped;
        }
        return vk;
    });
    if(from>kb::kFirstAction)
        Log("KEYCONFIG %d key(s) added since the store was seeded (slots 0x%X..) seeded from the ini (%d unmapped); the others kept",
            written,from,unmapped);
    else if(!seededLogged.exchange(true))
        Log("KEYCONFIG this plugin's keys seeded from the ini into the game's key store (slots 0x%X.., %d unmapped): from now on "
            "they are set in the game's key config (the 「插件」 pages), the ini's *Key values are no longer read",kb::kFirstAction,
            unmapped);
    else Log("KEYCONFIG the key store came back without this plugin's marker (COMMON.CFG loaded or reset): seeded from the ini again");
}

// --- The pages' callbacks (the stubs call these on the UI thread)
using KeyNameFn=const wchar_t*(__fastcall*)(void*,unsigned);
using TextLookupFn=const wchar_t*(__fastcall*)(void*,const wchar_t*);
using ResetFn=void(__fastcall*)(void*,int,int);
using AllowedFn=std::uintptr_t(__fastcall*)(void*,std::uintptr_t,std::uintptr_t,std::uintptr_t);
using Call4=std::uintptr_t(__fastcall*)(std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uintptr_t);

hudtext::Lang Language() noexcept { return hudtext::Resolve(Cfg().hudLanguage,GameTextLanguage()); }

const wchar_t* __fastcall KcLabel(int page,int row,void*) noexcept {
    const kb::Slot* s=kb::SlotAt(page,row);
    return s ? hudtext::Tr(kSlotText[kb::IndexOf(s)],Language()) : nullptr;
}
// The row's key as the stock rows show theirs: the key name of (the model, the action), through the text table. A code
// the menu cannot name (outside 0..0x78: never written by the game, the plugin or the seed) is shown unassigned.
const wchar_t* __fastcall KcValue(int page,int row,unsigned char* menu) noexcept {
    const kb::Slot* s=kb::SlotAt(page,row);
    if(!s || !menu)return L"";
    __try {
        void* table=At<void*>(image,kTextTable);
        unsigned char* store=KeyStore();
        if(!table || !store)return L"";
        const int who=At<std::int32_t>(menu,kMenuPlayer);
        const std::int32_t code=who>=0 && who<kb::kPlayers ? kb::Read(store,who,s->action) : kb::kNoAssign;
        const wchar_t* key=kb::ValidCode(code) ? reinterpret_cast<KeyNameFn>(image+kKeyName)(menu+kMenuModel,s->action)
                                               : At<const wchar_t*>(image,kNameTable);
        const wchar_t* text=key ? reinterpret_cast<TextLookupFn>(image+kTextLookup)(table,key) : nullptr;
        return text ? text : L"";
    } __except(EXCEPTION_EXECUTE_HANDLER){return L"";}
}
void __fastcall KcDialog(int page,int row,unsigned char* menu,DialogRow* out) noexcept {
    const kb::Slot* s=kb::SlotAt(page,row);
    out->label=KcLabel(page,row,nullptr);
    if(!out->label)out->label=L"";
    out->value=KcValue(page,row,menu);
    out->action=s ? s->action : 0;
}
// 「恢复默认」 of a page: the plugin's pages get their slots' defaults (all four sets, as their rows write); the stock
// pages the stock's.
void __fastcall ResetHook(void* store,int who,int page) {
    if(!kb::PluginPage(page)) {
        reinterpret_cast<ResetFn>(image+kResetPage)(store,who,page);
        return;
    }
    __try {
        if(who<0 || who>=kb::kPlayers)return;
        for(const kb::Slot& s:kb::kSlots)
            if(s.page==page)kb::WriteSynced(static_cast<unsigned char*>(store),who,s.action,kb::CodeOfVk(s.defaultVk));
        Log("KEYCONFIG page %d back to the plugin's defaults (player %d)",page,who+1);
    } __except(EXCEPTION_EXECUTE_HANDLER){}
}
// The dialog's key check: a plugin row takes a key or a mouse button the plugins read held (not the wheel), the stock
// rows the stock check.
std::uintptr_t __fastcall AllowedHook(void* model,std::uintptr_t a,std::uintptr_t action,std::uintptr_t key) {
    if(kb::SlotOf(static_cast<int>(action&0xFF)))return kb::BindableCode(static_cast<std::int32_t>(key)) ? 1 : 0;
    return reinterpret_cast<AllowedFn>(image+kKeyAllowed)(model,a,action,key);
}
// The top list (type 5, its six keyboard items as 0x892FF0 builds them): one item per plugin page after them, each a
// copy of the common page's item (lambda_3) capturing the page instead of 0, so that its own call opens the key config
// on it (0x88DD00). The pad's list (lambda_1 items) and anything else: left alone.
void AddPages(unsigned char* req) noexcept {
    if(!Cfg().enabled)return;
    if(At<std::int32_t>(req,0x128)!=kTopType)return;
    unsigned char* vec=req+0x150;
    if(gamedialog::Count(vec)!=kTopItems)return;
    unsigned char* impl0=At<unsigned char*>(gamedialog::ItemAt(vec,0),gamedialog::kItemImpl);
    unsigned char* impl5=At<unsigned char*>(gamedialog::ItemAt(vec,kTopItems-1),gamedialog::kItemImpl);
    if(!impl0 || !impl5 || At<void*>(impl0,0)!=image+kPageItemVtable || At<void*>(impl5,0)!=image+kVehicleItemVtable)return;
    if(unsigned char* store=KeyStore())EnsureSeeded(store);
    const gamedialog::ItemOps ops=gamedialog::GameOps(image);
    for(int p=0;p<kb::kPages;++p) {
        unsigned char* item=gamedialog::PushCopyOf(ops,vec,0);
        edf::wstr::Assign(image,item+gamedialog::kItemText,hudtext::Tr(kPageText[p],Language()));
        unsigned char* impl=At<unsigned char*>(item,gamedialog::kItemImpl);
        if(impl && At<void*>(impl,0)==image+kPageItemVtable)Put<std::int32_t>(impl,kItemPage,kb::kFirstPage+p);
    }
}
std::uintptr_t __fastcall TopDialogHook(std::uintptr_t system,std::uintptr_t out,std::uintptr_t type,std::uintptr_t req) {
    __try { AddPages(reinterpret_cast<unsigned char*>(req)); } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("KEYCONFIG the key config's list was not as expected: no plugin pages");
    }
    return reinterpret_cast<Call4>(image+kShowDialog)(system,out,type,req);
}

bool CheckCode() noexcept { return CheckSignatures(kSignatures,"KEYCONFIG"); }

// A redirected call: whether the site now calls the hook (`changed`: a page protection not restored after the write is
// logged, the hook in place all the same).
bool Redirect(std::size_t site,std::size_t target,void* hook) noexcept {
    bool changed=false;
    if(RedirectCall(image+site,image+target,hook,changed))return true;
    Log("KEYCONFIG call site %#zx %s",site,changed ? "redirected, its page protection not restored" : "not redirected (another hook holds it)");
    return changed;
}
// The stock common page's defaults (kStockDefaults) made the plugin's (`ours`; stops at the first that does not take) or
// put back (every one that is the plugin's). True when all of them are so.
bool PatchDefaults(bool ours) noexcept {
    bool ok=true;
    for(const StockDefault& d:kStockDefaults) {
        unsigned char stock[kDefaultSize],mine[kDefaultSize];
        StockDefaultCode(d,false,stock);StockDefaultCode(d,true,mine);
        const bool took=ours ? edf::PatchCode(image+d.rva,stock,mine,kDefaultSize) : edf::PatchCode(image+d.rva,mine,stock,kDefaultSize);
        if(!took && ours)return false;
        ok=ok && took;
    }
    return ok;
}
}  // namespace

bool OverlayStockKeys(Config& c,bool fromIni) noexcept {
    if(fromIni) {
        markWanted.store(c.npcMarkKey!=0);
        for(int i=0;i<kFieldCount;++i)iniKeys[i].store(Get(c,kFields[i]));
        iniKeysRead.store(true);
    }
    if(!installed.load())return false;
    unsigned char* store=KeyStore();
    if(!store)return false;
    __try {
        if(!kb::Seeded(store,kb::Owner::crew))return false;
        for(const kb::Slot& s:kb::kSlots) {
            if(s.owner!=kb::Owner::crew)continue;
            if(const KeyField* f=FieldOf(s.ini))Set(c,*f,kb::SlotVk(store,s.action));
        }
        // The Q mark is the stock 「标记」's key (NpcMarkKey 0: the custom Q off, as ever)
        c.npcMarkKey=markWanted.load() ? kb::SlotVk(store,kb::kSpotAction) : 0;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool StockKeysDiffer(const Config& a,const Config& b) noexcept {
    for(const KeyField& f:kFields)if(Get(a,f)!=Get(b,f))return true;
    return a.npcMarkKey!=b.npcMarkKey;
}

void KeyConfigTick() noexcept {
    if(!installed.load())return;
    unsigned char* store=KeyStore();
    if(!store)return;
    bool changed=false;
    __try {
        EnsureSeeded(store);
        // compared in place first: the config is copied (RepublishStockKeys) only when a key really changed
        const Config& now=Cfg();
        for(const kb::Slot& s:kb::kSlots) {
            if(s.owner!=kb::Owner::crew)continue;
            const KeyField* f=FieldOf(s.ini);
            changed=changed || (f && Get(now,*f)!=kb::SlotVk(store,s.action));
        }
        changed=changed || now.npcMarkKey!=(markWanted.load() ? kb::SlotVk(store,kb::kSpotAction) : 0);
    } __except(EXCEPTION_EXECUTE_HANDLER){return;}
    if(changed)RepublishStockKeys();
}

bool InstallKeyConfig() noexcept {
    if(installed.load())return true;
    if(!CheckCode())return false;
    // Everything built first; nothing is patched unless all of it was.
    unsigned char table[kRows*kRowBytes];
    BuildTable(image+kTable,table);
    void* tableAt=AllocateNearCode(image+kTableLeas[0],table,sizeof(table));
    static unsigned char zeros[kStubSlot*6]{};
    auto* stubs=static_cast<unsigned char*>(AllocateNearCode(image+kRowCount.site,zeros,sizeof(zeros)));
    if(!tableAt || !stubs){Log("KEYCONFIG no memory near EDF.dll for the page table / stubs: no plugin pages");return false;}
    const auto base=reinterpret_cast<std::uint64_t>(image);
    Callbacks cb;
    cb.label=reinterpret_cast<std::uint64_t>(&KcLabel);
    cb.value=reinterpret_cast<std::uint64_t>(&KcValue);
    cb.dialog=reinterpret_cast<std::uint64_t>(&KcDialog);
    Code code[6];
    unsigned char jumps[6][16];
    for(int i=0;i<6;++i) {
        code[i].at=reinterpret_cast<std::uint64_t>(stubs+i*kStubSlot);
        BuildStub(i,code[i],base,cb);
        if(!code[i].ok || code[i].n>kStubSlot ||
           !SiteJump(base+kDispatches[i].site,code[i].at,kDispatches[i].size,jumps[i])) {
            Log("KEYCONFIG the %s stub cannot reach EDF.dll: no plugin pages",kDispatches[i].name);
            return false;
        }
    }
    unsigned char leas[std::size(kTableLeas)][kLeaSize];
    for(std::size_t i=0;i<std::size(kTableLeas);++i)
        if(!LeaTo(base+kTableLeas[i],image+kTableLeas[i],reinterpret_cast<std::uint64_t>(tableAt),leas[i])) {
            Log("KEYCONFIG the page table is out of the lea at EDF+%#zx's reach: no plugin pages",kTableLeas[i]);
            return false;
        }
    // The stubs' code, the readers of the table, the model's all-sets rows, the dispatches, 「恢复默认」 and the dialog's key
    // check: each of these does for the stock pages 0..10 what the stock did, so a failure on the way leaves the stock menu
    // working. Then the two a player sees: the stock common page's defaults (Chat Enter, VoiceChat K: only what a new
    // store or 「恢复默认」 writes) and, last, the top list's items; a failure of either puts the defaults back, so there are
    // the plugin's pages and the changed defaults, or neither.
    bool ok=true;
    for(int i=0;i<6 && ok;++i)ok=edf::PatchCode(stubs+i*kStubSlot,zeros,code[i].b,code[i].n);
    unsigned char stockLea[kLeaSize];
    for(std::size_t i=0;i<std::size(kTableLeas) && ok;++i) {
        std::memcpy(stockLea,image+kTableLeas[i],kLeaSize);
        ok=edf::PatchCode(image+kTableLeas[i],stockLea,leas[i],kLeaSize);
    }
    ok=ok && edf::PatchCode(image+kSyncedRows,kSyncedRowsCode,kSyncedRowsOurs,sizeof(kSyncedRowsCode));
    for(int i=0;i<6 && ok;++i)ok=edf::PatchCode(image+kDispatches[i].site,kDispatchCode[i],jumps[i],kDispatches[i].size);
    ok=ok && Redirect(kResetCall,kResetPage,reinterpret_cast<void*>(&ResetHook));
    ok=ok && Redirect(kAllowedCall,kKeyAllowed,reinterpret_cast<void*>(&AllowedHook));
    if(!ok) {
        Log("KEYCONFIG plugin pages NOT offered (a patch did not take; what took does the stock's for the stock pages): the "
            "stock menu and its defaults as they were, the ini's *Key values the keys");
        return false;
    }
    const bool defaults=PatchDefaults(true);
    if(!defaults || !Redirect(kTopDialogCall,kShowDialog,reinterpret_cast<void*>(&TopDialogHook))) {
        const bool back=PatchDefaults(false);
        Log("KEYCONFIG plugin pages NOT offered (%s did not take): the stock common page's defaults %s, the ini's *Key values "
            "the keys",defaults ? "the top list's call" : "a stock default",back ? "put back" : "NOT all put back");
        return false;
    }
    installed.store(true);
    Log("KEYCONFIG plugin pages %d..%d in the game's key config: offered (%d keys; the keyboard's, the pads' stay the ini's)",
        kb::kFirstPage,kLastPage,kb::kSlotCount);
    return true;
}
}  // namespace crew

// EDF6AutoTurret asks whether the pages are offered before it seeds or reads its slot (common/edf/keybind.h kPagesExport).
extern "C" __declspec(dllexport) bool __cdecl EDF6VehicleCrew_KeyConfigPagesV1() { return crew::installed.load(); }
