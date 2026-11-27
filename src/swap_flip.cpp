// The swap chain in the flip model (the review's latency-5 / gpu-3; docs/frame-pacing-re.md), with the frame rate's
// gate (ini FrameRateCap other than 0). The Present is counted on the render thread (PresentHook) and the chain's frame
// statistics read there once a second: the frames the display was actually given, not the loop's count.
//
// The game builds its swap chain in 0x111B220 (callers 0x111B77B at the device's init and 0x111C8EC on a mode change:
// a mode change builds a new chain, there is no ResizeBuffers): the description the renderer keeps at +0xA8..+0xEF is
// copied to the stack (rsp+0x30) and handed to IDXGIFactory::CreateSwapChain at 0x111B340 (`mov rax,[rcx]; call
// [rax+50h]`, rcx the factory renderer+0xF8, rdx the device, r8 the description, r9 the chain's out). The game's
// description is the old blt model: SwapEffect DISCARD, one buffer, Flags ALLOW_MODE_SWITCH. In a window or a borderless
// window every frame then goes through DWM's copy (a refresh more of latency), a variable refresh display cannot follow
// the frame, and nothing above the refresh rate reaches the screen.
//
// Here those 6 bytes call the plugin's CreateSwapChain: FLIP_DISCARD with kBuffers buffers, and ALLOW_TEARING when the
// system has it (IDXGIFactory5::CheckFeatureSupport) and the chain is windowed; then the Present (0x1118B36: `mov rax,
// [rcx]; xor r8d,r8d`, the flags) passes DXGI_PRESENT_ALLOW_TEARING on a frame whose sync interval is 0 (frame_rate.cpp
// SyncHook: a cap above the refresh rate, no cap, the catch-up), so such frames are not held to the refresh either.
// The flip model forbids a multisampled back buffer: a description with SampleDesc.Count > 1 (the game's MSAA, 0x111B28D)
// is created as the game made it and logged. A flip chain the system refuses is created as the game made it and logged.
//
// The waitable object (latency-5). With the frame gate on (frame_rate.cpp), the chain is made with
// FRAME_LATENCY_WAITABLE_OBJECT and its queue set to ini MaxFrameLatency (the device's setting no longer applies to such
// a chain); the gate waits on the object once it lets a frame run, before it counts the ticks: the frame starts, and
// samples its input and its ticks, when the GPU can take it, not a queued frame earlier. A chain made again (a mode
// change) has a new object; the old one's handle is not closed (the game thread may be waiting on it), one handle a
// mode change.
#include "swap_flip.h"
#include "edf_layout.h"
#include "plugin_log.h"
#include "memory.h"
#include "edf/patch.h"
#include <atomic>
#include <cstring>
#include <dxgi1_5.h>

namespace crew {
namespace {
constexpr unsigned kCreateCall=0x111B33D,kPresentFlags=0x1118B36;
const unsigned char kCreateSig[]={0x48,0x8B,0x01,0xFF,0x50,0x50};      // mov rax,[rcx]; call [rax+50h]
const unsigned char kPresentSig[]={0x48,0x8B,0x01,0x45,0x33,0xC0};     // mov rax,[rcx]; xor r8d,r8d
constexpr UINT kBuffers=2;

// The Present's flags for a frame with sync interval 0 (read by the leaf stub on the render thread).
volatile UINT tearingFlags=0;
// The waitable object's queue (0: none) and the object of the chain made last.
UINT waitableLatency=0;
std::atomic<HANDLE> waitable{nullptr};
constexpr DWORD kWaitMs=100;   // a lost device or a hidden window does not stall the game thread longer

using CreateFn=HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*,IUnknown*,DXGI_SWAP_CHAIN_DESC*,IDXGISwapChain**);
using PresentFn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT);

// The Present's statistics (render thread writes; the snapshot is read under the lock by the game thread's log line).
SRWLOCK statsLock=SRWLOCK_INIT;
PresentStats published;
std::uint64_t qpcFreq=0,windowStart=0,windowCalls=0;
UINT lastPresentCount=0,lastRefreshCount=0;
LONGLONG lastSyncQpc=0;
unsigned failedCalls=0,occludedCalls=0;

