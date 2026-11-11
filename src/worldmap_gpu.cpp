// See worldmap_gpu.h.
#include "worldmap_gpu.h"
#include "worldmap_logic.h"
#include "worldmap_tiles.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cmath>
#include <cstring>
#include <new>
#include <vector>

namespace crew::worldmapgpu {
namespace {

using Microsoft::WRL::ComPtr;

// One quad: its corners in clip space, its texture rectangle, a colour (the texture's multiplier when textured).
constexpr char kShaders[]=R"hlsl(
cbuffer QuadBlock : register(b0) {
    float4 Corners01;   // top-left xy, top-right xy
    float4 Corners23;   // bottom-left xy, bottom-right xy
    float4 Texels;      // u0 v0 u1 v1
    float4 Colour;
    float4 Mode;        // x: 1 textured
};
Texture2D<float4> Picture : register(t0);
SamplerState Linear : register(s0);
struct Corner { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Corner QuadVertex(uint id : SV_VertexID) {
    Corner c;
    float2 p = id == 0 ? Corners01.xy : id == 1 ? Corners01.zw : id == 2 ? Corners23.xy : Corners23.zw;
    c.position = float4(p, 0.0, 1.0);
    c.uv = float2((id & 1) ? Texels.z : Texels.x, (id & 2) ? Texels.w : Texels.y);
    return c;
}
float4 QuadPixel(Corner c) : SV_Target0 {
    return Mode.x > 0.5 ? Picture.SampleLevel(Linear, c.uv, 0.0) * Colour : Colour;
}
)hlsl";

struct QuadBlock {
    float corners[8];
    float texels[4];
    float colour[4];
    float mode[4];
};
static_assert(sizeof(QuadBlock)%16==0,"a constant buffer is a multiple of 16 bytes");

using CompileFunction=HRESULT(WINAPI*)(LPCVOID,SIZE_T,LPCSTR,const D3D_SHADER_MACRO*,ID3DInclude*,LPCSTR,LPCSTR,UINT,UINT,
                                       ID3DBlob**,ID3DBlob**);

bool Srgb(DXGI_FORMAT f) noexcept {
    return f==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || f==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || f==DXGI_FORMAT_B8G8R8X8_UNORM_SRGB ||
           f==DXGI_FORMAT_BC1_UNORM_SRGB || f==DXGI_FORMAT_BC2_UNORM_SRGB || f==DXGI_FORMAT_BC3_UNORM_SRGB ||
           f==DXGI_FORMAT_BC7_UNORM_SRGB;
}

}  // namespace

struct Renderer::Impl {
    // The device's objects (made by Make, kept while the device is the same and alive).
    HMODULE compilerModule=nullptr;
    CompileFunction compile=nullptr;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext1> context1;
    ComPtr<ID3DDeviceContextState> privateState;
    ComPtr<ID3D11VertexShader> vertexShader;
    ComPtr<ID3D11PixelShader> pixelShader;
    ComPtr<ID3D11Buffer> block;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11BlendState> blend;
    ComPtr<ID3D11DepthStencilState> noDepth;
    ComPtr<ID3D11RasterizerState> raster;
    const char* why=nullptr;
    bool broken=false;

    // This frame's (between Begin and End).
    ID3D11DeviceContext* context=nullptr;
    ComPtr<ID3DDeviceContextState> nativeState;
    ComPtr<ID3D11RenderTargetView> target;
    Viewport viewport{};
    bool srgb=false;
    int targetW=0,targetH=0;

    // The tiles. srgbTiles: the views' format (the target's colour space at upload: a tile is read as it is written).
    struct Tile { int index; int level; unsigned used; ComPtr<ID3D11ShaderResourceView> view; };
    std::vector<Tile> tiles;
    bool srgbTiles=false;

    ~Impl() noexcept {
        tiles.clear();
        if(compilerModule)FreeLibrary(compilerModule);
    }
    bool Fail(const char* what) noexcept { why=what;broken=true;return false; }

    bool Compile(const char* entry,const char* profile,ComPtr<ID3DBlob>& code) noexcept {
        ComPtr<ID3DBlob> errors;
        return SUCCEEDED(compile(kShaders,sizeof(kShaders)-1,"worldmap_gpu",nullptr,nullptr,entry,profile,
                                 D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors));
    }

