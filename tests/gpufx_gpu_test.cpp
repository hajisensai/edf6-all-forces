// Real Direct3D 11 (WARP) run of the GPU particle renderer (src/gpufx/gpufx_gpu.cpp): the shaders compile, a lit smoke
// card lands where the projection puts it, scene depth in front hides it, depth just behind softens it, the target's
// alpha and every native binding survive the draw, and a stock-texture batch draws with its own texture.
#include "gpufx/gpufx_gpu.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace crew::gpufx;

static int failures=0;
#define CHECK(c) do { if(!(c)) { std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c); ++failures; } } while(0)

namespace {
constexpr UINT kSize=96;
struct Rig {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> color,depth,readColor;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11DepthStencilView> dsv;
    bool Make() {
        D3D_FEATURE_LEVEL level{};
        if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)))return false;
        if(level<D3D_FEATURE_LEVEL_11_0)return false;
        D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=kSize;d.MipLevels=d.ArraySize=1;d.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
        d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        if(FAILED(device->CreateTexture2D(&d,nullptr,&color)) || FAILED(device->CreateRenderTargetView(color.Get(),nullptr,&rtv)))return false;
        D3D11_TEXTURE2D_DESC r=d;r.Usage=D3D11_USAGE_STAGING;r.BindFlags=0;r.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        if(FAILED(device->CreateTexture2D(&r,nullptr,&readColor)))return false;
        D3D11_TEXTURE2D_DESC z=d;z.Format=DXGI_FORMAT_R24G8_TYPELESS;z.BindFlags=D3D11_BIND_DEPTH_STENCIL;
        if(FAILED(device->CreateTexture2D(&z,nullptr,&depth)))return false;
        D3D11_DEPTH_STENCIL_VIEW_DESC v{};v.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;v.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
        return SUCCEEDED(device->CreateDepthStencilView(depth.Get(),&v,&dsv));
    }
    // Pixel (x,y) of the colour target: rgb, a (half floats).
    std::vector<float> Read() {
        context->CopyResource(readColor.Get(),color.Get());
        D3D11_MAPPED_SUBRESOURCE m{};
        std::vector<float> out(kSize*kSize*4);
        if(FAILED(context->Map(readColor.Get(),0,D3D11_MAP_READ,0,&m)))return out;
        for(UINT y=0;y<kSize;++y) {
            const auto* row=reinterpret_cast<const std::uint16_t*>(static_cast<const unsigned char*>(m.pData)+y*m.RowPitch);
            for(UINT x=0;x<kSize*4;++x) {
                const std::uint16_t h=row[x];
                const int e=(h>>10)&31;const int f=h&1023;
                float v=e==0 ? std::ldexp(static_cast<float>(f),-24) : std::ldexp(static_cast<float>(f|1024),e-25);
                out[y*kSize*4+x]=(h&0x8000) ? -v : v;
            }
        }
        context->Unmap(readColor.Get(),0);
        return out;
    }
};

void Perspective(float p[16],float zn,float zf) {
    std::memset(p,0,sizeof(float)*16);
    p[0]=p[5]=1.0f;p[11]=-1.0f;p[10]=zf/(zn-zf);p[14]=zn*zf/(zn-zf);
}
}  // namespace

