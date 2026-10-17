// The EDF4.1 outfit (outfit41.h; docs/edf41-outfits.md). The save's outfit number stays the stock -1..3: the choice is
// the plugin's own (EDF6VehicleCrew.outfit41.ini, per local player as the save's outfit is), and the game is handed the
// outfit's paths (tools/edf41_outfits.py installs them, new names only) where it asks for a player's model:
//   - the battle model: 0xE34E0 from the player creation 0x5A3F90 (0x5A3FFC) and from the online session preload
//     0x59DC90 (0x59DD27), 0xD8730 from the offline preload 0x59DE50 (0x59DE91). Whose model it is: the offline
//     creation's description +0 (the local slot), the online member's mission index (+0x48 of the member 0x595960 is
//     handed, 0x591219; of the member the session preload is handed) and its remote bit (+0x10 bit 1).
//     The preload decides, per local slot and per mission index; the creation takes the outfit only when the preload took
//     it for that class (the resource must be preloaded), so the two never disagree.
//   - the 3D view's model: 0xE3420 from the HQ / room view's OnMessage (0x93B426 msg 3, 0x93BD03 msg 6), for the view's
//     own player (view +0x2AC, read in the chained OnMessage). The views are this machine's players only.
//   - the HQ outfit popup (built in 0x8AB310): a sixth item, EDF4.1's subtitle (hudtext outfit41Item), after the five
//     stock ones, a copy of the first (its std::function given a vtable of ours: picked, it turns the outfit on for the
//     screen's player and rebuilds the view; the five stock items, chained, turn it off). The popup opens on it while on.
// Who wears it (outfit41_logic.h Wears): the player chose it, or the mode is an EDF4.1 pack and the player's outfit is the
// mission's (-1) or the outfit change is not unlocked yet (GS +0x300).
// Online: this machine's member tells the room its two flags (EDF6Coop member attribute EDF6X_OUTFIT41, published from
// the room's frame when they change); a remote member is resolved from its mission index to its EOS id
// (EDF6Coop_ResolveMissionPlayerPuid) and its value read. No value (an older All Forces): the stock outfit, logged; the
// room's version notice names the feature (support_protocol.h kExtOutfit41).
#include "outfit41.h"
#include "outfit41_logic.h"
#include "coop_extension_api.h"
#include "coop_member_attribute_api.h"
#include "crew.h"
#include "edf/patch.h"
#include "hud.h"
#include "hudtext.h"
#include "loadout_file.h"
#include "memory.h"
#include <atomic>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string>