    bool Make(ID3D11DeviceContext* value) noexcept {
        value->GetDevice(&device);
        if(!device || device->GetFeatureLevel()<D3D_FEATURE_LEVEL_10_0 || FAILED(device->GetDeviceRemovedReason()))
            return Fail("no usable device (feature level below 10.0 or removed)");
        if(FAILED(value->QueryInterface(IID_PPV_ARGS(&context1))))return Fail("no D3D11.1 context (SwapDeviceContextState)");
        ComPtr<ID3D11Device1> device1;
        if(FAILED(device.As(&device1)))return Fail("no D3D11.1 device");
        const D3D_FEATURE_LEVEL level=device->GetFeatureLevel();
        const UINT flags=(device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED)
            ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED : 0;
        if(FAILED(device1->CreateDeviceContextState(flags,&level,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device1),nullptr,
                                                    &privateState)))return Fail("CreateDeviceContextState failed");
        compilerModule=LoadLibraryExW(L"d3dcompiler_47.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!compilerModule)return Fail("d3dcompiler_47.dll not found in System32");
        const FARPROC address=GetProcAddress(compilerModule,"D3DCompile");
        static_assert(sizeof(address)==sizeof(compile),"Windows function pointer size");
        std::memcpy(&compile,&address,sizeof(compile));
        if(!compile)return Fail("D3DCompile not exported");
        const bool five=level>=D3D_FEATURE_LEVEL_11_0;
        ComPtr<ID3DBlob> vs,ps;
        if(!Compile("QuadVertex",five ? "vs_5_0" : "vs_4_0",vs) || !Compile("QuadPixel",five ? "ps_5_0" : "ps_4_0",ps))
            return Fail("the shaders did not compile");
        if(FAILED(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertexShader)) ||
           FAILED(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&pixelShader)))
            return Fail("the shaders were refused");
        D3D11_BUFFER_DESC blockDesc{};
        blockDesc.ByteWidth=sizeof(QuadBlock);
        blockDesc.Usage=D3D11_USAGE_DYNAMIC;
        blockDesc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        blockDesc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        D3D11_SAMPLER_DESC samplerDesc{};
        samplerDesc.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU=samplerDesc.AddressV=samplerDesc.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.ComparisonFunc=D3D11_COMPARISON_NEVER;
        samplerDesc.MaxLOD=D3D11_FLOAT32_MAX;
        // Straight alpha over what is there; the target's alpha made opaque where the map is (a UI target composited
        // by its alpha shows the map, a back buffer ignores it).
        D3D11_BLEND_DESC blendDesc{};
        auto& rt=blendDesc.RenderTarget[0];
        rt.BlendEnable=TRUE;
        rt.SrcBlend=D3D11_BLEND_SRC_ALPHA;rt.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;rt.BlendOp=D3D11_BLEND_OP_ADD;
        rt.SrcBlendAlpha=D3D11_BLEND_ONE;rt.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;rt.BlendOpAlpha=D3D11_BLEND_OP_ADD;
        rt.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        D3D11_DEPTH_STENCIL_DESC depthDesc{};
        depthDesc.DepthEnable=FALSE;
        depthDesc.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
        depthDesc.DepthFunc=D3D11_COMPARISON_ALWAYS;
        D3D11_RASTERIZER_DESC rasterDesc{};
        rasterDesc.FillMode=D3D11_FILL_SOLID;
        rasterDesc.CullMode=D3D11_CULL_NONE;
        rasterDesc.DepthClipEnable=TRUE;
        rasterDesc.ScissorEnable=TRUE;
        if(FAILED(device->CreateBuffer(&blockDesc,nullptr,&block)) ||
           FAILED(device->CreateSamplerState(&samplerDesc,&sampler)) ||
           FAILED(device->CreateBlendState(&blendDesc,&blend)) ||
           FAILED(device->CreateDepthStencilState(&depthDesc,&noDepth)) ||
           FAILED(device->CreateRasterizerState(&rasterDesc,&raster)))return Fail("a pipeline state was refused");
        return true;
    }

    // Design units to clip space (the viewport is the select's: the layout stretched over it).
    static float ClipX(float x) noexcept { return x/960.f-1.f; }
    static float ClipY(float y) noexcept { return 1.f-y/540.f; }

    void Draw(const float (&xy)[4][2],const float* texels,std::uint32_t argb,ID3D11ShaderResourceView* view) noexcept {
        if(!context)return;
        D3D11_MAPPED_SUBRESOURCE m{};
        if(FAILED(context->Map(block.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&m)))return;
        QuadBlock q{};
        for(int i=0;i<4;++i){q.corners[2*i]=ClipX(xy[i][0]);q.corners[2*i+1]=ClipY(xy[i][1]);}
        if(texels)std::memcpy(q.texels,texels,sizeof q.texels);
        q.colour[0]=static_cast<float>((argb>>16)&0xFF)/255.f;
        q.colour[1]=static_cast<float>((argb>>8)&0xFF)/255.f;
        q.colour[2]=static_cast<float>(argb&0xFF)/255.f;
        q.colour[3]=static_cast<float>((argb>>24)&0xFF)/255.f;
        q.mode[0]=view ? 1.f : 0.f;
        std::memcpy(m.pData,&q,sizeof q);
        context->Unmap(block.Get(),0);
        ID3D11ShaderResourceView* views[1]={view};
        context->PSSetShaderResources(0,1,views);
        context->Draw(4,0);
    }

    Tile* Find(int index) noexcept {
        for(Tile& t:tiles)if(t.index==index)return &t;
        return nullptr;
    }
};

