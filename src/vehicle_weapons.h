#pragma once
// The weapon SGOs of a vehicle the plugin preloads, queued with it (vehicle_weapons_logic.h says why). Every site that
// preloads a vehicle's SGO for the mission or loads one on demand (support_spawn.cpp, support_variants.cpp,
// debug_spawn.cpp, player_vehicle.cpp, subcarrier.cpp) calls QueueVehicleWeapons after the vehicle's own preload: the
// weapons follow the vehicle, so they are in the mission's preload set (preload_set.h) exactly when the vehicle is,
// offline and online alike (online the room's set decides the vehicles; every member runs the same build: the same lists).
// Header-only: the files that preload vehicles are in several tests' builds.
#include "vehicle_weapons_logic.h"
#include "crew.h"
#include <windows.h>
#include <cstdio>
#include <cwchar>
#include <iterator>

namespace crew {
namespace vehicle_weapons::detail {
inline SRWLOCK lock=SRWLOCK_INIT;
inline Queued queued;
inline int vehicles=0,weapons=0;
inline unsigned char sgo[2u<<20];   // a vehicle's SGO (its models are in their archives): well under 1 MB
// <the game's directory>\Mods\OBJECT\NAME.SGO's bytes (the exe's directory: Mods sits beside EDF6.exe) into `out`
// (at most `capacity`); its size, 0 when there is no such file (a stock vehicle) or it cannot be read whole.
inline std::size_t ReadModsObject(std::wstring_view name,unsigned char* out,std::size_t capacity) noexcept {
    wchar_t path[MAX_PATH];
    const DWORD n=GetModuleFileNameW(nullptr,path,static_cast<DWORD>(std::size(path)));
    wchar_t* slash=n && n<std::size(path) ? wcsrchr(path,L'\\') : nullptr;
    if(!slash)return 0;
    const std::size_t room=std::size(path)-static_cast<std::size_t>(slash-path);
    if(swprintf_s(slash,room,L"\\Mods\\OBJECT\\%.*ls.SGO",static_cast<int>(name.size()),name.data())<0)return 0;
    FILE* in=nullptr;
    if(_wfopen_s(&in,path,L"rb") || !in)return 0;
    const std::size_t got=std::fread(out,1,capacity,in);
    const bool whole=got<capacity && std::feof(in)!=0;
    std::fclose(in);
    return whole ? got : 0;
}
// One weapon file queued with the game's preload; false when the call faulted.
inline bool QueueOne(void* mgr,void(*preload)(void*,const wchar_t*,std::int32_t,std::int32_t),const wchar_t* path) noexcept {
    __try { preload(mgr,path,2,-1);return true; } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}  // namespace vehicle_weapons::detail

using VehicleWeaponPreloadFn=void(*)(void*,const wchar_t*,std::int32_t,std::int32_t);

// A new mission: nothing queued yet (mission.cpp, before the mission's preloads).
inline void ResetVehicleWeapons() noexcept {
    using namespace vehicle_weapons::detail;
    AcquireSRWLockExclusive(&lock);
    queued.count=0;vehicles=0;weapons=0;
    ReleaseSRWLockExclusive(&lock);
}
// The weapons of the vehicle at `objectPath` (app:/object/NAME.sgo), each queued with the game's preload `preload` on
// `mgr` as the vehicle was (the game's WaitPreload, or the caller's load completion, waits for them too) unless queued
// already this mission. NAME's SGO in Mods/OBJECT is read; else it is a stock vehicle, src/vehicle_weapons.inc's.
// Neither: logged (its weapons are then read when it is made). The number newly queued.
inline int QueueVehicleWeapons(void* mgr,VehicleWeaponPreloadFn preload,const wchar_t* objectPath) noexcept {
    using namespace vehicle_weapons;
    using namespace vehicle_weapons::detail;
    if(!mgr || !preload || !objectPath)return 0;
    const std::wstring_view name=ObjectName(objectPath);
    if(name.empty())return 0;
    int added=0,faults=0;
    AcquireSRWLockExclusive(&lock);
    const std::size_t size=ReadModsObject(name,sgo,sizeof(sgo));
    const int listed=WeaponsOf(name,sgo,size,[&](std::wstring_view w) noexcept {
        if(queued.Has(w))return;
        wchar_t path[kPathMost];
        const std::size_t len=w.size()<kPathMost ? w.size() : kPathMost-1;
        for(std::size_t i=0;i<len;++i)path[i]=w[i];
        path[len]=0;
        if(!QueueOne(mgr,preload,path)){++faults;return;}
        queued.Add(w);++added;
    });
    ++vehicles;weapons+=added;
    ReleaseSRWLockExclusive(&lock);
    if(listed<0)Log("PRELOAD vehicle weapons: %.*ls is neither in Mods/OBJECT nor a stock vehicle of src/vehicle_weapons.inc "
                    "(tools/gen_vehicle_weapons.py): its weapons are read when it is made",static_cast<int>(name.size()),name.data());
    if(faults)Log("PRELOAD vehicle weapons: %d of %.*ls's faulted when queued",faults,static_cast<int>(name.size()),name.data());
    return added;
}
// The mission's count so far, for the log (mission.cpp after the preloads).
inline void LogVehicleWeapons() noexcept {
    using namespace vehicle_weapons::detail;
    AcquireSRWLockShared(&lock);
    const int v=vehicles,w=weapons;
    ReleaseSRWLockShared(&lock);
    Log("PRELOAD vehicle weapons: %d weapon files queued with %d vehicles",w,v);
}
}  // namespace crew
