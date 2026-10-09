// support_variants.h. EDF.dll TimeDateStamp 0x678CCB46: the preload (0x7A3780 on the preload manager 0x20B29A8) is the one
// support_soldier.cpp and support_spawn.cpp call for the stock templates and hulls, checked by the same bytes.
#include "support_variants.h"
#include "support_loadout.h"
#include "support_protocol.h"
#include "crew.h"
#include "memory.h"
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
struct Ready { wchar_t file[96]; };
Ready ready[kSupportVariantsMost]{};
int readyCount=0;
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
void Upper(wchar_t* s) noexcept { for(;*s;++s)*s=static_cast<wchar_t>(std::towupper(*s)); }
void Find(const wchar_t* pattern,void* mgr) noexcept {
    wchar_t path[MAX_PATH];
    if(!ModsPath(pattern,path,std::size(path)))return;
    WIN32_FIND_DATAW found;
    const HANDLE h=FindFirstFileW(path,&found);
    if(h==INVALID_HANDLE_VALUE)return;
    do {
        if(found.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)continue;
        if(readyCount>=kSupportVariantsMost) {
            Log("SUPPORT variant %ls: more than %d variant files, left unloaded (stock where a plan wants it)",found.cFileName,
                kSupportVariantsMost);
            continue;
        }
        auto& row=ready[readyCount];
        if(wcscpy_s(row.file,found.cFileName))continue;
        Upper(row.file);
        wchar_t app[128];
        if(swprintf_s(app,L"app:/object/%ls",found.cFileName)<0)continue;
        for(wchar_t* c=app;*c;++c)*c=static_cast<wchar_t>(std::towlower(*c));
        bool ok=false;
        __try {
            reinterpret_cast<void(*)(void*,const wchar_t*,std::int32_t,std::int32_t)>(image+kPreload)(mgr,app,2,-1);
            ok=true;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Log("SUPPORT variant %ls: preload fault, left unloaded",row.file);
        }
        if(!ok)continue;
        BloomAdd(bloom,VariantHash(row.file));
        ++readyCount;
    } while(FindNextFileW(h,&found));
    FindClose(h);
}
}  // namespace

void PreloadSupportVariants() noexcept {
    readyCount=0;std::memset(bloom,0,sizeof(bloom));notedCount=0;
    if(!image || !Matches(kPreload,kPreloadSig,sizeof(kPreloadSig))){Log("SUPPORT variants: preload profile mismatch, none loaded");return;}
    void* mgr=nullptr;
    __try { mgr=At<void*>(image,kPreloadMgr); } __except(EXCEPTION_EXECUTE_HANDLER){mgr=nullptr;}
    if(!mgr)return;
    Find(L"OBJECT\\EDF6VC_NPC_*.SGO",mgr);
    Find(L"OBJECT\\EDF6VC_LO_*.SGO",mgr);
    if(readyCount)Log("SUPPORT variants: %d generated soldier / vehicle files preloaded",readyCount);
}
bool SupportVariantReady(const wchar_t* file) noexcept {
    if(!file)return false;
    for(int i=0;i<readyCount;++i)if(!_wcsicmp(ready[i].file,file))return true;
    return false;
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
}  // namespace crew