void SampleStatistics(IDXGISwapChain* chain,std::uint64_t now) noexcept {
    DXGI_FRAME_STATISTICS st{};
    const HRESULT hr=chain->GetFrameStatistics(&st);
    PresentStats out;
    const double seconds=static_cast<double>(now-windowStart)/static_cast<double>(qpcFreq);
    out.calls=seconds>0 ? static_cast<double>(windowCalls)/seconds : 0;
    out.failed=failedCalls;
    out.occluded=occludedCalls;
    out.statsHr=hr;
    if(SUCCEEDED(hr)) {
        const LONGLONG dq=st.SyncQPCTime.QuadPart-lastSyncQpc;
        if(lastSyncQpc && dq>0) {
            const double s=static_cast<double>(dq)/static_cast<double>(qpcFreq);
            out.shown=static_cast<double>(st.PresentCount-lastPresentCount)/s;
            out.refreshes=static_cast<double>(st.SyncRefreshCount-lastRefreshCount)/s;
            out.valid=true;
        }
        lastPresentCount=st.PresentCount;
        lastRefreshCount=st.SyncRefreshCount;
        lastSyncQpc=st.SyncQPCTime.QuadPart;
    }
    else lastSyncQpc=0;
    AcquireSRWLockExclusive(&statsLock);
    published=out;
    ReleaseSRWLockExclusive(&statsLock);
    windowStart=now;
    windowCalls=0;
}

// In place of the chain's Present at 0x1118B45 (`call [rax+40h]`: the leaf stub at 0x1118B36 loads rax with
// presentTable, whose slot 8 is this), render thread. The chain's own Present, then the counts.
HRESULT STDMETHODCALLTYPE PresentHook(IDXGISwapChain* chain,UINT sync,UINT flags) {
    const HRESULT hr=reinterpret_cast<PresentFn>((*reinterpret_cast<void***>(chain))[8])(chain,sync,flags);
    if(hr==DXGI_STATUS_OCCLUDED)++occludedCalls;
    else if(FAILED(hr))++failedCalls;
    ++windowCalls;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    const auto t=static_cast<std::uint64_t>(now.QuadPart);
    if(!windowStart)windowStart=t;
    else if(t-windowStart>=qpcFreq)SampleStatistics(chain,t);
    return hr;
}
void* presentTable[9]={nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,reinterpret_cast<void*>(&PresentHook)};

bool TearingSupported(IDXGIFactory* factory) noexcept {
    IDXGIFactory5* f5=nullptr;
    if(FAILED(factory->QueryInterface(__uuidof(IDXGIFactory5),reinterpret_cast<void**>(&f5))) || !f5)return false;
    BOOL allow=FALSE;
    const bool ok=SUCCEEDED(f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,&allow,sizeof(allow))) && allow;
    f5->Release();
    return ok;
}

HRESULT CreateSwapChainHook(IDXGIFactory* factory,IUnknown* device,DXGI_SWAP_CHAIN_DESC* desc,IDXGISwapChain** out) {
    const auto create=reinterpret_cast<CreateFn>((*reinterpret_cast<void***>(factory))[10]);
    tearingFlags=0;
    if(desc->SampleDesc.Count>1) {
        Log("FLIP the game asks for a multisampled back buffer (%u samples): the flip model has none, the swap chain stays blt",
            desc->SampleDesc.Count);
        return create(factory,device,desc,out);
    }
    DXGI_SWAP_CHAIN_DESC flip=*desc;
    flip.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
    flip.BufferCount=kBuffers;
    const bool tearing=flip.Windowed && TearingSupported(factory);
    if(tearing)flip.Flags|=DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    if(waitableLatency)flip.Flags|=DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    waitable.store(nullptr);
    const HRESULT hr=create(factory,device,&flip,out);
    if(SUCCEEDED(hr)) {
        tearingFlags=tearing ? DXGI_PRESENT_ALLOW_TEARING : 0u;
        HANDLE object=nullptr;
        if(waitableLatency && out && *out) {
            IDXGISwapChain2* chain2=nullptr;
            if(SUCCEEDED((*out)->QueryInterface(__uuidof(IDXGISwapChain2),reinterpret_cast<void**>(&chain2))) && chain2) {
                if(SUCCEEDED(chain2->SetMaximumFrameLatency(waitableLatency)))object=chain2->GetFrameLatencyWaitableObject();
                chain2->Release();
            }
            waitable.store(object);
        }
        Log("FLIP swap chain %ux%u: flip discard, %u buffers, windowed %d, tearing %d, waitable %d (queue %u) "
            "(the game asked: effect %u, %u buffer(s), flags 0x%X)",
            flip.BufferDesc.Width,flip.BufferDesc.Height,flip.BufferCount,flip.Windowed,tearing,object!=nullptr,
            waitableLatency,static_cast<unsigned>(desc->SwapEffect),desc->BufferCount,desc->Flags);
        return hr;
    }
    Log("FLIP the flip swap chain was refused (0x%08X): created as the game asks",static_cast<unsigned>(hr));
    return create(factory,device,desc,out);
}
}  // namespace

