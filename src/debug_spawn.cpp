// The debug spawn tool (src/debug_spawn.h, docs/debug-spawn.md): ini DebugSpawn=1 (off by default), F8 opens a menu on the
// HUD, F5 / F6 pick a row, F7 the next category, F9 spawns it where the crosshair points. Offline only.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
//
// Nothing of it runs while it is off: PreloadDebugSpawn returns before touching the preload manager, DebugSpawnFrame
// before reading a key, DebugSpawnReadout has nothing to show. It patches nothing at all, on or off.
#include "crew.h"
#include "debug_spawn.h"
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

// The plugin's aircraft from here fly as an escort of the player (JetLaunch's `escort`) for this long; a heli guards
// the player as a rescue heli does.
constexpr DWORD kHeliFuelSec=600;
// The source jets launched from here share (jet_spawn.cpp FlightFor: one flight, their rounds pass each other).
const int kJetSource=0;
// How long a spawn's result stays on the HUD (wall ms).
constexpr ULONGLONG kStatusMs=4000;

bool profileChecked=false,profile=false;
void* missionMgr=nullptr;          // the object manager this mission's preload was for
bool preloaded[kEntryCount]{};     // its SGO queued for this mission
bool broken[kEntryCount]{};        // the game faulted making it: off until the game restarts (as support_spawn.cpp)
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
    GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    return pid==GetCurrentProcessId();
}
bool Down(int vk) noexcept { return vk>0 && vk<255 && (GetAsyncKeyState(vk)&0x8000)!=0; }

// The row's state for the HUD: can it be made now.
bool RowReady(int row) noexcept {
    if(row<0 || row>=kEntryCount)return false;
    const Entry& e=kEntries[row];
    switch(e.how) {
    case How::stockVehicle:
    case How::enemy: return profile && preloaded[row] && !broken[row];
    case How::jetRole: case How::drone: case How::heli: return Cfg().jetPilot && jet::SpawnReady();
    case How::soldier: return SupportSoldiersReady();
    }
    return false;
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
void Say(int status,int row,const char* format,...) noexcept {
    char line[400];
    va_list args;va_start(args,format);
    vsnprintf_s(line,sizeof(line),_TRUNCATE,format,args);
    va_end(args);
    Log("DEBUGSPAWN %s",line);
    AcquireSRWLockExclusive(&cueLock);
    cue.status=status;cue.statusRow=row;statusAt=GetTickCount64();
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
    const bool facePlayer=e.category==Category::enemy || e.category==Category::soldier;
    if(facePlayer)HeadingTo(at,me,look,heading);
    else HeadingTo(me,at,look,heading);
    return true;
}

void Spawn(int row,unsigned char* human) noexcept {
    if(row<0 || row>=kEntryCount)return;
    const Entry& e=kEntries[row];
    if(InSession()) {
        Say(kDebugSpawnOnline,row,"%s: refused: online session (a local-only object would not exist on the other machines)",e.id);
        return;
    }
    float at[3],heading[3],look[3];
    if(!Where(e,human,at,heading,look)) {
        Say(kDebugSpawnNoPlace,row,"%s: failed: no place (camera straight up / down with nothing hit)",e.id);
        return;
    }
    alignas(16) float m[16];
    FacingMatrix(heading,at,m);
    const char* why="";
    const void* made=nullptr;
    switch(e.how) {
    case How::stockVehicle:
    case How::enemy:
        if(!profile)why="EDF.dll profile mismatch (see DEBUGSPAWN profile)";
        else if(broken[row])why="it faulted before: off until the game restarts";
        else if(!preloaded[row])why="not preloaded this mission (DebugSpawn turned on mid-mission: next mission)";
        else made=e.how==How::enemy ? SpawnEnemy(row,m,&why) : SpawnVehicle(row,m,&why);
        break;
    case How::jetRole:
        if(!Cfg().jetPilot)why="JetPilot=0";
        else if(!JetLaunch(static_cast<JetRole>(e.arg),at,look,at,Cfg().jetFuelSec,&kJetSource,true))
            why="JetLaunch refused (its SGO not installed / not preloaded, or too many jets out)";
        else made=&kJetSource;
        break;
    case How::drone:
        if(!Cfg().jetPilot)why="JetPilot=0";
        else if(!(made=JetLaunchDrone(at,look,at,Cfg().jetFuelSec,&kJetSource,true)))
            why="JetLaunchDrone refused (EDF6VC_JET_DRONE.SGO not preloaded, or too many jets out)";
        break;
    case How::heli:
        if(!Cfg().jetPilot)why="JetPilot=0";
        else if(unsigned char* const v=HeliLaunch(static_cast<HeliBody>(e.arg),at,look);!v)
            why="HeliLaunch refused (its SGO not installed / not preloaded)";
        else{HeliCalled(v,false,at,kHeliFuelSec);made=v;}
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
        Say(kDebugSpawnDone,row,"%s: spawned %p at (%.1f,%.1f,%.1f) heading (%.2f,%.2f)%s",e.id,made,at[0],at[1],at[2],heading[0],heading[2],
            e.category==Category::enemy ? " team enemy" : e.category==Category::vehicle ? " team friend, empty" : "");
    } else {
        Say(kDebugSpawnFailed,row,"%s: failed at (%.1f,%.1f,%.1f): %s",e.id,at[0],at[1],at[2],why);
    }
}
}  // namespace

void PreloadDebugSpawn() noexcept {
    for(auto& p:preloaded)p=false;
    missionMgr=nullptr;
    menu.open=false;
    AcquireSRWLockExclusive(&cueLock);
    cue=DebugSpawnCue{};statusAt=0;
    ReleaseSRWLockExclusive(&cueLock);
    const Config& c=Cfg();
    if(!c.enabled || !c.debugSpawn)return;   // off: nothing preloaded, nothing touched
    if(!profileChecked){profileChecked=true;profile=CheckProfile();}
    if(!profile)return;
    int vehicles=0,enemies=0;
    __try {
        void* const mgr=At<void*>(image,kPreloadMgr);
        missionMgr=At<void*>(image,kObjectMgr);
        if(!mgr || !missionMgr)return;
        for(int i=0;i<kEntryCount;++i) {
            if(!kEntries[i].sgo || broken[i])continue;
            reinterpret_cast<PreloadFn>(image+kPreload)(mgr,kEntries[i].sgo,2,-1);
            preloaded[i]=true;
            ++(kEntries[i].how==How::enemy ? enemies : vehicles);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        for(auto& p:preloaded)p=false;
        Log("DEBUGSPAWN preload faulted: nothing of the tool's this mission");
        return;
    }
    Log("DEBUGSPAWN on: %d stock vehicles and %d enemies preloaded; keys menu=0x%X prev=0x%X next=0x%X category=0x%X spawn=0x%X",
        vehicles,enemies,c.debugSpawnKey,c.debugSpawnPrevKey,c.debugSpawnNextKey,c.debugSpawnCategoryKey,c.debugSpawnSpawnKey);
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
    if(!Cfg().debugSpawn)return false;
    AcquireSRWLockShared(&cueLock);
    *out=cue;
    const bool status=statusAt && GetTickCount64()-statusAt<=kStatusMs;
    ReleaseSRWLockShared(&cueLock);
    if(!status)out->status=kDebugSpawnNone;
    return out->open || status;
}
}  // namespace crew
