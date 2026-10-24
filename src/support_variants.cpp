// support_variants.h. EDF.dll TimeDateStamp 0x678CCB46: the preload (0x7A3780 on the preload manager 0x20B29A8) is the one
// support_soldier.cpp and support_spawn.cpp call for the stock templates and hulls, checked by the same bytes.
#include "support_variants.h"
#include "support_variants_logic.h"
#include "room_sync.h"
#include "support_config.h"
#include "support_loadout.h"
#include "support_protocol.h"
#include "crew.h"
#include "memory.h"
#include <string>
#include <cwchar>
#include <cwctype>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace crew {
namespace {
constexpr unsigned kPreload=0x7A3780;
constexpr std::size_t kPreloadMgr=0x20B29A8;
const unsigned char kPreloadSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48};
static_assert(kConfiguredVariantsMost==kSupportConfigUnits*(kSupportLoadoutMost*2+2)+2,
              "ForEachConfiguredVariant's bound: two looks per seat, a crew and a loadout per entry, the mission crews' two");
variants::MissionSet wanted;     // what this mission's plans can ask for (MissionVariants)
variants::MissionSet ready;      // ...of those, preloaded here
variants::HostSet hostList;      // the room's host list this mission came from (empty: none)
unsigned char bloom[kVariantBloomBytes]{};
wchar_t noted[32][96]{};
int notedCount=0;

// <the game's directory>\Mods\<sub> (the exe's: Mods sits beside EDF6.exe), into `out`.
bool ModsPath(const wchar_t* sub,wchar_t* out,std::size_t capacity) noexcept {
    const DWORD n=GetModuleFileNameW(nullptr,out,static_cast<DWORD>(capacity));
    wchar_t* slash=n && n<capacity ? wcsrchr(out,L'\\') : nullptr;
    if(!slash)return false;
    *slash=0;
    return wcscat_s(out,capacity,L"\\Mods\\")==0 && wcscat_s(out,capacity,sub)==0;
}
// One file (a variant name MissionVariants gave) preloaded when it is in Mods/OBJECT; true when it was.
bool PreloadOne(const wchar_t* file,void* mgr) noexcept {
    wchar_t path[MAX_PATH],sub[128];
    if(swprintf_s(sub,L"OBJECT\\%ls",file)<0 || !ModsPath(sub,path,std::size(path)))return false;
    const DWORD a=GetFileAttributesW(path);
    if(a==INVALID_FILE_ATTRIBUTES || (a&FILE_ATTRIBUTE_DIRECTORY))return false;
    wchar_t app[128];
    if(swprintf_s(app,L"app:/object/%ls",file)<0)return false;
    for(wchar_t* c=app;*c;++c)*c=static_cast<wchar_t>(std::towlower(*c));
    __try {
        reinterpret_cast<void(*)(void*,const wchar_t*,std::int32_t,std::int32_t)>(image+kPreload)(mgr,app,2,-1);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("SUPPORT variant %ls: preload fault, left unloaded",file);
        return false;
    }
}
// The game's preload manager (null: none yet).
void* PreloadManager() noexcept {
    void* mgr=nullptr;
    __try { mgr=At<void*>(image,kPreloadMgr); } __except(EXCEPTION_EXECUTE_HANDLER){mgr=nullptr;}
    return mgr;
}
// The pending list's bytes (empty when there is none).
std::string PendingText() noexcept {
    std::string text;
    wchar_t path[MAX_PATH];
    if(!ModsPath(L"Plugins\\EDF6VehicleCrew.variants_pending.txt",path,std::size(path)))return text;
    FILE* in=nullptr;
    if(_wfopen_s(&in,path,L"rb") || !in)return text;
    try {
        char buf[4096];
        for(std::size_t n;(n=std::fread(buf,1,sizeof(buf),in))>0 && text.size()<(1u<<20);)text.append(buf,n);   // 1 MiB: its tail
    } catch(...) { text.clear(); }
    std::fclose(in);
    return text;
}
}  // namespace

