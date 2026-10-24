// The game's file-system budget sized for the content this plugin adds (docs/cri-fs-loaders-re.md).
//
// EDF6 starts CRI File System with fixed pools: 0x73D10 builds a CriFsConfig on the stack (0x73DF2..0x73E8E; the same
// at 0x73F2C..0x73FCD, which nothing calls) of 0x60 each (num_loaders, num_stdio_handles, max_binds, max_files; one
// binder), adds the caller's struct to it (0x703540 -> 0x7AED20: loaders +0x22, binders +0xB, binds +0xB, files +0xB,
// stdio handles +0: 130 loaders, 12 binders, 96 stdio handles, 107 binds, 107 files), then calls criFs_InitializeLibrary
// 0x1092990(config, NULL, 0), which sizes its work from that config.
//
// Which pool scales with the archives the content keeps open (docs/cri-fs-loaders-re.md "哪个池随档案增长"): only the
// loaders. Every file the game opens for reading takes a CriFsLoader for as long as it stays open (sgs::fs open 0x741C0
// -> criFsLoader_Create 0x1099184 at 0x742BA), and a model archive with HD-TEXTURE members stays open for as long as its
// textures may stream (RandomAccessArchive::Open 0x6AF50, the record kept by 0x1133C50). The binds (and binders) are
// taken only where the game mounts its archives (0x74C70: BindCpk / BindDirectory / BindFile ...), a stdio handle only
// for a moment (the open's existence check of an unbound path, 0x74284, closed at 0x74298) or by a file opened for
// writing (0x74345, the saves); a file handle (max_files) by a read in flight, not by an open loader (the 10-18 trace:
// 130 loaders taken, 107 file handles, never a file-handle error before the loader one, nor with 512 loaders and 123
// archives open).
//
// With the test range's own vehicles plus the M map's 「摆放」 catalog preloaded (debug_spawn.cpp PreloadDebugSpawn), the
// 130 loaders ran out on the ~95th archive (traced 2026-10-18: CRI's "E2008070931:Can not allocate loader handle.
// (Increase num_loaders of CriFsConfig.)", then "RandomAccessArchive::Open( error : APP:\OBJECT\E601_MARTIAN.MRAB )");
// the stock TextureResource load (0x1110E80) does not check the failed open (0x1133C50 hands back an empty record) and
// reads it at 0x1134CCB: c0000005, every time the test range was entered.
//
// So criFs_InitializeLibrary's two call sites are redirected: the config the game built gets kCatalogModelArchives more
// loaders (src/spawn_catalog_archives.h: the distinct model archives the whole catalog names, RangePlaceAll=1 included;
// the game's own 130 stay for the game's own content, the range's vehicles among it), then the stock call runs with it
// (the work CRI allocates grows with it). It has to happen before the library starts: the plugins load in CPP_OnBoot,
// before the application's init reaches 0x73D10 (the file-system manager EDF+0x20B2880 is still null then; with it set,
// the library is up already and nothing is changed).
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "memory.h"
#include "edf/host.h"
#include "spawn_catalog_archives.h"

#include <cstdint>
#include <cstring>
#include <iterator>

