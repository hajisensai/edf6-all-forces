// Actual production sound hooks, private native endpoints, no game/files/audio.
// Native cache references are {cacheNode*, cacheGlobal*}; Bank is inline at +48.
#include "../src/crew.h"
#include "../src/memory.h"
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <string>
#include <vector>

namespace fixture {
DWORD WINAPI ModulePath(HMODULE,wchar_t*,DWORD);
DWORD WINAPI Attributes(const wchar_t*);
}
#define GetModuleFileNameW fixture::ModulePath
#define GetFileAttributesW fixture::Attributes
#include "../src/edf41sound.cpp"
#undef GetModuleFileNameW
#undef GetFileAttributesW

static_assert(sizeof(crew::BankRef)==16 && offsetof(crew::BankRef,node)==0 && offsetof(crew::BankRef,cache)==8,"native cache reference layout");
static_assert(sizeof(crew::CueRef)==16 && offsetof(crew::CueRef,bank)==0 && offsetof(crew::CueRef,index)==8,"native cue reference layout");
static_assert(sizeof(crew::GameString)==32 && offsetof(crew::GameString,size)==16 && offsetof(crew::GameString,capacity)==24,"native wide string layout");

namespace fixture {
crew::Config config{};
alignas(16) unsigned char cacheNode[0xA0]{},cacheGlobal[0x20]{},soundSystem[0xB0]{},otherSystem[0xB0]{},stockBank[0x80]{};
void (*shutdownCallback)() noexcept=nullptr;
int checks=0,loads=0,releases=0,stockLoads=0,directFinds=0,redirects=0;
int badImportedReply=0;
bool fileThere=true,loadOk=true,nativeSource=false,importSource=true,fallbackSource=true;
struct Lookup { void* system;int type;std::wstring name; };
std::vector<Lookup> lookups;
struct Redirect { unsigned char* site;void* target;void* hook; };
std::vector<Redirect> patched;
const wchar_t* const source=L"weapon_Ranger_RL_plasmaLauncherA3"; // dump 65672's exact incoming UTF-16 cue

void Check(bool yes,const char* why) {
    ++checks;
    if(!yes){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}
}
void Jump(std::size_t rva,void* to) {
    const unsigned char code[]={0xFF,0x25,0,0,0,0};
    std::memcpy(crew::image+rva,code,sizeof code);
    std::memcpy(crew::image+rva+sizeof code,&to,sizeof to);
}
DWORD WINAPI ModulePath(HMODULE,wchar_t* path,DWORD capacity) {
    const wchar_t text[]=L"C:\\offline-fixture\\EDF6.exe";
    Check(capacity>=sizeof(text)/sizeof(*text),"file query has room for the executable path");
    std::memcpy(path,text,sizeof text);
    return static_cast<DWORD>(std::wcslen(text));
}
DWORD WINAPI Attributes(const wchar_t* path) {
    Check(std::wcscmp(path,L"C:\\offline-fixture\\Mods\\SOUND\\PC\\EDF41_WEAPON_SE.ACB")==0,
          "production checks its optional Mods bank, without opening a real file");
    return fileThere ? FILE_ATTRIBUTE_NORMAL : INVALID_FILE_ATTRIBUTES;
}
bool __fastcall Load(void* result,const void* gamePath,int type,int flag,bool extra) {
    ++loads;
    const auto* path=static_cast<const crew::GameString*>(gamePath);
    Check(path->capacity>=8 && path->size==std::wcslen(path->data) &&
          std::wcscmp(path->data,L"app:/sound/pc/edf41_weapon_se.acb")==0,
          "loader gets the native wide GameString through the Mods-capable path");
    Check(type==0x41 && flag==1 && !extra,"imported cache bank keeps its isolated native type and load flags");
    auto** words=static_cast<void**>(result);
    words[0]=loadOk ? cacheNode : nullptr;
    words[1]=loadOk ? cacheGlobal : nullptr;
    return loadOk;
}
void __fastcall Release(void* reference) {
    ++releases;
    auto** words=static_cast<void**>(reference);
    Check(words[0]==cacheNode && words[1]==cacheGlobal,"release receives the entire original node/global cache reference");
    words[0]=words[1]=nullptr;
}
std::uint64_t __fastcall StockLoad(void*,const void*,std::uint64_t a,std::uint64_t b,std::uint64_t c,std::uint64_t d) {
    ++stockLoads;
    Check(a==11 && b==12 && c==13 && d==14,"original stock-bank loader receives every forwarded argument");
    return 0xBEEF;
}
bool __fastcall LookupCue(void* owner,void* result,int type,const wchar_t* name) {
    Check(name!=nullptr,"native lookup receives an actual wide cue");
    lookups.push_back({owner,type,name});
    auto* out=static_cast<crew::CueRef*>(result);
    const auto* fallback=crew::edf41sound::FallbackName(source);
    if(type==0x41) {
        Check(owner==soundSystem,"imported native lookup stays in the system that owns its cache reference");
        Check(std::wcscmp(name,source)==0,"imported path preserves the full UTF-16 name instead of reinterpreting bytes");
        if(!importSource)return false;
        out->bank=badImportedReply==1 ? stockBank : cacheNode+0x48;
        out->index=badImportedReply==2 ? -1 : 0x456;
        return true;
    }
    if(type==1 || (type==0 && ((nativeSource && std::wcscmp(name,source)==0) ||
                             (fallbackSource && fallback && std::wcscmp(name,fallback)==0)))) {
        out->bank=stockBank;out->index=type==1 ? 0x123 : 0x234;
        return true;
    }
    return false;
}
int __fastcall ForbiddenDirectFind(void*,const char*) {
    ++directFinds;
    Check(false,"production must not call Bank::Find with a cache node or UTF-16 string");
    return -1;
}
void ResetLookups() { lookups.clear();directFinds=0; }
void Reset() {
    crew::ReleaseWeaponBank();
    loads=releases=stockLoads=badImportedReply=0;fileThere=loadOk=importSource=fallbackSource=true;nativeSource=false;
    ResetLookups();
    *reinterpret_cast<void**>(crew::image+crew::kSystemGlobal)=soundSystem;
}
}
namespace crew {
unsigned char* image=nullptr;
const Config& Cfg() noexcept { return fixture::config; }
void Log(const char*,...) noexcept {}
namespace edf41c {
void AtGameShutdown(void(*callback)() noexcept) noexcept { fixture::shutdownCallback=callback; }
}
}
namespace edf {
bool Readable(const void* pointer,std::size_t,bool) noexcept { return pointer!=nullptr; }
bool RedirectCall(unsigned char* site,void* target,void* hook,bool& changed) noexcept {
    ++fixture::redirects;fixture::patched.push_back({site,target,hook});changed=true;return true;
}
}
int main() {
    using namespace fixture;
    crew::image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2100000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Check(crew::image!=nullptr,"allocate only this test's private native image");
    Jump(crew::kBankLoad,reinterpret_cast<void*>(&Load));Jump(crew::kLookup,reinterpret_cast<void*>(&LookupCue));
    Jump(0x7A9F80,reinterpret_cast<void*>(&ForbiddenDirectFind));Jump(crew::kSeBankLoad,reinterpret_cast<void*>(&StockLoad));
    // Keep the actual release fingerprint and unwind its two prologue effects
    // before tail-jumping to the recording endpoint. No source seam bypasses it.
    const unsigned char releasePrefix[]={0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0x51,0x08,0x48,0x83,0xC4,0x20,0x5B};
    std::memcpy(crew::image+crew::kBankRelease,releasePrefix,sizeof releasePrefix);
    Jump(crew::kBankRelease+sizeof releasePrefix,reinterpret_cast<void*>(&Release));
    constexpr std::size_t nativeLookupSites[]={0x7B2984,0x7B3A1C,0x7B3B28,0x7B3C55,0x7B3CB1,0x7B3D0A,0x7B3E83,0x7B4073,0x7B46B5};
    Check(sizeof(nativeLookupSites)==sizeof(crew::kLookupSites) && std::memcmp(nativeLookupSites,crew::kLookupSites,sizeof(nativeLookupSites))==0,
          "all nine reviewed native wide-lookup call sites are installed");
    Reset();
    Check(crew::InstallEdf41Sound() && shutdownCallback && redirects>=14,"production installs its lookup/load/release lifecycle hooks");
    for(const auto site:crew::kLookupSites) {
        int matches=0;
        for(const auto& patch:patched)
            matches+=patch.site==crew::image+site && patch.target==crew::image+crew::kLookup &&
                     patch.hook==reinterpret_cast<void*>(&crew::LookupHook);
        Check(matches==1,"every native SE lookup caller is redirected once to the actual production hook");
    }
    Check(crew::SeBankLoadHook(nullptr,nullptr,11,12,13,14)==0xBEEF && stockLoads==1 && loads==1,
          "production chains the native SE loader and loads its optional cache reference once");
    crew::LoadWeaponBank();Check(loads==1,"same sound system retains its existing counted cache reference");
    crew::CueRef out{};
    Check(crew::LookupHook(soundSystem,&out,0,source),"the exact crashing player cue can resolve through production routing");
    Check(lookups.size()==2 && lookups[0].type==0 && lookups[1].type==0x41 && directFinds==0,
          "native resident lookup precedes the native type-41 imported lookup");
    Check(out.bank==cacheNode+0x48 && out.bank!=cacheNode && out.index==0x456,
          "native lookup's real inline Bank and cue index reach the caller unchanged");
    ResetLookups();nativeSource=true;
    Check(crew::LookupHook(soundSystem,&out,0,source) && lookups.size()==1 && out.bank==stockBank,
          "a resident source cue wins without entering the imported bank");
    ResetLookups();nativeSource=false;importSource=false;
    Check(crew::LookupHook(soundSystem,&out,0,source) && lookups.size()==3 && lookups[2].type==0 &&
          lookups[2].name==crew::edf41sound::FallbackName(source) && out.bank==stockBank,
          "a missing imported cue falls back only through the reviewed resident name");
    importSource=true;
    for(const int malformed:{1,2}) {
        ResetLookups();badImportedReply=malformed;
        Check(crew::LookupHook(soundSystem,&out,0,source) && lookups.size()==3 &&
              lookups[2].type==0 && out.bank==stockBank && out.index==0x234,
              "an unexpected Bank or invalid index from native imported lookup is never published");
    }
    badImportedReply=0;importSource=false;
    ResetLookups();
    Check(crew::LookupHook(otherSystem,&out,0,source) && lookups.size()==2 && lookups[1].type==0,
          "a different sound system cannot use our cache handle but still has native fallbacks");
    ResetLookups();
    Check(crew::LookupHook(soundSystem,&out,1,L"voice cue") && lookups.size()==1 && lookups[0].type==1 && out.index==0x123,
          "non-SE types pass through native lookup without imported routing or substitutions");
    ResetLookups();fallbackSource=false;
    Check(!crew::LookupHook(soundSystem,&out,0,source) && lookups.size()==3,
          "failed source and resident fallback remain a failed lookup");
    ResetLookups();fallbackSource=true;
    shutdownCallback();Check(releases==1,"shutdown returns the reference while the sound system still exists");
    shutdownCallback();Check(releases==1,"the same cache reference is never released twice");
    Check(crew::LookupHook(soundSystem,&out,0,source) && lookups.size()==2 && lookups[1].type==0,
          "shutdown clears imported admission before any later fallback lookup");
    Reset();fileThere=false;crew::LoadWeaponBank();
    Check(loads==0 && crew::LookupHook(soundSystem,&out,0,source) && lookups.size()==2 &&
          lookups[1].type==0 && out.bank==stockBank,
          "a missing optional bank uses finite resident fallback without touching native load");
    Reset();loadOk=false;crew::LoadWeaponBank();
    Check(loads==1 && crew::LookupHook(soundSystem,&out,0,source) && lookups.size()==2 &&
          lookups[1].type==0 && out.bank==stockBank,
          "a failed native cache load cannot publish an imported Bank");
    crew::ReleaseWeaponBank();Check(releases==0,"a failed load owns no cache reference to release");
    VirtualFree(crew::image,0,MEM_RELEASE);crew::image=nullptr;
    std::printf("EDF41 production sound bank: %d checks passed\n",checks);
}
