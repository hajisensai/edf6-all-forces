// 对象更新的骨骼 -> 碰撞代理同步多线程（object_update_mt_logic.h、docs/object-update-mt-re.md）。
// 这里只做安装与胶水：指纹核对、窗口与登记两组调用点、11 个闸门 cave、异常 minidump、PerfProbe / Debug 日志。
#include "object_update_mt.h"
#include "object_update_mt_logic.h"
#include "edf_layout.h"
#include "config.h"
#include "plugin_log.h"
#include "crash_dump.h"
#include "edf/memory.h"
#include "edf/patch.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace crew {
namespace {
using namespace objmt;

Engine engine;
bool installed=false;
// 过滤层表（[[image+0x20B2970]+8]+0x24，32 个 uint32）：每次开窗读一份；读不到时任何查询都 flush。
std::uint32_t layers[32]{};
bool layersKnown=false;
std::atomic<bool> dumped{false};
unsigned mismatchLogs=0;
double tickMs=0;

void ReadLayers() noexcept {
    layersKnown=false;
    void* const* const wrapper=reinterpret_cast<void* const*>(image+kFilterWrapper);
    if(!edf::Readable(wrapper,8) || !*wrapper)return;
    auto* const w=static_cast<unsigned char*>(*wrapper);
    if(!edf::Readable(w+8,8))return;
    auto* const filter=*reinterpret_cast<unsigned char* const*>(w+8);
    if(!filter || !edf::Readable(filter+0x24,sizeof(layers)))return;
    std::memcpy(layers,filter+0x24,sizeof(layers));
    layersKnown=true;
}

// <本 DLL 目录>\EDF6VehicleCrew.objmt.dmp
bool DumpPath(wchar_t* out,std::size_t capacity) noexcept {
    HMODULE self=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&DumpPath),&self))return false;
    const DWORD n=GetModuleFileNameW(self,out,static_cast<DWORD>(capacity));
    wchar_t* slash=n && n<capacity ? wcsrchr(out,L'\\') : nullptr;
    return slash && (*++slash=0,wcscat_s(out,capacity,L"EDF6VehicleCrew.objmt.dmp")==0);
}
MiniDumpWriteDumpFn writeDump=nullptr;
wchar_t dumpPath[MAX_PATH]{};

// 工作线程 __except 过滤器里：第一次异常写 minidump（带异常现场），任务交回游戏线程按原版现算，此后整体停用。
void OnFault(EXCEPTION_POINTERS* info,void* container) noexcept {
    Log("OBJMT fault: code 0x%08lX at %p, container %p; handed back to the game thread, ObjectUpdateMT off for this run",
        info->ExceptionRecord->ExceptionCode,info->ExceptionRecord->ExceptionAddress,container);
    if(dumped.exchange(true) || !writeDump || !dumpPath[0])return;
    const HANDLE file=CreateFileW(dumpPath,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return;
    MINIDUMP_EXCEPTION_INFORMATION ex{GetCurrentThreadId(),info,FALSE};
    const BOOL ok=writeDump(GetCurrentProcess(),GetCurrentProcessId(),file,kDumpType,&ex,nullptr,nullptr);
    CloseHandle(file);
    Log("OBJMT fault dump: %ls %s",dumpPath,ok ? "written" : "failed");
}
void OnMismatch(void* container,const char* what,const char* when) noexcept {
    if(mismatchLogs>=32)return;
    ++mismatchLogs;
    Log("OBJMT MISMATCH container %p: %s differs (%s)",container,what,when);
}

// ---- 调用点 ----
void WindowOpen(void* arg) noexcept {
    engine.stock.lock(arg);
    ReadLayers();
    engine.Open();
    // ini 热重载关掉 ObjectUpdateMT（启动时开着才装上）：这一窗全由游戏线程当场做，与原版同（perfrun 一次启动内 A/B）
    if(!Cfg().objectUpdateMT)engine.open=false;
}
void WindowClose(void* arg) noexcept {
    engine.Close();
    engine.stock.unlock(arg);
}
void RegisterContainer(unsigned char* c) noexcept {engine.Register(c);}

// 闸门（cave 已判过 pending != 0）。别的线程上的查询不 flush（读到的是上一帧的代理，与原版里跨线程查询的时序相同）。
void GateHit(unsigned char* src,unsigned gate) noexcept {
    if(!engine.pending || engine.inFlush || !engine.OnGameThread())return;
    switch(static_cast<Gate>(gate)) {
    case Gate::kRay:
        if(!layersKnown || FilterMayHitProxies(*reinterpret_cast<const std::uint32_t*>(src+0x20),layers))engine.Flush(Reason::kRayQuery);
        break;
    case Gate::kBox:
        if(!layersKnown || FilterMayHitProxies(*reinterpret_cast<const std::uint32_t*>(src+0x10),layers))engine.Flush(Reason::kBoxQuery);
        break;
    case Gate::kBody: engine.Flush(Reason::kBodyQuery);break;
    case Gate::kPose: engine.Flush(Reason::kPoseMode);break;
    case Gate::kSetPosition: engine.Flush(Reason::kSetPosition);break;
    // 0x118A1C2：bit2（已删）时原版直接跳到尾声 0x118A3C3，什么都不碰，不必等工作线程
    case Gate::kDelete: if(!(src[0x18]&4))engine.Flush(Reason::kDelete);break;
    case Gate::kContainer: if(engine.IsPending(src))engine.Flush(Reason::kRecall);break;
    }
}

bool Verify() noexcept {
    for(const auto& r:kCodeRanges) {
        const auto* const code=image+r.begin;
        if(!edf::Readable(code,r.end-r.begin) || Fnv1a64(code,r.end-r.begin)!=r.fnv) {
            Log("OBJMT refused: code at EDF+0x%X..0x%X is not the stock code",r.begin,r.end);return false;
        }
    }
    struct Site { unsigned rva; const unsigned char* bytes; std::size_t n; };
    const Site calls[]{{kWindowOpen,kCallLock,5},{kWindowClose,kCallUnlock,5},{kRegisterInsect,kCallInsect,5},{kRegisterHuman,kCallHuman,5}};
    for(const auto& s:calls)
        if(!edf::Readable(image+s.rva,s.n) || std::memcmp(image+s.rva,s.bytes,s.n)) {
            Log("OBJMT refused: call at EDF+0x%X is not the stock one (another module changed it?)",s.rva);return false;
        }
    for(const auto& g:kGateSites)
        if(!edf::Readable(image+g.site,g.size) || std::memcmp(image+g.site,g.bytes,g.size)) {
            Log("OBJMT refused: gate site EDF+0x%X is not the stock code",g.site);return false;
        }
    return true;
}
}  // namespace