void SwapFlipUseWaitable(unsigned latency) noexcept { waitableLatency=latency; }

PresentStats SwapFlipPresentStats() noexcept {
    AcquireSRWLockShared(&statsLock);
    const PresentStats out=published;
    ReleaseSRWLockShared(&statsLock);
    return out;
}

void SwapFlipWait() noexcept {
    const HANDLE object=waitable.load(std::memory_order_relaxed);
    if(object)WaitForSingleObject(object,kWaitMs);
}

bool InstallSwapFlip(bool flipModel) noexcept {
    if(!edf::Matches(image,kCreateCall,kCreateSig,sizeof(kCreateSig)) ||
       !edf::Matches(image,kPresentFlags,kPresentSig,sizeof(kPresentSig))) {
        Log("FLIP the swap chain's creation or Present is not where docs/frame-pacing-re.md has them: blt model kept");
        return false;
    }
    // The creation: `movabs rax,hook; jmp rax`, called in place of `mov rax,[rcx]; call [rax+50h]` (the hook returns to
    // the game as CreateSwapChain would).
    unsigned char jump[12]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    const auto hook=reinterpret_cast<std::uintptr_t>(&CreateSwapChainHook);
    std::memcpy(jump+2,&hook,sizeof(hook));
    // The Present's flags, a leaf: `mov rax,&presentTable (in place of the chain's vtable: the `call [rax+40h]` that
    // follows at 0x1118B45 goes to PresentHook; nothing between touches rax); xor r8d,r8d; pextrd r10d,xmm0,2 (the sync
    // interval, about to go to edx); test r10d,r10d; jnz 1f; mov r10,&tearingFlags; mov r8d,[r10]; 1: ret`.
    unsigned char leaf[]={0x48,0xB8,0,0,0,0,0,0,0,0,0x45,0x33,0xC0,0x66,0x41,0x0F,0x3A,0x16,0xC2,0x02,0x45,0x85,0xD2,
                          0x75,0x0D,0x49,0xBA,0,0,0,0,0,0,0,0,0x45,0x8B,0x02,0xC3};
    const auto table=reinterpret_cast<std::uintptr_t>(&presentTable[0]);
    const auto flags=reinterpret_cast<std::uintptr_t>(&tearingFlags);
    std::memcpy(leaf+2,&table,sizeof(table));
    std::memcpy(leaf+27,&flags,sizeof(flags));
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    qpcFreq=static_cast<std::uint64_t>(freq.QuadPart);
    void* const createStub=edf::AllocateNearCode(image+kCreateCall,jump,sizeof(jump));
    void* const presentStub=edf::AllocateNearCode(image+kPresentFlags,leaf,sizeof(leaf));
    if(!createStub || !presentStub)return false;
    auto callTo=[](unsigned rva,const void* to,const unsigned char* expected)->bool {
        unsigned char* const site=image+rva;
        const auto delta=static_cast<const unsigned char*>(to)-(site+5);
        if(delta>0x7FFFFFF0 || delta<-0x7FFFFFF0)return false;
        unsigned char code[6]={0xE8,0,0,0,0,0x90};
        const auto rel=static_cast<std::int32_t>(delta);
        std::memcpy(code+1,&rel,4);
        return edf::PatchCode(site,expected,code,sizeof(code));
    };
    // The Present first: tearingFlags is 0 until a chain with tearing exists.
    if(!callTo(kPresentFlags,presentStub,kPresentSig))return false;
    // The flip model is a developer switch (the review of #386): a windowed chain with tearing that the game turns
    // exclusive in place (SetFullscreenState at 0x111CAC3 / 0x111BF4D, no rebuild) fails its Present with the tearing
    // flag; the rebuild 0x111B220 leaves +0x90 null when both creations fail; a minimised window's waitable object never
    // fires. None of it is tested on the real game: players keep the game's blt chain (sync interval 1 is enough for
    // the display's refresh rate).
    if(!flipModel)return true;
    return callTo(kCreateCall,createStub,kCreateSig);
}
}  // namespace crew
