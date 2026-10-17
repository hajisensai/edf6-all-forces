// The EDF4.1 outfit (outfit41.h; docs/edf41-outfits.md). The save's outfit number stays the stock -1..3: the choice is
// the plugin's own (EDF6VehicleCrew.outfit41.ini, per class), and the game is handed the outfit's paths
// (tools/edf41_outfits.py installs them, new names only) where it asks for this machine's first player's model:
//   - the battle model: 0xE34E0 from the player creation 0x5A3F90 (0x5A3FFC) and from the online session preload
//     0x59DC90 (0x59DD27), 0xD8730 from the offline preload 0x59DE50 (0x59DE91). The creation's description says
//     whose model it is (+0: the local slot, -1 another machine's); the session preload's member says it (+0x10 bit 1).
//     The creation takes the outfit only when the preload took it for that class (the resource must be preloaded).
//   - the 3D view's model: 0xE3420 from the HQ / room view's messages (4 calls in 0x93B0A0 .. 0x93BD03); the views
//     are this machine's players only.
//   - the HQ outfit popup (built in 0x8AB310): a sixth item "EDF4.1" after the five stock ones, a copy of the first
//     (its std::function given a vtable of ours: picked, it turns the outfit on for the screen's class and rebuilds
//     the view; the five stock items, chained, turn it off). The popup opens on it while it is on.
// Another machine never sees the choice: online the game sends the stock outfit number, so players without the plugin
// (or with it) see that machine's stock outfit (docs: the second step, sending it through EDF6Coop, is not done).
#include "outfit41.h"
#include "outfit41_logic.h"
#include "crew.h"
#include "loadout_file.h"
#include "memory.h"
#include <cwchar>
#include <iterator>
#include <string>