bool InstallObjectUpdateMultithread() noexcept {
    if(!Cfg().objectUpdateMT){Log("OBJMT off (ini ObjectUpdateMT=0)");return false;}
    if(!Verify())return false;
    const DWORD cores=GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    const unsigned workers=cores>1 ? (cores-1<Engine::kMaxThreads ? cores-1 : Engine::kMaxThreads) : 0;
    if(!workers){Log("OBJMT off: one processor");return false;}
    engine.onFault=&OnFault;
    if(Cfg().debug){engine.shadowEvery=64;engine.onMismatch=&OnMismatch;}
    writeDump=MiniDumpWriter();
    if(!DumpPath(dumpPath,std::size(dumpPath)))dumpPath[0]=0;
    if(!engine.Init(image,workers) || engine.threadCount==0){Log("OBJMT refused: worker threads not created");return false;}
    LARGE_INTEGER f;QueryPerformanceFrequency(&f);tickMs=1000.0/static_cast<double>(f.QuadPart);
    // 闸门先装（pending 为 0 时只多两次压栈和一次比较），再装窗口，最后装登记：任何一步失败时已装的都无害。
    unsigned gates=0;
    for(const auto& g:kGateSites) {
        unsigned char cave[kCaveMax];
        const std::size_t n=BuildGateCave(cave,sizeof(cave),&engine.pending,g.src,static_cast<std::uint32_t>(g.gate),
                                          reinterpret_cast<const void*>(&GateHit),g.bytes,g.size,image+g.site+g.size);
        if(!n || !edf::RedirectToCode(image+g.site,g.bytes,g.size,cave,n,edf::Branch::kJump)) {
            Log("OBJMT refused: gate EDF+0x%X not installed (%u of %u in, harmless while nothing registers)",g.site,gates,kGateCount);
            return false;
        }
        ++gates;
    }
    bool changed=false;
    if(!edf::RedirectCall(image+kWindowOpen,image+kProxyLock,reinterpret_cast<void*>(&WindowOpen),changed) ||
       !edf::RedirectCall(image+kWindowClose,image+kProxyUnlock,reinterpret_cast<void*>(&WindowClose),changed)) {
        Log("OBJMT refused: update window calls not redirected");return false;
    }
    const bool insect=edf::RedirectCall(image+kRegisterInsect,image+kBonesToProxies,reinterpret_cast<void*>(&RegisterContainer),changed);
    const bool human=edf::RedirectCall(image+kRegisterHuman,image+kBonesToProxies,reinterpret_cast<void*>(&RegisterContainer),changed);
    if(!insect || !human)Log("OBJMT: register call %s not redirected (stays synchronous)",!insect ? "InsectBase" : "HumanBase");
    installed=insect || human;
    Log("OBJMT on: %u worker threads, %u gates, InsectBase %s, HumanBase %s%s",engine.threadCount,gates,insect ? "async" : "sync",
        human ? "async" : "sync",engine.shadowEvery ? ", shadow compare every 64th" : "");
    return installed;
}

void FlushObjectUpdates() noexcept {
    if(installed && engine.pending && engine.OnGameThread())engine.Flush(Reason::kPlugin);
}

void ReportObjectUpdateMT(unsigned frames) noexcept {
    if(!installed)return;
    Stats& s=engine.stats;
    if(Cfg().perfProbe && s.registered+s.inlineCalls) {
        char reasons[512];
        int at=0;
        for(unsigned i=0;i<static_cast<unsigned>(Reason::kCount);++i)
            if(s.flushes[i])at+=std::snprintf(reasons+at,sizeof(reasons)-static_cast<std::size_t>(at),"%s%s %u",at ? ", " : "",kReasonNames[i],s.flushes[i]);
        if(!at)std::snprintf(reasons,sizeof(reasons),"none");
        const double n=frames ? frames : 1.0;
        Log("PERF OBJMT %u frames: %u registered (%.1f a frame), %u inline, %u committed, %u handed back (%u faults, %u overflows), "
            "%u run by the game thread; flushes %s; compute %.3f ms (parallel, %.3f a frame), commit %.3f ms (serial, %.3f a frame), "
            "wait %.3f ms; shadow %u checked %u mismatched",frames,s.registered,s.registered/n,s.inlineCalls,s.committed,s.handedBack,
            s.faults,s.overflows,s.ranByGame,reasons,static_cast<double>(s.computeTicks.load())*tickMs,
            static_cast<double>(s.computeTicks.load())*tickMs/n,static_cast<double>(s.commitTicks)*tickMs,
            static_cast<double>(s.commitTicks)*tickMs/n,static_cast<double>(s.waitTicks)*tickMs,s.shadowed,s.mismatches);
    }
    s.Reset();
}
}  // namespace crew
