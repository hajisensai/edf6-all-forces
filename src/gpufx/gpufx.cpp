#include "gpufx.h"
#include "gpufx_gpu.h"
#include "gpufx_layers.h"
#include "gpufx_sim.h"
#include "../crew.h"
#include "../memory.h"
#include "../vhud.h"
#include "edf/memory.h"
#include "edf/patch.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

namespace crew::gpufx {
namespace {
using Microsoft::WRL::ComPtr;

// ---- the stock ParticleRender's draw (docs/gpufx-re.md) ----
// Slot 1 of vtable 0x17A8F70: void Draw(obj, nativeCtx, pass, view); draws only passes 2 / 5 with obj+0x200 != 0.
constexpr unsigned kParticleDrawSlot=0x17A8F78,kParticleDraw=0x302420;
constexpr unsigned char kParticleDrawCode[]={0x48,0x89,0x5C,0x24,0x18,0x57,0x48,0x81,0xEC,0x90,0x00,0x00,0x00,0x48,0x8B,0xFA,
    0x48,0x8B,0xD9,0x41,0x83,0xF8,0x02,0x74,0x0A,0x41,0x83,0xF8,0x05,0x0F,0x85,0x96,0x01,0x00,0x00,0x48,0x83,0xB9,0x00,0x02,
    0x00,0x00,0x00};
// The particle core's fields (ParticleRender embeds its core at +0x70; the 0x3027B0 draw takes a bare core): blend
// +0x14 (0 opaque, 1 alpha, 2 additive, 3 custom), lit +0x110 (nonzero: the lit draw 0x1154F70; its set by the byte
// +0x120: 0 the effect 0xA40 with VS 0x196F5C0 / PS 0x196BA60, else 0xC08 with VS 0x19848A0 / PS 0x19839A0; the
// normal's weights +0x114 / +0x118 / +0x11C), frames +0x168,
// live +0x190, snapshot +0x1A8 / count +0x1B8, texture holder +0x1C0 (its +0x10, the core's +0x1D0, holds the bound SRV's
// holder: the SRV at that pointer's +0x10).
constexpr std::size_t kCoreInRender=0x70;
constexpr std::size_t kBlend=0x14,kLit=0x110,kLitWeights=0x114,kLitSet=0x120,kFrames=0x168,kLive=0x190,kSnapshot=0x1A8,kSnapshotCount=0x1B8,
    kTexture=0x1C0,kTextureB=0x1D0,kCoreSize=0x200,kRenderWorld=0x2B0;
constexpr std::size_t kEntry=0x40;   // a render snapshot entry: pos vec4, rgba, normal, rot +0x30, size +0x34, frame byte +0x38
// The embedded core's own draw: void Draw(core, nativeCtx, const float world[16]) (MuzzleSmoke01, SmokeCandle, flame,
// plasma, ClaySplash ...: 21 callers, several tail jumps, each filtering the pass itself). 19-byte prologue, no relative
// instruction: mov r11,rsp; mov [r11+18],rbx; mov [r11+20],rsi; push rdi; sub rsp,90.
constexpr unsigned kCoreDraw=0x3027B0;
constexpr unsigned char kCoreDrawCode[]={0x4C,0x8B,0xDC,0x49,0x89,0x5B,0x18,0x49,0x89,0x73,0x20,0x57,0x48,0x81,0xEC,0x90,0x00,
    0x00,0x00};
using CoreDrawFn=void(__fastcall*)(void*,void*,const float*);
CoreDrawFn coreDrawNext=nullptr;

// The renderer's light block (probe dump): *(EDF+0x20B2958) + 0xB00 .. 0xC40.
constexpr unsigned kRendererPtr=0x20B2958;
using DrawFn=void(__fastcall*)(void*,void*,int,const void*);

// ---- game thread ----
constexpr int kBudget=8192;   // the layers' smoke lives up to 30 s; Renderer::kMaxInstances leaves the rest to the takeover
// On the heap, made by the first tick: a static Pool<4096> would grow the image's .data past the guard
// (tests/plugin_image_size_guard).
std::unique_ptr<Pool<kBudget>> pool;
std::atomic<unsigned> poolDropped{0};   // the render thread's probe line reads this, never the pool
Rng testRng{0x6F5EED01u};
int deskFixed=-1,deskPuff=-1;
bool anchored=false;
float anchor[3]{},right[3]{},ground=0.0f;
ULONGLONG lastMs=0;
float spawnDebt=0.0f;
ULONGLONG ticked=~ULONGLONG(0);
std::vector<Instance> staging;

// The published frame: the game thread swaps a full vector in, the render thread copies it out.
SRWLOCK publishLock=SRWLOCK_INIT;
std::vector<Instance> published;
std::atomic<bool> optEnabled{false},optProbe{false},optTest{false},optLayers{false};
bool layersInstalled=false;   // Install: the Explosion hook and its particles' branches (game thread reads after)
std::atomic<int> optNative{0};   // 0 stock, 1 ours, 2 the screen split (stock left, ours right), 3 the wipe (below)
std::atomic<bool> anchorReady{false};
float anchorShared[3]{};   // under publishLock

// ---- the flipbook (worker thread) ----
std::vector<std::uint8_t> flipPixels;
std::atomic<bool> flipReady{false};
DWORD WINAPI FlipbookThread(void*) {
    try {
        flipPixels.resize(static_cast<std::size_t>(kFlipSide)*kFlipSide*4);
        LARGE_INTEGER a,b,f;QueryPerformanceCounter(&a);
        GenerateSmokeFlipbook(flipPixels.data());
        QueryPerformanceCounter(&b);QueryPerformanceFrequency(&f);
        Log("GPUFX flipbook %dx%d (%d frames) made in %.0f ms, fnv %08X",kFlipSide,kFlipSide,kFlipFrames,
            static_cast<double>(b.QuadPart-a.QuadPart)*1000.0/static_cast<double>(f.QuadPart),Fnv(flipPixels.data(),flipPixels.size()));
        flipReady.store(true,std::memory_order_release);
    } catch(...) { Log("GPUFX flipbook: out of memory; our smoke stays off"); }
    return 0;
}

// ---- render thread ----
std::atomic<bool> installed{false};
Renderer renderer;
bool flipGiven=false;
const void* currentView=nullptr;
bool inWorld=false,observed=false;
ComPtr<ID3D11Texture2D> observedDepth;
unsigned observedFormat=0;
std::vector<Instance> renderOwn;
// The stock cards taken over in this view: their instances and one batch per stock draw (in the stock order).
struct NativeBatch { ComPtr<ID3D11ShaderResourceView> texture; Kind kind; int frames,grid,start,count,litSet;float litWeights[3]; };
std::vector<Instance> nativeInstances;
std::vector<NativeBatch> nativeBatches;
std::vector<Batch> batches;
// Which pointer chain from the object resolves to the SRV the stock draw binds (probe: 1 obj+0x240 -> +0x10,
// 2 obj+0x230 -> +0x10 -> +0x10, 3 obj+0x230 -> +0x10); 0 until one stock draw showed it. The takeover waits for it.
std::atomic<int> textureChain{0};
// Probe counters.
struct Probe {
    ULONGLONG loggedAt=0;
    int nativeLogged=0;
    unsigned nativeCalls=0,nativeReplaced=0,nativeOutside=0,nativeRefused=0,byPass[8]{};
    unsigned coreCalls=0,coreReplaced=0,refusedLit=0,refusedBlend=0,refusedGrid=0,stateLogged=0;
    unsigned litReplaced=0,litSetB=0,litCaptures=0,litCaptureFailed=0;
    bool litLogged=false;
    double cpuMs=0.0;unsigned cpuSamples=0;
    float jitterMin[2]{1e9f,1e9f},jitterMax[2]{-1e9f,-1e9f};
    bool lightLogged=false;
    float blendSeen[3][4]{};
};
Probe probe;
std::atomic<bool> probeReset{false};

double NowMs() noexcept {
    static const double perTick=[]{LARGE_INTEGER f;QueryPerformanceFrequency(&f);return 1000.0/static_cast<double>(f.QuadPart);}();
    LARGE_INTEGER c;QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart)*perTick;
}

bool MainView(const void* view) noexcept {
    return view && Readable(view,0xA0) && At<int>(view,4)==0;   // query type 0: a camera's world (not distant, not shadow)
}

// The view's own matrices: row vectors, world -> view (+0x20) and view -> clip (+0x60).
void ReadMatrices(const void* view,ViewParams& v) noexcept {
    std::memcpy(v.view,static_cast<const unsigned char*>(view)+0x20,sizeof(v.view));
    std::memcpy(v.proj,static_cast<const unsigned char*>(view)+0x60,sizeof(v.proj));
}

// The scene's directional light from the renderer's light block (run 1 probe, a daytime test range):
// +B40 the light's travel direction (y -0.651, pointing down; +B90 holds its negation), +B50 its colour
// (0.81 0.80 0.80), +B60 the ambient (0.19 0.22 0.21). The renderer and the block are the stock ones,
// so a missing block means no world: the smoke keeps the header's defaults for that frame.
void ReadLight(ViewParams& v) noexcept {
    __try {
        const unsigned char* r=At<const unsigned char*>(image,kRendererPtr);
        if(!r || !Readable(r+0xB40,0x30))return;
        const float* d=reinterpret_cast<const float*>(r+0xB40);
        const float len=d[0]*d[0]+d[1]*d[1]+d[2]*d[2];
        if(!(len>0.5f && len<2.0f))return;
        for(int i=0;i<3;++i){v.sunWorld[i]=-d[i];v.sunColor[i]=d[4+i];v.ambient[i]=d[8+i];}
        v.exposure=1.0f;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

void TransformPoint(const float p[3],const float m[16],float out[4]) noexcept {
    for(int c=0;c<4;++c)out[c]=p[0]*m[c]+p[1]*m[4+c]+p[2]*m[8+c]+m[12+c];
}

// SEH-only readers (no C++ objects: __try needs none with destructors in the frame).
struct NativeView {
    int blend,lit,litSet,frames;
    float litWeights[3];
    std::uint64_t live,count;
    const unsigned char* snapshot;
    float world[16];
    void* chain[3];
};
// `core`: a particle core (a ParticleRender's +0x70 or the 0x3027B0 draw's own); `world` its object -> world matrix.
bool ReadNative(const unsigned char* obj,const float* world,NativeView& n) noexcept {
    __try {
        if(!Readable(obj,kCoreSize) || !world || !Readable(world,sizeof(n.world)))return false;
        n.blend=At<int>(obj,kBlend);n.lit=At<unsigned char>(obj,kLit);n.frames=At<int>(obj,kFrames);
        n.litSet=At<unsigned char>(obj,kLitSet) ? 2 : 1;
        std::memcpy(n.litWeights,obj+kLitWeights,sizeof(n.litWeights));
        n.live=At<std::uint64_t>(obj,kLive);n.count=At<std::uint64_t>(obj,kSnapshotCount);
        n.snapshot=At<const unsigned char*>(obj,kSnapshot);
        std::memcpy(n.world,world,sizeof(n.world));
        n.chain[0]=n.chain[1]=n.chain[2]=nullptr;
        const unsigned char* b=At<const unsigned char*>(obj,kTextureB);
        if(b && Readable(b,0x18))n.chain[0]=At<void*>(b,0x10);
        const unsigned char* t=At<const unsigned char*>(obj,kTexture);
        if(t && Readable(t,0x18)) {
            const unsigned char* mid=At<const unsigned char*>(t,0x10);
            n.chain[2]=const_cast<unsigned char*>(mid);
            if(mid && Readable(mid,0x28))n.chain[1]=At<void*>(mid,0x10);
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// The snapshot entries into instances. The stock VS (0x1968790): the centre is the entry's position times the world
// matrix times the view, `size` the corner's view-space distance from it (the half diagonal, not scaled by the world
// matrix); the corners' turn: StockRotToOurs (gpufx_sim.h).
int CopySnapshot(const NativeView& n,Instance* out,int most) noexcept {
    __try {
        const int count=static_cast<int>(n.count<static_cast<std::uint64_t>(most) ? n.count : static_cast<std::uint64_t>(most));
        if(count<=0 || !n.snapshot || !Readable(n.snapshot,kEntry*static_cast<std::size_t>(count)))return 0;
        for(int i=0;i<count;++i) {
            const unsigned char* e=n.snapshot+kEntry*static_cast<std::size_t>(i);
            float p[4];
            TransformPoint(reinterpret_cast<const float*>(e),n.world,p);
            Instance& o=out[i];
            o.pos[0]=p[0];o.pos[1]=p[1];o.pos[2]=p[2];
            std::memcpy(o.color,e+0x10,sizeof(o.color));
            o.rot=StockRotToOurs(At<float>(e,0x30));
            o.size=At<float>(e,0x34);
            o.frame=static_cast<float>(At<unsigned char>(e,0x38));
            // The stock PS has no soft fade (a hard cut where a card meets the ground): ours fades over half its size.
            o.soft=std::clamp(o.size*0.5f,0.05f,3.0f);o.emissive=0.0f;
            if(n.lit && n.litSet==1) {
                // The set 0xA40's normal: the entry's +0x20 through the world matrix's rotation, normalised.
                const float* sn=reinterpret_cast<const float*>(e+0x20);
                float w[3];
                for(int c=0;c<3;++c)w[c]=sn[0]*n.world[c]+sn[1]*n.world[4+c]+sn[2]*n.world[8+c];
                const float len=std::sqrt(w[0]*w[0]+w[1]*w[1]+w[2]*w[2]);
                if(len>1e-6f){for(float& v:w)v/=len;}
                o.emissive=PackNormal(w);
            }
        }
        return count;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
void* Resolve(const NativeView& n,int chain) noexcept { return chain>=1 && chain<=3 ? n.chain[chain-1] : nullptr; }

void LogBoundState(ID3D11DeviceContext* context) noexcept {
    ComPtr<ID3D11BlendState> blend;FLOAT factor[4];UINT mask=0;
    context->OMGetBlendState(&blend,factor,&mask);
    if(blend) {
        D3D11_BLEND_DESC d{};blend->GetDesc(&d);const auto& r=d.RenderTarget[0];
        Log("GPUFX native   blend en=%d src=%d dst=%d op=%d srcA=%d dstA=%d opA=%d mask=%X a2c=%d",r.BlendEnable,r.SrcBlend,
            r.DestBlend,r.BlendOp,r.SrcBlendAlpha,r.DestBlendAlpha,r.BlendOpAlpha,r.RenderTargetWriteMask,d.AlphaToCoverageEnable);
    } else Log("GPUFX native   blend default");
    ComPtr<ID3D11DepthStencilState> ds;UINT ref=0;
    context->OMGetDepthStencilState(&ds,&ref);
    if(ds){D3D11_DEPTH_STENCIL_DESC d{};ds->GetDesc(&d);Log("GPUFX native   depth en=%d write=%d func=%d stencil=%d",d.DepthEnable,d.DepthWriteMask,d.DepthFunc,d.StencilEnable);}
    else Log("GPUFX native   depth default (test on, write on, LESS)");
    ID3D11RenderTargetView* rtv[2]{};ComPtr<ID3D11DepthStencilView> dsv;
    context->OMGetRenderTargets(2,rtv,&dsv);
    for(int i=0;i<2;++i) {
        if(!rtv[i])continue;
        D3D11_RENDER_TARGET_VIEW_DESC d{};rtv[i]->GetDesc(&d);
        ComPtr<ID3D11Resource> res;rtv[i]->GetResource(&res);ComPtr<ID3D11Texture2D> t;
        D3D11_TEXTURE2D_DESC td{};if(SUCCEEDED(res.As(&t)))t->GetDesc(&td);
        Log("GPUFX native   rtv%d fmt=%d %ux%u samples=%u",i,d.Format,td.Width,td.Height,td.SampleDesc.Count);
        rtv[i]->Release();
    }
    if(dsv){D3D11_DEPTH_STENCIL_VIEW_DESC d{};dsv->GetDesc(&d);Log("GPUFX native   dsv fmt=%d flags=%u (read-only depth: %d)",d.Format,d.Flags,(d.Flags&D3D11_DSV_READ_ONLY_DEPTH)!=0);}
    else Log("GPUFX native   dsv none");
    ID3D11ShaderResourceView* srv[8]{};
    context->PSGetShaderResources(0,8,srv);
    for(int i=0;i<8;++i) {
        if(!srv[i])continue;
        D3D11_SHADER_RESOURCE_VIEW_DESC d{};srv[i]->GetDesc(&d);
        ComPtr<ID3D11Resource> res;srv[i]->GetResource(&res);ComPtr<ID3D11Texture2D> t;
        D3D11_TEXTURE2D_DESC td{};if(SUCCEEDED(res.As(&t)))t->GetDesc(&td);
        Log("GPUFX native   ps t%d=%p fmt=%d dim=%d %ux%u mips=%u bind=%X%s",i,static_cast<void*>(srv[i]),d.Format,d.ViewDimension,
            td.Width,td.Height,td.MipLevels,td.BindFlags,(td.BindFlags&D3D11_BIND_DEPTH_STENCIL) ? " (A DEPTH BUFFER: soft particles)" : "");
        srv[i]->Release();
    }
    ID3D11Buffer* cb[4]{};
    context->PSGetConstantBuffers(0,4,cb);
    for(int i=0;i<4;++i){if(!cb[i])continue;D3D11_BUFFER_DESC d{};cb[i]->GetDesc(&d);Log("GPUFX native   ps b%d %u bytes",i,d.ByteWidth);cb[i]->Release();}
    ID3D11ShaderResourceView* vsrv[4]{};
    context->VSGetShaderResources(0,4,vsrv);
    for(int i=0;i<4;++i){if(!vsrv[i])continue;Log("GPUFX native   vs t%d=%p",i,static_cast<void*>(vsrv[i]));vsrv[i]->Release();}
}

// The SRV the stock draw left bound at ps t0 (the stock draw sets its texture there: 0x115DB10).
void* BoundTexture(ID3D11DeviceContext* context) noexcept {
    ID3D11ShaderResourceView* srv=nullptr;
    context->PSGetShaderResources(0,1,&srv);
    if(srv)srv->Release();   // only compared, never used
    return srv;
}

// The stock particle VS's distance fade constants (xgl_system, VS b0 bytes 568 / 572: cb0[35].z/.w, 608 bytes): a
// GPU constant buffer the CPU never sees, so a 16-byte copy of cb0[35] goes into a ring of staging buffers at the first
// stock particle draw of a world and is read back without waiting a few frames later (WorldEnd). Render thread only.
struct FadeReader {
    static constexpr int kRing=3;
    ComPtr<ID3D11Buffer> ring[kRing];
    bool pending[kRing]{};
    int next=0;
    bool copiedThisWorld=false,known=false;
    float mul=0.0f,add=0.0f;
    unsigned bytes=0;
    void Copy(ID3D11DeviceContext* context) noexcept {
        if(copiedThisWorld || !context)return;
        copiedThisWorld=true;
        ComPtr<ID3D11Buffer> cb;
        context->VSGetConstantBuffers(0,1,&cb);
        if(!cb)return;
        D3D11_BUFFER_DESC d{};cb->GetDesc(&d);bytes=d.ByteWidth;
        if(d.ByteWidth<576 || pending[next])return;
        if(!ring[next]) {
            ComPtr<ID3D11Device> device;context->GetDevice(&device);
            D3D11_BUFFER_DESC sd{};sd.ByteWidth=16;sd.Usage=D3D11_USAGE_STAGING;sd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            if(!device || FAILED(device->CreateBuffer(&sd,nullptr,&ring[next])))return;
        }
        const D3D11_BOX box{560,0,0,576,1,1};
        context->CopySubresourceRegion(ring[next].Get(),0,0,0,0,cb.Get(),0,&box);
        pending[next]=true;next=(next+1)%kRing;
    }
    void Read(ID3D11DeviceContext* context) noexcept {
        for(int i=0;i<kRing;++i) {
            if(!pending[i])continue;
            D3D11_MAPPED_SUBRESOURCE m{};
            if(context->Map(ring[i].Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&m)!=S_OK)continue;
            const float* f=static_cast<const float*>(m.pData);
            if(std::isfinite(f[2]) && std::isfinite(f[3])){mul=f[2];add=f[3];known=true;}
            context->Unmap(ring[i].Get(),0);
            pending[i]=false;
        }
    }
    void Reset() noexcept { for(auto& r:ring)r.Reset();for(bool& p:pending)p=false;known=false;mul=add=0.0f; }
};
FadeReader fade;

// The stock lighting a lit takeover draws with (Kind::nativeLit*): what the stock lit draw binds, taken after the first
// stock lit draw of each world view (that one stays stock). The set 0xA40 binds it to its PS (b0 system, b3 shadow,
// t1 shadow map, t2 point lights, t3..t5 cluster masks), the set 0xC08 to its VS (b0, b3, t0 .. t4). Render thread.
struct LitCapture {
    ComPtr<ID3D11Buffer> system,shadow;
    ComPtr<ID3D11ShaderResourceView> views[5];
    bool complete=false,tried=false;
    void Reset() noexcept { system.Reset();shadow.Reset();for(auto& v:views)v.Reset();complete=tried=false; }
    static bool BufferView(ID3D11ShaderResourceView* v) noexcept {
        if(!v)return false;
        D3D11_SHADER_RESOURCE_VIEW_DESC d{};v->GetDesc(&d);
        return d.ViewDimension==D3D11_SRV_DIMENSION_BUFFER || d.ViewDimension==D3D11_SRV_DIMENSION_BUFFEREX;
    }
    static unsigned Bytes(ID3D11Buffer* b) noexcept { if(!b)return 0;D3D11_BUFFER_DESC d{};b->GetDesc(&d);return d.ByteWidth; }
    void Capture(ID3D11DeviceContext* context,int set) noexcept {
        tried=true;
        ID3D11Buffer* cb[4]{};ID3D11ShaderResourceView* srv[6]{};
        if(set==1){context->PSGetConstantBuffers(0,4,cb);context->PSGetShaderResources(1,5,srv);}
        else{context->VSGetConstantBuffers(0,4,cb);context->VSGetShaderResources(0,5,srv);}
        system.Attach(cb[0]);shadow.Attach(cb[3]);
        if(cb[1])cb[1]->Release();
        if(cb[2])cb[2]->Release();
        for(int i=0;i<5;++i)views[i].Attach(srv[i]);
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        if(views[0])views[0]->GetDesc(&sd);
        complete=Bytes(system.Get())>=608 && Bytes(shadow.Get())>=400 && views[0] && sd.ViewDimension==D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        for(int i=1;i<5;++i)complete=complete && BufferView(views[i].Get());
        if(complete)++probe.litCaptures;else ++probe.litCaptureFailed;
        if(!probe.litLogged && optProbe.load(std::memory_order_relaxed)) {
            probe.litLogged=true;
            Log("GPUFX lit capture (set %d): system %u bytes, shadow %u bytes, shadow map dim %d, lights %d %d %d %d -> %s",set,
                Bytes(system.Get()),Bytes(shadow.Get()),views[0] ? static_cast<int>(sd.ViewDimension) : -1,BufferView(views[1].Get()),
                BufferView(views[2].Get()),BufferView(views[3].Get()),BufferView(views[4].Get()),complete ? "complete" : "INCOMPLETE");
        }
        if(!complete){system.Reset();shadow.Reset();for(auto& v:views)v.Reset();}
    }
    StockLights Lights() const noexcept {
        StockLights l{};
        if(!complete)return l;
        l.system=system.Get();l.shadow=shadow.Get();l.shadowMap=views[0].Get();l.points=views[1].Get();
        for(int i=0;i<3;++i)l.masks[i]=views[2+i].Get();
        return l;
    }
};
LitCapture litCapture;

// GpuFxNative=2, the A/B picture in one frame: of a taken-over draw, the entries right of the screen's centre are ours,
// the left ones stay in the stock snapshot (moved to its front, its count cut) for the stock draw, then put back.
float splitView[16]{};
struct SplitRestore {
    unsigned char* snapshot=nullptr;
    std::uint64_t* countAt=nullptr;
    std::uint64_t count=0;
    std::vector<unsigned char> saved;
} split;
void RestoreSplit() noexcept {
    if(!split.snapshot)return;
    std::memcpy(split.snapshot,split.saved.data(),split.saved.size());
    *split.countAt=split.count;
    split.snapshot=nullptr;split.countAt=nullptr;
}
// `got` instances (from the snapshot's first entries) at nativeInstances[start]: ours keeps the right ones; the stock
// snapshot gets the left ones (and any past `got`) at its front. Returns whether the stock draw has nothing left.
bool Split(const unsigned char* core,const NativeView& n,std::size_t start,int got) noexcept {
    auto* snap=const_cast<unsigned char*>(n.snapshot);
    const std::size_t total=static_cast<std::size_t>(n.count);
    if(!Readable(snap,kEntry*total,true) || !Readable(core+kSnapshotCount,8,true)){nativeInstances.resize(start);return false;}
    try { split.saved.assign(snap,snap+kEntry*total); } catch(...) { nativeInstances.resize(start);return false; }
    int keep=0;std::size_t left=0;
    for(std::size_t i=0;i<total;++i) {
        bool ours=false;
        if(i<static_cast<std::size_t>(got)) {
            const Instance in=nativeInstances[start+i];
            float v[4];TransformPoint(in.pos,splitView,v);
            ours=v[0]>0.0f;
            if(ours)nativeInstances[start+static_cast<std::size_t>(keep++)]=in;
        }
        if(!ours){std::memcpy(snap+left*kEntry,split.saved.data()+i*kEntry,kEntry);++left;}
    }
    nativeInstances.resize(start+static_cast<std::size_t>(keep));
    if(left==0){split.saved.clear();return true;}
    split.snapshot=snap;split.countAt=reinterpret_cast<std::uint64_t*>(const_cast<unsigned char*>(core)+kSnapshotCount);split.count=n.count;
    *split.countAt=left;
    if(keep==0)RestoreSplit();
    return false;
}

// GpuFxNative=3, the wipe: every taken-over card drawn twice, the stock draw cut to the left half of its viewport and
// ours to the right half: the same cards side by side at the centre line (a debug view: the cards' cost twice).
struct StockScissor {
    ID3D11DeviceContext* context=nullptr;
    ComPtr<ID3D11RasterizerState> saved,cut;
    D3D11_RECT rects[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
    UINT rectCount=0;
    void Begin(ID3D11DeviceContext* c) noexcept {
        if(!c)return;
        D3D11_VIEWPORT v{};UINT nv=1;c->RSGetViewports(&nv,&v);
        if(nv==0 || v.Width<2.0f)return;
        ComPtr<ID3D11Device> device;c->GetDevice(&device);
        c->RSGetState(&saved);
        D3D11_RASTERIZER_DESC d{};
        if(saved)saved->GetDesc(&d);
        else{d.FillMode=D3D11_FILL_SOLID;d.CullMode=D3D11_CULL_BACK;d.DepthClipEnable=TRUE;}
        d.ScissorEnable=TRUE;
        if(!device || FAILED(device->CreateRasterizerState(&d,&cut))){saved.Reset();return;}
        rectCount=D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;c->RSGetScissorRects(&rectCount,rects);
        const D3D11_RECT left{static_cast<LONG>(v.TopLeftX),static_cast<LONG>(v.TopLeftY),static_cast<LONG>(v.TopLeftX+v.Width*0.5f),
                              static_cast<LONG>(v.TopLeftY+v.Height)};
        c->RSSetState(cut.Get());c->RSSetScissorRects(1,&left);
        context=c;
    }
    void End() noexcept {
        if(!context)return;
        context->RSSetState(saved.Get());context->RSSetScissorRects(rectCount,rectCount ? rects : nullptr);
        context=nullptr;saved.Reset();cut.Reset();rectCount=0;
    }
};

enum class Source { render, core };
// One stock particle draw: taken over (its snapshot copied into a batch, the stock draw skipped; true) or left to the
// stock draw (false). Both the ParticleRender slot and the embedded core's own draw come here.
bool TakeOver(const unsigned char* core,const float* world,void* native,Source source,const void* obj) noexcept {
    const bool wantProbe=optProbe.load(std::memory_order_relaxed);
    const int nativeMode=optNative.load(std::memory_order_relaxed);
    const bool wantNative=nativeMode!=0 && optEnabled.load(std::memory_order_relaxed);
    NativeView n{};
    const bool read=ReadNative(core,world,n);
    ID3D11DeviceContext* context=native && Readable(native,0x10) ? At<ID3D11DeviceContext*>(native,8) : nullptr;
    if(inWorld && context)fade.Copy(context);
    // The takeover: our world's view, a card emitter (a lit one once this view's stock lighting is captured), blend
    // 1 / 2, a stock atlas grid, its texture's chain known.
    const int chain=textureChain.load(std::memory_order_relaxed);
    const int grid=read ? StockGrid(n.frames) : 0;
    if(wantNative && read && inWorld && chain && n.live && (n.lit==0 || litCapture.complete) && (n.blend==1 || n.blend==2) && n.count>0 && grid) {
        auto* srv=static_cast<ID3D11ShaderResourceView*>(Resolve(n,chain));
        const int room=Renderer::kMaxInstances-kBudget-static_cast<int>(nativeInstances.size());
        if(srv && room>0 && static_cast<int>(nativeBatches.size())<Renderer::kMaxBatches-1) {
            const std::size_t start=nativeInstances.size();
            nativeInstances.resize(start+static_cast<std::size_t>(room));
            const int got=CopySnapshot(n,nativeInstances.data()+start,room);
            nativeInstances.resize(start+static_cast<std::size_t>(got));
            bool all=true;
            if(got>0 && nativeMode==2)all=Split(core,n,start,got);
            const int kept=static_cast<int>(nativeInstances.size()-start);
            if(kept>0) {
                NativeBatch b{};b.texture=srv;
                b.kind=n.lit ? (n.blend==2 ? Kind::nativeLitAdditive : Kind::nativeLitAlpha) : n.blend==2 ? Kind::nativeAdditive : Kind::nativeAlpha;
                b.frames=grid*grid;b.grid=grid;b.start=static_cast<int>(start);b.count=kept;
                b.litSet=n.lit ? n.litSet : 0;std::memcpy(b.litWeights,n.litWeights,sizeof(b.litWeights));
                nativeBatches.push_back(std::move(b));
                ++probe.nativeReplaced;if(source==Source::core)++probe.coreReplaced;
                if(n.lit){++probe.litReplaced;if(n.litSet==2)++probe.litSetB;}
            }
            if(got>0)return nativeMode==3 ? false : nativeMode==2 ? all : true;   // the stock draw skipped (split: it draws
                                                                                  // what is left; the wipe: all, cut)
        }
        ++probe.nativeRefused;
    } else if(wantNative && read && n.live && n.count>0) {
        if(!inWorld)++probe.nativeOutside;
        else if(n.lit)++probe.refusedLit;
        else if(n.blend!=1 && n.blend!=2)++probe.refusedBlend;
        else if(!grid)++probe.refusedGrid;
    }
    if(!wantProbe || probe.nativeLogged>=32 || !read || n.live==0)return false;
    ++probe.nativeLogged;
    Log("GPUFX native draw %s obj=%p vt=EDF+%llX (world view %p, in world %d) blend=%d lit=%d frames=%d live=%llu snap=%llu "
        "W.diag=%.3f %.3f %.3f T=%.1f %.1f %.1f cand=%p %p %p",source==Source::core ? "core" : "render",obj,
        static_cast<unsigned long long>(*static_cast<const unsigned char* const*>(obj)-image),currentView,inWorld,n.blend,n.lit,n.frames,
        static_cast<unsigned long long>(n.live),static_cast<unsigned long long>(n.count),n.world[0],n.world[5],n.world[10],n.world[12],
        n.world[13],n.world[14],n.chain[0],n.chain[1],n.chain[2]);
    if(n.count>0 && n.snapshot && Readable(n.snapshot,kEntry)) {
        const float* e=reinterpret_cast<const float*>(n.snapshot);
        Log("GPUFX native   entry0 pos=%.2f %.2f %.2f rgba=%.2f %.2f %.2f %.2f rot=%.2f size=%.3f frame=%u",e[0],e[1],e[2],e[4],e[5],e[6],e[7],
            e[12],e[13],static_cast<unsigned>(n.snapshot[0x38]));
    }
    return false;
}

// After a stock draw that was not taken over: which texture chain is the SRV it bound, and (probe) its state.
void AfterStock(const unsigned char* core,const float* world,void* native,bool logState) noexcept {
    ID3D11DeviceContext* context=native && Readable(native,0x10) ? At<ID3D11DeviceContext*>(native,8) : nullptr;
    if(!context)return;
    NativeView n{};
    if(!ReadNative(core,world,n) || n.live==0)return;
    if(n.lit && inWorld && !litCapture.tried)litCapture.Capture(context,n.litSet);
    void* bound=BoundTexture(context);
    if(bound && !textureChain.load(std::memory_order_relaxed)) {
        for(int c=1;c<=3;++c) if(Resolve(n,c)==bound){textureChain.store(c,std::memory_order_relaxed);Log("GPUFX native texture chain %d (%s) is the bound SRV",c,
            c==1 ? "core+0x1D0 -> +0x10" : c==2 ? "core+0x1C0 -> +0x10 -> +0x10" : "core+0x1C0 -> +0x10");break;}
    }
    if(logState)LogBoundState(context);
}

void __fastcall ParticleDrawHook(void* obj,void* native,int pass,const void* view) {
    const auto stock=reinterpret_cast<DrawFn>(image+kParticleDraw);
    if(pass!=2 && pass!=5){stock(obj,native,pass,view);return;}
    ++probe.nativeCalls;++probe.byPass[pass&7];
    const auto* o=static_cast<const unsigned char*>(obj);
    const unsigned char* core=o+kCoreInRender;
    const float* world=reinterpret_cast<const float*>(o+kRenderWorld);
    if(view==currentView && TakeOver(core,world,native,Source::render,obj))return;
    StockScissor wipe;
    if(view==currentView && inWorld && optNative.load(std::memory_order_relaxed)==3 && native && Readable(native,0x10))
        wipe.Begin(At<ID3D11DeviceContext*>(native,8));
    stock(obj,native,pass,view);
    wipe.End();
    RestoreSplit();
    const bool logState=optProbe.load(std::memory_order_relaxed) && probe.stateLogged<24;
    if(logState)++probe.stateLogged;
    AfterStock(core,world,native,logState);
}

void __fastcall CoreDrawHook(void* core,void* native,const float* world) {
    ++probe.coreCalls;
    const auto* c=static_cast<const unsigned char*>(core);
    if(TakeOver(c,world,native,Source::core,core))return;
    StockScissor wipe;
    if(inWorld && optNative.load(std::memory_order_relaxed)==3 && native && Readable(native,0x10))
        wipe.Begin(At<ID3D11DeviceContext*>(native,8));
    coreDrawNext(core,native,world);
    wipe.End();
    RestoreSplit();
    AfterStock(c,world,native,false);
}

bool InstallCoreDraw() noexcept {
    if(coreDrawNext)return true;
    if(!Matches(kCoreDraw,kCoreDrawCode,sizeof(kCoreDrawCode)))return false;
    unsigned char trampoline[sizeof(kCoreDrawCode)+14];
    std::memcpy(trampoline,kCoreDrawCode,sizeof(kCoreDrawCode));
    const unsigned char jump[6]={0xFF,0x25,0,0,0,0};
    std::memcpy(trampoline+sizeof(kCoreDrawCode),jump,6);
    const auto back=reinterpret_cast<std::uintptr_t>(image+kCoreDraw+sizeof(kCoreDrawCode));
    std::memcpy(trampoline+sizeof(kCoreDrawCode)+6,&back,8);
    void* const code=edf::AllocateNearCode(image+kCoreDraw,trampoline,sizeof(trampoline));
    if(!code)return false;
    unsigned char patch[sizeof(kCoreDrawCode)];std::memset(patch,0x90,sizeof(patch));
    std::memcpy(patch,jump,6);
    const auto hook=reinterpret_cast<std::uintptr_t>(&CoreDrawHook);
    std::memcpy(patch+6,&hook,8);
    coreDrawNext=reinterpret_cast<CoreDrawFn>(code);
    if(edf::PatchCode(image+kCoreDraw,kCoreDrawCode,patch,sizeof(patch)))return true;
    coreDrawNext=nullptr;VirtualFree(code,0,MEM_RELEASE);
    return false;
}

void PublishOptions() noexcept {
    const Config& c=Cfg();
    optEnabled.store(c.enabled && c.gpuFx,std::memory_order_relaxed);
    optLayers.store(c.enabled && c.gpuFx && c.gpuFxLayers && layersInstalled,std::memory_order_relaxed);
    optNative.store(c.gpuFxNative,std::memory_order_relaxed);
    optProbe.store(c.gpuFxProbe,std::memory_order_relaxed);
    optTest.store(c.gpuFxTest,std::memory_order_relaxed);
}

// ---- the layered emitters (gpufx_layers.h) ----
// The events come from the game's own hooks (the shot entry 0x696FD0 through heli.cpp, EffectGenUtil::Explosion
// 0x2E5ED0 here), queued under eventLock and spawned by TickNative. Both are this machine's: a remote player's round
// is replayed through 0x696FD0 (replay 1), a remote blast is this machine's own bullet's.
SRWLOCK eventLock=SRWLOCK_INIT;
constexpr std::size_t kQueueMost=256;
ShotEvent shotQueue[kQueueMost];std::size_t shotQueued=0;
BlastEvent blastQueue[kQueueMost];std::size_t blastQueued=0;
LayerDescs layerDescs{};
Rng layerRng{0x5EED7A11u};
std::atomic<unsigned> layerShots{0},layerBlasts{0},layerParticles{0},layerQueueFull{0};
// A fast gun (an autocannon with a blast) makes a full shot's layers only every kShotRefire s; between, a thin one.
constexpr float kShotRefire=0.35f;
struct RecentShot { const void* weapon=nullptr;ULONGLONG ms=0; };
RecentShot recentShots[16]{};

// Weapon fields (docs/gpufx-re.md, layers): the owner +0x120, the stock muzzle effect +0x480 (0x697F85 skips a null
// one), AmmoExplosion +0x8B0, the muzzle list +0x1D0 / count +0x1E0, stride 0xF0, an entry's forward +0x70 and
// position +0x80.
constexpr std::size_t kWeaponOwner=0x120,kWeaponMuzzleEffect=0x480,kWeaponBlast=0x8B0,kWeaponMuzzles=0x1D0,
    kWeaponMuzzleCount=0x1E0,kMuzzleStride=0xF0,kMuzzleForward=0x70,kMuzzlePosition=0x80;

// EffectGenUtil::Explosion 0x2E5ED0: its head hook (the 15-byte prologue, no relative instructions) and the two
// "nothing to spawn" branches made unconditional so its own particles are ours: 0x2E68BC (the three ParticleObjects'
// loop, to its tail 0x2E6C74) and 0x2E6F75 (the extra particle 0x2DFEF0, to 0x2E70D0). The shake, the decal and the
// point light stay stock.
constexpr unsigned kExplosion=0x2E5ED0;
constexpr unsigned char kExplosionSig[]={0x48,0x8B,0xC4,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57};
constexpr unsigned kExplosionLoop=0x2E68BC,kExplosionExtra=0x2E6F75;
constexpr unsigned char kLoopJe[]={0x0F,0x84,0xB2,0x03,0x00,0x00},kLoopJmp[]={0xE9,0xB3,0x03,0x00,0x00,0x90};
constexpr unsigned char kExtraJe[]={0x0F,0x84,0x55,0x01,0x00,0x00},kExtraJmp[]={0xE9,0x56,0x01,0x00,0x00,0x90};
// EffectGenUtil::Explosion_Big 0x2E7A30 (egu, pos, dir, size, shake, flag): the same prologue; its particles are two
// calls, 0x2E7A81 -> 0x2E4D90 (the smoke card) and 0x2E7BD4 -> 0x2E0B70 (Blazing, the 15 puffs' loop), both void, made
// NOPs. The shake 0x104C00, the decal 0x2E7290 and the light 0x2E50C0 stay stock.
constexpr unsigned kExplosionBig=0x2E7A30,kBigSmoke=0x2E7A81,kBigPuffs=0x2E7BD4;
constexpr unsigned char kBigSmokeCall[]={0xE8,0x0A,0xD3,0xFF,0xFF},kBigPuffsCall[]={0xE8,0x97,0x8F,0xFF,0xFF};
constexpr unsigned char kNop5[]={0x90,0x90,0x90,0x90,0x90};
using ExplosionFn=void(__fastcall*)(void*,const float*,const float*,float,float,const float*,float);
using ExplosionBigFn=void(__fastcall*)(void*,const float*,const float*,float,float,unsigned char);
ExplosionFn explosionNext=nullptr;
ExplosionBigFn explosionBigNext=nullptr;

void QueueBlast(const float* pos,float size) noexcept {
    if(!optLayers.load(std::memory_order_relaxed))return;
    __try {
        if(Readable(pos,12) && std::isfinite(pos[0]) && std::isfinite(pos[1]) && std::isfinite(pos[2]) && std::isfinite(size) && size>0.0f) {
            AcquireSRWLockExclusive(&eventLock);
            if(blastQueued<kQueueMost) {
                BlastEvent& e=blastQueue[blastQueued++];
                std::memcpy(e.pos,pos,sizeof(e.pos));e.size=size;
            } else layerQueueFull.fetch_add(1,std::memory_order_relaxed);
            ReleaseSRWLockExclusive(&eventLock);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

void __fastcall ExplosionHook(void* egu,const float* pos,const float* dir,float size,float shake,const float* color,float light) {
    QueueBlast(pos,size);
    explosionNext(egu,pos,dir,size,shake,color,light);
}

void __fastcall ExplosionBigHook(void* egu,const float* pos,const float* dir,float size,float shake,unsigned char flag) {
    QueueBlast(pos,size);
    explosionBigNext(egu,pos,dir,size,shake,flag);
}

// A head hook over the 15-byte prologue kExplosionSig (no relative instructions in it): the trampoline runs those bytes
// and jumps back; nullptr when the bytes differ or the patch fails.
void* HeadHook(unsigned rva,const void* hook) noexcept {
    if(!Matches(rva,kExplosionSig,sizeof(kExplosionSig)))return nullptr;
    unsigned char trampoline[sizeof(kExplosionSig)+14];
    std::memcpy(trampoline,kExplosionSig,sizeof(kExplosionSig));
    const unsigned char jump[6]={0xFF,0x25,0,0,0,0};
    std::memcpy(trampoline+sizeof(kExplosionSig),jump,6);
    const auto back=reinterpret_cast<std::uintptr_t>(image+rva+sizeof(kExplosionSig));
    std::memcpy(trampoline+sizeof(kExplosionSig)+6,&back,8);
    void* const code=edf::AllocateNearCode(image+rva,trampoline,sizeof(trampoline));
    if(!code)return nullptr;
    unsigned char patch[sizeof(kExplosionSig)];std::memset(patch,0x90,sizeof(patch));
    std::memcpy(patch,jump,6);
    const auto target=reinterpret_cast<std::uintptr_t>(hook);
    std::memcpy(patch+6,&target,8);
    if(!edf::PatchCode(image+rva,kExplosionSig,patch,sizeof(patch))) {
        VirtualFree(code,0,MEM_RELEASE);
        return nullptr;
    }
    return code;
}

// Once, at Install, with GpuFxLayers on (the stock blast particles off for the session: turning the layers off needs
// a restart, the ini says so). Explosion_Big is optional on top: when its bytes differ it stays stock.
bool InstallLayers() noexcept {
    if(!Matches(kExplosionLoop,kLoopJe,sizeof(kLoopJe)) || !Matches(kExplosionExtra,kExtraJe,sizeof(kExtraJe)))return false;
    void* const code=HeadHook(kExplosion,reinterpret_cast<const void*>(&ExplosionHook));
    if(!code)return false;
    explosionNext=reinterpret_cast<ExplosionFn>(code);
    const bool loop=edf::PatchCode(image+kExplosionLoop,kLoopJe,kLoopJmp,sizeof(kLoopJe));
    const bool extra=edf::PatchCode(image+kExplosionExtra,kExtraJe,kExtraJmp,sizeof(kExtraJe));
    Log("GPUFX layers: Explosion hook EDF+%X, its particles off: loop %d, extra %d",kExplosion,loop,extra);
    if(Matches(kBigSmoke,kBigSmokeCall,sizeof(kBigSmokeCall)) && Matches(kBigPuffs,kBigPuffsCall,sizeof(kBigPuffsCall))) {
        if(void* const big=HeadHook(kExplosionBig,reinterpret_cast<const void*>(&ExplosionBigHook))) {
            explosionBigNext=reinterpret_cast<ExplosionBigFn>(big);
            const bool smoke=edf::PatchCode(image+kBigSmoke,kBigSmokeCall,kNop5,sizeof(kNop5));
            const bool puffs=edf::PatchCode(image+kBigPuffs,kBigPuffsCall,kNop5,sizeof(kNop5));
            Log("GPUFX layers: Explosion_Big hook EDF+%X, its particles off: smoke %d, puffs %d",kExplosionBig,smoke,puffs);
        }
    }
    if(!explosionBigNext)Log("GPUFX layers: Explosion_Big EDF+%X not as expected; it stays stock",kExplosionBig);
    return true;
}

// The camera's stand-in for the lod (the game thread has no camera): the player.
bool ListenerPosition(float* out) noexcept {
    const unsigned char* h=PlayerHuman();
    if(!h || !Readable(h,edf::kPosition+12))return false;
    std::memcpy(out,h+edf::kPosition,12);
    return true;
}

void SpawnLayers() noexcept {
    if(!layerDescs.Ready())layerDescs=RegisterLayers(*pool);
    if(!layerDescs.Ready())return;
    ShotEvent shots[kQueueMost];BlastEvent blasts[kQueueMost];
    AcquireSRWLockExclusive(&eventLock);
    const std::size_t ns=shotQueued,nb=blastQueued;
    std::memcpy(shots,shotQueue,ns*sizeof(ShotEvent));std::memcpy(blasts,blastQueue,nb*sizeof(BlastEvent));
    shotQueued=blastQueued=0;
    ReleaseSRWLockExclusive(&eventLock);
    if(ns==0 && nb==0)return;
    float eye[3]{};
    const bool haveEye=ListenerPosition(eye);
    auto lodAt=[&](const float* p){
        float d=0.0f;
        if(haveEye){const float x=p[0]-eye[0],y=p[1]-eye[1],z=p[2]-eye[2];d=std::sqrt(x*x+y*y+z*z);}
        return LayerLod(d,pool->Live(),kBudget);
    };
    // A barrage (many blasts in one frame): past the first 24, each one thinner, so a frame never spawns more than
    // about a frame's worth of the budget.
    unsigned made=0;
    for(std::size_t i=0;i<nb;++i) {
        float lod=lodAt(blasts[i].pos);
        if(i>=24)lod*=0.35f;
        made+=static_cast<unsigned>(SpawnBlast(*pool,layerDescs,layerRng,blasts[i],lod));
    }
    for(std::size_t i=0;i<ns;++i)made+=static_cast<unsigned>(SpawnShot(*pool,layerDescs,layerRng,shots[i],lodAt(shots[i].pos)));
    layerShots.fetch_add(static_cast<unsigned>(ns),std::memory_order_relaxed);
    layerBlasts.fetch_add(static_cast<unsigned>(nb),std::memory_order_relaxed);
    layerParticles.fetch_add(made,std::memory_order_relaxed);
}

void RegisterDescs() noexcept {
    if(deskFixed>=0)return;
    Desc fixed{};
    fixed.lifeMin=fixed.lifeMax=1e9f;fixed.sizeMin=fixed.sizeMax=1.0f;fixed.drag=0.0f;fixed.buoyancy=0.0f;fixed.windFollow=0.0f;
    fixed.spin=0.0f;fixed.frameRate=6.0f;fixed.soft=2.0f;
    fixed.color0[0]=fixed.color0[1]=fixed.color0[2]=0.7f;fixed.color1[0]=fixed.color1[1]=fixed.color1[2]=0.7f;
    fixed.alpha=Curve::Constant(0.95f);
    deskFixed=pool->Register(fixed);
    Desc puff{};
    puff.lifeMin=6.0f;puff.lifeMax=9.0f;puff.sizeMin=1.2f;puff.sizeMax=2.0f;
    puff.size=Curve{};puff.size.Add(0.0f,1.0f);puff.size.Add(1.0f,3.5f);
    puff.alpha=Curve{};puff.alpha.Add(0.0f,0.0f);puff.alpha.Add(0.08f,0.8f);puff.alpha.Add(0.6f,0.45f);puff.alpha.Add(1.0f,0.0f);
    puff.colorMix=Curve{};puff.colorMix.Add(0.0f,0.0f);puff.colorMix.Add(1.0f,1.0f);
    puff.color0[0]=0.35f;puff.color0[1]=0.33f;puff.color0[2]=0.3f;puff.color1[0]=puff.color1[1]=puff.color1[2]=0.62f;
    puff.drag=0.7f;puff.buoyancy=0.8f;puff.spin=0.5f;puff.soft=2.0f;
    deskPuff=pool->Register(puff);
}

bool PlaceAnchor() noexcept {
    const unsigned char* h=PlayerHuman();
    if(!h || !Readable(h,edf::kPosition+12))return false;
    float pos[3],fwd[3];
    std::memcpy(pos,h+edf::kPosition,sizeof(pos));
    std::memcpy(fwd,h+edf::kMatrix+0x20,sizeof(fwd));
    fwd[1]=0.0f;
    const float len=std::sqrt(fwd[0]*fwd[0]+fwd[2]*fwd[2]);
    if(!(len>1e-3f))return false;
    fwd[0]/=len;fwd[2]/=len;
    for(int a=0;a<3;++a)anchor[a]=pos[a]+fwd[a]*25.0f;
    ground=pos[1];anchor[1]=ground;
    right[0]=-fwd[2];right[1]=0.0f;right[2]=fwd[0];
    // Three still cards: one in the air (alignment, following the camera), one half under the ground (the soft cut,
    // occlusion by the terrain), one small at eye height off to the left (scale).
    const float zero[3]{};
    const float air[3]{anchor[0],ground+4.0f,anchor[2]};
    pool->Spawn(deskFixed,air,zero,testRng);
    const float half[3]{anchor[0]+right[0]*-6.0f,ground,anchor[2]+right[2]*-6.0f};
    pool->Spawn(deskFixed,half,zero,testRng);
    const float eye[3]{anchor[0]+right[0]*6.0f,ground+1.6f,anchor[2]+right[2]*6.0f};
    pool->Spawn(deskFixed,eye,zero,testRng);
    Log("GPUFX test anchor %.1f %.1f %.1f (player %.1f %.1f %.1f, forward %.2f %.2f)",anchor[0],anchor[1],anchor[2],pos[0],pos[1],pos[2],fwd[0],fwd[2]);
    return true;
}

void TickNative() noexcept {
    const ULONGLONG now=GameMs();
    const float dt=lastMs && now>lastMs ? (std::min)(0.1f,static_cast<float>(now-lastMs)*0.001f) : 0.0f;
    lastMs=now;
    if(GamePaused())return;
    if(!pool)pool=std::make_unique<Pool<kBudget>>();
    if(optTest.load(std::memory_order_relaxed)) {
        RegisterDescs();
        if(!anchored && PlaceAnchor()) {
            anchored=true;
            AcquireSRWLockExclusive(&publishLock);
            std::memcpy(anchorShared,anchor,sizeof(anchor));
            ReleaseSRWLockExclusive(&publishLock);
            anchorReady.store(true,std::memory_order_release);
        }
        if(anchored && dt>0.0f) {
            // The puff column (8 m to the right of the air card): 12 puffs a second rising and widening.
            spawnDebt+=dt*12.0f;
            while(spawnDebt>=1.0f) {
                spawnDebt-=1.0f;
                const float at[3]{anchor[0]+right[0]*14.0f+testRng.Range(-0.6f,0.6f),ground+0.5f,anchor[2]+right[2]*14.0f+testRng.Range(-0.6f,0.6f)};
                const float vel[3]{testRng.Range(-1.0f,1.0f),testRng.Range(4.0f,7.0f),testRng.Range(-1.0f,1.0f)};
                pool->Spawn(deskPuff,at,vel,testRng);
            }
        }
    }
    // A light steady breeze: the stock has no wind field the smoke could follow.
    pool->SetWind(1.5f,0.0f,0.5f);
    if(optLayers.load(std::memory_order_relaxed))SpawnLayers();
    if(dt>0.0f)pool->Step(dt);
    if(staging.size()!=static_cast<std::size_t>(kBudget))staging.resize(kBudget);
    const int n=pool->Build(staging.data(),kBudget,kFlipFrames);
    poolDropped.store(pool->Dropped(),std::memory_order_relaxed);
    std::vector<Instance> next(staging.begin(),staging.begin()+n);
    AcquireSRWLockExclusive(&publishLock);
    published.swap(next);
    ReleaseSRWLockExclusive(&publishLock);
}

// Our smoke's exposure from the scene: the colour target's median luminance before our draw (Stats::sceneLuma) over
// what a mid-grey (albedo 0.25) ground would show under the light block's own sun (at 0.6 of full) and ambient. The
// stock renderer's HDR scale is not ours to know; the ratio says how bright its lit ground came out, smoothed over
// about a second, clamped to a sane range.
float exposure=1.0f;
void Calibrate(const Stats& st,const ViewParams& vp) noexcept {
    if(!(st.sceneLuma>0.0f))return;
    auto y=[](const float c[3]){return 0.2126f*c[0]+0.7152f*c[1]+0.0722f*c[2];};
    const float ref=0.25f*(0.6f*y(vp.sunColor)+y(vp.ambient));
    if(!(ref>1e-4f))return;
    const float k=std::clamp(st.sceneLuma/ref,0.35f,2.5f);
    exposure+=(k-exposure)*0.03f;
}

void LogLight() noexcept {
    __try {
        const unsigned char* r=At<const unsigned char*>(image,kRendererPtr);
        if(!r || !Readable(r+0xB00,0x140))return;
        for(unsigned o=0xB00;o<0xC40;o+=0x40) {
            const float* f=reinterpret_cast<const float*>(r+o);
            Log("GPUFX light +%03X: %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f",o,
                f[0],f[1],f[2],f[3],f[4],f[5],f[6],f[7],f[8],f[9],f[10],f[11],f[12],f[13],f[14],f[15]);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

} // namespace

bool Install() noexcept {
    if(installed.load(std::memory_order_acquire))return true;
    PublishOptions();
    if(HANDLE t=CreateThread(nullptr,0,&FlipbookThread,nullptr,0,nullptr))CloseHandle(t);
    bool ok=false;
    __try {
        void** slot=reinterpret_cast<void**>(image+kParticleDrawSlot);
        if(Matches(kParticleDraw,kParticleDrawCode,sizeof(kParticleDrawCode)) && *slot==image+kParticleDraw)
            ok=PatchVtableSlot(slot,image+kParticleDraw,reinterpret_cast<void*>(&ParticleDrawHook));
        // Who else points at the stock draw (other vtables sharing it): logged, for the takeover's coverage.
        unsigned others=0;
        for(unsigned rva=0x1700000;rva<0x1A00000 && rva+8<=0x22CE000;rva+=8) {
            if(rva==kParticleDrawSlot)continue;
            if(Readable(image+rva,8) && *reinterpret_cast<void**>(image+rva)==image+kParticleDraw) {
                if(others<16)Log("GPUFX stock particle draw also at EDF+%X",rva);
                ++others;
            }
        }
        const bool core=InstallCoreDraw();
        Log("GPUFX installed: stock particle draw slot=%d (%u other references), core draw EDF+%X=%d",ok,others,kCoreDraw,core);
        const Config& c=Cfg();
        if(c.enabled && c.gpuFx && c.gpuFxLayers) {
            layersInstalled=InstallLayers();
            if(!layersInstalled)Log("GPUFX layers: Explosion EDF+%X not as expected; the layered emitters stay off",kExplosion);
        }
        PublishOptions();
    } __except(EXCEPTION_EXECUTE_HANDLER) {Log("GPUFX profile fault; stock particles untouched");ok=false;}
    installed.store(true,std::memory_order_release);
    return ok;
}

void Tick() noexcept {
    const ULONGLONG frame=GameFrame();
    if(ticked==frame)return;
    ticked=frame;
    PublishOptions();
    if(!optEnabled.load(std::memory_order_relaxed))return;
    try { TickNative(); } catch(...) {}
}

bool ExplosionHooked() noexcept { return explosionNext!=nullptr; }
bool ExplosionBigHooked() noexcept { return explosionBigNext!=nullptr; }

void* ShotBegin(unsigned char* weapon) noexcept {
    if(!optLayers.load(std::memory_order_relaxed))return nullptr;
    __try {
        if(!Readable(weapon,kWeaponBlast+4,true))return nullptr;
        const float blast=At<float>(weapon,kWeaponBlast);
        if(!(blast>=kCannonBlast) || !KnownVehicle(At<const void*>(weapon,kWeaponOwner)))return nullptr;
        void* effect=At<void*>(weapon,kWeaponMuzzleEffect);
        *reinterpret_cast<void**>(weapon+kWeaponMuzzleEffect)=nullptr;   // ours draws a vehicle gun's flash and smoke
        return effect;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

void ShotEnd(unsigned char* weapon,unsigned muzzle,void* muzzleEffect) noexcept {
    __try {
        if(muzzleEffect)*reinterpret_cast<void**>(weapon+kWeaponMuzzleEffect)=muzzleEffect;
        if(!optLayers.load(std::memory_order_relaxed) || !Readable(weapon,kWeaponBlast+4))return;
        const float blast=At<float>(weapon,kWeaponBlast);
        if(!(blast>=kCannonBlast))return;
        const unsigned char* list=At<const unsigned char*>(weapon,kWeaponMuzzles);
        const std::uint64_t count=At<std::uint64_t>(weapon,kWeaponMuzzleCount);
        if(count==0 || count>64 || !Readable(list,count*kMuzzleStride))return;
        const unsigned char* m=list+(muzzle%count)*kMuzzleStride;
        ShotEvent e{};
        std::memcpy(e.pos,m+kMuzzlePosition,sizeof(e.pos));std::memcpy(e.dir,m+kMuzzleForward,sizeof(e.dir));
        const float len=std::sqrt(e.dir[0]*e.dir[0]+e.dir[1]*e.dir[1]+e.dir[2]*e.dir[2]);
        if(!(len>1e-3f) || !std::isfinite(e.pos[0]) || !std::isfinite(e.pos[1]) || !std::isfinite(e.pos[2]))return;
        for(float& v:e.dir)v/=len;
        const unsigned char* owner=At<const unsigned char*>(weapon,kWeaponOwner);
        e.vehicle=KnownVehicle(owner);
        e.ground=Readable(owner,edf::kPosition+12) ? At<float>(owner,edf::kPosition+4) : e.pos[1]-10.0f;
        e.blast=blast;
        // A fast gun: the full layers once per kShotRefire, a thin shot (a fifth) between.
        const ULONGLONG now=GameMs();
        RecentShot* slot=&recentShots[0];
        for(auto& r:recentShots) {
            if(r.weapon==weapon){slot=&r;break;}
            if(r.ms<slot->ms)slot=&r;
        }
        const bool thin=slot->weapon==weapon && now-slot->ms<static_cast<ULONGLONG>(kShotRefire*1000.0f);
        if(!thin){slot->weapon=weapon;slot->ms=now;}
        if(thin)e.blast=(std::max)(kCannonBlast,e.blast*0.2f);
        AcquireSRWLockExclusive(&eventLock);
        if(shotQueued<kQueueMost)shotQueue[shotQueued++]=e;
        else layerQueueFull.fetch_add(1,std::memory_order_relaxed);
        ReleaseSRWLockExclusive(&eventLock);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

void Reset() noexcept {
    AcquireSRWLockExclusive(&eventLock);
    shotQueued=blastQueued=0;
    ReleaseSRWLockExclusive(&eventLock);
    for(auto& r:recentShots)r=RecentShot{};
    if(pool)pool->Clear();
    poolDropped.store(0,std::memory_order_relaxed);
    anchored=false;spawnDebt=0.0f;lastMs=0;ticked=~ULONGLONG(0);
    anchorReady.store(false,std::memory_order_release);
    AcquireSRWLockExclusive(&publishLock);
    published.clear();
    ReleaseSRWLockExclusive(&publishLock);
    probeReset.store(true,std::memory_order_release);
}

void WorldBegin(ID3D11DeviceContext*,const void* view) noexcept {
    if(!installed.load(std::memory_order_acquire))return;
    currentView=view;inWorld=MainView(view);observed=false;
    if(inWorld)std::memcpy(splitView,static_cast<const unsigned char*>(view)+0x20,sizeof(splitView));
    observedDepth.Reset();observedFormat=0;fade.copiedThisWorld=false;
    litCapture.Reset();
    nativeInstances.clear();nativeBatches.clear();
}

void ObserveSurface(ID3D11DeviceContext* context,int pass) noexcept {
    if(!inWorld || observed || pass!=1 || !context)return;
    observed=true;
    ComPtr<ID3D11DepthStencilView> dsv;
    context->OMGetRenderTargets(0,nullptr,&dsv);
    if(!dsv)return;
    D3D11_DEPTH_STENCIL_VIEW_DESC d{};dsv->GetDesc(&d);
    if(d.ViewDimension!=D3D11_DSV_DIMENSION_TEXTURE2D || d.Texture2D.MipSlice!=0)return;
    ComPtr<ID3D11Resource> res;dsv->GetResource(&res);
    if(FAILED(res.As(&observedDepth))){observedDepth.Reset();return;}
    observedFormat=static_cast<unsigned>(d.Format);
}

void WorldEnd(ID3D11DeviceContext* context,const void* view) noexcept {
    if(!installed.load(std::memory_order_acquire) || view!=currentView || !inWorld) {inWorld=false;return;}
    inWorld=false;
    if(probeReset.exchange(false,std::memory_order_acq_rel)){probe=Probe{};}
    const bool enabled=optEnabled.load(std::memory_order_relaxed);
    const bool wantProbe=optProbe.load(std::memory_order_relaxed);
    if(!enabled || !context){nativeInstances.clear();nativeBatches.clear();return;}
    const double t0=NowMs();
    if(!flipGiven && flipReady.load(std::memory_order_acquire)){renderer.SetFlipbook(flipPixels.data());flipGiven=true;}
    try {
        AcquireSRWLockShared(&publishLock);
        renderOwn.assign(published.begin(),published.end());
        float anchorNow[3];std::memcpy(anchorNow,anchorShared,sizeof(anchorNow));
        ReleaseSRWLockShared(&publishLock);
        // The depth: what is bound at the world's end, else the scene depth seen in this view's opaque pass.
        ComPtr<ID3D11DepthStencilView> dsv;
        context->OMGetRenderTargets(0,nullptr,&dsv);
        ComPtr<ID3D11Texture2D> depth;unsigned format=0;const char* source="none";
        if(dsv) {
            D3D11_DEPTH_STENCIL_VIEW_DESC d{};dsv->GetDesc(&d);ComPtr<ID3D11Resource> res;dsv->GetResource(&res);
            if(d.ViewDimension==D3D11_DSV_DIMENSION_TEXTURE2D && SUCCEEDED(res.As(&depth))){format=static_cast<unsigned>(d.Format);source="bound";}
        }
        if(!depth && observedDepth){depth=observedDepth;format=observedFormat;source="observed";}
        ViewParams vp{};
        ReadMatrices(view,vp);
        ReadLight(vp);
        fade.Read(context);
        vp.fadeMul=fade.mul;vp.fadeAdd=fade.add;
        vp.exposure=exposure;
        vp.lights=litCapture.Lights();
        vp.rightHalf=optNative.load(std::memory_order_relaxed)==3;
        batches.clear();
        if(!renderOwn.empty() && flipGiven) {
            Batch b{};b.instances=renderOwn.data();b.count=static_cast<int>(renderOwn.size());
            batches.push_back(b);
        }
        for(const auto& nb:nativeBatches) {
            Batch b{};b.texture=nb.texture.Get();b.kind=nb.kind;b.frames=nb.frames;
            b.gridX=b.gridY=nb.grid;b.frames=nb.frames;b.cornerScale=0.70710678f;b.uInset=0.0005f;b.sorted=false;
            b.instances=nativeInstances.data()+nb.start;b.count=nb.count;
            b.litSet=nb.litSet;std::memcpy(b.litWeights,nb.litWeights,sizeof(b.litWeights));
            batches.push_back(b);
        }
        Stats st{};
        bool drew=false;
        if(!batches.empty() && depth)drew=renderer.Draw(context,depth.Get(),format,vp,batches.data(),static_cast<int>(batches.size()),&st);
        if(drew)Calibrate(st,vp);
        nativeInstances.clear();nativeBatches.clear();litCapture.Reset();
        const double ms=NowMs()-t0;
        probe.cpuMs+=ms;++probe.cpuSamples;
        for(int a=0;a<2;++a){const float j=vp.proj[8+a];probe.jitterMin[a]=(std::min)(probe.jitterMin[a],j);probe.jitterMax[a]=(std::max)(probe.jitterMax[a],j);}
        if(!wantProbe)return;
        if(!probe.lightLogged){probe.lightLogged=true;LogLight();}
        const ULONGLONG now=GetTickCount64();
        if(probe.loggedAt && now-probe.loggedAt<5000)return;
        probe.loggedAt=now;
        Log("GPUFX view=%p idx=%d rt %ux%u fmt=%u samples=%u vp=%.0f,%.0f %.0fx%.0f depth=%s fmt=%u reversed=%d negZ=%d drawn=%d batches=%d%s%s "
            "cpu=%.3f ms gpu=%.3f ms",view,At<int>(view,0),st.width,st.height,st.colorFormat,st.samples,st.viewport[0],st.viewport[1],st.viewport[2],
            st.viewport[3],source,format,st.reversed,st.lookNegZ,st.drawn,st.batches,drew ? "" : " refused: ",drew ? "" : (st.refused ? st.refused : "nothing to draw"),
            probe.cpuSamples ? probe.cpuMs/probe.cpuSamples : 0.0,st.gpuMs);
        const float* P=vp.proj;const float* V=vp.view;
        Log("GPUFX   P=[%.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.6f %.4f | %.4f %.4f %.6f %.4f] jitter x %.6f..%.6f y %.6f..%.6f",
            P[0],P[1],P[2],P[3],P[4],P[5],P[6],P[7],P[8],P[9],P[10],P[11],P[12],P[13],P[14],P[15],
            probe.jitterMin[0],probe.jitterMax[0],probe.jitterMin[1],probe.jitterMax[1]);
        Log("GPUFX   V=[%.3f %.3f %.3f | %.3f %.3f %.3f | %.3f %.3f %.3f | T %.2f %.2f %.2f]",V[0],V[1],V[2],V[4],V[5],V[6],V[8],V[9],V[10],V[12],V[13],V[14]);
        if(anchorReady.load(std::memory_order_acquire) && st.viewport[2]>0.0f) {
            const float air[3]{anchorNow[0],anchorNow[1]+4.0f,anchorNow[2]};
            float v4[4],c4[4];TransformPoint(air,vp.view,v4);
            for(int c=0;c<4;++c)c4[c]=v4[0]*P[c]+v4[1]*P[4+c]+v4[2]*P[8+c]+v4[3]*P[12+c];
            if(c4[3]!=0.0f)Log("GPUFX   test air card -> view z=%.2f, pixel %.0f,%.0f (ndc %.3f %.3f %.4f)",v4[2],
                st.viewport[0]+(c4[0]/c4[3]*0.5f+0.5f)*st.viewport[2],st.viewport[1]+(0.5f-c4[1]/c4[3]*0.5f)*st.viewport[3],c4[0]/c4[3],c4[1]/c4[3],c4[2]/c4[3]);
        }
        Log("GPUFX   own live=%d/%d dropped=%u; stock draws=%u (pass2 %u, pass5 %u) core draws=%u taken over=%u (cores %u) refused=%u "
            "(lit %u blend %u grid %u) outside the world view=%u chain=%d",
            static_cast<int>(renderOwn.size()),kBudget,poolDropped.load(std::memory_order_relaxed),probe.nativeCalls,probe.byPass[2],probe.byPass[5],
            probe.coreCalls,probe.nativeReplaced,probe.coreReplaced,probe.nativeRefused,probe.refusedLit,probe.refusedBlend,probe.refusedGrid,
            probe.nativeOutside,textureChain.load(std::memory_order_relaxed));
        Log("GPUFX   lit taken over=%u (set 0xC08 %u); lighting captured in %u views, incomplete %u",probe.litReplaced,probe.litSetB,
            probe.litCaptures,probe.litCaptureFailed);
        Log("GPUFX   layers %s: shots %u blasts %u -> particles %u, queue full %u",optLayers.load(std::memory_order_relaxed) ? "on" : "off",
            layerShots.load(std::memory_order_relaxed),layerBlasts.load(std::memory_order_relaxed),
            layerParticles.load(std::memory_order_relaxed),layerQueueFull.load(std::memory_order_relaxed));
        Log("GPUFX   scene luma median %.3f max %.3f -> exposure %.3f; stock fade (b0 %u bytes, %s) mul %.6f add %.4f",st.sceneLuma,st.sceneLumaMax,
            exposure,fade.bytes,fade.known ? "read" : "not yet",fade.mul,fade.add);
        probe.cpuMs=0.0;probe.cpuSamples=0;
        for(int a=0;a<2;++a){probe.jitterMin[a]=1e9f;probe.jitterMax[a]=-1e9f;}
    } catch(...) { nativeInstances.clear();nativeBatches.clear(); }
}

} // namespace crew::gpufx
