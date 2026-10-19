// The debug spawn tool (src/debug_spawn.h, docs/debug-spawn.md): ini DebugSpawn=1 (off by default), F8 opens a menu on the
// HUD, F5 / F6 pick a row, F7 the next category, F9 spawns it where the crosshair points. Range enemies also use the reliable online transaction.
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
#include "support_call.h"
#include "support_soldier.h"
#include "support_spawn.h"
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
constexpr unsigned kSceneObjectType=0x2006450,kGameObjectType=0x2006400,kVehicleType=0x2006480;
constexpr unsigned kEnemyCastSite=0x1D893E,kVehicleCastSite=0x1B2AFF,kActivateSite=0x1D8A9E,kEnemyTeamSite=0x1AD37F;
constexpr std::size_t kPreloadMgr=0x20B29A8,kObjectMgr=0x20B2958;
constexpr std::int32_t kTeamEnemy=1,kTeamFriendly=2;
const unsigned char kPreloadSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48};
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
// mov dword [rbp+8],1: CreateEnemy's team
const unsigned char kEnemyTeamSiteSig[]={0xC7,0x45,0x08,0x01,0x00,0x00,0x00};
struct Sig { unsigned rva; const unsigned char* bytes; std::size_t size; };
const Sig kSigs[]={{kPreload,kPreloadSig,sizeof(kPreloadSig)},{kCreate,kCreateSig,sizeof(kCreateSig)},
    {kDelete,kDeleteSig,sizeof(kDeleteSig)},{kReadSetup,kReadSetupSig,sizeof(kReadSetupSig)},
    {kSetTeam,kSetTeamSig,sizeof(kSetTeamSig)},{kSetLevel,kSetLevelSig,sizeof(kSetLevelSig)},
    {kActivate,kActivateSig,sizeof(kActivateSig)},{kCast,kCastSig,sizeof(kCastSig)},
    {kEnemyCastSite,kEnemyCastSiteSig,sizeof(kEnemyCastSiteSig)},{kVehicleCastSite,kVehicleCastSiteSig,sizeof(kVehicleCastSiteSig)},
    {kActivateSite,kActivateSiteSig,sizeof(kActivateSiteSig)},{kEnemyTeamSite,kEnemyTeamSiteSig,sizeof(kEnemyTeamSiteSig)}};

struct alignas(16) InitParam { const void* vtable; unsigned char rest[0x28]; };
static_assert(sizeof(InitParam)==0x30);
using PreloadFn=void(*)(void*,const wchar_t*,std::int32_t,std::int32_t);
using CreateFn=unsigned char*(*)(void*,const float*,const wchar_t*,InitParam*);
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
// This mission is the test range pack's (edf5campaign.cpp TestRangeMode at its start): the M map's 「摆放」 is on
// (ini RangePlace), whatever DebugSpawn says.
bool rangeMission=false;
struct NetworkEnemy { std::uint64_t token=0;ObjRef ref{};int row=-1;bool activated=false; };
NetworkEnemy networkEnemies[support_net::kMaxTransactions]{};
bool submittingNetwork=false;
std::uint32_t networkRequest=0;
int networkRequestRow=-1;

bool NetworkEnemyLive(const NetworkEnemy& e) noexcept {
    // Weak control blocks are retained until transaction teardown: test the strong count before the actor pointer.
    return e.ref.ctrl && At<LONG>(e.ref.ctrl,8)>0 && e.ref.Is(e.ref.obj) &&
        !(At<unsigned char>(e.ref.obj,0x18)&4);
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
        return profile && preloaded[row] ? RowState::ready : RowState::notPreloaded;
    case How::drone: return Cfg().jetPilot && jet::SpawnReady() ? RowState::ready : RowState::unavailable;
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
    InitParam param{image+kInitVtable,{}};
    alignas(16) float nativeMatrix[16];   // SceneObject's constructor loads its rows with MOVAPS (support-soldier-native.md)
    std::memcpy(nativeMatrix,matrix,sizeof(nativeMatrix));
    unsigned char* object=nullptr;
    __try {
        void* const mgr=At<void*>(image,kObjectMgr);
        if(!mgr || mgr!=missionMgr){*why="the object manager is not this mission's preload's";return nullptr;}
        object=reinterpret_cast<CreateFn>(image+kCreate)(mgr,nativeMatrix,kEntries[row].sgo,&param);
        if(!object){*why="CreateObject returned null";return nullptr;}
        void* const cast=reinterpret_cast<CastFn>(image+kCast)(object,0,image+kSceneObjectType,image+type,0);
        if(cast==object)return object;
        *why=cast ? "the cast moved the pointer" : type==kVehicleType ? "not a VehicleBase" : "not a GameObjectBase";
        reinterpret_cast<DeleteFn>(image+kDelete)(object);
        return nullptr;
    } __except(SpawnFault(row,GetExceptionInformation())) { *why="the game faulted making it";return nullptr; }
}

