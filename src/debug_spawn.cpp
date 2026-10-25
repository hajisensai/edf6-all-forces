// The debug spawn tool (src/debug_spawn.h, docs/debug-spawn.md): ini DebugSpawn=1 (off by default), F8 opens a menu on the
// HUD, F5 / F6 pick a row, F7 the next category, F9 spawns it where the crosshair points. Online the range's rows
// (enemies, vehicles, soldiers, drones) are made on every peer through the reliable support transaction (NetSpawn).
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
//
// Nothing of it runs while it is off: PreloadDebugSpawn returns before touching the preload manager, DebugSpawnFrame
// before reading a key, DebugSpawnReadout has nothing to show. It patches nothing at all, on or off.
#include "crew.h"
#include "debug_spawn.h"
#include "debug_spawn_net.h"
#include "support_net.h"
#include "memory.h"
#include "online_authority.h"
#include "scene_object.h"
#include "support_call.h"
#include "support_soldier.h"
#include "support_spawn.h"
#include "support_drone.h"
#include "enemy_control.h"
#include "pvp.h"
#include "pvp_units.h"
#include "room_pvp.h"
#include <cstdarg>
#include <cstring>
#include <cwchar>

namespace crew {
namespace {
using namespace debugspawn;

// The natives (the same ones support_spawn.cpp / jet_spawn.cpp call): the preload (0x7A3780(manager, path, 2, -1)),
// CreateObject (0x11945E0(objectManager, &matrix, path, &InitParamBase)), Delete, mission_setup's read (0x62D6E0: the
// shared ApplyMissionSetup), SetLevel (0x54E740). The script's CreateEnemy (0x1AD220) fills the creation descriptor with
// team 1 (0x1AD37F) and its bool at +0x84; 0x1D8900 casts the new object to GameObjectBase (0x1D893E), SetTeam(team, 1)
// (0x1D8A60), SetLevel and, the bool set, 0x548D50 (0x1D8A9E): the object's activation (+0x4A0 = 1, its slot 26, then
// 0x54D180). The script's CreateVehicle2 casts to VehicleBase the same way (0x1B2AFF). __RTDynamicCast is the import
// thunk at 0x12DA7AA (stockgauge.cpp calls it too); its type descriptors SceneObject 0x2006450, GameObjectBase 0x2006400,
// VehicleBase 0x2006480 are the ones those call sites name.
constexpr unsigned kPreload=0x7A3780,kCreate=0x11945E0,kDelete=0x118A1B0,kReadSetup=0x62D6E0,kSetLevel=0x54E740;
constexpr unsigned kActivate=0x548D50,kCast=0x12DA7AA,kInitVtable=0x1762068,kSetupDtors=0x1765220;
// The resource manager's completion query (the script's IsCompleteResource, 0x70ED20 -> 0x7A3600; WaitPreload 0x1BC200
// polls it): false while its command queue holds a load or one of its four work slots (+0x1A4 + i*0x90) is busy, true
// once a fence confirms every worker idle. The loader threads run all mission long, so a load queued mid-mission
// (0x7A3780) is worked off and reported here; its entry lives in the same group-2 table the mission's end releases
// (0x7A2EE0). docs/debug-spawn.md 「按需加载」.
constexpr unsigned kLoadDone=0x7A3600,kLoadDoneScript=0x70ED20;
// WaitPreload's (0x1BC200) step between its two completion queries: 0x111CC20(&renderer 0x2136FF0) pushes a fence onto the
// render thread's command queue (+0x28A8: 0x51D00 / 0x51D60, submitted by 0x51DB0) and waits for it, pumping the
// window's messages (PeekMessage / Translate / Dispatch) and Sleep(0) meanwhile (0x111CC20..0x111CD3B, static reading),
// then Sleep(10), then the query again (0x1BC231). Read as: the loads' GPU-side creation (queued to the render thread)
// done before the loads count as complete. The plugin makes the same step once a batch of loads reports complete
// (LoadTick), then queries again, as WaitPreload does. Inferred from the code; the fence's purpose is not seen at run time.
constexpr unsigned kRenderSync=0x111CC20,kWaitPreloadSyncSite=0x1BC20B;
constexpr std::size_t kRenderer=0x2136FF0;
// An empty vehicle as the script's CreateVehicle2 (0x1B28C0) makes one: its creation descriptor's SetLevel flag cleared
// (+0x85 = 0 at 0x1B2A2E, team 5 at 0x1B2A27), then at 0x1B2BD9..0x1B2C09
//   lvl = max(0xD7AE0(GameStatus *(EDF+0x20B2890), level) / 10, 1);  0x633280(vehicle, lvl)
// 0x633280 reads its mission_setup (0x62D6E0), keeps lvl at +0x67C, applies the setup (slot 46), +0xE30 = 2, +0xE34 = lvl,
// 0x6DD730: never SetLevel (0x54E740), whose tail 0x54E8F7 always calls 0x118A4B0(obj, 8): +0x1A |= 8, the native AI
// list. A vehicle on that list gets the AI pass every frame (603: slot 7 0x673300 clears every seat's input block
// 0x62C120(seat+0x2C0) before its AI action) and is skipped by the per-frame crew team pass (0x63049E): the player in it
// neither drives nor fires (the user, 2026-10-19: 「火神炮和bmpt没办法移动和开火，不知道是不是用m地图召唤的原因」).
constexpr unsigned kVehicleLevelSetup=0x633280,kLevelScale=0xD7AE0,kCreateVehicleLevelSite=0x1B2BD9;
constexpr std::size_t kGameStatus=0x20B2890;
constexpr float kVehicleLevel=1.0f;   // the range's vehicles' level (testrange/gen.py plan.vehicle_level's default)
constexpr unsigned kSceneObjectType=0x2006450,kGameObjectType=0x2006400,kVehicleType=0x2006480;
constexpr unsigned kEnemyCastSite=0x1D893E,kVehicleCastSite=0x1B2AFF,kActivateSite=0x1D8A9E,kEnemyTeamSite=0x1AD37F;
constexpr std::size_t kPreloadMgr=0x20B29A8,kObjectMgr=0x20B2958;
constexpr std::int32_t kTeamEnemy=1,kTeamVehicle=5;   // 5: an empty vehicle's (CreateVehicle2 0x1B2A27)
const unsigned char kPreloadSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48};
// mov [rsp+18h],rbx; push rdi; sub rsp,20h; mov rbx,rcx; add rcx,20h; call 0x51E70 (the queue empty); test al,al
const unsigned char kLoadDoneSig[]={0x48,0x89,0x5C,0x24,0x18,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x83,0xC1,0x20,
                                    0xE8,0x5A,0xE8,0x8A,0xFF,0x84,0xC0};
// mov rcx,[the resource manager 0x20B29A8]; jmp 0x7A3600: the script's IsCompleteResource asks the same manager
const unsigned char kLoadDoneScriptSig[]={0x48,0x8B,0x0D,0x81,0x3C,0x9A,0x01,0xE9,0xD4,0x48,0x09,0x00};
// WaitPreload 0x1BC20B: mov rcx,[the resource manager]; mov rbx,rax; call 0x7A3600; lea rcx,[0x2136FF0]; call 0x111CC20
const unsigned char kWaitPreloadSyncSiteSig[]={0x48,0x8B,0x0D,0x96,0x67,0xEF,0x01,0x48,0x8B,0xD8,0xE8,0xE6,0x73,0x5E,0x00,
                                               0x48,0x8D,0x0D,0xCF,0xAD,0xF7,0x01,0xE8,0xFA,0x09,0xF6,0x00};
// mov [rsp+20h],rbx; push rdi; sub rsp,70h; movaps [rsp+60h],xmm6; mov rdi,rcx
const unsigned char kRenderSyncSig[]={0x48,0x89,0x5C,0x24,0x20,0x57,0x48,0x83,0xEC,0x70,0x0F,0x29,0x74,0x24,0x60,0x48,0x8B,0xF9};
const unsigned char kCreateSig[]={0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x8D,0x6C,0x24,0xD9};
const unsigned char kDeleteSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x60,0x48};
const unsigned char kReadSetupSig[]={0x48,0x89,0x5C,0x24,0x18,0x56,0x57,0x41,0x56,0x48,0x83,0xEC,0x40,0x48,0x8B,0xDA};
const unsigned char kSetTeamSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x41};
const unsigned char kSetLevelSig[]={0x48,0x8B,0xC4,0x48,0x89,0x58,0x18,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x8D};
// push rbx; sub rsp,20h; cmp byte [rcx+4A0h],0; mov rbx,rcx
const unsigned char kActivateSig[]={0x40,0x53,0x48,0x83,0xEC,0x20,0x80,0xB9,0xA0,0x04,0x00,0x00,0x00,0x48,0x8B,0xD9};
// jmp [rip+...]: the import thunk
const unsigned char kCastSig[]={0xFF,0x25,0xE8,0xB8,0x47,0x00};
// lea r9,GameObjectBase; lea r8,SceneObject; xor edx,edx; mov rcx,[rbp-20h]; call __RTDynamicCast
const unsigned char kEnemyCastSiteSig[]={0x4C,0x8D,0x0D,0xBB,0xDA,0xE2,0x01,0x4C,0x8D,0x05,0x04,0xDB,0xE2,0x01,0x33,0xD2,
                                         0x48,0x8B,0x4D,0xE0,0xE8,0x53,0x1E,0x10,0x01};