namespace crew {
namespace {
using namespace outfit41;

// EDF.dll 0x678CCB46 RVAs (tools/outfit41_sigs.py checks every one).
constexpr std::size_t kGameStatus=0x20B2890,kGsTables=0x130,kGsClass=0x6E90,kGsStride=0x3E60;
constexpr std::size_t kBattlePath=0xE34E0,kMenuPath=0xE3420,kOwnPath=0xD8730,kPlayerOutfit=0xD8700;
constexpr std::size_t kCreate=0x5A3F90,kCreateOnline=0x591234,kCreateOffline=0x59145B;
constexpr std::size_t kCreatePathCall=0x5A3FFC,kSessionPathCall=0x59DD27,kOwnPathCall=0x59DE91;
constexpr std::size_t kMenuPathCalls[]={0x93B130,0x93B24C,0x93B426,0x93BD03};
constexpr std::size_t kShowDialog=0x119C600,kOutfitDialogCall=0x8ABC10;
constexpr std::size_t kReserve=0xB91D0,kItemCopy=0x89950,kAssign=0x3D450,kClosePopup=0x7E2EB0,kRebuildView=0x8BDE80;
// The five stock items' std::function vtables, in the popup's order (outfit -1, 2, 3, 1, 0); the first is the one ours
// is copied from.
constexpr std::size_t kItemVtables[kStockItems]={0x1805CF0,0x1805D28,0x1805D60,0x1805D98,0x1805DD0};

using PathFn=const wchar_t*(__fastcall*)(void*,int,int);
using Call4=std::uintptr_t(__fastcall*)(std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uintptr_t);
using ReserveFn=void(__fastcall*)(void*,std::size_t);
using ItemCopyFn=void*(__fastcall*)(void*,void*,const void*);
using AssignFn=void*(__fastcall*)(void*,const wchar_t*,std::size_t);
using CloseFn=std::uintptr_t(__fastcall*)(void*);
using RebuildFn=void(__fastcall*)(int,int,int);
using OutfitFn=int(__fastcall*)(void*,int);
using FuncCopyFn=void*(__fastcall*)(void*,void*);
using FuncCallFn=std::uintptr_t(__fastcall*)(void*,void*);

struct Signature { std::size_t rva; const unsigned char* bytes; std::size_t size; };
#include "outfit41_signatures.inc"

// Main thread only (the menus, the mission's load): no lock.
State state;
bool available[kClasses]{};
bool preloaded[kClasses]{};   // the last preload of this machine's first player took the outfit, for this class
wchar_t statePath[MAX_PATH]{};
wchar_t modsDir[MAX_PATH]{};
// What the path calls below are building: 0 nothing of ours, 1 a preload of this machine's first player, 2 its creation.
thread_local int building=0;
FuncCallFn stockCall[kStockItems]{};
void* ourVtable[1+kFuncSlots]{};   // [0] the stock vtable's [-1] (its RTTI locator), then the six slots

unsigned char* GameStatus() noexcept { return At<unsigned char*>(image,kGameStatus); }

bool CheckCode() noexcept {
    for(const auto& s:kSignatures) {
        bool ok=false;
        __try { ok=Matches(s.rva,s.bytes,s.size); } __except(EXCEPTION_EXECUTE_HANDLER) {}
        if(!ok){Log("OUTFIT41 REFUSED: code at EDF+0x%zX is not the expected (%zu bytes)",s.rva,s.size);return false;}
    }
    return true;
}

// --- The choice and the files
void LoadState() noexcept {
    std::string text;
    state=ReadFileBytes(statePath,&text) ? Parse(text) : State{};
}
void SaveState() noexcept {
    if(!ReplaceFileAtomic(statePath,Format(state)))Log("OUTFIT41 could not save %ls",statePath);
}
void CheckFiles() noexcept {
    for(int c=0;c<kClasses;++c) {
        bool all=true;
        for(const wchar_t* rel:kFiles[c].onDisk) {
            wchar_t path[MAX_PATH];
            if(swprintf_s(path,L"%ls%ls",modsDir,rel)<0){all=false;break;}
            const DWORD a=GetFileAttributesW(path);
            all=all && a!=INVALID_FILE_ATTRIBUTES && !(a&FILE_ATTRIBUTE_DIRECTORY);
        }
        available[c]=all;
    }
}
bool On(int cls) noexcept { return ValidClass(cls) && available[cls] && state.on[cls]; }
int ClassOf(int who) noexcept {
    unsigned char* gs=GameStatus();
    return gs && who>=0 && who<2 ? At<std::int32_t>(gs,kGsClass+static_cast<std::size_t>(who)*kGsStride) : -1;
}

// --- The battle model
const wchar_t* __fastcall BattlePathHook(void* tables,int cls,int outfit) {
    if(building==1 && ValidClass(cls)) {
        preloaded[cls]=On(cls);
        if(preloaded[cls])return kFiles[cls].battle;
    }
    if(building==2 && On(cls) && preloaded[cls])return kFiles[cls].battle;
    return reinterpret_cast<PathFn>(image+kBattlePath)(tables,cls,outfit);
}
const wchar_t* __fastcall OwnPathHook(void* gs,int who,int cls) {
    if(OwnPlayer(who) && ValidClass(cls)) {
        preloaded[cls]=On(cls);
        if(preloaded[cls])return kFiles[cls].battle;
    }
    return reinterpret_cast<PathFn>(image+kOwnPath)(gs,who,cls);
}
std::uintptr_t Create(std::uintptr_t desc,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d) {
    std::int32_t who=-1;
    __try { who=At<std::int32_t>(reinterpret_cast<void*>(desc),kDescPlayer); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    const int was=building;
    building=OwnPlayer(who) ? 2 : 0;
    const auto result=reinterpret_cast<Call4>(image+kCreate)(desc,b,c,d);
    building=was;
    return result;
}
std::uintptr_t __fastcall CreateOnlineHook(std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d) { return Create(a,b,c,d); }
std::uintptr_t __fastcall CreateOfflineHook(std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d) { return Create(a,b,c,d); }

// --- The 3D view's model
const wchar_t* __fastcall MenuPathHook(void* tables,int cls,int outfit) {
    if(On(cls))return kFiles[cls].menu;
    return reinterpret_cast<PathFn>(image+kMenuPath)(tables,cls,outfit);
}

// --- The popup
void Rebuild(int who,int cls) noexcept {
    __try {
        unsigned char* gs=GameStatus();
        if(!gs)return;
        const int outfit=reinterpret_cast<OutfitFn>(image+kPlayerOutfit)(gs,who);
        reinterpret_cast<RebuildFn>(image+kRebuildView)(who,cls,outfit);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
// The screen's first player and class from an item's std::function implementation ({vtable, screen}); -1 otherwise.
int ScreenClass(void* impl) noexcept {
    __try {
        auto screen=At<unsigned char*>(impl,kLambdaScreen);
        if(!screen || At<std::int32_t>(screen,kScreenPlayer)!=0)return -1;
        return ClassOf(0);
    } __except(EXCEPTION_EXECUTE_HANDLER){return -1;}
}
void* __fastcall OurCopy(void* impl,void* where) {
    void* made=reinterpret_cast<FuncCopyFn>(*reinterpret_cast<void* const*>(image+kItemVtables[0]+kSlotCopy*8))(impl,where);
    if(made)Put<void*>(made,0,&ourVtable[1]);
    return made;
}
void* __fastcall OurMove(void* impl,void* where) {
    void* made=reinterpret_cast<FuncCopyFn>(*reinterpret_cast<void* const*>(image+kItemVtables[0]+kSlotMove*8))(impl,where);
    if(made)Put<void*>(made,0,&ourVtable[1]);
    return made;
}
std::uintptr_t __fastcall OurPicked(void* impl,void* arg) {
    const int cls=ScreenClass(impl);
    if(ValidClass(cls) && available[cls] && !state.on[cls]) {
        state.on[cls]=true;
        SaveState();
        Log("OUTFIT41 class %d: the EDF4.1 outfit on",cls);
    }
    const auto result=reinterpret_cast<CloseFn>(image+kClosePopup)(At<void*>(arg,0));
    if(ValidClass(cls))Rebuild(0,cls);
    return result;
}
template<int I> std::uintptr_t __fastcall StockPicked(void* impl,void* arg) {
    const int cls=ScreenClass(impl);
    const bool was=On(cls);
    if(was) {
        state.on[cls]=false;
        SaveState();
        Log("OUTFIT41 class %d: the EDF4.1 outfit off (stock item %d)",cls,I);
    }
    const auto result=stockCall[I](impl,arg);
    if(was)Rebuild(0,cls);
    return result;
}
constexpr FuncCallFn kStockHooks[kStockItems]={&StockPicked<0>,&StockPicked<1>,&StockPicked<2>,&StockPicked<3>,&StockPicked<4>};

// Adds our item to the outfit popup's request (`req`, 0x119C600's fourth argument at 0x8ABC10) when the screen is this
// machine's first player's and the class's files are in Mods. Anything not as 0x8AB310 builds it: left alone.
void AddItem(unsigned char* req) noexcept {
    if(At<std::int32_t>(req,kReqType)!=kOutfitDialog)return;
    unsigned char* vec=req+kReqVector;
    auto data=At<unsigned char*>(vec,kVecData);
    auto size=At<std::size_t>(vec,kVecSize);
    if(!data || size!=kStockItems)return;
    auto impl0=At<unsigned char*>(data,kItemImpl);
    if(!impl0 || At<void*>(impl0,0)!=image+kItemVtables[0])return;
    const int cls=ScreenClass(impl0);
    CheckFiles();
    if(!ValidClass(cls) || !available[cls])return;
    if(size==At<std::size_t>(vec,kVecCapacity)) {
        reinterpret_cast<ReserveFn>(image+kReserve)(vec,Grown(size));
        data=At<unsigned char*>(vec,kVecData);
    }
    unsigned char* item=data+size*kItemSize;
    Put<std::size_t>(vec,kVecSize,size+1);   // as the game's loop: the size first, then the item made in place
    reinterpret_cast<ItemCopyFn>(image+kItemCopy)(vec,item,data);
    reinterpret_cast<AssignFn>(image+kAssign)(item+kItemText,kItemLabel,std::size(kItemLabel)-1);
    auto impl=At<unsigned char*>(item,kItemImpl);
    if(impl)Put<void*>(impl,0,&ourVtable[1]);
    Put<std::int32_t>(req,kReqCursor,Cursor(On(cls),At<std::int32_t>(req,kReqCursor)));
}
std::uintptr_t __fastcall ShowDialogHook(std::uintptr_t system,std::uintptr_t out,std::uintptr_t type,std::uintptr_t req) {
    __try { AddItem(reinterpret_cast<unsigned char*>(req)); } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("OUTFIT41 the popup's request was not as expected: no EDF4.1 item");
    }
    return reinterpret_cast<Call4>(image+kShowDialog)(system,out,type,req);
}

int Redirect(std::size_t site,std::size_t target,void* hook) noexcept {
    bool changed=false;
    if(RedirectCall(image+site,image+target,hook,changed))return 1;
    Log("OUTFIT41 call site %#zx %s",site,changed ? "half patched" : "not redirected (another hook holds it)");
    return 0;
}
}  // namespace

std::uintptr_t Outfit41SessionPreload(PlayerPreloadFn call,std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d) {
    bool own=false;
    __try {
        auto member=a ? At<unsigned char*>(reinterpret_cast<void*>(a),0) : nullptr;
        own=member && !RemoteMember(At<std::uint32_t>(member,kMemberFlags));
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    const int was=building;
    building=own ? 1 : 0;
    const auto result=call(a,b,c,d);
    building=was;
    return result;
}

bool InstallOutfit41() noexcept {
    const wchar_t* ini=ConfigPath();
    if(!ini || !*ini)return false;
    wcscpy_s(statePath,ini);
    wchar_t* dot=wcsrchr(statePath,L'.');
    if(!dot || wcscpy_s(dot,static_cast<std::size_t>(std::end(statePath)-dot),L".outfit41.ini"))return false;
    const DWORD n=GetModuleFileNameW(nullptr,modsDir,MAX_PATH);
    wchar_t* slash=n && n<MAX_PATH ? wcsrchr(modsDir,L'\\') : nullptr;
    if(!slash)return false;
    slash[1]=0;
    if(wcscat_s(modsDir,L"Mods\\"))return false;
    if(!CheckCode())return false;
    LoadState();
    CheckFiles();
    // our item's vtable: the first stock item's, with copy / move / call ours
    auto stock=reinterpret_cast<void* const*>(image+kItemVtables[0]);
    ourVtable[0]=stock[-1];
    for(int i=0;i<kFuncSlots;++i)ourVtable[1+i]=stock[i];
    ourVtable[1+kSlotCopy]=reinterpret_cast<void*>(&OurCopy);
    ourVtable[1+kSlotMove]=reinterpret_cast<void*>(&OurMove);
    ourVtable[1+kSlotCall]=reinterpret_cast<void*>(&OurPicked);
    int battle=Redirect(kOwnPathCall,kOwnPath,reinterpret_cast<void*>(&OwnPathHook));
    battle+=Redirect(kSessionPathCall,kBattlePath,reinterpret_cast<void*>(&BattlePathHook));
    battle+=Redirect(kCreatePathCall,kBattlePath,reinterpret_cast<void*>(&BattlePathHook));
    battle+=Redirect(kCreateOnline,kCreate,reinterpret_cast<void*>(&CreateOnlineHook));
    battle+=Redirect(kCreateOffline,kCreate,reinterpret_cast<void*>(&CreateOfflineHook));
    int menu=0;
    for(std::size_t site:kMenuPathCalls)menu+=Redirect(site,kMenuPath,reinterpret_cast<void*>(&MenuPathHook));
    int items=0;
    for(int i=0;i<static_cast<int>(kStockItems);++i) {
        void* next=nullptr;
        auto slot=reinterpret_cast<void**>(image+kItemVtables[i])+kSlotCall;
        if(edf::ChainVtableSlot(slot,reinterpret_cast<void*>(kStockHooks[i]),&next)){stockCall[i]=reinterpret_cast<FuncCallFn>(next);++items;}
    }
    // our item only once every stock item can turn the outfit off again
    const int popup=items==static_cast<int>(kStockItems) ?
        Redirect(kOutfitDialogCall,kShowDialog,reinterpret_cast<void*>(&ShowDialogHook)) : 0;
    Log("OUTFIT41 battle=%d/5 menu=%d/%zu stock items=%d/%zu popup=%d files ranger=%d wingdiver=%d airraider=%d fencer=%d "
        "on=%d%d%d%d (%ls)",battle,menu,std::size(kMenuPathCalls),items,kStockItems,popup,available[0],available[1],
        available[2],available[3],state.on[0],state.on[1],state.on[2],state.on[3],statePath);
    return battle==5 && menu==static_cast<int>(std::size(kMenuPathCalls)) && popup==1;
}
}  // namespace crew