Renderer::Renderer() noexcept=default;
Renderer::~Renderer() noexcept { End(); }

Renderer::Begun Renderer::Begin(ID3D11DeviceContext* context,const Viewport& viewport) noexcept {
    if(!context || context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE) {
        if(!impl_)impl_.reset(new(std::nothrow) Impl());
        if(impl_){impl_->why="the select's draw has no immediate D3D11 context";impl_->broken=true;}
        return Begun::broken;
    }
    if(!(viewport.w>=1.f) || !(viewport.h>=1.f) || viewport.w>16384.f || viewport.h>16384.f)return Begun::skip;
    {
        ComPtr<ID3D11Device> device;
        context->GetDevice(&device);
        if(impl_ && !impl_->broken && (impl_->device.Get()!=device.Get() || FAILED(impl_->device->GetDeviceRemovedReason())))
            impl_.reset();   // another device: everything made again
    }
    if(!impl_) {
        impl_.reset(new(std::nothrow) Impl());
        if(!impl_)return Begun::broken;
        if(!impl_->Make(context))return Begun::broken;
    }
    Impl& s=*impl_;
    if(s.broken)return Begun::broken;
    if(s.context)End();
    // The native UI draw's colour target (RTV 0), and its size.
    ID3D11RenderTargetView* bound=nullptr;
    context->OMGetRenderTargets(1,&bound,nullptr);
    ComPtr<ID3D11RenderTargetView> target;
    target.Attach(bound);
    if(!target)return Begun::skip;
    D3D11_RENDER_TARGET_VIEW_DESC targetDesc{};
    target->GetDesc(&targetDesc);
    ComPtr<ID3D11Resource> resource;
    target->GetResource(&resource);
    ComPtr<ID3D11Texture2D> texture;
    if(FAILED(resource.As(&texture)))return Begun::skip;
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    s.targetW=static_cast<int>(desc.Width);
    s.targetH=static_cast<int>(desc.Height);
    s.srgb=Srgb(targetDesc.Format);
    if(s.srgb!=s.srgbTiles){s.tiles.clear();s.srgbTiles=s.srgb;}
    s.target=std::move(target);
    s.viewport=viewport;
    s.context=context;
    s.context1->SwapDeviceContextState(s.privateState.Get(),&s.nativeState);
    context->ClearState();
    ID3D11RenderTargetView* targets[1]={s.target.Get()};
    context->OMSetRenderTargets(1,targets,nullptr);
    context->OMSetBlendState(s.blend.Get(),nullptr,0xFFFFFFFFu);
    context->OMSetDepthStencilState(s.noDepth.Get(),0);
    context->RSSetState(s.raster.Get());
    const D3D11_VIEWPORT vp{viewport.x,viewport.y,viewport.w,viewport.h,0.f,1.f};
    context->RSSetViewports(1,&vp);
    const D3D11_RECT scissor{static_cast<LONG>(viewport.x),static_cast<LONG>(viewport.y),
                             static_cast<LONG>(viewport.x+viewport.w),static_cast<LONG>(viewport.y+viewport.h)};
    context->RSSetScissorRects(1,&scissor);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    context->VSSetShader(s.vertexShader.Get(),nullptr,0);
    context->PSSetShader(s.pixelShader.Get(),nullptr,0);
    ID3D11Buffer* blocks[1]={s.block.Get()};
    context->VSSetConstantBuffers(0,1,blocks);
    context->PSSetConstantBuffers(0,1,blocks);
    ID3D11SamplerState* samplers[1]={s.sampler.Get()};
    context->PSSetSamplers(0,1,samplers);
    return Begun::ok;
}

void Renderer::End() noexcept {
    if(!impl_ || !impl_->context)return;
    Impl& s=*impl_;
    // Nothing native stays referenced by the private state; then the native state back whole.
    s.context->ClearState();
    s.context1->SwapDeviceContextState(s.nativeState.Get(),nullptr);
    s.nativeState.Reset();
    s.target.Reset();
    s.context=nullptr;
}

const char* Renderer::Why() const noexcept { return impl_ && impl_->why ? impl_->why : "not begun"; }
int Renderer::TargetWidth() const noexcept { return impl_ ? impl_->targetW : 0; }
int Renderer::TargetHeight() const noexcept { return impl_ ? impl_->targetH : 0; }