// lea r9,VehicleBase; lea r8,SceneObject; xor edx,edx; mov rcx,r10; call __RTDynamicCast
const unsigned char kVehicleCastSiteSig[]={0x4C,0x8D,0x0D,0x7A,0x39,0xE5,0x01,0x4C,0x8D,0x05,0x43,0x39,0xE5,0x01,0x33,0xD2,
                                           0x49,0x8B,0xCA,0xE8,0x93,0x7C,0x12,0x01};
// cmp byte [r14+84h],0; je +8; mov rcx,rsi; call 0x548D50
const unsigned char kActivateSiteSig[]={0x41,0x80,0xBE,0x84,0x00,0x00,0x00,0x00,0x74,0x08,0x48,0x8B,0xCE,0xE8,0xA0,0x02,0x37,0x00};
// push rbx; sub rsp,60h; movaps [rsp+50h],xmm6; mov rax,[__security_cookie]
const unsigned char kVehicleLevelSetupSig[]={0x40,0x53,0x48,0x83,0xEC,0x60,0x0F,0x29,0x74,0x24,0x50,0x48,0x8B,0x05};
// push rbx; sub rsp,30h; cmp byte [rcx+44h],0; mov rbx,rcx; movaps [rsp+20h],xmm6
const unsigned char kLevelScaleSig[]={0x40,0x53,0x48,0x83,0xEC,0x30,0x80,0x79,0x44,0x00,0x48,0x8B,0xD9,0x0F,0x29,0x74,0x24,0x20};
// CreateVehicle2's level: movaps xmm1,xmm7; mov rcx,[GameStatus]; call 0xD7AE0; divss xmm0,[10.0]; comiss xmm0,[1.0];
// ja +8; movss xmm0,[1.0]; movaps xmm1,xmm0; mov rcx,[rsp+30h]; call 0x633280
const unsigned char kCreateVehicleLevelSiteSig[]={0x0F,0x28,0xCF,0x48,0x8B,0x0D,0xAD,0xFC,0xEF,0x01,0xE8,0xF8,0x4E,0xF2,0xFF,
    0xF3,0x0F,0x5E,0x05,0x54,0x2E,0x5B,0x01,0x0F,0x2F,0x05,0x9D,0x3D,0xA8,0x01,0x77,0x08,0xF3,0x0F,0x10,0x05,0x93,0x3D,0xA8,0x01,
    0x0F,0x28,0xC8,0x48,0x8B,0x4C,0x24,0x30,0xE8,0x72,0x06,0x48,0x00};
// mov dword [rbp+8],1: CreateEnemy's team
const unsigned char kEnemyTeamSiteSig[]={0xC7,0x45,0x08,0x01,0x00,0x00,0x00};
struct Sig { unsigned rva; const unsigned char* bytes; std::size_t size; };
// The hex stays here (tests/debug_spawn_resource_audit.py reads it): the same as scene_object.h's.
static_assert(kCreate==scene::kCreateObject && kInitVtable==scene::kInitParamBaseVtable,"scene_object.h's CreateObject");
const Sig kSigs[]={{kPreload,kPreloadSig,sizeof(kPreloadSig)},{kCreate,kCreateSig,sizeof(kCreateSig)},
    {kDelete,kDeleteSig,sizeof(kDeleteSig)},{kReadSetup,kReadSetupSig,sizeof(kReadSetupSig)},
    {kSetTeam,kSetTeamSig,sizeof(kSetTeamSig)},{kSetLevel,kSetLevelSig,sizeof(kSetLevelSig)},
    {kActivate,kActivateSig,sizeof(kActivateSig)},{kCast,kCastSig,sizeof(kCastSig)},
    {kEnemyCastSite,kEnemyCastSiteSig,sizeof(kEnemyCastSiteSig)},{kVehicleCastSite,kVehicleCastSiteSig,sizeof(kVehicleCastSiteSig)},
    {kActivateSite,kActivateSiteSig,sizeof(kActivateSiteSig)},{kEnemyTeamSite,kEnemyTeamSiteSig,sizeof(kEnemyTeamSiteSig)},
    {kVehicleLevelSetup,kVehicleLevelSetupSig,sizeof(kVehicleLevelSetupSig)},{kLevelScale,kLevelScaleSig,sizeof(kLevelScaleSig)},
    {kCreateVehicleLevelSite,kCreateVehicleLevelSiteSig,sizeof(kCreateVehicleLevelSiteSig)}};

using PreloadFn=void(*)(void*,const wchar_t*,std::int32_t,std::int32_t);
using LoadDoneFn=bool(*)(void*);
using RenderSyncFn=void(*)(void*);
using VehicleLevelSetupFn=void(__fastcall*)(void*,float);
using LevelScaleFn=float(__fastcall*)(void*,float);
using DeleteFn=void(*)(void*);
using SetTeamFn=void(*)(void*,std::int32_t,bool);
using SetLevelFn=void(__fastcall*)(void*,float);
using ActivateFn=void(*)(void*);
using CastFn=void*(*)(void*,std::int32_t,void*,void*,std::int32_t);

// The source the gun drones launched from here share (jet_spawn.cpp FlightFor: one flight, their rounds pass each other).
const int kJetSource=0;
// How long a spawn's result stays on the HUD (wall ms).
constexpr ULONGLONG kStatusMs=4000;

bool profileChecked=false,profile=false;
void* missionMgr=nullptr;          // the object manager this mission's preload was for
bool preloaded[kEntryCount]{};     // its SGO queued for this mission
bool missing[kEntryCount]{};       // a Mods row with a file not in Mods (not installed): never preloaded nor made
bool broken[kEntryCount]{};        // the game faulted making it: off until the game restarts (as support_spawn.cpp)
// Loading on demand (RequestLoad): a row not preloaded at the mission's start is queued when it is first asked for, and
// the spawn asked for is made where it was asked as soon as the manager reports every queued load complete.
bool loadOk=false;                 // the completion query matched (CheckProfile)
bool syncOk=false;                 // ...and WaitPreload's render-thread step (kRenderSync) is this build's
bool loading[kEntryCount]{};       // queued this mission, not yet reported complete
// The spawns asked for rows still loading, in the order asked: each made where it was asked once its load is complete
// (none replaces another: the user's every click stands). Full: the next ask is refused, with why.
struct PendingSpawn { int row=-1; float at[3]{},heading[3]{},look[3]{}; };
constexpr int kPendingSpawns=16;
PendingSpawn pending[kPendingSpawns]{};
int pendingCount=0;
ULONGLONG loadAskedAt=0;           // wall ms of the last load queued (the log's how long)
ULONGLONG loadTickFrame=0;
// This mission is the test range pack's (edf5campaign.cpp TestRangeMode at its start): the M map's 「摆放」 is on
// (ini RangePlace), whatever DebugSpawn says.
bool rangeMission=false;
// The objects made by a network transaction (NetSpawn), each by its kind (debug_spawn_net.h KindByte): held weakly
// until the transaction ends, activated when every peer has made and registered it.
// pvp: a PvP member's own unit (kPvpUnitCatalog); local: that member is this machine's player, whose control of it is
// asked for once it is active (controlAskedAt the last ask).
struct NetworkEnemy {
    std::uint64_t token=0;ObjRef ref{};int row=-1;How how=How::enemy;bool activated=false;
    bool pvp=false,local=false;ULONGLONG controlAskedAt=0;
};
NetworkEnemy networkEnemies[support_net::kMaxTransactions]{};
bool submittingNetwork=false;
int submittingRow=-1;
// This machine's requests waiting for their outcome (RangeEnemyRequestNotice), each with its row: a newer one never
// takes an older one's outcome.
struct NetAsk { std::uint32_t request=0;int row=-1; };
constexpr int kNetAsks=8;
NetAsk netAsks[kNetAsks]{};
int netAskNext=0;