namespace crew {
namespace {
using namespace outfit41;

// EDF.dll 0x678CCB46 RVAs (tools/outfit41_sigs.py checks every one).
constexpr std::size_t kGameStatus=0x20B2890,kGsClass=0x6E90,kGsOutfit=0x6E94,kGsStride=0x3E60,kGsUnlocked=0x300;
constexpr std::size_t kBattlePath=0xE34E0,kMenuPath=0xE3420,kOwnPath=0xD8730,kPlayerOutfit=0xD8700;
constexpr std::size_t kCreate=0x5A3F90,kCreateOnline=0x591234,kCreateOffline=0x59145B;
constexpr std::size_t kMemberDesc=0x595960,kMemberDescCall=0x591219;
constexpr std::size_t kCreatePathCall=0x5A3FFC,kSessionPathCall=0x59DD27,kOwnPathCall=0x59DE91;
constexpr std::size_t kMenuPathCalls[]={0x93B426,0x93BD03};
constexpr std::size_t kViewVtable=0x180FA60;
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
using MessageFn=std::uintptr_t(__fastcall*)(unsigned char*,std::uintptr_t,std::uintptr_t);

struct Signature { std::size_t rva; const unsigned char* bytes; std::size_t size; };
#include "outfit41_signatures.inc"

// The choice and the files: the UI thread writes, the mission's load reads (a bool a player: no lock).
State state;
std::atomic<bool> available[kClasses]{};
wchar_t statePath[MAX_PATH]{};
wchar_t modsDir[MAX_PATH]{};
// What the preloads took, -1 or the class whose EDF4.1 outfit was preloaded: per local slot (offline), per mission index
// (online). The creation reads them, possibly on another thread than the preload.
std::atomic<int> preloadedLocal[kLocalPlayers]{-1,-1};
std::atomic<int> preloadedOnline[kMissionPlayers];
// What the path calls below are building, on the calling thread.
enum class Build { none, preloadOnline, createLocal, createOnline };
struct Building { Build what=Build::none; int who=-1; bool remote=false; };
thread_local Building building;
thread_local int memberIndex=-1;    // 0x595960's member (0x591219), for the creation right after it in 0x591130
thread_local int viewPlayer=-1;     // inside the 3D view's OnMessage: its player
FuncCallFn stockCall[kStockItems]{};
MessageFn nextMessage=nullptr;
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
bool Installed(int cls) noexcept { return ValidClass(cls) && available[cls]; }

// --- Who wears it
bool InPack41() noexcept { return Pack41(GameContentId(),Cfg().edf41Content); }
// A local player's two flags from its save (GS) and the plugin's choice.
Wire LocalWire(int p) noexcept {
    Wire w;
    if(!ValidLocal(p))return w;
    w.chosen=state.on[p];
    __try {
        unsigned char* gs=GameStatus();
        if(gs) {
            const auto outfit=At<std::int32_t>(gs,kGsOutfit+static_cast<std::size_t>(p)*kGsStride);
            w.missionDefault=MissionDefault(outfit,At<unsigned char>(gs,kGsUnlocked)!=0);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){w.missionDefault=false;}
    return w;
}
bool LocalWears(int p,int cls) noexcept {
    if(!Installed(cls) || !ValidLocal(p))return false;
    const Wire w=LocalWire(p);
    return Wears(w.chosen,w.missionDefault,InPack41());
}
int ClassOf(int who) noexcept {
    unsigned char* gs=GameStatus();
    return gs && ValidLocal(who) ? At<std::int32_t>(gs,kGsClass+static_cast<std::size_t>(who)*kGsStride) : -1;
}

// --- The room (EDF6Coop): this machine's value, the others'
const EDF6CoopMemberAttributeApi* attrApi=nullptr;
EDF6CoopResolveMissionPlayerPuidFn resolvePuid=nullptr;
bool coopLooked=false;
void LookCoop() noexcept {
    if(coopLooked)return;
    const HMODULE coop=GetModuleHandleW(L"EDF6Coop.dll");
    if(!coop)return;   // looked again later: EDF6Coop loads before us, but a test harness may not
    coopLooked=true;
    const auto get=reinterpret_cast<EDF6CoopGetMemberAttributeApiFn>(GetProcAddress(coop,"EDF6CoopGetMemberAttributeApi"));
    const EDF6CoopMemberAttributeApi* table=nullptr;
    __try { table=get ? get(EDF6COOP_MEMBER_ATTRIBUTE_VERSION) : nullptr; } __except(EXCEPTION_EXECUTE_HANDLER) { table=nullptr; }
    if(table && (table->version!=EDF6COOP_MEMBER_ATTRIBUTE_VERSION || !EDF6COOP_MA_HAS(table,snapshot)))table=nullptr;
    attrApi=table;
    resolvePuid=reinterpret_cast<EDF6CoopResolveMissionPlayerPuidFn>(GetProcAddress(coop,"EDF6Coop_ResolveMissionPlayerPuid"));
    Log("OUTFIT41 EDF6Coop: member attributes %s, mission player ids %s",attrApi ? "found" : "missing (older EDF6Coop)",
        resolvePuid ? "found" : "missing (older EDF6Coop)");
}
char published[kWireSize]{};   // the value EDF6Coop last accepted from us ("" none yet)
void PublishOwn() noexcept {
    LookCoop();
    if(!attrApi)return;
    char value[kWireSize];
    FormatWire(LocalWire(0),value);
    if(std::strcmp(value,published)==0)return;
    std::uint32_t ok=0;
    __try { ok=attrApi->publish(kAttrKey,value); } __except(EXCEPTION_EXECUTE_HANDLER){ok=0;}
    if(ok){std::memcpy(published,value,sizeof(value));Log("OUTFIT41 told the room %s=%s",kAttrKey,value);}
    else {
        std::uint32_t why=0;
        __try { why=EDF6COOP_MA_HAS(attrApi,lastError) && attrApi->lastError ? attrApi->lastError() : 0u; } __except(EXCEPTION_EXECUTE_HANDLER){why=0;}
        static std::uint32_t loggedWhy=~0u;
        if(why!=loggedWhy){loggedWhy=why;Log("OUTFIT41 EDF6Coop refused %s=%s (lastError %u): tried again next frame",kAttrKey,value,why);}
    }
}
struct Lookup {
    const char* id=nullptr;
    bool found=false,has=false;
    char value[EDF6COOP_MEMBER_ATTRIBUTE_MAX_VALUE+1]{};
};
void EDF6COOP_MA_CALL Each(void* context,const EDF6CoopMemberValue* m) {
    auto* l=static_cast<Lookup*>(context);
    if(!m || !m->memberId || std::strcmp(m->memberId,l->id)!=0)return;
    l->found=true;
    l->has=m->hasValue!=0 && m->value;
    if(l->has)strncpy_s(l->value,m->value,_TRUNCATE);
}
// A remote member's flags by its mission index; false (logged why) when there is no valid value: the stock outfit.
bool RemoteWire(int index,Wire& out) noexcept {
    LookCoop();
    if(!attrApi || !resolvePuid){Log("OUTFIT41 mission player %d: no EDF6Coop member attributes here: stock outfit",index);return false;}
    EDF6CoopPeer peer{};
    std::uint32_t ok=0;
    __try { ok=resolvePuid(index,&peer); } __except(EXCEPTION_EXECUTE_HANDLER){ok=0;}
    if(!ok || !peer.id[0]){Log("OUTFIT41 mission player %d: EDF6Coop does not know its member id: stock outfit",index);return false;}
    Lookup l;l.id=peer.id;
    __try { ok=attrApi->snapshot(kAttrKey,&Each,&l,nullptr); } __except(EXCEPTION_EXECUTE_HANDLER){ok=0;}
    if(!ok || !l.found){Log("OUTFIT41 mission player %d (%s): not among the room's members: stock outfit",index,peer.id);return false;}
    if(!l.has){Log("OUTFIT41 mission player %d (%s): tells the room nothing of the EDF4.1 outfit (an older All Forces): stock outfit",index,peer.id);return false;}
    if(!ParseWire(l.value,out)){Log("OUTFIT41 mission player %d (%s): %s=\"%s\" of another version: stock outfit",index,peer.id,kAttrKey,l.value);return false;}
    return true;
}
// This machine's value told when it changed, the others' watched (EDF6Coop watches a key from its first read on: a preload
// reads them at once). At most every kTickMs: from the room's frame and from the 3D view's messages (the HQ and the room,
// also with RoomLoadout=0, where room_ui installs nothing), so a save loaded or an outfit unlocked reaches the room
// before the next mission.
constexpr ULONGLONG kTickMs=500;
ULONGLONG lastTick=0;
void Tick() noexcept {
    const ULONGLONG now=GetTickCount64();
    if(lastTick && now-lastTick<kTickMs)return;
    lastTick=now;
    PublishOwn();
    if(attrApi) {
        Lookup none;none.id="";
        __try { attrApi->snapshot(kAttrKey,&Each,&none,nullptr); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
}
bool RemoteWears(int index,int cls) noexcept {
    if(!Installed(cls))return false;
    Wire w;
    return RemoteWire(index,w) && Wears(w.chosen,w.missionDefault,InPack41());
}

// --- The battle model
const wchar_t* Decided(int cls,bool wears,std::atomic<int>& slot) noexcept {
    slot=wears ? cls : -1;
    return wears ? kFiles[cls].battle : nullptr;
}
const wchar_t* __fastcall BattlePathHook(void* tables,int cls,int outfit) {
    const Building b=building;
    const wchar_t* ours=nullptr;
    if(ValidClass(cls)) {
        if(b.what==Build::preloadOnline && ValidIndex(b.who)) {
            const bool wears=b.remote ? RemoteWears(b.who,cls) : LocalWears(0,cls);
            ours=Decided(cls,wears,preloadedOnline[b.who]);
            Log("OUTFIT41 online preload: mission player %d (%s) class %d: %s",b.who,b.remote ? "remote" : "this machine's",cls,
                ours ? "EDF4.1 outfit" : "stock outfit");
        } else if(b.what==Build::createOnline && ValidIndex(b.who) && preloadedOnline[b.who]==cls)ours=kFiles[cls].battle;
        else if(b.what==Build::createLocal && ValidLocal(b.who) && preloadedLocal[b.who]==cls)ours=kFiles[cls].battle;
    }
    return ours ? ours : reinterpret_cast<PathFn>(image+kBattlePath)(tables,cls,outfit);
}
const wchar_t* __fastcall OwnPathHook(void* gs,int who,int cls) {
    if(ValidLocal(who) && ValidClass(cls)) {
        const wchar_t* ours=Decided(cls,LocalWears(who,cls),preloadedLocal[who]);
        Log("OUTFIT41 preload: player %d class %d: %s",who+1,cls,ours ? "EDF4.1 outfit" : "stock outfit");
        if(ours)return ours;
    }
    return reinterpret_cast<PathFn>(image+kOwnPath)(gs,who,cls);
}
std::uintptr_t Create(Building b,std::uintptr_t desc,std::uintptr_t x,std::uintptr_t y,std::uintptr_t z) {
    const Building was=building;
    building=b;
    const auto result=reinterpret_cast<Call4>(image+kCreate)(desc,x,y,z);
    building=was;
    return result;
}
std::uintptr_t __fastcall CreateOnlineHook(std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d) {
    const int index=memberIndex;
    memberIndex=-1;
    return Create(Building{Build::createOnline,index,false},a,b,c,d);
}
std::uintptr_t __fastcall CreateOfflineHook(std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d) {
    std::int32_t who=-1;
    __try { who=At<std::int32_t>(reinterpret_cast<void*>(a),kDescPlayer); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return Create(Building{Build::createLocal,who,false},a,b,c,d);
}
// 0x591130's 0x595960(desc, &member, ...): the member's mission index kept for the creation that follows.
int IndexOf(std::uintptr_t member) noexcept {
    __try {
        auto obj=member ? At<unsigned char*>(reinterpret_cast<void*>(member),0) : nullptr;
        return obj ? At<std::int32_t>(obj,kMemberIndex) : -1;
    } __except(EXCEPTION_EXECUTE_HANDLER){return -1;}
}
std::uintptr_t __fastcall MemberDescHook(std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d) {
    memberIndex=IndexOf(b);
    return reinterpret_cast<Call4>(image+kMemberDesc)(a,b,c,d);
}

// --- The 3D view's model
std::uintptr_t __fastcall MessageHook(unsigned char* view,std::uintptr_t id,std::uintptr_t payload) {
    const int was=viewPlayer;
    int who=-1;
    __try { who=At<std::int32_t>(view,kViewPlayer); } __except(EXCEPTION_EXECUTE_HANDLER){who=-1;}
    viewPlayer=who;
    const auto result=nextMessage(view,id,payload);
    viewPlayer=was;
    Tick();
    return result;
}
const wchar_t* __fastcall MenuPathHook(void* tables,int cls,int outfit) {
    if(LocalWears(viewPlayer,cls))return kFiles[cls].menu;
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
// The screen's player (0 / 1) from an item's std::function implementation ({vtable, screen}); -1 otherwise.
int ScreenPlayer(void* impl) noexcept {
    __try {
        auto screen=At<unsigned char*>(impl,kLambdaScreen);
        if(!screen)return -1;
        const auto who=At<std::int32_t>(screen,kScreenPlayer);
        return ValidLocal(who) ? who : -1;
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
    const int who=ScreenPlayer(impl);
    if(ValidLocal(who) && !state.on[who]) {
        state.on[who]=true;
        SaveState();
        Log("OUTFIT41 player %d: the EDF4.1 outfit chosen",who+1);
    }
    const auto result=reinterpret_cast<CloseFn>(image+kClosePopup)(At<void*>(arg,0));
    if(ValidLocal(who))Rebuild(who,ClassOf(who));
    if(who==0)PublishOwn();
    return result;
}
template<int I> std::uintptr_t __fastcall StockPicked(void* impl,void* arg) {
    const int who=ScreenPlayer(impl);
    const bool was=ValidLocal(who) && state.on[who];
    if(was) {
        state.on[who]=false;
        SaveState();
        Log("OUTFIT41 player %d: the EDF4.1 outfit off (stock item %d)",who+1,I);
    }
    const auto result=stockCall[I](impl,arg);
    if(was)Rebuild(who,ClassOf(who));
    if(who==0)PublishOwn();   // the save's outfit may have changed the mission default too
    return result;
}
constexpr FuncCallFn kStockHooks[kStockItems]={&StockPicked<0>,&StockPicked<1>,&StockPicked<2>,&StockPicked<3>,&StockPicked<4>};

// Adds our item to the outfit popup's request (`req`, 0x119C600's fourth argument at 0x8ABC10) when the screen is a local
// player's and its class's files are in Mods. Anything not as 0x8AB310 builds it: left alone.
void AddItem(unsigned char* req) noexcept {
    if(At<std::int32_t>(req,kReqType)!=kOutfitDialog)return;
    unsigned char* vec=req+kReqVector;
    auto data=At<unsigned char*>(vec,kVecData);
    auto size=At<std::size_t>(vec,kVecSize);
    if(!data || size!=kStockItems)return;
    auto impl0=At<unsigned char*>(data,kItemImpl);
    if(!impl0 || At<void*>(impl0,0)!=image+kItemVtables[0])return;
    const int who=ScreenPlayer(impl0);
    CheckFiles();
    if(!ValidLocal(who) || !Installed(ClassOf(who)))return;
    const wchar_t* label=hudtext::Tr(hudtext::Tx::outfit41Item,hudtext::Resolve(Cfg().hudLanguage,GameTextLanguage()));
    if(size==At<std::size_t>(vec,kVecCapacity)) {
        reinterpret_cast<ReserveFn>(image+kReserve)(vec,Grown(size));
        data=At<unsigned char*>(vec,kVecData);
    }
    unsigned char* item=data+size*kItemSize;
    Put<std::size_t>(vec,kVecSize,size+1);   // as the game's loop: the size first, then the item made in place
    reinterpret_cast<ItemCopyFn>(image+kItemCopy)(vec,item,data);
    reinterpret_cast<AssignFn>(image+kAssign)(item+kItemText,label,std::wcslen(label));
    auto impl=At<unsigned char*>(item,kItemImpl);
    if(impl)Put<void*>(impl,0,&ourVtable[1]);
    Put<std::int32_t>(req,kReqCursor,Cursor(state.on[who],At<std::int32_t>(req,kReqCursor)));
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
bool installed=false;
}  // namespace

std::uintptr_t Outfit41SessionPreload(PlayerPreloadFn call,std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d) {
    Building next;
    __try {
        auto member=a ? At<unsigned char*>(reinterpret_cast<void*>(a),0) : nullptr;
        if(member)next=Building{Build::preloadOnline,At<std::int32_t>(member,kMemberIndex),RemoteMember(At<std::uint32_t>(member,kMemberFlags))};
    } __except(EXCEPTION_EXECUTE_HANDLER){next=Building{};}
    if(!installed)next=Building{};
    if(ValidIndex(next.who))preloadedOnline[next.who]=-1;
    const Building was=building;
    building=next;
    const auto result=call(a,b,c,d);
    building=was;
    return result;
}

void Outfit41RoomTick() noexcept {
    if(installed)Tick();
}

bool InstallOutfit41() noexcept {
    for(auto& p:preloadedOnline)p=-1;
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
    installed=true;   // before the first redirect: the hooks may run at once
    int battle=Redirect(kOwnPathCall,kOwnPath,reinterpret_cast<void*>(&OwnPathHook));
    battle+=Redirect(kSessionPathCall,kBattlePath,reinterpret_cast<void*>(&BattlePathHook));
    battle+=Redirect(kCreatePathCall,kBattlePath,reinterpret_cast<void*>(&BattlePathHook));
    battle+=Redirect(kMemberDescCall,kMemberDesc,reinterpret_cast<void*>(&MemberDescHook));
    battle+=Redirect(kCreateOnline,kCreate,reinterpret_cast<void*>(&CreateOnlineHook));
    battle+=Redirect(kCreateOffline,kCreate,reinterpret_cast<void*>(&CreateOfflineHook));
    constexpr int kBattleSites=6;
    int menu=0;
    {
        void* next=nullptr;
        auto slot=reinterpret_cast<void**>(image+kViewVtable)+kViewSlotMessage;
        if(edf::ChainVtableSlot(slot,reinterpret_cast<void*>(&MessageHook),&next)) {
            nextMessage=reinterpret_cast<MessageFn>(next);
            for(std::size_t site:kMenuPathCalls)menu+=Redirect(site,kMenuPath,reinterpret_cast<void*>(&MenuPathHook));
        } else Log("OUTFIT41 the 3D view's OnMessage could not be chained: the menu keeps the stock model");
    }
    int items=0;
    for(int i=0;i<static_cast<int>(kStockItems);++i) {
        void* next=nullptr;
        auto slot=reinterpret_cast<void**>(image+kItemVtables[i])+kSlotCall;
        if(edf::ChainVtableSlot(slot,reinterpret_cast<void*>(kStockHooks[i]),&next)){stockCall[i]=reinterpret_cast<FuncCallFn>(next);++items;}
    }
    // our item only once every stock item can turn the outfit off again
    const int popup=items==static_cast<int>(kStockItems) ?
        Redirect(kOutfitDialogCall,kShowDialog,reinterpret_cast<void*>(&ShowDialogHook)) : 0;
    Log("OUTFIT41 battle=%d/%d menu=%d/%zu stock items=%d/%zu popup=%d files ranger=%d wingdiver=%d airraider=%d fencer=%d "
        "chosen=%d%d EDF41Content=%d (%ls)",battle,kBattleSites,menu,std::size(kMenuPathCalls),items,kStockItems,popup,
        available[0].load(),available[1].load(),available[2].load(),available[3].load(),state.on[0],state.on[1],
        Cfg().edf41Content,statePath);
    return battle==kBattleSites && menu==static_cast<int>(std::size(kMenuPathCalls)) && popup==1;
}
}  // namespace crew
