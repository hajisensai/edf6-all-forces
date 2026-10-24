// The room pre-sync (src/room_sync.h; the logic and the wire: src/room_sync_logic.h). This file only wires the game's
// side into it: EDF6Coop's member attribute table (resolved from the loaded EDF6Coop.dll; none, or an older Coop
// without the export: SyncState::unavailable and nothing else changes, the pending list does the work as before), the
// files in Mods/OBJECT, the pending list (support_variants.h NoteMissingVariant), the background generator
// (loadout_store.h) and the host's configuration (support_variants.h ForEachConfiguredVariant). UI thread only.
#include "room_sync.h"
#include "room_sync_logic.h"
#include "coop_attr.h"
#include "crew.h"
#include "loadout_store.h"
#include "support_config.h"
#include "support_variants.h"
#include <Windows.h>
#include <cstddef>
#include <cwchar>
#include <iterator>

namespace crew::roomsync {
namespace {
Engine engine;   // the UI thread's (room_sync.h)
// The engine's HostList() copied for other threads (ForEachRoomHostVariant), under its lock, when its hash changes.
SRWLOCK hostLock=SRWLOCK_INIT;
VariantSet hostCopy;
std::uint64_t hostCopyHash=0;
void PublishHostList() noexcept {
    const std::uint64_t hash=engine.HostListHash();
    if(hash==hostCopyHash)return;
    AcquireSRWLockExclusive(&hostLock);
    hostCopy=engine.HostList();
    if(!hash)hostCopy.count=0;
    hostCopyHash=hash;
    ReleaseSRWLockExclusive(&hostLock);
}
// The other members' player loadouts the last room published (MemberPlayerVehicles), for the mission's start.
SRWLOCK playersLock=SRWLOCK_INIT;
VariantSet playersCopy;
void PublishPlayers() noexcept {
    AcquireSRWLockExclusive(&playersLock);
    playersCopy=engine.MemberPlayerVehicles();
    ReleaseSRWLockExclusive(&playersLock);
}
bool apiLogged=false;

// coop_attr.h: v1's functions up to snapshot required; lastError (appended within v1) optional (room_sync_logic.h asks
// EDF6COOP_MA_HAS before calling it).
const EDF6CoopMemberAttributeApi* Api() noexcept {
    const EDF6CoopMemberAttributeApi* table=coopattr::Api();
    if(!apiLogged) {
        apiLogged=true;
        Log("ROOMSYNC EDF6Coop member attributes %s",table ? "found: room pre-sync on" :
            "missing (older EDF6Coop or no EDF6Coop.dll): room pre-sync off, the pending list as before");
    }
    return table;
}

// Mods/OBJECT beside EDF6.exe (support_variants.cpp ModsPath).
bool ObjectPath(const wchar_t* file,wchar_t* out,std::size_t capacity) noexcept {
    const DWORD n=GetModuleFileNameW(nullptr,out,static_cast<DWORD>(capacity));
    wchar_t* slash=n && n<capacity ? wcsrchr(out,L'\\') : nullptr;
    if(!slash)return false;
    slash[1]=0;
    return wcscat_s(out,capacity,L"Mods\\OBJECT\\")==0 && wcscat_s(out,capacity,file)==0;
}
bool FileThere(void*,const wchar_t* file) noexcept {
    wchar_t path[MAX_PATH];
    if(!ObjectPath(file,path,std::size(path)))return false;
    const DWORD a=GetFileAttributesW(path);
    return a!=INVALID_FILE_ATTRIBUTES && !(a&FILE_ATTRIBUTE_DIRECTORY);
}
void NoteMissing(void*,const wchar_t* file) noexcept { NoteMissingVariant(file,"the room host's list"); }
bool StartGeneration(void*) noexcept {
    wchar_t why[160]{};
    return StartVariantGeneration(why,std::size(why));
}
GenStatus Generation(void*) noexcept {
    const GenerateStatus s=VariantGenerationStatus();
    GenStatus g;
    switch(s.state) {
        case GenerateState::running:g.state=Gen::running;break;
        case GenerateState::ok:g.state=Gen::ok;break;
        case GenerateState::failed:g.state=Gen::failed;break;
        case GenerateState::missing:g.state=Gen::missing;break;
        default:g.state=Gen::idle;break;
    }
    wcsncpy_s(g.message,s.message,_TRUNCATE);
    return g;
}
void Configured(void*,void (*each)(void*,const wchar_t*),void* set) noexcept { ForEachConfiguredVariant(each,set); }
void LogLine(void*,const char* text) noexcept { Log("%s",text); }
bool OwnPlayer(void*,char* value,std::size_t capacity) noexcept {
    return playerveh::OwnValue(SupportCfg().playerVehicle,kPlayerVehicleCount,value,capacity);
}

Env TheEnv() noexcept {
    Env env;
    env.fileThere=FileThere;env.noteMissing=NoteMissing;env.startGeneration=StartGeneration;env.generation=Generation;
    env.configured=Configured;env.log=LogLine;env.ownPlayer=OwnPlayer;
    return env;
}
}  // namespace

void TickRoomSync() noexcept { engine.Tick(Api(),GetTickCount64(),TheEnv());PublishHostList();PublishPlayers(); }
void LeaveRoomSync() noexcept { engine.Leave(Api()); }
void ForgetRoomHostList() noexcept { engine.ForgetHostList();PublishHostList(); }
int ForEachRoomHostVariant(void (*each)(void* context,const wchar_t* file),void* context) noexcept {
    if(!each)return 0;
    int given=0;
    AcquireSRWLockShared(&hostLock);
    for(int i=0;i<hostCopy.count;++i) {
        wchar_t file[96];
        if(VariantFileOf(hostCopy.ref[i],file,96)){each(context,file);++given;}
    }
    ReleaseSRWLockShared(&hostLock);
    return given;
}
int ForEachRoomPlayerVehicle(void (*each)(void* context,VehicleBody body,std::uint64_t bits),void* context) noexcept {
    if(!each)return 0;
    int given=0;
    AcquireSRWLockShared(&playersLock);
    for(int i=0;i<playersCopy.count;++i)
        if(playersCopy.ref[i].vehicle){each(context,static_cast<VehicleBody>(playersCopy.ref[i].sub),playersCopy.ref[i].bits);++given;}
    ReleaseSRWLockShared(&playersLock);
    return given;
}
SyncState RoomSyncState() noexcept { return engine.State(); }
int RoomSyncLines(SyncLine* out,int capacity) noexcept { return engine.Lines(out,capacity,hudtext::InUse()); }
}  // namespace crew::roomsync