bool NetworkEnemyLive(const NetworkEnemy& e) noexcept {
    // Weak control blocks are retained until transaction teardown: test the strong count before the actor pointer.
    return e.ref.ctrl && At<LONG>(e.ref.ctrl,8)>0 && e.ref.Is(e.ref.obj) &&
        !(At<unsigned char>(e.ref.obj,0x18)&4);
}
// A dead actor: the byte the native walks skip one by (kDead, 0x5E0FB6).
bool NetworkEnemyDead(const NetworkEnemy& e) noexcept {
    return !NetworkEnemyLive(e) || At<unsigned char>(e.ref.obj,kDead)!=0;
}
void ReleaseNetworkEnemy(NetworkEnemy& e) noexcept {
    if(e.ref.ctrl) {
        auto* ctrl=const_cast<unsigned char*>(static_cast<const unsigned char*>(e.ref.ctrl));
        if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(ctrl+0xC))==0)
            reinterpret_cast<void(__fastcall*)(void*)>(At<void* const*>(ctrl,0)[1])(ctrl);
    }
    e={};
}
Menu menu{};
bool held[5]{};                    // the keys' own state (Press)
ULONGLONG lastFrame=0;

// What the HUD shows (DebugSpawnReadout, the draw thread), written on the game thread.
SRWLOCK cueLock=SRWLOCK_INIT;
DebugSpawnCue cue{};
ULONGLONG statusAt=0;              // wall ms; 0 none