namespace crew {
namespace {
constexpr std::size_t kInitCalls[]={0x73E8E,0x73FCD};   // call criFs_InitializeLibrary(rcx = the config on the stack, 0, 0)
constexpr std::size_t kInitLibrary=0x1092990;           // criFs_InitializeLibrary(const CriFsConfig*, void* work, CriSint32 size)
constexpr std::size_t kConfigCheck=0x109317C;            // its config check: version (+0x24) -1, the counts >= 0
constexpr std::size_t kFsManager=0x20B2880;              // the game's file-system manager, set at 0x73DB6 just before the call
constexpr std::size_t kNumLoaders=0x08;                  // CriFsConfig::num_loaders
// The loaders the plugin's content adds: one per model archive the spawn catalog's whole preload can hold open.
constexpr std::int32_t kAddedLoaders=debugspawn::kCatalogModelArchives;
static_assert(kAddedLoaders>0 && kAddedLoaders<0x1000,"the catalog's archives (tools/gen_spawn_catalog.py --archives)");
constexpr std::int32_t kMaxLoaders=0x10000;              // a config past this is none the game built: left alone
// 0x73E1A / 0x73F5D: mov dword [rsp+0x28],0x60 -- the config is at rsp+0x20, so this is its num_loaders
const unsigned char kLoadersStoreSig[]={0xC7,0x44,0x24,0x28,0x60,0x00,0x00,0x00};
constexpr std::size_t kLoadersStores[]={0x73E1A,0x73F5D};
// 0x73E84 / 0x73FC3: xor r8d,r8d; ... lea rcx,[rsp+0x20] -- the config, no work buffer (CRI allocates it from the config)
const unsigned char kInitArgsSigA[]={0x45,0x33,0xC0,0x33,0xD2,0x48,0x8D,0x4C,0x24,0x20};   // 0x73E84
const unsigned char kInitArgsSigB[]={0x45,0x33,0xC0,0x48,0x8D,0x4C,0x24,0x20,0x33,0xD2};   // 0x73FC3
const unsigned char kInitLibrarySig[]={0x4C,0x8B,0xDC,0x49,0x89,0x5B,0x08,0x49,0x89,0x6B,0x10,0x49,0x89,0x7B,0x18,0x41};
const unsigned char kConfigCheckSig[]={0x40,0x53,0x48,0x83,0xEC,0x20,0x83,0x79,0x24,0xFF};
// 0x73DB6: mov [rip+0x203EAC3],rbx -- the manager stored, the library initialised after it
const unsigned char kManagerStoreSig[]={0x48,0x89,0x1D,0xC3,0xEA,0x03,0x02};

using InitFn=std::int32_t(__fastcall*)(void*,void*,std::int32_t);
InitFn prior[std::size(kInitCalls)]{};

unsigned char* CallTarget(std::size_t site) noexcept {
    unsigned char* p=image+site;
    if(!Readable(p,5) || p[0]!=0xE8)return nullptr;
    std::int32_t rel;
    std::memcpy(&rel,p+1,4);
    return p+5+rel;
}
// The stock init, or another plugin's redirect of it (outside EDF.dll); never a different call inside it.
bool Chainable(const unsigned char* target) noexcept {
    return target && (target==image+kInitLibrary || target<image || target>=image+edf::kImageSize);
}

template<std::size_t I> std::int32_t __fastcall Init(void* config,void* work,std::int32_t size) {
    if(config && Readable(static_cast<unsigned char*>(config)+kNumLoaders,4,true)) {
        const std::int32_t stock=At<std::int32_t>(config,kNumLoaders);
        if(stock>=0 && stock<=kMaxLoaders) {
            Put<std::int32_t>(config,kNumLoaders,stock+kAddedLoaders);
            Log("FSBUDGET criFs_InitializeLibrary (EDF+%#zx): num_loaders %d -> %d (+%d: the spawn catalog's model archives)",
                kInitCalls[I],stock,stock+kAddedLoaders,kAddedLoaders);
        } else Log("FSBUDGET criFs_InitializeLibrary (EDF+%#zx): num_loaders %d is no count the game builds: kept",kInitCalls[I],stock);
    }
    return prior[I](config,work,size);
}
constexpr InitFn kHooks[]={&Init<0>,&Init<1>};
static_assert(std::size(kHooks)==std::size(kInitCalls),"one hook per call site");
}  // namespace

bool InstallFsLoaderBudget() noexcept {
    if(!Cfg().enabled){Log("FSBUDGET off (Enabled=0): the stock 130 loaders");return false;}
    bool read=false;
    __try {
        read=Matches(kInitLibrary,kInitLibrarySig,sizeof(kInitLibrarySig)) && Matches(kConfigCheck,kConfigCheckSig,sizeof(kConfigCheckSig)) &&
             Matches(0x73DB6,kManagerStoreSig,sizeof(kManagerStoreSig)) &&
             Matches(0x73E84,kInitArgsSigA,sizeof(kInitArgsSigA)) && Matches(0x73FC3,kInitArgsSigB,sizeof(kInitArgsSigB));
        for(const std::size_t at:kLoadersStores)read=read && Matches(at,kLoadersStoreSig,sizeof(kLoadersStoreSig));
    } __except(EXCEPTION_EXECUTE_HANDLER) {read=false;}
    if(!read){Log("FSBUDGET not as read: the stock 130 loaders (the test range's place panel may run them out)");return false;}
    if(At<void*>(image,kFsManager)){Log("FSBUDGET the file system is up already: the stock loaders kept");return false;}
    unsigned char* targets[std::size(kInitCalls)]{};
    for(std::size_t i=0;i<std::size(kInitCalls);++i) {
        targets[i]=CallTarget(kInitCalls[i]);
        if(!Chainable(targets[i])){Log("FSBUDGET EDF+%#zx is no call of criFs_InitializeLibrary: not hooked",kInitCalls[i]);return false;}
    }
    int done=0;
    for(std::size_t i=0;i<std::size(kInitCalls);++i) {
        prior[i]=reinterpret_cast<InitFn>(targets[i]);
        if(targets[i]!=image+kInitLibrary)Log("FSBUDGET call site %#zx already redirected (another plugin): chained",kInitCalls[i]);
        bool changed=false;
        if(RedirectCall(image+kInitCalls[i],targets[i],reinterpret_cast<void*>(kHooks[i]),changed))++done;
        else Log("FSBUDGET call site %#zx %s",kInitCalls[i],changed ? "half patched" : "not patched");
    }
    Log("FSBUDGET hooked %d/%zu init calls (num_loaders +%d)",done,std::size(kInitCalls),kAddedLoaders);
    return done==static_cast<int>(std::size(kInitCalls));
}
}  // namespace crew