// A stock vehicle, empty, on the friend team: support_spawn.cpp's steps (CreateObject, its mission_setup, SetTeam(2),
// SetLevel(1), NoteLocalCopy), its class checked by the script's own VehicleBase cast instead of a table of vtables.
unsigned char* SpawnVehicle(int row,const float* matrix,const char** why) noexcept {
    unsigned char* const v=Create(row,matrix,kVehicleType,why);
    if(!v)return nullptr;
    __try {
        if(!ApplyMissionSetup(v)) {
            *why="its mission_setup did not apply";
            reinterpret_cast<DeleteFn>(image+kDelete)(v);
            return nullptr;
        }
        reinterpret_cast<SetTeamFn>(image+kSetTeam)(v,kTeamFriendly,true);
        reinterpret_cast<SetLevelFn>(image+kSetLevel)(v,1.0f);
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

// Makes row `row` at `at` (its lift in already) facing `heading` (`look`: the drone's flight heading), by its How; the
// result for the HUD and the log (Say), and returned (DebugSpawnStatus) with a support call's own answer in `note`. The
// one way every spawn goes, the debug spawn tool's (Spawn) and the M map's 「摆放」 (PlaceRow): online it refuses
// before any of it (a native object made here exists on this machine alone: the other players' games never see it,
// docs/feature-2026-10-17-testrange-update.md).
int MakeRow(int row,const float* at,const float* heading,const float* look,wchar_t* noteOut,std::size_t noteSize) noexcept {
    if(row<0 || row>=kEntryCount)return kDebugSpawnFailed;
    const Entry& e=kEntries[row];
    if(noteOut && noteSize)noteOut[0]=0;
    if(InSession()) {
        if(e.how==How::enemy && RangePlaceOn() && StateOf(row)==RowState::ready) {
            wchar_t note[160]{};
            submittingNetwork=true;networkRequest=0;networkRequestRow=row;
            const bool queued=SubmitSupportRequest(static_cast<int>(kNetworkCatalog),at,note,_countof(note),Descriptor(e));
            submittingNetwork=false;
            if(noteOut && noteSize)wcsncpy_s(noteOut,noteSize,note,_TRUNCATE);
            Say(queued ? kDebugSpawnRequested : kDebugSpawnRefused,row,note,
                "%s: online enemy request %s: waiting for host plan / all-peer native registration",e.id,queued ? "queued" : "refused");
            return queued ? kDebugSpawnRequested : kDebugSpawnRefused;
        }
        Say(kDebugSpawnOnline,row,nullptr,"%s: refused: only preloaded range enemies have an online spawn transaction",e.id);
        return kDebugSpawnOnline;
    }
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
        else if(!preloaded[row])why="not preloaded this mission (turned on mid-mission, or a heavy row with RangePlaceAll=0)";
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
}  // namespace

// The support session supplies authenticated participant admission, a world epoch, host-issued unique native IDs,
// all-peer PREPARE validation, COMMIT results, ACTIVATE and cancel/timeout rollback. No object is made from a request.
support_net::PlanResult PlanRangeEnemy(std::uint32_t catalog,const float* target,std::uint64_t descriptor,
                                      support_net::Plan* out) noexcept {
    if(catalog!=kNetworkCatalog || !OnlineHostOnly() || !InSession() || !RangePlaceOn() || !target || !out)
        return support_net::PlanResult::refused;
    const int row=NetworkRow(descriptor);
    if(row<0 || StateOf(row)!=RowState::ready)return support_net::PlanResult::refused;
    float ground[3]={target[0],target[1]-kEntries[row].lift,target[2]};
    if(!std::isfinite(ground[0]+ground[1]+ground[2]) || !GroundUnder(ground))return support_net::PlanResult::refused;
    ground[1]+=kEntries[row].lift;
    const float heading[3]={0.0f,0.0f,1.0f};
    alignas(16) float matrix[16];FacingMatrix(heading,ground,matrix);
    return BuildNetworkPlan(row,matrix,out) ? support_net::PlanResult::ready : support_net::PlanResult::refused;
}
bool ValidateRangeEnemy(const support_net::Plan& plan) noexcept {
    const int row=NetworkPlanRow(plan);
    return InSession() && RangePlaceOn() && row>=0 && StateOf(row)==RowState::ready;
}
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
    auto* object=Create(row,plan.units[0].matrix,kGameObjectType,&why);
    if(!object){Log("RANGE NET create failed row=%s: %s",kEntries[row].id,why);return false;}
    __try {
        const auto ctrl=At<unsigned char*>(object,kSelfCtrl);
        if(!Readable(ctrl,16) || At<LONG>(ctrl,8)<=0) {
            reinterpret_cast<DeleteFn>(image+kDelete)(object);return false;
        }
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(ctrl+0xC));
        *free={token,ObjRef::Of(object),row,false};
        reinterpret_cast<SetTeamFn>(image+kSetTeam)(object,kTeamEnemy,true);
        reinterpret_cast<SetLevelFn>(image+kSetLevel)(object,1.0f);
        // Generic NetworkObject registration, exactly as native script objects: class-5 ID is generated once by the
        // host, the same ID on each peer; native registration sets host/local vs guest/remote ownership itself.
        if(At<std::int32_t>(object,kTeam)!=kTeamEnemy || !RegisterSupportObject(object,plan.units[0].netId)) {
            DestroyRangeEnemy(token);return false;
        }
        Log("RANGE NET registered token=%llu row=%s remote=%d flags=%u; waiting for all-peer ACTIVATE",
            static_cast<unsigned long long>(token),kEntries[row].id,remote,At<unsigned>(object,0x128)&3u);
        return true;
    } __except(SpawnFault(row,GetExceptionInformation())) {DestroyRangeEnemy(token);return false;}
}
void DestroyRangeEnemy(std::uint64_t token) noexcept {
    if(!token)return;
    for(auto& e:networkEnemies)if(e.token==token) {
        __try {if(NetworkEnemyLive(e))reinterpret_cast<DeleteFn>(image+kDelete)(const_cast<void*>(e.ref.obj));}
        __except(EXCEPTION_EXECUTE_HANDLER) {Log("RANGE NET rollback fault token=%llu",static_cast<unsigned long long>(token));}
        ReleaseNetworkEnemy(e);return;
    }
}
void ResetRangeEnemyNetwork() noexcept {
    // Session::Stop has already rolled back its owned transactions; release any residual weak bookkeeping only.
    for(auto& e:networkEnemies)ReleaseNetworkEnemy(e);
    submittingNetwork=false;networkRequest=0;networkRequestRow=-1;
}
void RangeEnemyNetworkTick() noexcept {
    for(auto& e:networkEnemies)if(e.token && !e.activated && SupportTransactionActive(e.token)) {
        __try {
            if(!NetworkEnemyLive(e)){ReportSupportFailure(e.token);continue;}
            reinterpret_cast<ActivateFn>(image+kActivate)(const_cast<void*>(e.ref.obj));
            e.activated=true;
            Log("RANGE NET activated token=%llu row=%s (all peers registered)",
                static_cast<unsigned long long>(e.token),kEntries[e.row].id);
        } __except(SpawnFault(e.row,GetExceptionInformation())) {ReportSupportFailure(e.token);}
    }
}
bool RangeEnemyRequestNotice(std::uint32_t request,support_net::RequestStatus status) noexcept {
    if(submittingNetwork && status==support_net::RequestStatus::accepted)networkRequest=request;
    if(!request || request!=networkRequest)return false;
    const wchar_t* text=L"正在同步敌人生成请求";
    int result=kDebugSpawnRequested;
    switch(status) {
    case support_net::RequestStatus::accepted:break;
    case support_net::RequestStatus::active:text=L"全员已创建并登记敌人，已启用原生联机同步";result=kDebugSpawnDone;break;
    case support_net::RequestStatus::refused:text=L"房主或成员拒绝生成（测试场、资源预载或版本不一致）";result=kDebugSpawnRefused;break;
    case support_net::RequestStatus::timeout:text=L"成员生成确认超时，已取消并回滚本次敌人";result=kDebugSpawnFailed;break;
    case support_net::RequestStatus::cancelled:text=L"成员生成失败，本次敌人已回滚";result=kDebugSpawnFailed;break;
    case support_net::RequestStatus::interrupted:text=L"联机参与者或连接已变化，本次生成中断";result=kDebugSpawnFailed;break;
    }
    Say(result,networkRequestRow,text,"online request=%u status=%u",request,static_cast<unsigned>(status));
    return true;
}

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
    if(!c.enabled || (!c.debugSpawn && !range))return;   // off: nothing preloaded, nothing touched
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
            if(!PreloadsRow(kEntries[i],range) || broken[i])continue;
            if(InSession() && range && kEntries[i].how!=How::enemy)continue;
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
    if(InSession() && (!RangePlaceOn() || row<0 || row>=kEntryCount || kEntries[row].how!=How::enemy))return RowState::online;
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