bool CheckProfile() noexcept {
    __try {
        for(const auto& s:kSigs)if(!Matches(s.rva,s.bytes,s.size)){Log("DEBUGSPAWN profile mismatch at %#x: the tool stays off",s.rva);return false;}
        loadOk=Matches(kLoadDone,kLoadDoneSig,sizeof(kLoadDoneSig)) && Matches(kLoadDoneScript,kLoadDoneScriptSig,sizeof(kLoadDoneScriptSig));
        if(!loadOk)Log("DEBUGSPAWN the resource manager's completion query differs: rows not preloaded stay off (no loading on demand)");
        syncOk=loadOk && Matches(kWaitPreloadSyncSite,kWaitPreloadSyncSiteSig,sizeof(kWaitPreloadSyncSiteSig)) &&
               Matches(kRenderSync,kRenderSyncSig,sizeof(kRenderSyncSig)) && Readable(image+kRenderer,8);
        if(loadOk && !syncOk)Log("DEBUGSPAWN WaitPreload's render-thread step differs: loads on demand complete without it");
        return Readable(image+kInitVtable,8) && Readable(image+kSetupDtors,8) && Readable(image+kSceneObjectType,16) &&
               Readable(image+kGameObjectType,16) && Readable(image+kVehicleType,16);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool InFront() noexcept {
    DWORD pid=0;
    GetWindowThreadProcessId(edf::ForegroundWindow(),&pid);
    return pid==GetCurrentProcessId();
}
bool Down(int vk) noexcept { return vk>0 && vk<255 && (edf::AsyncKey(vk)&0x8000)!=0; }

// The map's support catalog entry (support_dispatch.cpp SupportCallKey) of kSupportCalls[call], -1 none (not in this
// build's catalog: a renamed call).
int SupportCatalogOf(int call) noexcept {
    if(call<0 || call>=kSupportCallCount)return -1;
    for(int i=0,n=SupportCallCount();i<n;++i)
        if(const wchar_t* key=SupportCallKey(i);key && std::wcscmp(key,kSupportCalls[call])==0)return i;
    return -1;
}

// The row's state for the HUD: can it be made now, else why not (debug_spawn.h RowState).
RowState StateOf(int row) noexcept {
    if(row<0 || row>=kEntryCount)return RowState::unavailable;
    const Entry& e=kEntries[row];
    switch(e.how) {
    case How::vehicle:
    case How::enemy:
        if(missing[row])return RowState::missing;
        if(broken[row])return RowState::broken;
        if(loading[row])return RowState::loading;
        return profile && preloaded[row] ? RowState::ready : RowState::notPreloaded;
    case How::drone:   // online every peer makes it as a support drone (jet_spawn.cpp PrepareSupportDrone)
        return Cfg().jetPilot && (InSession() ? SupportDroneReady() : jet::SpawnReady()) ? RowState::ready : RowState::unavailable;
    case How::supportCall:
        return SupportCatalogOf(e.arg)>=0 && SupportCallReadiness().state==SupportReady::ready ? RowState::ready : RowState::unavailable;
    case How::soldier: return SupportSoldiersReady() ? RowState::ready : RowState::unavailable;
    }
    return RowState::unavailable;
}
bool RowReady(int row) noexcept { return StateOf(row)==RowState::ready; }

// A Mods row's files all there (Mods\OBJECT\): the list testrange/catalog.py files_of gives, the one catalog.available
// checks for the range's own tools (src/spawn_catalog.inc). The first one that is not, or nullptr: every one is.
bool ModRowFileThere(const wchar_t* file) noexcept {
    const auto reference=ParseModFile(file);
    if(!reference)return false;
    wchar_t path[MAX_PATH];
    const DWORD length=GetModuleFileNameW(nullptr,path,MAX_PATH);
    wchar_t* const slash=length && length<MAX_PATH ? std::wcsrchr(path,L'\\') : nullptr;
    if(!slash)return false;
    *slash=0;
    const wchar_t* directory=reference.directory==ModDirectory::weapon ? L"\\Mods\\WEAPON\\" : L"\\Mods\\OBJECT\\";
    if(wcscat_s(path,directory)!=0 || wcscat_s(path,reference.name)!=0)return false;
    const DWORD attributes=GetFileAttributesW(path);
    return attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_DIRECTORY);
}
const wchar_t* ModRowLacks(const Entry& e) noexcept {
    if(!e.files || !e.files[0])return L"?";   // a Mods row always lists its files (debug_spawn.h FilesAgree)
    for(const wchar_t* f=e.files;*f;f+=std::wcslen(f)+1)
        if(!ModRowFileThere(f))return f;
    return nullptr;
}

void Publish() noexcept {
    const int row=Picked(menu);
    AcquireSRWLockExclusive(&cueLock);
    cue.open=menu.open;cue.category=menu.category;cue.row=row;
    cue.pick=menu.category>=0 && menu.category<kCategoryCount ? menu.pick[menu.category] : 0;
    cue.count=CountIn(static_cast<Category>(menu.category));
    cue.ready=RowReady(row);
    const Config& c=Cfg();
    cue.keys[0]=c.debugSpawnKey;cue.keys[1]=c.debugSpawnPrevKey;cue.keys[2]=c.debugSpawnNextKey;
    cue.keys[3]=c.debugSpawnCategoryKey;cue.keys[4]=c.debugSpawnSpawnKey;
    ReleaseSRWLockExclusive(&cueLock);
}

// The result of a spawn: on the HUD for kStatusMs (`status` of `row`: hudtext's words) and in the log (`format`, ASCII).
void Say(int status,int row,const wchar_t* note,const char* format,...) noexcept {
    char line[400];
    va_list args;va_start(args,format);
    vsnprintf_s(line,sizeof(line),_TRUNCATE,format,args);
    va_end(args);
    Log("DEBUGSPAWN %s",line);
    AcquireSRWLockExclusive(&cueLock);
    cue.status=status;cue.statusRow=row;statusAt=GetTickCount64();
    wcsncpy_s(cue.note,note ? note : L"",_TRUNCATE);
    ReleaseSRWLockExclusive(&cueLock);
}

int SpawnFault(int row,const EXCEPTION_POINTERS* e) noexcept {
    const auto r=e->ExceptionRecord;
    const auto at=static_cast<const unsigned char*>(r->ExceptionAddress);
    const bool inGame=at>=image && at<image+0x22CE000;
    Log("DEBUGSPAWN %s: the game faulted making it (%08lX at %s%llX): this row is off until the game restarts",kEntries[row].id,
        r->ExceptionCode,inGame ? "EDF+" : "",static_cast<unsigned long long>(inGame ? static_cast<std::uintptr_t>(at-image) :
                                                                               reinterpret_cast<std::uintptr_t>(at)));
    broken[row]=true;preloaded[row]=false;
    return EXCEPTION_EXECUTE_HANDLER;
}

// CreateObject on the row's preloaded SGO, then the cast its script entry makes (`type`: GameObjectBase for an enemy,
// VehicleBase for a vehicle). The object, or nullptr (`why` says which step). An object that is not of the type is
// deleted again; one whose cast moves the pointer too (the plugin's code takes the object's own address for every
// class) is refused the same way, rather than guessed at.
unsigned char* Create(int row,const float* matrix,unsigned type,const char** why) noexcept {
    unsigned char* object=nullptr;
    __try {
        void* const mgr=At<void*>(image,kObjectMgr);
        if(!mgr || mgr!=missionMgr){*why="the object manager is not this mission's preload's";return nullptr;}
        object=scene::CreateSceneObject(matrix,kEntries[row].sgo);   // the matrix copied aligned there (MOVAPS)
        if(!object){*why="CreateObject returned null";return nullptr;}
        void* const cast=reinterpret_cast<CastFn>(image+kCast)(object,0,image+kSceneObjectType,image+type,0);
        if(cast==object)return object;
        *why=cast ? "the cast moved the pointer" : type==kVehicleType ? "not a VehicleBase" : "not a GameObjectBase";
        reinterpret_cast<DeleteFn>(image+kDelete)(object);
        return nullptr;
    } __except(SpawnFault(row,GetExceptionInformation())) { *why="the game faulted making it";return nullptr; }
}

// A vehicle, empty, as the script's CreateVehicle2 makes one (kVehicleLevelSetup above): CreateObject, the VehicleBase
// cast, SetTeam(5), then 0x633280 with CreateVehicle2's level, which reads and applies its mission_setup. No SetLevel: it
// would put the vehicle on the native AI list, whose pass clears the seats' input every frame. NoteLocalCopy as before.
unsigned char* SpawnVehicle(int row,const float* matrix,const char** why) noexcept {
    unsigned char* const v=Create(row,matrix,kVehicleType,why);
    if(!v)return nullptr;
    __try {
        void* const status=At<void*>(image,kGameStatus);
        if(!status) {
            *why="no GameStatus for the vehicle's level";
            reinterpret_cast<DeleteFn>(image+kDelete)(v);
            return nullptr;
        }
        reinterpret_cast<SetTeamFn>(image+kSetTeam)(v,kTeamVehicle,true);
        const float scaled=reinterpret_cast<LevelScaleFn>(image+kLevelScale)(status,kVehicleLevel)/10.0f;
        reinterpret_cast<VehicleLevelSetupFn>(image+kVehicleLevelSetup)(v,scaled>1.0f ? scaled : 1.0f);
        if(At<unsigned char>(v,0x1A)&8)Log("DEBUGSPAWN %s: on the native AI list after its setup (unexpected)",kEntries[row].id);
        NoteLocalCopy(v,nullptr);
        return v;
    } __except(SpawnFault(row,GetExceptionInformation())) { *why="the game faulted setting it up";return nullptr; }
}

// A stock enemy: CreateEnemy's steps (0x1D8900 with team 1, level 1, its bool true): SetTeam(1), SetLevel(1), 0x548D50.
// Not added to the script's object group (0x1DB1E0): the mission's own scripts never wait on it by name; the team's
// count (GetTeamObjectCount) sees it, so a "kill them all" wait waits for it too.
unsigned char* SpawnEnemy(int row,const float* matrix,const char** why) noexcept {
    unsigned char* const o=Create(row,matrix,kGameObjectType,why);
    if(!o)return nullptr;
    __try {
        reinterpret_cast<SetTeamFn>(image+kSetTeam)(o,kTeamEnemy,true);
        reinterpret_cast<SetLevelFn>(image+kSetLevel)(o,1.0f);
        reinterpret_cast<ActivateFn>(image+kActivate)(o);
        return o;
    } __except(SpawnFault(row,GetExceptionInformation())) { *why="the game faulted setting it up";return nullptr; }
}

// The ground under (x, z) near `p`: the first hit of a ray from well over it to well under it; false with none.
bool GroundUnder(float* p) noexcept {
    const float top[3]={p[0],p[1]+600.0f,p[2]},bottom[3]={p[0],p[1]-1500.0f,p[2]};
    float hit[3];
    if(MapRay(top,bottom,hit)<0.0f)return false;
    p[1]=hit[1];
    return true;
}

// The way a row made at `at` faces: a vehicle away from the player (`me`: its door side to them), an enemy or a soldier
// at them; `look` when they are on one spot.
void FaceOf(const Entry& e,const float* me,const float* at,const float* look,float* heading) noexcept {
    const bool facePlayer=e.how==How::enemy || e.how==How::soldier;
    if(facePlayer)HeadingTo(at,me,look,heading);
    else HeadingTo(me,at,look,heading);
}

// Where the row goes: the crosshair's ground point (DebugSpawnRange) or DebugSpawnDistance ahead (debug_spawn.h
// PlaceSpawn), its lift added; `heading` the way it faces (a vehicle away from the player, the others at them) and
// `look` the camera's horizontal heading.
bool Where(const Entry& e,const unsigned char* human,float* at,float* heading,float* look) noexcept {
    const float* const me=reinterpret_cast<const float*>(human+kPosition);
    float eye[3],dir[3];
    if(!CameraRay(eye,dir)) {   // no camera drawn yet: the player's own facing (its matrix's forward row)
        const float* m=reinterpret_cast<const float*>(human+kMatrix);
        std::memcpy(eye,me,12);dir[0]=m[8];dir[1]=0.0f;dir[2]=m[10];
    }
    const Config& c=Cfg();
    const float reach[3]={eye[0]+dir[0]*c.debugSpawnRange,eye[1]+dir[1]*c.debugSpawnRange,eye[2]+dir[2]*c.debugSpawnRange};
    float hit[3];
    const bool hasHit=MapRay(eye,reach,hit)>=0.0f;
    Spot s{};
    if(!PlaceSpawn(me,dir,hit,hasHit,kMinSpawnDistance,c.debugSpawnDistance,&s))return false;
    if(s.snap && !GroundUnder(s.at))s.at[1]=me[1];
    std::memcpy(at,s.at,12);
    at[1]+=e.lift;
    const float origin[3]={0.0f,0.0f,0.0f},north[3]={0.0f,0.0f,1.0f};
    HeadingTo(origin,dir,north,look);   // dir's own horizontal part, unit (straight up / down: north)
    FaceOf(e,me,at,look,heading);
    return true;
}

// Queues row `row`'s load (the resource manager's 0x7A3780, as the mission's own preload) when it was not preloaded at
// the mission's start, and (`at` not null) keeps `at` / `heading` / `look` to make it there once the manager reports the
// load complete (LoadTick). Online the load alone (`at` null): the network transaction makes it once every peer has it
// (PlanRangeEnemy on the host, PrepareRangeEnemy on the others). False when it cannot be queued: the completion query
// not this build's, the tool off this mission, another mission's object manager, or the spawns waiting full.
bool RequestLoad(int row,const float* at,const float* heading,const float* look) noexcept {
    if(row<0 || row>=kEntryCount || !kEntries[row].sgo || !profile || !loadOk)return false;
    if(at && pendingCount>=kPendingSpawns){Log("DEBUGSPAWN %s: %d spawns already wait for their loads: refused",kEntries[row].id,pendingCount);return false;}
    __try {
        void* const mgr=At<void*>(image,kPreloadMgr);
        void* const objects=At<void*>(image,kObjectMgr);
        if(!mgr || !objects || objects!=missionMgr)return false;
        if(!loading[row]) {
            reinterpret_cast<PreloadFn>(image+kPreload)(mgr,kEntries[row].sgo,2,-1);
            loading[row]=true;loadAskedAt=GetTickCount64();
            Log("DEBUGSPAWN %s: not loaded this mission: its load queued (%ls)",kEntries[row].id,kEntries[row].sgo);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { Log("DEBUGSPAWN %s: queuing its load faulted",kEntries[row].id);return false; }
    if(!at)return true;
    PendingSpawn& p=pending[pendingCount++];
    p.row=row;std::memcpy(p.at,at,12);std::memcpy(p.heading,heading,12);std::memcpy(p.look,look,12);
    return true;
}
int NetSpawn(int row,const float* at,wchar_t* noteOut,std::size_t noteSize) noexcept;

// Makes row `row` at `at` (its lift in already) facing `heading` (`look`: the drone's flight heading), by its How; the
// result for the HUD and the log (Say), and returned (DebugSpawnStatus) with a support call's own answer in `note`. The
// one way every spawn goes, the debug spawn tool's (Spawn) and the M map's 「摆放」 (PlaceRow). Online an object row goes
// to every peer (NetSpawn: a native object made here alone would exist on this machine only, the other players' games
// never seeing it); a support call's row is that call, which has its own online transaction.
int MakeRow(int row,const float* at,const float* heading,const float* look,wchar_t* noteOut,std::size_t noteSize) noexcept {
    if(row<0 || row>=kEntryCount)return kDebugSpawnFailed;
    const Entry& e=kEntries[row];
    if(noteOut && noteSize)noteOut[0]=0;
    if(InSession() && e.how!=How::supportCall)return NetSpawn(row,at,noteOut,noteSize);
    alignas(16) float m[16];
    FacingMatrix(heading,at,m);
    const char* why="";
    const void* made=nullptr;
    switch(e.how) {
    case How::vehicle:
    case How::enemy:
        if(!profile)why="EDF.dll profile mismatch (see DEBUGSPAWN profile)";
        else if(missing[row])why="a file of it is not in Mods (not installed: run the installer / install the test range)";
        else if(broken[row])why="it faulted before: off until the game restarts";
        else if(!preloaded[row]) {
            if(RequestLoad(row,at,heading,look)) {
                Say(kDebugSpawnLoading,row,nullptr,"%s: loading on demand; made at (%.1f,%.1f,%.1f) once the game reports it loaded",
                    e.id,at[0],at[1],at[2]);
                return kDebugSpawnLoading;
            }
            why="not loaded this mission, and its load cannot be queued now (see the lines above)";
        }
        else made=e.how==How::enemy ? SpawnEnemy(row,m,&why) : SpawnVehicle(row,m,&why);
        break;
    case How::supportCall: {
        // A support call, as the map's air support makes it (support_dispatch.cpp SupportCallAt): its route planned from the
        // map's edge to this point, the aircraft made in the air with their real crew seated aboard (BoardAirborne). The
        // result comes later (planning, then the deployment); this says only whether it was accepted.
        const int catalog=SupportCatalogOf(e.arg);
        wchar_t note[96]{};
        if(catalog<0){why="no such support call in this build's catalog";break;}
        const bool accepted=SupportCallAt(catalog,at,note,_countof(note));
        Say(accepted ? kDebugSpawnRequested : kDebugSpawnRefused,row,note,"%s: support call %ls (catalog %d) to (%.1f,%.1f,%.1f): %s",e.id,
            kSupportCalls[e.arg],catalog,at[0],at[1],at[2],accepted ? "accepted (see SUPPORT lines)" : "refused (see SUPPORT lines)");
        if(noteOut && noteSize)wcsncpy_s(noteOut,noteSize,note,_TRUNCATE);
        return accepted ? kDebugSpawnRequested : kDebugSpawnRefused;
    }
    case How::drone:
        if(!Cfg().jetPilot)why="JetPilot=0";
        else if(!(made=JetLaunchDrone(at,look,at,Cfg().jetFuelSec,&kJetSource,true)))
            why="JetLaunchDrone refused (EDF6VC_JET_DRONE.SGO not preloaded, or too many jets out)";
        break;
    case How::soldier: {
        ObjRef ref{};
        if(!ApplySupportSoldierResource(m,SupportSoldierResource(static_cast<SupportWeapon>(e.arg),false),nullptr,true,&ref))
            why="the support soldier refused (see SUPPORT soldier lines)";
        else{HoldSupportSoldier(ref,false);made=ref.obj;}   // let go at once: offline, nothing to wait for
        break;
    }
    }
    if(made) {
        Say(kDebugSpawnDone,row,nullptr,"%s: spawned %p at (%.1f,%.1f,%.1f) heading (%.2f,%.2f)%s",e.id,made,at[0],at[1],at[2],heading[0],heading[2],
            e.how==How::enemy ? " team enemy" : e.how==How::vehicle ? " team friend, empty" : "");
        return kDebugSpawnDone;
    }
    Say(kDebugSpawnFailed,row,nullptr,"%s: failed at (%.1f,%.1f,%.1f): %s",e.id,at[0],at[1],at[2],why);
    return kDebugSpawnFailed;
}

// Once a game frame while a load is queued: the manager's completion query (0x7A3600, the one WaitPreload polls); done,
// every queued row is ready and the spawn asked for last is made where it was asked. Another mission's object manager:
// the queue is this mission's no more (the mission's end released it), dropped.
void LoadTick() noexcept {
    bool any=false;
    for(const bool l:loading)any=any || l;
    if(!any)return;
    const ULONGLONG frame=GameFrame();
    if(frame==loadTickFrame)return;
    loadTickFrame=frame;
    bool done=false;
    __try {
        void* const objects=At<void*>(image,kObjectMgr);
        void* const mgr=At<void*>(image,kPreloadMgr);
        if(!objects || objects!=missionMgr || !mgr) {
            for(auto& l:loading)l=false;
            pendingCount=0;
            Log("DEBUGSPAWN loads on demand dropped: the mission changed");
            return;
        }
        done=reinterpret_cast<LoadDoneFn>(image+kLoadDone)(mgr);
        // WaitPreload's own order (kRenderSync): complete, then the render thread's fence, then the query again.
        if(done && syncOk) {
            reinterpret_cast<RenderSyncFn>(image+kRenderSync)(image+kRenderer);
            done=reinterpret_cast<LoadDoneFn>(image+kLoadDone)(mgr);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        for(auto& l:loading)l=false;
        pendingCount=0;loadOk=false;
        Log("DEBUGSPAWN the completion query faulted: loading on demand off until the game restarts");
        return;
    }
    if(!done)return;
    int rows=0;
    for(int i=0;i<kEntryCount;++i)if(loading[i]){loading[i]=false;preloaded[i]=true;++rows;}
    Log("DEBUGSPAWN %d row(s) loaded on demand in %llu ms%s",rows,static_cast<unsigned long long>(GetTickCount64()-loadAskedAt),
        syncOk ? " (render thread synchronised as WaitPreload does)" : "");
    // Every spawn that waited, in the order asked (a spawn may queue another load: it waits for the next batch).
    PendingSpawn made[kPendingSpawns];
    const int n=pendingCount;
    std::memcpy(static_cast<void*>(made),pending,sizeof(PendingSpawn)*static_cast<std::size_t>(n));
    pendingCount=0;
    for(int i=0;i<n;++i)if(made[i].row>=0 && preloaded[made[i].row])MakeRow(made[i].row,made[i].at,made[i].heading,made[i].look,nullptr,0);
}

void Spawn(int row,unsigned char* human) noexcept {
    if(row<0 || row>=kEntryCount)return;
    const Entry& e=kEntries[row];
    float at[3],heading[3],look[3];
    if(!Where(e,human,at,heading,look)) {
        Say(kDebugSpawnNoPlace,row,nullptr,"%s: failed: no place (camera straight up / down with nothing hit)",e.id);
        return;
    }
    MakeRow(row,at,heading,look,nullptr,0);
}

// Whether row `row` can be made here now for a network transaction: ready; being loaded (pending; a row not preloaded
// this mission has its load queued now: its file installed, the completion query this build's); refused.
support_net::PlanResult LoadedHere(int row) noexcept {
    const RowState state=StateOf(row);
    if(state==RowState::ready)return support_net::PlanResult::ready;
    if(state==RowState::loading)return support_net::PlanResult::pending;
    if(state==RowState::notPreloaded && RequestLoad(row,nullptr,nullptr,nullptr))return support_net::PlanResult::pending;
    return support_net::PlanResult::refused;
}
}  // namespace

// The support session supplies authenticated participant admission, a world epoch, host-issued unique native IDs,
// all-peer PREPARE validation, COMMIT results, ACTIVATE and cancel/timeout rollback. No object is made from a request.
// The host plans once its own copy of the row is loaded (pending until then); each peer READYs once its own is
// (PrepareRangeEnemy): no peer makes it while another lacks it.
support_net::PlanResult PlanRangeEnemy(std::uint32_t catalog,const float* target,std::uint64_t descriptor,
                                      support_net::Plan* out) noexcept {
    if(catalog!=kNetworkCatalog || !OnlineHostOnly() || !InSession() || !RangePlaceOn() || !target || !out)
        return support_net::PlanResult::refused;
    const int row=NetworkRow(descriptor);
    if(row<0)return support_net::PlanResult::refused;
    if(const auto loaded=LoadedHere(row);loaded!=support_net::PlanResult::ready)return loaded;
    float ground[3]={target[0],target[1]-kEntries[row].lift,target[2]};
    if(!std::isfinite(ground[0]+ground[1]+ground[2]) || !GroundUnder(ground))return support_net::PlanResult::refused;
    ground[1]+=kEntries[row].lift;
    const float heading[3]={0.0f,0.0f,1.0f};
    alignas(16) float matrix[16];FacingMatrix(heading,ground,matrix);
    return BuildNetworkPlan(row,matrix,out) ? support_net::PlanResult::ready : support_net::PlanResult::refused;
}
// The PvP unit's plan: the member's committed pick is this row as their unit, and it is a unit of their faction's side.
support_net::PlanResult PlanPvpUnit(const char* requesterPuid,const float* target,std::uint64_t descriptor,
                                    support_net::Plan* out) noexcept {
    if(!OnlineHostOnly() || !InSession() || !target || !out || !requesterPuid)return support_net::PlanResult::refused;
    const int row=NetworkRow(descriptor);
    pvp_match::Pick pick{};
    if(row<0 || kEntries[row].how!=How::enemy || !PvpMemberPick(requesterPuid,&pick) ||
       pick.role!=static_cast<std::uint8_t>(pvp_match::Role::unit) || pick.unit!=row ||
       !pvp_units::Playable(row,pvp_match::kFactions[pick.faction].side)) {
        Log("PVP UNIT plan refused: row %d is not %s's committed unit",row,requesterPuid);
        return support_net::PlanResult::refused;
    }
    if(const auto loaded=LoadedHere(row);loaded!=support_net::PlanResult::ready)return loaded;
    float ground[3]={target[0],target[1]-kEntries[row].lift,target[2]};
    if(!std::isfinite(ground[0]+ground[1]+ground[2]) || !GroundUnder(ground))return support_net::PlanResult::refused;
    ground[1]+=kEntries[row].lift;
    const float heading[3]={0.0f,0.0f,1.0f};
    alignas(16) float matrix[16];FacingMatrix(heading,ground,matrix);
    return BuildNetworkPlan(row,matrix,out,kPvpUnitCatalog) ? support_net::PlanResult::ready : support_net::PlanResult::refused;
}
// A network row may be made here: the range's on the test range; a PvP unit while this machine's match is on.
bool NetworkRowAllowed(const support_net::Plan& plan) noexcept {
    return plan.catalogId==kPvpUnitCatalog ? PvpSupportTeam(nullptr)!=0 : RangePlaceOn();
}
support_net::PlanResult PrepareRangeEnemy(const support_net::Plan& plan) noexcept {
    const int row=NetworkPlanRow(plan);
    if(!InSession() || !NetworkRowAllowed(plan) || row<0)return support_net::PlanResult::refused;
    return LoadedHere(row);
}
bool ValidateRangeEnemy(const support_net::Plan& plan) noexcept {
    const int row=NetworkPlanRow(plan);
    return InSession() && NetworkRowAllowed(plan) && row>=0 && StateOf(row)==RowState::ready;
}
namespace {
// The native object of kind `how` for a network plan's unit, registered under its NetId (the same ID on each peer; the
// native registration sets host/local vs guest/remote ownership itself), inert until ACTIVATE. Null: not made (any part
// made is undone here).
// A guest's copy of a networked enemy (docs/pvp-unit-spawn-re.md): the script's own creation callback 0x1DED20 calls
// 0x54EDB0 on it before registering it (0x1DEDD6; +0x338 = -1, +0x340 = 0, then +0x348: its own target state cleared,
// the owner's target messages drive it). Checked by its first bytes; refused (nothing made) when they differ.
constexpr std::size_t kGuestCopy=0x54EDB0;
constexpr unsigned char kGuestCopySig[]={0xC7,0x81,0x38,0x03,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0x48,0xC7,0x81,0x40,0x03,0x00};
// `team`: the enemy team, or a PvP unit's faction team (its plan's). `remote`: this machine is not the owner (a guest).
unsigned char* MakeNetworkObject(int row,const support_net::Unit& unit,const float* target,std::int32_t team,bool remote,
                                 const char** why) noexcept {
    const Entry& e=kEntries[row];
    switch(e.how) {
    case How::enemy: {
        // Generic NetworkObject registration, exactly as native script objects: SetTeam, SetLevel, a guest's copy reset,
        // then the ID.
        if(remote && !Matches(kGuestCopy,kGuestCopySig,sizeof(kGuestCopySig))){*why="unexpected EDF.dll code at 0x54EDB0";return nullptr;}
        auto* object=Create(row,unit.matrix,kGameObjectType,why);
        if(!object)return nullptr;
        reinterpret_cast<SetTeamFn>(image+kSetTeam)(object,team,true);
        reinterpret_cast<SetLevelFn>(image+kSetLevel)(object,1.0f);
        if(remote)reinterpret_cast<void(__fastcall*)(void*)>(image+kGuestCopy)(object);
        if(At<std::int32_t>(object,kTeam)!=team || !RegisterSupportObject(object,unit.netId)) {
            *why="its team or its registration failed";reinterpret_cast<DeleteFn>(image+kDelete)(object);return nullptr;
        }
        return object;
    }
    case How::vehicle: {   // empty, as offline (SpawnVehicle: CreateVehicle2's steps, never on the native AI list)
        auto* v=SpawnVehicle(row,unit.matrix,why);
        if(!v)return nullptr;
        if(!RegisterSupportObject(v,unit.netId)){*why="its registration failed";reinterpret_cast<DeleteFn>(image+kDelete)(v);return nullptr;}
        return v;
    }
    case How::soldier: {   // the support soldier (support_soldier.cpp: registered there), held until ACTIVATE
        ObjRef ref{};
        if(!ApplySupportSoldierResource(unit.matrix,SupportSoldierResource(static_cast<SupportWeapon>(e.arg),false),unit.netId,false,&ref)) {
            *why="the support soldier refused (see SUPPORT soldier lines)";return nullptr;
        }
        if(!HoldSupportSoldier(ref,true)){*why="the soldier could not be held";DeleteSupportSoldier(ref);return nullptr;}
        return static_cast<unsigned char*>(const_cast<void*>(ref.obj));
    }
    case How::drone: {     // the support drone (jet_spawn.cpp), flown by its authority once activated
        auto* v=PrepareSupportDrone(unit.matrix,target,nullptr);
        if(!v){*why="PrepareSupportDrone refused (its SGO not preloaded, or too many jets out)";return nullptr;}
        if(!RegisterSupportObject(v,unit.netId)){*why="its registration failed";DeleteSupportDrone(ObjRef::Of(v));return nullptr;}
        return v;
    }
    case How::supportCall: break;
    }
    *why="no network kind";return nullptr;
}
// Undoes what MakeNetworkObject made (its own module's delete for a soldier or a drone).
void DeleteNetworkObject(const NetworkEnemy& e) noexcept {
    if(!NetworkEnemyLive(e))return;
    if(e.how==How::soldier)DeleteSupportSoldier(e.ref);
    else if(e.how==How::drone)DeleteSupportDrone(e.ref);
    else reinterpret_cast<DeleteFn>(image+kDelete)(const_cast<void*>(e.ref.obj));
}
}  // namespace
bool SpawnRangeEnemy(std::uint64_t token,const support_net::Plan& plan,bool remote) noexcept {
    if(!token || !ValidateRangeEnemy(plan))return false;
    const int row=NetworkPlanRow(plan);
    NetworkEnemy* free=nullptr;
    for(auto& e:networkEnemies) {
        if(e.token==token)return e.row==row && NetworkEnemyLive(e); // idempotent COMMIT
        if(!e.token && !free)free=&e;
    }
    if(!free)return false;
    const char* why="";
    unsigned char* object=nullptr;
    const bool pvp=plan.catalogId==kPvpUnitCatalog;
    const std::int32_t team=pvp ? static_cast<std::int32_t>(plan.team) : kTeamEnemy;
    if(pvp && !pvp_match::PvpTeam(team)){Log("RANGE NET PvP unit refused: plan team %d is no faction's",team);return false;}
    __try { object=MakeNetworkObject(row,plan.units[0],plan.target,team,remote,&why); }
    __except(SpawnFault(row,GetExceptionInformation())) {object=nullptr;why="the game faulted making it";}
    if(!object){Log("RANGE NET create failed row=%s: %s",kEntries[row].id,why);return false;}
    __try {
        const auto ctrl=At<unsigned char*>(object,kSelfCtrl);
        if(!Readable(ctrl,16) || At<LONG>(ctrl,8)<=0) {
            NetworkEnemy gone{token,ObjRef::Of(object),row,kEntries[row].how,false};DeleteNetworkObject(gone);return false;
        }
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(ctrl+0xC));
        *free={token,ObjRef::Of(object),row,kEntries[row].how,false};
        free->pvp=pvp;free->local=pvp && SupportPlanRequesterIsLocal(plan);
        Log("RANGE NET registered token=%llu row=%s remote=%d flags=%u; waiting for all-peer ACTIVATE",
            static_cast<unsigned long long>(token),kEntries[row].id,remote,At<unsigned>(object,0x128)&3u);
        return true;
    } __except(SpawnFault(row,GetExceptionInformation())) {DestroyRangeEnemy(token);return false;}
}
void DestroyRangeEnemy(std::uint64_t token) noexcept {
    if(!token)return;
    for(auto& e:networkEnemies)if(e.token==token) {
        __try {DeleteNetworkObject(e);}
        __except(EXCEPTION_EXECUTE_HANDLER) {Log("RANGE NET rollback fault token=%llu",static_cast<unsigned long long>(token));}
        ReleaseNetworkEnemy(e);return;
    }
}
void ResetRangeEnemyNetwork() noexcept {
    // Session::Stop has already rolled back its owned transactions; release any residual weak bookkeeping only.
    for(auto& e:networkEnemies)ReleaseNetworkEnemy(e);
    submittingNetwork=false;submittingRow=-1;
    for(auto& a:netAsks)a=NetAsk{};
}
void RangeEnemyNetworkTick() noexcept {
    for(auto& e:networkEnemies)if(e.token && !e.activated && SupportTransactionActive(e.token)) {
        __try {
            if(!NetworkEnemyLive(e)){ReportSupportFailure(e.token);continue;}
            // Its kind's activation: an enemy's 0x548D50 (CreateEnemy's), a soldier let go, a drone flown; an empty
            // vehicle has none (CreateVehicle2 makes it active).
            bool on=true;
            if(e.how==How::enemy)reinterpret_cast<ActivateFn>(image+kActivate)(const_cast<void*>(e.ref.obj));
            else if(e.how==How::soldier)on=HoldSupportSoldier(e.ref,false);
            else if(e.how==How::drone)on=ActivateSupportDrone(e.ref);
            if(!on){ReportSupportFailure(e.token);continue;}
            e.activated=true;
            Log("RANGE NET activated token=%llu row=%s (all peers registered)%s",
                static_cast<unsigned long long>(e.token),kEntries[e.row].id,e.pvp ? (e.local ? " PvP unit: this player's" : " PvP unit") : "");
        } __except(SpawnFault(e.row,GetExceptionInformation())) {ReportSupportFailure(e.token);}
    }
    // This player's PvP unit, active: its control asked for (enemy control's own online lease), again every 2 s until it
    // holds it; a dead one is let go (pvp.cpp asks for a new one).
    const ULONGLONG now=GetTickCount64();
    for(auto& e:networkEnemies)if(e.token && e.activated && e.pvp && e.local) {
        __try {
            if(NetworkEnemyDead(e) || EnemyControlTargetActive(e.ref.obj))continue;
            if(e.controlAskedAt && now-e.controlAskedAt<2000)continue;
            e.controlAskedAt=now;
            const ObjRef me=ObjRef::Of(PlayerHuman());
            enemy_control::Snapshot snapshot{};
            const bool asked=me.obj && EnemyControlDescribe(me,e.ref,&snapshot) && EnemyControlRequest(snapshot);
            Log("PVP UNIT control of %s %s",kEntries[e.row].id,asked ? "asked for" : "not available yet");
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
}
bool RangeEnemyRequestNotice(std::uint32_t request,support_net::RequestStatus status) noexcept {
    if(submittingNetwork && status==support_net::RequestStatus::accepted && request) {
        netAsks[netAskNext]=NetAsk{request,submittingRow};netAskNext=(netAskNext+1)%kNetAsks;
    }
    int row=-1;NetAsk* ask=nullptr;
    for(auto& a:netAsks)if(request && a.request==request){ask=&a;row=a.row;}
    if(!ask)return false;
    const wchar_t* text=L"正在同步生成请求";
    int result=kDebugSpawnRequested;
    switch(status) {
    case support_net::RequestStatus::accepted:break;
    case support_net::RequestStatus::active:text=L"全员已创建并登记，已启用原生联机同步";result=kDebugSpawnDone;break;
    case support_net::RequestStatus::refused:text=L"房主或成员拒绝生成（测试场、资源加载或版本不一致：旧版本只认敌人行）";result=kDebugSpawnRefused;break;
    case support_net::RequestStatus::timeout:text=L"成员加载或生成确认超时，已取消并回滚本次生成";result=kDebugSpawnFailed;break;
    case support_net::RequestStatus::cancelled:text=L"成员生成失败，本次生成已回滚";result=kDebugSpawnFailed;break;
    case support_net::RequestStatus::interrupted:text=L"联机参与者或连接已变化，本次生成中断";result=kDebugSpawnFailed;break;
    }
    if(status!=support_net::RequestStatus::accepted)*ask=NetAsk{};   // its outcome told: the slot free again
    Say(result,row,text,"online request=%u status=%u",request,static_cast<unsigned>(status));
    return true;
}
bool RequestPvpUnit(int row,const float* at,wchar_t* note,std::size_t size) noexcept {
    if(row<0 || row>=kEntryCount || kEntries[row].how!=How::enemy || !at)return false;
    const bool queued=SubmitSupportRequest(static_cast<int>(kPvpUnitCatalog),at,note,size,Descriptor(kEntries[row]));
    Log("PVP UNIT %s requested at (%.1f,%.1f,%.1f): %s",kEntries[row].id,at[0],at[1],at[2],queued ? "queued" : "refused");
    return queued;
}
bool PvpUnitLive() noexcept {
    for(const auto& e:networkEnemies)if(e.token && e.pvp && e.local) {
        bool dead=true;
        __try { dead=NetworkEnemyDead(e); } __except(EXCEPTION_EXECUTE_HANDLER) {}
        if(!dead)return true;
    }
    return false;
}
namespace {
// An object row online (MakeRow): one support transaction for every peer (preload on demand -> PREPARE -> every peer
// creates and registers -> ACTIVATE), the support calls' own path. Refused here when it cannot be: off the test range
// (the host plans these on it alone), not installed, faulted before.
int NetSpawn(int row,const float* at,wchar_t* noteOut,std::size_t noteSize) noexcept {
    const Entry& e=kEntries[row];
    if(!RangePlaceOn() || !NetworkKind(e.how)) {
        Say(kDebugSpawnOnline,row,nullptr,"%s: refused: online only the test range's rows are made on every peer",e.id);
        return kDebugSpawnOnline;
    }
    const RowState state=StateOf(row);
    if(state==RowState::missing || state==RowState::broken || state==RowState::unavailable) {
        Say(kDebugSpawnFailed,row,nullptr,"%s: refused online: %s",e.id,state==RowState::missing ? "not installed" :
            state==RowState::broken ? "it faulted before" : "not available here");
        return kDebugSpawnFailed;
    }
    wchar_t note[160]{};
    submittingNetwork=true;submittingRow=row;
    const bool queued=SubmitSupportRequest(static_cast<int>(kNetworkCatalog),at,note,_countof(note),Descriptor(e));
    submittingNetwork=false;submittingRow=-1;
    if(noteOut && noteSize)wcsncpy_s(noteOut,noteSize,note,_TRUNCATE);
    Say(queued ? kDebugSpawnRequested : kDebugSpawnRefused,row,note,
        "%s: online request %s: the host plans it once loaded, every peer loads, makes and registers it",e.id,queued ? "queued" : "refused");
    return queued ? kDebugSpawnRequested : kDebugSpawnRefused;
}
}  // namespace

// Which object rows this mission preloads: in the test range (the M map's 「摆放」, `range`) every row there is (a Mods
// row whose SGO is in Mods), the heavy ones (Entry::preload false: another 17 MB model each) only with RangePlaceAll=1;
// elsewhere (the debug spawn tool alone, DebugSpawn=1) the stock rows only, as before the range's rows came in: a Mods
// row's file may be there in any mission, but a story mission pays for none of it.
bool PreloadsRow(const Entry& e,bool range) noexcept {
    if(!e.sgo)return false;
    if(!range)return !e.mod;
    return e.preload || Cfg().rangePlaceAll;
}

void PreloadDebugSpawn() noexcept {
    for(auto& p:preloaded)p=false;
    for(auto& m:missing)m=false;
    for(auto& l:loading)l=false;
    pendingCount=0;
    missionMgr=nullptr;
    menu.open=false;
    AcquireSRWLockExclusive(&cueLock);
    cue=DebugSpawnCue{};statusAt=0;
    ReleaseSRWLockExclusive(&cueLock);
    const Config& c=Cfg();
    rangeMission=c.enabled && TestRangeMode();
    // Preload on every peer before the mission's native WaitPreload. An online enemy is created only after every
    // participant validates this same descriptor/resource and ACKs the host's plan.
    const bool range=rangeMission && c.rangePlace;
    // A PvP match this machine started from (room_pvp.h): every member's unit is made on every machine, so each preloads
    // them all (the transaction refuses a peer that has not: PrepareRangeEnemy).
    bool pvpRows[kEntryCount]{};int pvpUnits=0;
    {
        pvp_protocol::State lock{};
        if(c.enabled && RoomPvpMatch(&lock,nullptr))
            for(unsigned i=0;i<lock.count;++i)if(lock.entries[i].role==static_cast<std::uint8_t>(pvp_match::Role::unit) &&
                                                 lock.entries[i].unit<kEntryCount && !pvpRows[lock.entries[i].unit]) {
                pvpRows[lock.entries[i].unit]=true;++pvpUnits;
            }
    }
    if(!c.enabled || (!c.debugSpawn && !range && !pvpUnits))return;   // off: nothing preloaded, nothing touched
    if(!profileChecked){profileChecked=true;profile=CheckProfile();}
    if(!profile)return;
    int lacking=0;
    for(int i=0;i<kEntryCount;++i) {
        const wchar_t* const lacks=kEntries[i].mod ? ModRowLacks(kEntries[i]) : nullptr;
        missing[i]=lacks!=nullptr;
        if(lacks && ++lacking<=8)Log("DEBUGSPAWN %s: not installed (required Mods file %ls missing)",kEntries[i].id,lacks);
    }
    int vehicles=0,enemies=0,absent=0;
    __try {
        void* const mgr=At<void*>(image,kPreloadMgr);
        missionMgr=At<void*>(image,kObjectMgr);
        if(!mgr || !missionMgr)return;
        for(int i=0;i<kEntryCount;++i) {
            if((!PreloadsRow(kEntries[i],range) && !pvpRows[i]) || broken[i])continue;
            if(missing[i]){++absent;continue;}   // not installed: a missing SGO is never handed to the preload
            reinterpret_cast<PreloadFn>(image+kPreload)(mgr,kEntries[i].sgo,2,-1);
            preloaded[i]=true;
            ++(kEntries[i].how==How::enemy ? enemies : vehicles);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        for(auto& p:preloaded)p=false;
        Log("DEBUGSPAWN preload faulted: nothing of the tool's this mission");
        return;
    }
    Log("DEBUGSPAWN on (%s): %d vehicles and %d enemies preloaded, %d not installed (RangePlaceAll=%d); keys menu=0x%X prev=0x%X next=0x%X category=0x%X spawn=0x%X",
        range ? "the test range's place panel" : "the debug spawn tool",vehicles,enemies,absent,c.rangePlaceAll,
        c.debugSpawnKey,c.debugSpawnPrevKey,c.debugSpawnNextKey,c.debugSpawnCategoryKey,c.debugSpawnSpawnKey);
}

bool RangePlaceOn() noexcept { return rangeMission && Cfg().enabled && Cfg().rangePlace; }

RowState SpawnRowState(int row) noexcept {
    // Online the test range's rows go to every peer (NetSpawn), the support calls through their own call; elsewhere none.
    if(InSession() && (!RangePlaceOn() || row<0 || row>=kEntryCount))return RowState::online;
    return StateOf(row);
}

int PlaceRow(int row,const float* ground,wchar_t* note,std::size_t size) noexcept {
    if(note && size)note[0]=0;
    if(row<0 || row>=kEntryCount || !ground || !std::isfinite(ground[0]+ground[1]+ground[2]))return kDebugSpawnNoPlace;
    if(!RangePlaceOn())return kDebugSpawnFailed;
    const Entry& e=kEntries[row];
    unsigned char* const human=PlayerHuman();
    float at[3]={ground[0],ground[1]+e.lift,ground[2]},heading[3],look[3]={0.0f,0.0f,1.0f};
    float me[3]={ground[0],ground[1],ground[2]-1.0f};   // no player seen: face north
    float eye[3],dir[3];
    if(CameraRay(eye,dir)){const float origin[3]={},north[3]={0.0f,0.0f,1.0f};HeadingTo(origin,dir,north,look);}
    __try { if(human)std::memcpy(me,human+kPosition,12); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    FaceOf(e,me,at,look,heading);
    Log("DEBUGSPAWN place panel: %s at the map's point (%.1f,%.1f,%.1f)",e.id,ground[0],ground[1],ground[2]);
    return MakeRow(row,at,heading,look,note,size);
}

void DebugSpawnFrame(unsigned char* human) noexcept {
    LoadTick();   // a load queued on demand (the M map's 「摆放」 too): made once complete, whatever the tool's keys
    if(!human)return;
    const Config& c=Cfg();
    if(!c.enabled || !c.debugSpawn) {   // off (or turned off in the ini): no key read; a menu left open goes
        if(menu.open){menu.open=false;Publish();}
        return;
    }
    const ULONGLONG frame=GameFrame();
    if(frame==lastFrame)return;          // once a frame: the player's frame and the vehicles' tick both call it
    lastFrame=frame;
    if(!human[kHumanPlayer] || !IsPlayer(human))return;
    const bool front=InFront() && !MapHoldsKeys();   // keys typed into another window, or the map's, are not ours
    Keys k{};
    k.toggle=Press(held[0],front && Down(c.debugSpawnKey));
    k.prev=Press(held[1],front && Down(c.debugSpawnPrevKey));
    k.next=Press(held[2],front && Down(c.debugSpawnNextKey));
    k.category=Press(held[3],front && Down(c.debugSpawnCategoryKey));
    k.spawn=Press(held[4],front && Down(c.debugSpawnSpawnKey));
    const Act act=Step(menu,k);
    if(act==Act::opened)Log("DEBUGSPAWN menu open (category %d)",menu.category);
    if(act==Act::spawn)Spawn(Picked(menu),human);
    Publish();
}

bool DebugSpawnReadout(DebugSpawnCue* out) noexcept {
    if(!Cfg().debugSpawn && !rangeMission)return false;
    AcquireSRWLockShared(&cueLock);
    *out=cue;
    const bool status=statusAt && GetTickCount64()-statusAt<=kStatusMs;
    ReleaseSRWLockShared(&cueLock);
    if(!status)out->status=kDebugSpawnNone;
    return out->open || status;
}
}  // namespace crew