int main() {
    Rig rig;
    if(!rig.Make()){std::printf("gpufx_gpu: no WARP device; skipped\n");return 77;}
    std::vector<std::uint8_t> flip(static_cast<std::size_t>(kFlipSide)*kFlipSide*4);
    GenerateSmokeFlipbook(flip.data());

    ViewParams vp{};
    std::memset(vp.view,0,sizeof(vp.view));vp.view[0]=vp.view[5]=vp.view[10]=vp.view[15]=1.0f;
    Perspective(vp.proj,0.5f,1000.0f);
    // One card 10 m ahead, size 3 (covers +-0.3 of the screen: 29 px from the centre), frame 20 (a dense one).
    Instance card{};card.pos[2]=-10.0f;card.size=3.0f;card.color[0]=card.color[1]=card.color[2]=0.7f;card.color[3]=1.0f;
    card.frame=20.0f;card.soft=2.0f;
    Batch batch{};batch.instances=&card;batch.count=1;

    // Native state the draw must leave as it found it.
    D3D11_BLEND_DESC bd{};bd.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    ComPtr<ID3D11BlendState> nativeBlend;rig.device->CreateBlendState(&bd,&nativeBlend);
    const D3D11_VIEWPORT nativeVp{0.0f,0.0f,static_cast<float>(kSize),static_cast<float>(kSize),0.0f,1.0f};
    auto bindNative=[&]{
        ID3D11RenderTargetView* t=rig.rtv.Get();
        rig.context->OMSetRenderTargets(1,&t,rig.dsv.Get());
        const float f[4]{0.1f,0.2f,0.3f,0.4f};
        rig.context->OMSetBlendState(nativeBlend.Get(),f,0x1234u);
        rig.context->RSSetViewports(1,&nativeVp);
    };
    auto centre=[](const std::vector<float>& px,int dx=0){const std::size_t i=((kSize/2)*kSize+kSize/2+dx)*4;return &px[i];};

    Renderer r;
    r.SetFlipbook(flip.data());
    const float clear[4]{0.0f,0.0f,0.0f,0.25f};
    // 1. Far scene: the card shows in the centre, not in the corner; the alpha channel is untouched.
    rig.context->ClearRenderTargetView(rig.rtv.Get(),clear);
    rig.context->ClearDepthStencilView(rig.dsv.Get(),D3D11_CLEAR_DEPTH,1.0f,0);
    bindNative();
    Stats st{};
    CHECK(r.Draw(rig.context.Get(),rig.depth.Get(),DXGI_FORMAT_D24_UNORM_S8_UINT,vp,&batch,1,&st));
    if(st.refused)std::printf("refused: %s\n",st.refused);
    CHECK(st.drawn==1 && st.batches==1 && !st.reversed && st.lookNegZ && st.width==kSize);
    auto px=rig.Read();
    const float lit=centre(px)[0];
    std::printf("centre %.3f %.3f %.3f a=%.3f corner %.3f\n",centre(px)[0],centre(px)[1],centre(px)[2],centre(px)[3],px[0]);
    CHECK(lit>0.05f);
    CHECK(px[0]==0.0f && px[1]==0.0f);
    CHECK(std::fabs(centre(px)[3]-0.25f)<1e-3f);
    // Native bindings back.
    {
        ID3D11RenderTargetView* t=nullptr;ComPtr<ID3D11DepthStencilView> d;
        rig.context->OMGetRenderTargets(1,&t,&d);
        CHECK(t==rig.rtv.Get() && d.Get()==rig.dsv.Get());
        if(t)t->Release();
        ComPtr<ID3D11BlendState> b;float f[4]{};UINT mask=0;
        rig.context->OMGetBlendState(&b,f,&mask);
        CHECK(b.Get()==nativeBlend.Get() && mask==0x1234u && f[3]==0.4f);
        UINT n=1;D3D11_VIEWPORT v{};rig.context->RSGetViewports(&n,&v);
        CHECK(n==1 && v.Width==nativeVp.Width);
    }
    // 2. Scene 5 m ahead (in front of the card): nothing drawn.
    const float z5=DepthFromViewZ(vp.proj,-5.0f);
    rig.context->ClearRenderTargetView(rig.rtv.Get(),clear);
    rig.context->ClearDepthStencilView(rig.dsv.Get(),D3D11_CLEAR_DEPTH,z5,0);
    bindNative();
    CHECK(r.Draw(rig.context.Get(),rig.depth.Get(),DXGI_FORMAT_D24_UNORM_S8_UINT,vp,&batch,1,&st));
    px=rig.Read();
    CHECK(centre(px)[0]==0.0f);
    // 3. Scene 0.5 m behind the card (soft range 2 m): about a quarter of the far case.
    const float z105=DepthFromViewZ(vp.proj,-10.5f);
    rig.context->ClearRenderTargetView(rig.rtv.Get(),clear);
    rig.context->ClearDepthStencilView(rig.dsv.Get(),D3D11_CLEAR_DEPTH,z105,0);
    bindNative();
    CHECK(r.Draw(rig.context.Get(),rig.depth.Get(),DXGI_FORMAT_D24_UNORM_S8_UINT,vp,&batch,1,&st));
    px=rig.Read();
    const float soft=centre(px)[0];
    std::printf("soft %.4f of %.4f\n",soft,lit);
    CHECK(soft>0.0f && soft<lit*0.5f);
    // 4. A stock-texture batch (blend 2, ONE/ONE): a white 1x1 texture adds texel * colour (alpha not applied to it) and
    // adds its alpha to the target's.
    {
        D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=1;d.MipLevels=d.ArraySize=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.SampleDesc.Count=1;
        d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        const std::uint32_t white=0xFFFFFFFFu;D3D11_SUBRESOURCE_DATA init{&white,4,0};
        ComPtr<ID3D11Texture2D> t;ComPtr<ID3D11ShaderResourceView> v;
        CHECK(SUCCEEDED(rig.device->CreateTexture2D(&d,&init,&t)) && SUCCEEDED(rig.device->CreateShaderResourceView(t.Get(),nullptr,&v)));
        Instance glow=card;glow.color[0]=2.0f;glow.color[1]=1.0f;glow.color[2]=0.5f;glow.color[3]=0.5f;glow.soft=0.001f;glow.frame=0.0f;
        Batch b{};b.texture=v.Get();b.kind=Kind::nativeAdditive;b.gridX=b.gridY=b.frames=1;b.cornerScale=0.70710678f;b.instances=&glow;b.count=1;
        rig.context->ClearRenderTargetView(rig.rtv.Get(),clear);
        rig.context->ClearDepthStencilView(rig.dsv.Get(),D3D11_CLEAR_DEPTH,1.0f,0);
        bindNative();
        CHECK(r.Draw(rig.context.Get(),rig.depth.Get(),DXGI_FORMAT_D24_UNORM_S8_UINT,vp,&b,1,&st));
        px=rig.Read();
        std::printf("additive %.3f %.3f %.3f a=%.3f\n",centre(px)[0],centre(px)[1],centre(px)[2],centre(px)[3]);
        CHECK(std::fabs(centre(px)[0]-2.0f)<0.01f && std::fabs(centre(px)[1]-1.0f)<0.01f && std::fabs(centre(px)[3]-0.75f)<1e-3f);
        // The stock corner layout: half width size/sqrt2 = 2.12 m at 10 m -> 0.212 of the half screen = 10 px; 14 px out is clear.
        CHECK(centre(px,8)[0]>0.5f && centre(px,14)[0]==0.0f);
    }
    // 6. A stock blend-1 batch from a 2x2 atlas (4 frames): frame 3 is the bottom-right tile, upright at stock rot
    // -pi/4 (the tile's top-left texel top left on screen), alpha written ONE/ZERO, the stock distance fade applied.
    {
        // 4x4 texels: each 2x2 tile one frame; the bottom-right tile's own 2x2: red, green / blue, white (alpha 1).
        std::uint32_t texels[16];
        for(auto& tx:texels)tx=0xFF000000u;   // other tiles black
        texels[2*4+2]=0xFF0000FFu;texels[2*4+3]=0xFF00FF00u;texels[3*4+2]=0xFFFF0000u;texels[3*4+3]=0xFFFFFFFFu;
        D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=4;d.MipLevels=d.ArraySize=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.SampleDesc.Count=1;
        d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init{texels,16,0};
        ComPtr<ID3D11Texture2D> t;ComPtr<ID3D11ShaderResourceView> v;
        CHECK(SUCCEEDED(rig.device->CreateTexture2D(&d,&init,&t)) && SUCCEEDED(rig.device->CreateShaderResourceView(t.Get(),nullptr,&v)));
        Instance c=card;c.color[0]=c.color[1]=c.color[2]=1.0f;c.color[3]=1.0f;c.soft=0.001f;c.frame=3.0f;
        c.rot=StockRotToOurs(-0.78539816f);
        Batch b{};b.texture=v.Get();b.kind=Kind::nativeAlpha;b.gridX=b.gridY=2;b.frames=4;b.cornerScale=0.70710678f;b.uInset=0.0005f;
        b.instances=&c;b.count=1;
        ViewParams fv=vp;fv.fadeMul=0.05f;fv.fadeAdd=0.0f;   // 10 m: 1 - 0.5 -> alpha 0.5
        rig.context->ClearRenderTargetView(rig.rtv.Get(),clear);
        rig.context->ClearDepthStencilView(rig.dsv.Get(),D3D11_CLEAR_DEPTH,1.0f,0);
        bindNative();
        CHECK(r.Draw(rig.context.Get(),rig.depth.Get(),DXGI_FORMAT_D24_UNORM_S8_UINT,fv,&b,1,&st));
        px=rig.Read();
        // Half width 10 px: 5 px up-left of the centre is the tile's top-left quarter (red), 5 px down-right its white.
        const float* tl=&px[((kSize/2-5)*kSize+kSize/2-5)*4];
        const float* br=&px[((kSize/2+5)*kSize+kSize/2+5)*4];
        const float* tr=&px[((kSize/2-5)*kSize+kSize/2+5)*4];
        std::printf("atlas tl %.3f %.3f %.3f a=%.3f tr %.3f %.3f %.3f br %.3f %.3f %.3f\n",tl[0],tl[1],tl[2],tl[3],tr[0],tr[1],tr[2],br[0],br[1],br[2]);
        CHECK(tl[0]>0.3f && tl[0]>tl[1]*2.0f && tl[0]>tl[2]*2.0f);
        CHECK(tr[1]>0.3f && tr[1]>tr[0]*2.0f);
        CHECK(br[0]>0.3f && br[1]>0.3f && br[2]>0.3f);
        CHECK(std::fabs(tl[3]-0.5f)<0.01f);   // ONE/ZERO: the card's alpha replaces the target's 0.25
        CHECK(tl[0]<0.55f);                    // faded to half over black
    }
    // 7. The stock lit kinds with a made-up stock lighting: light 0 (red) faces the card's normal, light 1 (green) too,
    // the ambient blue 0.125, one point light (blue 1) 2 m behind the card in its cluster; no shadow (cascade range 0).
    // Set 0xA40 (the instance's normal, towards the camera): (0.5, 0.25, 1.125). Set 0xC08 (an up normal): neither
    // directional light, the ambient and the point light: (0, 0, 1.125). Without the lighting the lit batch is skipped.
    {
        D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=1;d.MipLevels=d.ArraySize=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.SampleDesc.Count=1;
        d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        const std::uint32_t white=0xFFFFFFFFu;D3D11_SUBRESOURCE_DATA init{&white,4,0};
        ComPtr<ID3D11Texture2D> t;ComPtr<ID3D11ShaderResourceView> v;
        CHECK(SUCCEEDED(rig.device->CreateTexture2D(&d,&init,&t)) && SUCCEEDED(rig.device->CreateShaderResourceView(t.Get(),nullptr,&v)));
        auto constants=[&](const float* data,UINT bytes){
            ComPtr<ID3D11Buffer> b;D3D11_BUFFER_DESC bd2{};bd2.ByteWidth=bytes;bd2.Usage=D3D11_USAGE_DEFAULT;bd2.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
            D3D11_SUBRESOURCE_DATA i{data,0,0};CHECK(SUCCEEDED(rig.device->CreateBuffer(&bd2,&i,&b)));return b;
        };
        auto structured=[&](const void* data,UINT stride,UINT count,ComPtr<ID3D11Buffer>& b){
            D3D11_BUFFER_DESC bd2{};bd2.ByteWidth=stride*count;bd2.Usage=D3D11_USAGE_DEFAULT;bd2.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            bd2.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;bd2.StructureByteStride=stride;
            D3D11_SUBRESOURCE_DATA i{data,0,0};CHECK(SUCCEEDED(rig.device->CreateBuffer(&bd2,&i,&b)));
            ComPtr<ID3D11ShaderResourceView> view;CHECK(SUCCEEDED(rig.device->CreateShaderResourceView(b.Get(),nullptr,&view)));return view;
        };
        float sys[152]{};
        sys[4*4+0]=1.0f;sys[5*4+1]=1.0f;sys[6*4+2]=1.0f;   // view -> world: identity, the camera at the origin
        const std::uint32_t screen[2]{kSize,kSize};std::memcpy(&sys[20*4],screen,sizeof(screen));
        sys[21*4+2]=-1.0f;sys[22*4+0]=0.5f;                // light 0 travels -z (towards the card's front), red 0.5
        sys[24*4+2]=-1.0f;sys[25*4+1]=0.25f;               // light 1 the same way, green 0.25
        sys[33*4+2]=0.125f;                                // ambient
        float sh[100]{};
        const std::uint32_t one=1;std::memcpy(&sh[19*4+1],&one,4);   // one point light
        sh[23*4+3]=0.5f;sh[24*4+3]=1.0f;                   // cluster depth 0.5 * 128 = 64
        ComPtr<ID3D11Buffer> sysCb=constants(sys,sizeof(sys)),shCb=constants(sh,sizeof(sh));
        std::vector<std::uint32_t> mask(65*129,0u);
        for(int i=0;i<129;++i){mask[i*65+0]=1u;mask[i*65+64]=1u;}
        float point[16]{};point[2]=-8.0f;point[3]=10.0f;point[6]=1.0f;point[14]=1.0f;point[15]=1.0f;
        ComPtr<ID3D11Buffer> mb[3],pb;
        ComPtr<ID3D11ShaderResourceView> masks[3];
        for(int i=0;i<3;++i)masks[i]=structured(mask.data(),260,129,mb[i]);
        ComPtr<ID3D11ShaderResourceView> points=structured(point,64,1,pb);
        D3D11_TEXTURE2D_DESC sd{};sd.Width=sd.Height=4;sd.MipLevels=1;sd.ArraySize=2;sd.Format=DXGI_FORMAT_R32_FLOAT;sd.SampleDesc.Count=1;
        sd.Usage=D3D11_USAGE_DEFAULT;sd.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> shadowTex;ComPtr<ID3D11ShaderResourceView> shadowMap;
        CHECK(SUCCEEDED(rig.device->CreateTexture2D(&sd,nullptr,&shadowTex)) && SUCCEEDED(rig.device->CreateShaderResourceView(shadowTex.Get(),nullptr,&shadowMap)));
        ViewParams lv=vp;
        lv.lights.system=sysCb.Get();lv.lights.shadow=shCb.Get();lv.lights.shadowMap=shadowMap.Get();lv.lights.points=points.Get();
        for(int i=0;i<3;++i)lv.lights.masks[i]=masks[i].Get();
        Instance c=card;c.color[0]=c.color[1]=c.color[2]=1.0f;c.color[3]=1.0f;c.soft=0.001f;c.frame=0.0f;
        const float towards[3]{0.0f,0.0f,1.0f};c.emissive=PackNormal(towards);
        Batch b{};b.texture=v.Get();b.kind=Kind::nativeLitAlpha;b.gridX=b.gridY=b.frames=1;b.cornerScale=0.70710678f;
        b.litSet=1;b.litWeights[0]=1.0f;b.instances=&c;b.count=1;
        auto drawLit=[&](const ViewParams& p){
            rig.context->ClearRenderTargetView(rig.rtv.Get(),clear);
            rig.context->ClearDepthStencilView(rig.dsv.Get(),D3D11_CLEAR_DEPTH,1.0f,0);
            bindNative();
            CHECK(r.Draw(rig.context.Get(),rig.depth.Get(),DXGI_FORMAT_D24_UNORM_S8_UINT,p,&b,1,&st));
            return rig.Read();
        };
        px=drawLit(lv);
        std::printf("lit set A %.3f %.3f %.3f a=%.3f\n",centre(px)[0],centre(px)[1],centre(px)[2],centre(px)[3]);
        CHECK(std::fabs(centre(px)[0]-0.5f)<0.01f && std::fabs(centre(px)[1]-0.25f)<0.01f && std::fabs(centre(px)[2]-1.125f)<0.01f);
        b.litSet=2;
        px=drawLit(lv);
        std::printf("lit set B %.3f %.3f %.3f\n",centre(px)[0],centre(px)[1],centre(px)[2]);
        CHECK(centre(px)[0]<0.01f && centre(px)[1]<0.01f && std::fabs(centre(px)[2]-1.125f)<0.01f);
        px=drawLit(vp);
        CHECK(centre(px)[0]==0.0f && centre(px)[2]==0.0f && st.drawn==0);
    }
    // 5. A refusal changes nothing: depth of another size.
    {
        D3D11_TEXTURE2D_DESC z{};rig.depth->GetDesc(&z);z.Width=z.Height=32;
        ComPtr<ID3D11Texture2D> other;rig.device->CreateTexture2D(&z,nullptr,&other);
        bindNative();
        CHECK(!r.Draw(rig.context.Get(),other.Get(),DXGI_FORMAT_D24_UNORM_S8_UINT,vp,&batch,1,&st) && st.refused);
    }
    if(failures){std::printf("gpufx_gpu: %d failure(s)\n",failures);return 1;}
    std::printf("gpufx_gpu: ok\n");
    return 0;
}