bool Renderer::Has(int index) const noexcept {
    if(!impl_)return false;
    for(const Impl::Tile& t:impl_->tiles)if(t.index==index)return true;
    return false;
}
int Renderer::Cached() const noexcept { return impl_ ? static_cast<int>(impl_->tiles.size()) : 0; }

bool Renderer::Upload(const worldmap::TilePixels& tile,int level,unsigned frame) noexcept {
    if(!impl_ || !impl_->context || tile.w<=0 || tile.h<=0 ||
       tile.bgra.size()!=static_cast<std::size_t>(tile.w)*static_cast<std::size_t>(tile.h))return false;
    Impl& s=*impl_;
    if(s.Find(tile.index))return true;
    std::size_t slot=s.tiles.size();
    if(s.tiles.size()>=static_cast<std::size_t>(kCacheTiles)) {
        int levels[kCacheTiles];
        unsigned used[kCacheTiles];
        const int n=static_cast<int>(s.tiles.size()<static_cast<std::size_t>(kCacheTiles) ? s.tiles.size() : kCacheTiles);
        for(int i=0;i<n;++i){levels[i]=s.tiles[static_cast<std::size_t>(i)].level;used[i]=s.tiles[static_cast<std::size_t>(i)].used;}
        const int victim=worldmap::Victim(levels,used,n,frame);
        if(victim<0)return false;
        slot=static_cast<std::size_t>(victim);
    }
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width=static_cast<UINT>(tile.w);
    desc.Height=static_cast<UINT>(tile.h);
    desc.MipLevels=1;
    desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_B8G8R8A8_TYPELESS;
    desc.SampleDesc.Count=1;
    desc.Usage=D3D11_USAGE_IMMUTABLE;
    desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA data{tile.bgra.data(),static_cast<UINT>(tile.w)*4u,0};
    ComPtr<ID3D11Texture2D> texture;
    if(FAILED(s.device->CreateTexture2D(&desc,&data,&texture)))return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
    viewDesc.Format=s.srgbTiles ? DXGI_FORMAT_B8G8R8A8_UNORM_SRGB : DXGI_FORMAT_B8G8R8A8_UNORM;
    viewDesc.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;
    viewDesc.Texture2D.MipLevels=1;
    ComPtr<ID3D11ShaderResourceView> view;
    if(FAILED(s.device->CreateShaderResourceView(texture.Get(),&viewDesc,&view)))return false;
    Impl::Tile t{tile.index,level,frame,std::move(view)};
    if(slot<s.tiles.size())s.tiles[slot]=std::move(t);
    else s.tiles.push_back(std::move(t));
    return true;
}

void Renderer::DrawTile(int index,float x0,float y0,float x1,float y1,float u0,float v0,float u1,float v1,unsigned frame) noexcept {
    if(!impl_)return;
    Impl::Tile* t=impl_->Find(index);
    if(!t)return;
    t->used=frame;
    Image(t->view.Get(),x0,y0,x1,y1,u0,v0,u1,v1,0xFFFFFFFFu);
}

void Renderer::Reset() noexcept {
    End();
    impl_.reset();
}

void Renderer::Quad(const float (&xy)[4][2],std::uint32_t argb) noexcept {
    if(impl_)impl_->Draw(xy,nullptr,argb,nullptr);
}
void Renderer::Rect(float x0,float y0,float x1,float y1,std::uint32_t argb) noexcept {
    const float xy[4][2]={{x0,y0},{x1,y0},{x0,y1},{x1,y1}};
    Quad(xy,argb);
}
void Renderer::Line(float x0,float y0,float x1,float y1,float width,std::uint32_t argb) noexcept {
    const float dx=x1-x0,dy=y1-y0,len=std::sqrt(dx*dx+dy*dy);
    if(!(len>0.f))return;
    // Half the width across, half along (square ends).
    const float h=width*0.5f,nx=-dy/len*h,ny=dx/len*h,ex=dx/len*h,ey=dy/len*h;
    const float xy[4][2]={{x0-ex+nx,y0-ey+ny},{x1+ex+nx,y1+ey+ny},{x0-ex-nx,y0-ey-ny},{x1+ex-nx,y1+ey-ny}};
    Quad(xy,argb);
}
void Renderer::Image(ID3D11ShaderResourceView* texture,float x0,float y0,float x1,float y1,float u0,float v0,float u1,
                     float v1,std::uint32_t tint) noexcept {
    if(!impl_ || !texture)return;
    const float xy[4][2]={{x0,y0},{x1,y0},{x0,y1},{x1,y1}};
    const float texels[4]={u0,v0,u1,v1};
    impl_->Draw(xy,texels,tint,texture);
}
ID3D11Device* Renderer::Device() const noexcept { return impl_ && impl_->context ? impl_->device.Get() : nullptr; }

}  // namespace crew::worldmapgpu
