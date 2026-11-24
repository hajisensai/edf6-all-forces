// The swap chain in the flip model (the review's latency-5 / gpu-3; docs/frame-pacing-re.md). Developer only, with the
// frame rate's stage (ini FrameRateDev=1).
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
#include "swap_flip.h"
#include "crew.h"
#include "memory.h"
#include "edf/patch.h"
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

using CreateFn=HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*,IUnknown*,DXGI_SWAP_CHAIN_DESC*,IDXGISwapChain**);

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
    const HRESULT hr=create(factory,device,&flip,out);
    if(SUCCEEDED(hr)) {
        tearingFlags=tearing ? DXGI_PRESENT_ALLOW_TEARING : 0u;
        Log("FLIP swap chain %ux%u: flip discard, %u buffers, windowed %d, tearing %d (the game asked: effect %u, %u buffer(s), flags 0x%X)",
            flip.BufferDesc.Width,flip.BufferDesc.Height,flip.BufferCount,flip.Windowed,tearing,
            static_cast<unsigned>(desc->SwapEffect),desc->BufferCount,desc->Flags);
        return hr;
    }
    Log("FLIP the flip swap chain was refused (0x%08X): created as the game asks",static_cast<unsigned>(hr));
    return create(factory,device,desc,out);
}
}  // namespace

bool InstallSwapFlip() noexcept {
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
    // The Present's flags, a leaf: `mov rax,[rcx]; xor r8d,r8d; pextrd r10d,xmm0,2 (the sync interval, about to go to
    // edx); test r10d,r10d; jnz 1f; mov r10,&tearingFlags; mov r8d,[r10]; 1: ret`.
    unsigned char leaf[]={0x48,0x8B,0x01,0x45,0x33,0xC0,0x66,0x41,0x0F,0x3A,0x16,0xC2,0x02,0x45,0x85,0xD2,0x75,0x0D,
                          0x49,0xBA,0,0,0,0,0,0,0,0,0x45,0x8B,0x02,0xC3};
    const auto flags=reinterpret_cast<std::uintptr_t>(&tearingFlags);
    std::memcpy(leaf+20,&flags,sizeof(flags));
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
    if(!callTo(kCreateCall,createStub,kCreateSig))return false;
    return true;
}
}  // namespace crew
