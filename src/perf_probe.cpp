// The measurement probes (perf_probe.h; docs/plan-2026-11-24-optimization.md section 3 A).
#include "perf_probe.h"

#include <dxgi.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <iterator>

#include "crew.h"
#include "memory.h"
#include "net_counters.h"

namespace crew::perfprobe {
namespace {
// The call sites (E8 rel32) and the stock function each calls (EDF.dll, checked by disassembly 2026-11-24).
// The main loop 0x11A2E80: tick wait (0x11A2F43..0x11A2F74), the frame's input sample, Application::Update (vtable +8 at
// 0x11A3006), the render fence wait, the Present command.
constexpr unsigned kInputCall=0x11A2FF3,kInput=0x1182A10;
constexpr unsigned kFenceCall=0x11A305A,kFence=0x111CC20;
constexpr unsigned kPresentCall=0x11A309B,kPresentCmd=0x111D890;
// Application::Update 0x705530 -> System::update (rcx system, xmm1 dt, r8d steps); in it the frame's render recording
// (before the steps), and in that Umbra's resolveVisibility (an import jump, 0x11B6B1D).
constexpr unsigned kUpdateCall=0x705715,kUpdate=0x1198DA0;
constexpr unsigned kDrawCall=0x1198FA4,kDraw=0x11978D0;
constexpr unsigned kResolveCall=0x11B6B1D,kResolve=0x12D6110;
// The draw submits (system, object, pass in r8d) of the scene's lists, and the mesh draws.
constexpr unsigned kSubmitCalls[]={0x11E255C,0x11E25F9,0x11E3A3F,0x11E4029,0x11E40BC};
constexpr unsigned kSubmit=0x11E1630;
constexpr unsigned kMeshCalls[]={0x1103FD1,0x6C30CB};
constexpr unsigned kMesh=0x10FF4E0;
// The renderer (lea 0x2136FF0, the Present command's rcx) and its swap chain: the render thread's Present command
// 0x1118AF0 calls [[renderer+0x90]]+0x40 (IDXGISwapChain::Present, slot 8) at 0x1118B45.
constexpr std::size_t kRenderer=0x2136FF0,kSwapChain=0x90;
constexpr unsigned kPresentSlot=8;

constexpr double kLateMs=20.0,kSlowMs=33.4;
constexpr unsigned kSlowLinesMax=100;   // a period's SLOW FRAME lines at most (a 30 fps machine: every frame is slow)
constexpr unsigned kPasses=6;           // submits by pass 0..4, then every other pass

using InputFn=void(__fastcall*)(void*);
using UpdateFn=void(__fastcall*)(void*,float,std::uint32_t);
using DrawFn=void(__fastcall*)(void*,std::uint8_t,std::uint8_t);
using PassFn=void*(__fastcall*)(void*,void*,void*,void*);   // all four registers forwarded, rax back
using SubmitFn=void(__fastcall*)(void*,void*,int);
using MeshFn=void(__fastcall*)(void*,void*,int,std::uint8_t,void*);
using PresentFn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT);

InputFn nextInput=nullptr;
UpdateFn nextUpdate=nullptr;
DrawFn nextDraw=nullptr;
PassFn nextResolve=nullptr,nextFence=nullptr,nextPresentCmd=nullptr;
SubmitFn nextSubmit[std::size(kSubmitCalls)]={};
MeshFn nextMesh[std::size(kMeshCalls)]={};
PresentFn nextPresent=nullptr;
bool split=false;          // PerfProbe: the frame split hooks are in
bool presentTried=false;
std::atomic<DWORD> gameThread{0};   // the main loop's thread (the frame anchor's), 0 before the first frame

double tickMs=0.0;   // ms a QPC tick
LONGLONG Qpc() noexcept { LARGE_INTEGER t;QueryPerformanceCounter(&t);return t.QuadPart; }

// Points the call at `rva` to `hook` over whatever it calls now: the stock function, or another hook's near thunk
// (common/memory.cpp AllocateNearThunk: mov rax,imm64; jmp rax) that ends in it. `*next` gets that target first.
bool Chain(unsigned rva,unsigned stock,void* hook,void** next) noexcept {
    __try {
        unsigned char* const site=image+rva;
        if(!Readable(site,5) || site[0]!=0xE8)return false;
        std::int32_t rel=0;std::memcpy(&rel,site+1,sizeof(rel));
        unsigned char* const now=site+5+rel;
        const bool thunk=now!=image+stock && Readable(now,12) && now[0]==0x48 && now[1]==0xB8 && now[10]==0xFF &&
                         now[11]==0xE0;
        if(now!=image+stock && !thunk)return false;
        *next=now;
        bool changed=false;
        // a failed write after the site changed keeps *next: the site calls the hook
        if(RedirectCall(site,now,hook,changed) || changed)return changed;
        *next=nullptr;
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

// --- The frame (game thread only) ---
struct Frame { LONGLONG begin,inputEnd,update,draw,resolve,fenceBegin,fence,present; const void* scene; std::uint32_t step; bool open; };
Frame cur{};
struct Base { unsigned frames,steps,over20,over33,slowLines,slowDropped; double sum,worst; } base{};
struct Split { unsigned frames,noStep; double input,app,draw,resolve,steps,stepCount,fence,present,tail,without; } parts{};
// the input samples' QPC of the last three frames (the Present pairs with the one its scene was stepped with)
LONGLONG inputs[3]={};

// The render thread's Present pairs with the frame whose Present command was queued: a single-producer ring.
struct Queued { LONGLONG own,stepped; };
constexpr unsigned kRing=64;
Queued ring[kRing];
std::atomic<unsigned> ringHead{0},ringTail{0};

// What the render thread and the draw submits count (whichever thread they run on): relaxed atomics, taken each period.
std::atomic<unsigned> presents{0},unpaired{0};
std::atomic<long long> presentTicks{0},presentWorst{0},gapTicks{0},gapWorst{0},ownTicks{0},steppedTicks{0},steppedWorst{0};
std::atomic<unsigned> paired{0};
LONGLONG lastPresent=0;   // render thread only
std::atomic<unsigned> submits[kPasses]={},meshes{0};

void Max(std::atomic<long long>& m,long long v) noexcept {
    long long was=m.load(std::memory_order_relaxed);
    while(v>was && !m.compare_exchange_weak(was,v,std::memory_order_relaxed)){}
}

void CloseFrame(LONGLONG now) noexcept {
    const double ms=static_cast<double>(now-cur.begin)*tickMs;
    const SceneStep s=SceneStepNow();
    const std::uint32_t steps=s.scene && s.scene==cur.scene ? s.count-cur.step : 0u;
    if(ms<1000.0) {
        ++base.frames;base.steps+=steps;base.sum+=ms;base.over20+=ms>kLateMs;base.over33+=ms>kSlowMs;
        if(ms>base.worst)base.worst=ms;
    }
    const bool whole=split && cur.fenceBegin && cur.update;
    double app=0.0,stepMs=0.0,tail=0.0,fence=0.0,draw=0.0;
    if(whole) {
        draw=static_cast<double>(cur.draw)*tickMs;
        stepMs=static_cast<double>(cur.update-cur.draw)*tickMs;
        app=static_cast<double>(cur.fenceBegin-cur.inputEnd-cur.update)*tickMs;
        fence=static_cast<double>(cur.fence)*tickMs;
        const double input=static_cast<double>(cur.inputEnd-cur.begin)*tickMs,present=static_cast<double>(cur.present)*tickMs;
        tail=static_cast<double>(now-cur.fenceBegin-cur.fence-cur.present)*tickMs;
        if(ms<1000.0) {
            ++parts.frames;parts.noStep+=steps==0;parts.input+=input;parts.app+=app;parts.draw+=draw;
            parts.resolve+=static_cast<double>(cur.resolve)*tickMs;parts.steps+=stepMs;parts.stepCount+=steps;
            parts.fence+=fence;parts.present+=present;parts.tail+=tail;parts.without+=input+app+draw+fence+present;
        }
    }
    if(ms>kSlowMs && Cfg().debug) {
        if(base.slowLines>=kSlowLinesMax)++base.slowDropped;
        else if(whole) {
            ++base.slowLines;
            Log("SLOW FRAME %lld %lld %.1f ms, %u steps: app %.1f, draw %.1f, steps %.1f, fence %.1f, tail %.1f",cur.begin,now,
                ms,steps,app,draw,stepMs,fence,tail);
        } else {
            ++base.slowLines;
            Log("SLOW FRAME %lld %lld %.1f ms, %u steps",cur.begin,now,ms,steps);
        }
    }
}

void TryPresentHook() noexcept;

void __fastcall InputHook(void* input) {
    const LONGLONG now=Qpc();
    gameThread.store(GetCurrentThreadId(),std::memory_order_relaxed);
    if(cur.open)CloseFrame(now);
    const SceneStep s=SceneStepNow();
    cur=Frame{};cur.begin=now;cur.scene=s.scene;cur.step=s.count;cur.open=true;
    inputs[2]=inputs[1];inputs[1]=inputs[0];inputs[0]=now;
    nextInput(input);
    cur.inputEnd=Qpc();
    if(split && !presentTried)TryPresentHook();
}
void __fastcall UpdateHook(void* system,float dt,std::uint32_t steps) {
    const LONGLONG t=Qpc();
    nextUpdate(system,dt,steps);
    cur.update+=Qpc()-t;
}
void __fastcall DrawHook(void* system,std::uint8_t a,std::uint8_t b) {
    const LONGLONG t=Qpc();
    nextDraw(system,a,b);
    cur.draw+=Qpc()-t;
}
void* __fastcall ResolveHook(void* a,void* b,void* c,void* d) {
    const LONGLONG t=Qpc();
    void* const r=nextResolve(a,b,c,d);
    cur.resolve+=Qpc()-t;
    return r;
}
void* __fastcall FenceHook(void* a,void* b,void* c,void* d) {
    const LONGLONG t=Qpc();
    void* const r=nextFence(a,b,c,d);
    if(!cur.fenceBegin)cur.fenceBegin=t;
    cur.fence+=Qpc()-t;
    return r;
}
void* __fastcall PresentCmdHook(void* a,void* b,void* c,void* d) {
    const LONGLONG t=Qpc();
    // queued before the command: the render thread may run it at once
    const unsigned head=ringHead.load(std::memory_order_relaxed);
    if(head-ringTail.load(std::memory_order_acquire)<kRing) {
        ring[head%kRing]=Queued{inputs[0],inputs[2]};
        ringHead.store(head+1,std::memory_order_release);
    }
    void* const r=nextPresentCmd(a,b,c,d);
    cur.present+=Qpc()-t;
    return r;
}
void __fastcall SubmitHook(unsigned i,void* system,void* object,int pass) {
    submits[pass>=0 && pass<static_cast<int>(kPasses)-1 ? pass : kPasses-1].fetch_add(1,std::memory_order_relaxed);
    nextSubmit[i](system,object,pass);
}
template<unsigned I> void __fastcall SubmitAt(void* system,void* object,int pass) { SubmitHook(I,system,object,pass); }
template<unsigned I> void __fastcall MeshAt(void* mesh,void* ctx,int pass,std::uint8_t flag,void* cache) {
    meshes.fetch_add(1,std::memory_order_relaxed);
    nextMesh[I](mesh,ctx,pass,flag,cache);
}

// --- The render thread's Present ---
HRESULT STDMETHODCALLTYPE PresentHook(IDXGISwapChain* chain,UINT sync,UINT flags) {
    const LONGLONG t=Qpc();
    const HRESULT hr=nextPresent(chain,sync,flags);
    const LONGLONG done=Qpc();
    presents.fetch_add(1,std::memory_order_relaxed);
    presentTicks.fetch_add(done-t,std::memory_order_relaxed);Max(presentWorst,done-t);
    if(lastPresent){gapTicks.fetch_add(done-lastPresent,std::memory_order_relaxed);Max(gapWorst,done-lastPresent);}
    lastPresent=done;
    const unsigned tail=ringTail.load(std::memory_order_relaxed);
    if(tail!=ringHead.load(std::memory_order_acquire)) {
        const Queued q=ring[tail%kRing];
        ringTail.store(tail+1,std::memory_order_release);
        if(q.own && q.stepped) {
            paired.fetch_add(1,std::memory_order_relaxed);
            ownTicks.fetch_add(done-q.own,std::memory_order_relaxed);
            steppedTicks.fetch_add(done-q.stepped,std::memory_order_relaxed);Max(steppedWorst,done-q.stepped);
        }
    } else unpaired.fetch_add(1,std::memory_order_relaxed);
    return hr;
}

// The swap chain as the renderer holds it, checked to be one (its own QueryInterface), or null.
IDXGISwapChain* SwapChain() noexcept {
    __try {
        void* const p=At<void*>(image,kRenderer+kSwapChain);
        if(!p || !Readable(p,8))return nullptr;
        void** const vt=*static_cast<void***>(p);
        if(!Readable(vt,(kPresentSlot+1)*sizeof(void*)))return nullptr;
        void* q=nullptr;
        if(FAILED(static_cast<IUnknown*>(p)->QueryInterface(__uuidof(IDXGISwapChain),&q)) || !q)return nullptr;
        static_cast<IUnknown*>(q)->Release();
        return q==p ? static_cast<IDXGISwapChain*>(p) : nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER){return nullptr;}
}

// Once, with the swap chain made: the adapter and the swap chain as created (gpu-2), and the Present hooked.
void TryPresentHook() noexcept {
    IDXGISwapChain* const chain=SwapChain();
    if(!chain)return;
    presentTried=true;
    DXGI_SWAP_CHAIN_DESC d{};
    if(SUCCEEDED(chain->GetDesc(&d)))
        Log("PERF RENDER swap chain %ux%u %u/%u Hz, %u buffer(s), swap effect %d, flags 0x%X, windowed %d",d.BufferDesc.Width,
            d.BufferDesc.Height,d.BufferDesc.RefreshRate.Numerator,d.BufferDesc.RefreshRate.Denominator,d.BufferCount,
            static_cast<int>(d.SwapEffect),d.Flags,static_cast<int>(d.Windowed));
    IDXGIDevice* device=nullptr;
    if(SUCCEEDED(chain->GetDevice(__uuidof(IDXGIDevice),reinterpret_cast<void**>(&device))) && device) {
        IDXGIAdapter* adapter=nullptr;
        DXGI_ADAPTER_DESC a{};
        if(SUCCEEDED(device->GetAdapter(&adapter)) && adapter && SUCCEEDED(adapter->GetDesc(&a)))
            Log("PERF RENDER adapter %ls, %llu MB video memory",a.Description,
                static_cast<unsigned long long>(a.DedicatedVideoMemory>>20));
        if(adapter)adapter->Release();
        device->Release();
    }
    void** const slot=*reinterpret_cast<void***>(chain)+kPresentSlot;
    // nextPresent is written before the slot: the render thread may present at once
    const bool ok=edf::ChainVtableSlot(slot,reinterpret_cast<void*>(&PresentHook),reinterpret_cast<void**>(&nextPresent));
    Log("HOOK perf present=%d (the render thread's Present, input to Present)",ok);
}

// "<key> <msgs>/<bytes> B" for each row used, zeroed (the keys stay: a channel keeps its row).
int NetRows(netcount::Row* rows,const char* keyFormat,char* out,std::size_t size) noexcept {
    int n=0;
    for(unsigned i=0;i<netcount::kRows;++i) {
        const unsigned long long k=rows[i].key.load(std::memory_order_relaxed);
        const unsigned m=rows[i].msgs.exchange(0,std::memory_order_relaxed);
        const unsigned long long b=rows[i].bytes.exchange(0,std::memory_order_relaxed);
        if(!k || !m || static_cast<std::size_t>(n)>=size)continue;
        char key[24];std::snprintf(key,sizeof(key),keyFormat,static_cast<unsigned>(k-1));
        const int w=std::snprintf(out+n,size-n,"%s%s %u/%llu B",n ? ", " : "",key,m,b);
        if(w>0)n+=w;
    }
    return n;
}
}  // namespace

void Install() noexcept {
    LARGE_INTEGER hz;QueryPerformanceFrequency(&hz);
    tickMs=1000.0/static_cast<double>(hz.QuadPart);
    if(!Cfg().debug)return;
    // each hook's next is the global it calls through, written by Chain before the site is
    const bool anchor=Chain(kInputCall,kInput,reinterpret_cast<void*>(&InputHook),reinterpret_cast<void**>(&nextInput));
    Log("PERF QPC %lld Hz; HOOK perf frame=%d (the main loop's input sample: frame times, steps a frame, SLOW FRAME)",
        hz.QuadPart,anchor);
    if(!anchor || !Cfg().perfProbe)return;
    // The frame split: all or none (a frame's parts must add up).
    const bool whole=Chain(kUpdateCall,kUpdate,reinterpret_cast<void*>(&UpdateHook),reinterpret_cast<void**>(&nextUpdate)) &&
        Chain(kDrawCall,kDraw,reinterpret_cast<void*>(&DrawHook),reinterpret_cast<void**>(&nextDraw)) &&
        Chain(kResolveCall,kResolve,reinterpret_cast<void*>(&ResolveHook),reinterpret_cast<void**>(&nextResolve)) &&
        Chain(kFenceCall,kFence,reinterpret_cast<void*>(&FenceHook),reinterpret_cast<void**>(&nextFence)) &&
        Chain(kPresentCall,kPresentCmd,reinterpret_cast<void*>(&PresentCmdHook),reinterpret_cast<void**>(&nextPresentCmd));
    split=whole;
    // a site chained before one failed stays chained: its hook only times and calls through
    void* const submitHooks[]={reinterpret_cast<void*>(&SubmitAt<0>),reinterpret_cast<void*>(&SubmitAt<1>),
                               reinterpret_cast<void*>(&SubmitAt<2>),reinterpret_cast<void*>(&SubmitAt<3>),
                               reinterpret_cast<void*>(&SubmitAt<4>)};
    static_assert(std::size(submitHooks)==std::size(kSubmitCalls));
    unsigned subs=0,mesh=0;
    for(unsigned i=0;i<std::size(kSubmitCalls);++i) {
        subs+=Chain(kSubmitCalls[i],kSubmit,submitHooks[i],reinterpret_cast<void**>(&nextSubmit[i]));
    }
    void* const meshHooks[]={reinterpret_cast<void*>(&MeshAt<0>),reinterpret_cast<void*>(&MeshAt<1>)};
    static_assert(std::size(meshHooks)==std::size(kMeshCalls));
    for(unsigned i=0;i<std::size(kMeshCalls);++i) {
        mesh+=Chain(kMeshCalls[i],kMesh,meshHooks[i],reinterpret_cast<void**>(&nextMesh[i]));
    }
    Log("HOOK perf split=%d submits=%u/%zu meshes=%u/%zu (PerfProbe=1: the frame's parts, the draw counts)",whole,subs,
        std::size(kSubmitCalls),mesh,std::size(kMeshCalls));
}

bool ChainCall(unsigned rva,unsigned stock,void* hook,void** next) noexcept { return Chain(rva,stock,hook,next); }
DWORD GameThread() noexcept { return gameThread.load(std::memory_order_relaxed); }

Frames TakeFrames() noexcept {
    const Frames f{base.frames,base.steps,base.over20,base.over33,base.sum,base.worst};
    if(base.slowDropped)Log("SLOW FRAME %u more not logged (at most %u lines a period)",base.slowDropped,kSlowLinesMax);
    base=Base{};
    return f;
}

void Report(unsigned frames) noexcept {
    if(Cfg().debug && split && parts.frames) {
        const double n=parts.frames;
        Log("PERF FRAME %u frames split, %u without a step: a frame input %.2f, app %.2f, draw %.2f (umbra resolve %.2f), "
            "steps %.2f (%.2f a step), fence wait %.2f, present command %.3f, tail %.2f ms (the tick wait in it); "
            "without the steps and the tail %.2f ms",parts.frames,parts.noStep,parts.input/n,parts.app/n,parts.draw/n,
            parts.resolve/n,parts.steps/n,parts.stepCount>0 ? parts.steps/parts.stepCount : 0.0,parts.fence/n,
            parts.present/n,parts.tail/n,parts.without/n);
    }
    parts=Split{};
    const unsigned p=presents.exchange(0,std::memory_order_relaxed),u=unpaired.exchange(0,std::memory_order_relaxed);
    const unsigned pr=paired.exchange(0,std::memory_order_relaxed);
    const double pt=static_cast<double>(presentTicks.exchange(0,std::memory_order_relaxed))*tickMs;
    const double pw=static_cast<double>(presentWorst.exchange(0,std::memory_order_relaxed))*tickMs;
    const double gt=static_cast<double>(gapTicks.exchange(0,std::memory_order_relaxed))*tickMs;
    const double gw=static_cast<double>(gapWorst.exchange(0,std::memory_order_relaxed))*tickMs;
    const double ot=static_cast<double>(ownTicks.exchange(0,std::memory_order_relaxed))*tickMs;
    const double st=static_cast<double>(steppedTicks.exchange(0,std::memory_order_relaxed))*tickMs;
    const double sw=static_cast<double>(steppedWorst.exchange(0,std::memory_order_relaxed))*tickMs;
    unsigned sub[kPasses];
    for(unsigned i=0;i<kPasses;++i)sub[i]=submits[i].exchange(0,std::memory_order_relaxed);
    const unsigned me=meshes.exchange(0,std::memory_order_relaxed);
    if(Cfg().debug && p) {
        const double f=frames ? frames : 1.0;
        Log("PERF RENDER %u presents: the call %.2f ms (worst %.2f), %.2f ms apart (worst %.2f); input to present %.1f ms "
            "(the frame's own sample), %.1f ms (worst %.1f; the sample its scene was stepped with, two frames back), %u "
            "unpaired; submits a frame by pass %.0f %.0f %.0f %.0f %.0f, other %.0f; mesh draws a frame %.0f",p,pt/p,pw,
            p>1 ? gt/(p-1) : 0.0,gw,pr ? ot/pr : 0.0,pr ? st/pr : 0.0,sw,u,sub[0]/f,sub[1]/f,sub[2]/f,sub[3]/f,sub[4]/f,
            sub[5]/f,me/f);
    }
    char out[700],in[700];
    const int no=NetRows(netcount::out,"bit 0x%X",out,sizeof(out)),ni=NetRows(netcount::in,"head 0x%08X",in,sizeof(in));
    const unsigned w=netcount::walks.exchange(0,std::memory_order_relaxed);
    const double wt=static_cast<double>(netcount::walkTicks.exchange(0,std::memory_order_relaxed))*tickMs;
    if(Cfg().debug && (no || ni || w))
        Log("PERF NET CHANNELS out: %s; in: %s; world walks %u (%.2f ms, %.2f a frame)",no ? out : "none",ni ? in : "none",w,
            wt,frames ? static_cast<double>(w)/frames : 0.0);
}

}  // namespace crew::perfprobe
