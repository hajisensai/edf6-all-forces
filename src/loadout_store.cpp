// The in-game loadout menu's saving: loadout_store.h.
#include "loadout_store.h"
#include "crew.h"
#include "support_call.h"
#include <cwchar>
#include <iterator>
#include <new>

namespace crew {
namespace {
constexpr wchar_t kGenerator[]=L"EDF6VehicleCrew.generator.exe";       // tools/installer.py GENERATOR
constexpr wchar_t kStatus[]=L"EDF6VehicleCrew.generate_status.txt";    // tools/installer.py GENERATE_STATUS
SRWLOCK generatorLock=SRWLOCK_INIT;
VariantGenerator* generator=nullptr;   // never freed: the plugin lives as long as the game

void Say(wchar_t* why,std::size_t capacity,const wchar_t* text) noexcept {
    if(why && capacity)wcsncpy_s(why,capacity,text,_TRUNCATE);
}
// `name` beside the ini (Mods/Plugins).
bool PluginsPath(const wchar_t* name,wchar_t* out,std::size_t capacity) noexcept {
    const wchar_t* ini=ConfigPath();
    if(!ini || !*ini || wcscpy_s(out,capacity,ini))return false;
    wchar_t* slash=wcsrchr(out,L'\\');
    if(!slash)return false;
    slash[1]=0;
    return wcscat_s(out,capacity,name)==0;
}
// The game directory: EDF6.exe's (as support_variants.cpp ModsPath).
bool GameDir(wchar_t* out,std::size_t capacity) noexcept {
    const DWORD n=GetModuleFileNameW(nullptr,out,static_cast<DWORD>(capacity));
    wchar_t* slash=n && n<capacity ? wcsrchr(out,L'\\') : nullptr;
    if(!slash)return false;
    *slash=0;
    return true;
}
// The catalog entry `unit`'s seats (SupportCallSeats), -1 when no entry has that key.
int SeatsOf(const wchar_t* unit) noexcept {
    for(int i=0;i<SupportCallCount();++i)
        if(const wchar_t* key=SupportCallKey(i);key && !_wcsicmp(key,unit))return SupportCallSeats(i);
    return -1;
}
VariantGenerator* Generator() noexcept {
    if(!generator)generator=new(std::nothrow) VariantGenerator();
    return generator;
}
}  // namespace

bool SaveSupportLoadouts(const LoadoutSave* items,int count,int* failed,wchar_t* why,std::size_t capacity) noexcept {
    if(failed)*failed=-1;
    if(count<0 || count>kLoadoutSaveMost || (count && !items)){Say(why,capacity,L"要保存的项太多");return false;}
    LoadoutEdit edits[kLoadoutSaveMost];
    for(int i=0;i<count;++i) {
        const LoadoutSave& s=items[i];
        // A crew colour may also be every crew's default (unit "") or the mission vehicles' support crews' (kCrewMissionKey).
        const bool crewOnly=s.field==LoadoutField::crew && s.unit && (!*s.unit || !_wcsicmp(s.unit,kCrewMissionKey));
        const int seats=crewOnly ? 0 : s.unit ? SeatsOf(s.unit) : -1;
        if(seats<0){if(failed)*failed=i;Say(why,capacity,L"没有这个支援单位");return false;}
        edits[i]=LoadoutEdit{s.field,s.unit,seats,s.value};
    }
    const wchar_t* path=ConfigPath();
    std::string before,after;
    try {
        if(!ReadFileBytes(path,&before)){Say(why,capacity,L"读不到 EDF6VehicleCrew.ini");return false;}
        if(!EditLoadoutIniAll(before,edits,count,&after,failed,why,capacity))return false;
    } catch(...) {   // std::string's allocations: out of memory
        Say(why,capacity,L"内存不足");return false;
    }
    // One write, one reload, whatever the count (the UI thread waits on both).
    if(after!=before && !ReplaceFileAtomic(path,after)){Say(why,capacity,L"写不进 EDF6VehicleCrew.ini");return false;}
    for(int i=0;i<count;++i)Log("LOADOUT %ls=%ls saved",LoadoutKey(items[i].field,items[i].unit).c_str(),items[i].value);
    ReloadConfigNow();
    return true;
}
bool SaveSupportLoadout(LoadoutField field,const wchar_t* unit,const wchar_t* value,wchar_t* why,std::size_t capacity) noexcept {
    const LoadoutSave one{field,unit,value};
    return SaveSupportLoadouts(&one,1,nullptr,why,capacity);
}

bool StartVariantGeneration(wchar_t* why,std::size_t capacity) noexcept {
    wchar_t exe[MAX_PATH],game[MAX_PATH],args[MAX_PATH+40];
    if(!PluginsPath(kGenerator,exe,std::size(exe)) || !GameDir(game,std::size(game))){Say(why,capacity,L"路径太长");return false;}
    _snwprintf_s(args,_TRUNCATE,L"loadout-generate \"%ls\"",game);
    AcquireSRWLockExclusive(&generatorLock);
    VariantGenerator* g=Generator();
    const bool started=g && g->Start(exe,args,why,capacity);
    ReleaseSRWLockExclusive(&generatorLock);
    Log("LOADOUT generator %s: %ls",started ? "started" : "not started",started ? exe : (why ? why : L""));
    return started;
}

GenerateStatus VariantGenerationStatus() noexcept {
    wchar_t status[MAX_PATH],exe[MAX_PATH];
    GenerateStatus s;
    if(!PluginsPath(kStatus,status,std::size(status)) || !PluginsPath(kGenerator,exe,std::size(exe)))return s;
    AcquireSRWLockExclusive(&generatorLock);
    VariantGenerator* g=Generator();
    if(g)s=g->Status(status);
    ReleaseSRWLockExclusive(&generatorLock);
    if(s.state==GenerateState::idle && GetFileAttributesW(exe)==INVALID_FILE_ATTRIBUTES) {
        s.state=GenerateState::missing;
        Say(s.message,std::size(s.message),L"找不到生成器（请用安装器重新安装）");
    }
    return s;
}
}  // namespace crew