void PreloadSupportVariants() noexcept {
    ready.count=0;wanted.count=0;hostList.count=0;std::memset(bloom,0,sizeof(bloom));notedCount=0;
    if(!image || !Matches(kPreload,kPreloadSig,sizeof(kPreloadSig))){Log("SUPPORT variants: preload profile mismatch, none loaded");return;}
    void* const mgr=PreloadManager();
    if(!mgr)return;
    roomsync::ForEachRoomHostVariant([](void*,const wchar_t* file) noexcept { hostList.Add(file); },nullptr);
    const std::string pending=PendingText();
    const variants::MissionParts parts=variants::MissionVariants(&wanted,
        [](auto&& put){ForEachConfiguredVariant([](void* c,const wchar_t* f){(*static_cast<decltype(&put)>(c))(f);},&put);},
        [](auto&& put){for(int i=0;i<hostList.count;++i)put(hostList.name[i]);},
        pending.data(),pending.size());
    for(int i=0;i<wanted.count;++i)
        if(PreloadOne(wanted.name[i],mgr))ready.Add(wanted.name[i]);
    variants::HelloBloom(ready,&hostList,bloom);
    Log("SUPPORT variants: %d of %d wanted files preloaded (configured %d, the room host's list %d, pending %d; %d over a bound)%s",
        ready.count,wanted.count,parts.configured,parts.host,parts.pending,parts.dropped,
        hostList.count ? "; the hello names the host's list's alone" : "");
}
bool SupportVariantReady(const wchar_t* file) noexcept {
    return file && ready.Has(file);
}
void SupportVariantHello(std::uint32_t* ext,unsigned char* bloom32) noexcept {
    if(ext)*ext=support_net::kExtVariants;
    if(bloom32)std::memcpy(bloom32,bloom,sizeof(bloom));
}
void NoteMissingVariant(const wchar_t* file,const char* why) noexcept {
    if(!file || !*file)return;
    for(int i=0;i<notedCount;++i)if(!_wcsicmp(noted[i],file))return;
    if(notedCount<static_cast<int>(std::size(noted)))wcscpy_s(noted[notedCount++],file);
    Log("SUPPORT variant %ls missing here (%s): stock this time; written to EDF6VehicleCrew.variants_pending.txt, "
        "the installer (menu 7 or 1, game closed) makes it",file,why ? why : "");
    wchar_t path[MAX_PATH];
    if(!ModsPath(L"Plugins\\EDF6VehicleCrew.variants_pending.txt",path,std::size(path)))return;
    char line[128]{};
    if(!WideCharToMultiByte(CP_UTF8,0,file,-1,line,sizeof(line)-2,nullptr,nullptr))return;
    // Already listed (an earlier mission's): leave it once.
    if(FILE* in=nullptr;!_wfopen_s(&in,path,L"rb") && in) {
        char have[128];bool listed=false;
        while(!listed && std::fgets(have,sizeof(have),in)) {
            std::size_t n=std::strlen(have);
            while(n && (have[n-1]=='\n' || have[n-1]=='\r'))have[--n]=0;
            listed=!_stricmp(have,line);
        }
        std::fclose(in);
        if(listed)return;
    }
    FILE* out=nullptr;
    if(_wfopen_s(&out,path,L"ab") || !out)return;
    std::fprintf(out,"%s\r\n",line);
    std::fclose(out);
}
namespace {
using EachVariant=void (*)(void* context,const wchar_t* file);
// One coloured soldier's file to `each` (nothing for a stock look); 1 when given.
int GiveLook(SupportWeapon kind,bool leader,const SupportLook& look,EachVariant each,void* context) noexcept {
    wchar_t file[96];
    if(!look.On() || !SupportLookFile(kind,leader,look,file,std::size(file)))return 0;
    each(context,file);
    return 1;
}
// Catalog entry `i`'s crew (SupportCrewVariant: the kind its plan seats, as a member), when it has one.
int GiveCrew(const SupportConfig& cfg,int i,EachVariant each,void* context) noexcept {
    const wchar_t* key=SupportCallKey(i);
    if(!HasCrew(key))return 0;
    const SupportWeapon kind=IsGroundCrewKey(key) ? cfg.vehicleCrew : cfg.aircraftCrew;
    return GiveLook(kind,false,cfg.CrewLook(i),each,context);
}
}  // namespace

int ForEachConfiguredVariant(EachVariant each,void* context) noexcept {
    if(!each)return 0;
    const SupportConfig& cfg=SupportCfg();
    const int units=SupportCallCount()<kSupportConfigUnits ? SupportCallCount() : kSupportConfigUnits;
    int given=0;
    wchar_t file[96];
    for(int i=0;i<units;++i) {
        given+=ForEachPresetVariant(cfg.preset[i],[&](const wchar_t* name) noexcept {each(context,name);});
        given+=GiveCrew(cfg,i,each,context);
        const VehicleLoadout& l=cfg.vehicle[i];
        if(l.On() && l.body==VehicleBodyOfKey(SupportCallKey(i)) && VehicleVariantFile(l,file,std::size(file))){each(context,file);++given;}
    }
    // The mission vehicles' support crews (mission_crew_support.cpp): rifle members and their leader.
    given+=GiveLook(SupportWeapon::rifle,false,cfg.MissionCrewLook(),each,context);
    given+=GiveLook(SupportWeapon::rifle,true,cfg.MissionCrewLook(),each,context);
    return given;
}
}  // namespace crew
