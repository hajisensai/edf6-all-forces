// Real Direct3D11 WARP execution of the picture-in-picture scope's production pass (src/scope_pip_gpu.cpp):
//  - inside the lens every pixel is the frame's own picture scaled up round the centre by the magnification
//    (scope_pip.h Source, sampled bilinearly from a linear gradient: exact up to the target's precision);
//  - outside it nothing is written; the native bindings (RTV, DSV, viewport, PS, constant buffer) are as they were;
//  - unsupported surfaces (multisampled, a second RTV, a partial viewport, no magnification) change nothing;
//  - a typeless target with a typed view, a resized target, the game's R16G16B16A16_FLOAT and an 8-bit one.
#include "scope_pip.h"
#include "scope_pip_gpu.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using Microsoft::WRL::ComPtr;
using crew::scopepip::Magnifier;
namespace {
int checks=0;
void Check(bool yes,const char* what) { ++checks;if(!yes){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);} }
void Hr(HRESULT result,const char* what) { if(FAILED(result)){std::fprintf(stderr,"HRESULT %08lX: %s\n",static_cast<unsigned long>(result),what);std::exit(1);} }

float HalfToFloat(std::uint16_t h) {
    const unsigned sign=(h>>15)&1u,exponent=(h>>10)&0x1Fu,mantissa=h&0x3FFu;
    float v;
    if(exponent==0)v=std::ldexp(static_cast<float>(mantissa),-24);
    else if(exponent==31)v=mantissa ? NAN : INFINITY;
    else v=std::ldexp(static_cast<float>(mantissa|0x400u),static_cast<int>(exponent)-25);
    return sign ? -v : v;
}
std::uint16_t FloatToHalf(float f) {
    // Values in [0, 2): normal halves only.
    if(f<=0.0f)return 0;
    int e;const float m=std::frexp(f,&e);   // f = m * 2^e, m in [0.5, 1)
    const int exponent=e-1+15;
    const unsigned mantissa=static_cast<unsigned>(std::lround((m*2.0f-1.0f)*1024.0f));
    if(mantissa>=1024)return static_cast<std::uint16_t>((exponent+1)<<10);
    return static_cast<std::uint16_t>((exponent<<10)|mantissa);
}

struct Fixture {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    Fixture() {
        D3D_FEATURE_LEVEL level{};
        const HRESULT r=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context);
        if(FAILED(r)){std::printf("SKIP: no WARP device (%08lX)\n",static_cast<unsigned long>(r));std::exit(77);}
        Check(level>=D3D_FEATURE_LEVEL_11_0,"WARP supports shader model 5");
    }
};

// A w x h target whose texel (i, j) holds ((i+0.5)/w, (j+0.5)/h, 0.25, 1): bilinear sampling at px (sx, sy) inside the
// texel centres gives (sx/w, sy/h) exactly.
struct Target {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> rtv;
    UINT w=0,h=0;
    DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
};
Target MakeTarget(Fixture& f,UINT w,UINT h,DXGI_FORMAT resourceFormat,DXGI_FORMAT viewFormat,UINT samples=1) {
    Target t;t.w=w;t.h=h;t.format=viewFormat;
    D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=1;d.Format=resourceFormat;d.SampleDesc.Count=samples;
    d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    std::vector<std::uint16_t> halves;std::vector<std::uint8_t> bytes;
    D3D11_SUBRESOURCE_DATA data{};
    if(samples==1) {
        if(viewFormat==DXGI_FORMAT_R16G16B16A16_FLOAT) {
            halves.resize(static_cast<std::size_t>(w)*h*4);
            for(UINT j=0;j<h;++j)for(UINT i=0;i<w;++i) {
                std::uint16_t* p=&halves[(static_cast<std::size_t>(j)*w+i)*4];
                p[0]=FloatToHalf((i+0.5f)/w);p[1]=FloatToHalf((j+0.5f)/h);p[2]=FloatToHalf(0.25f);p[3]=FloatToHalf(1.0f);
            }
            data.pSysMem=halves.data();data.SysMemPitch=w*8;
        } else {
            bytes.resize(static_cast<std::size_t>(w)*h*4);
            for(UINT j=0;j<h;++j)for(UINT i=0;i<w;++i) {
                std::uint8_t* p=&bytes[(static_cast<std::size_t>(j)*w+i)*4];
                p[0]=static_cast<std::uint8_t>(i*255/(w-1));p[1]=static_cast<std::uint8_t>(j*255/(h-1));p[2]=64;p[3]=255;
            }
            data.pSysMem=bytes.data();data.SysMemPitch=w*4;
        }
    }
    Hr(f.device->CreateTexture2D(&d,samples==1 ? &data : nullptr,&t.texture),"target");
    D3D11_RENDER_TARGET_VIEW_DESC rd{};rd.Format=viewFormat;
    rd.ViewDimension=samples==1 ? D3D11_RTV_DIMENSION_TEXTURE2D : D3D11_RTV_DIMENSION_TEXTURE2DMS;
    Hr(f.device->CreateRenderTargetView(t.texture.Get(),&rd,&t.rtv),"target RTV");
    return t;
}
struct Pixels { std::vector<float> v;UINT w=0,h=0;
    const float* At(UINT x,UINT y) const { return &v[(static_cast<std::size_t>(y)*w+x)*4]; } };
Pixels Read(Fixture& f,const Target& t) {
    D3D11_TEXTURE2D_DESC d{};t.texture->GetDesc(&d);
    d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;d.MiscFlags=0;
    ComPtr<ID3D11Texture2D> staging;Hr(f.device->CreateTexture2D(&d,nullptr,&staging),"staging");
    f.context->CopyResource(staging.Get(),t.texture.Get());
    D3D11_MAPPED_SUBRESOURCE m{};Hr(f.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&m),"map");
    Pixels p;p.w=t.w;p.h=t.h;p.v.resize(static_cast<std::size_t>(t.w)*t.h*4);
    for(UINT y=0;y<t.h;++y)for(UINT x=0;x<t.w;++x) {
        const auto* row=static_cast<const std::uint8_t*>(m.pData)+static_cast<std::size_t>(y)*m.RowPitch;
        float* out=&p.v[(static_cast<std::size_t>(y)*t.w+x)*4];
        if(t.format==DXGI_FORMAT_R16G16B16A16_FLOAT) {
            const auto* s=reinterpret_cast<const std::uint16_t*>(row)+x*4;
            for(int k=0;k<4;++k)out[k]=HalfToFloat(s[k]);
        } else {
            const std::uint8_t* s=row+x*4;
            for(int k=0;k<4;++k)out[k]=s[k]/255.0f;
        }
    }
    f.context->Unmap(staging.Get(),0);
    return p;
}
bool Same(const Pixels& a,const Pixels& b) { return a.v==b.v; }

// Bind `t` as the native world pass would leave it: RTV0, a depth view, one full viewport, a PS and a constant buffer.
struct Native {
    ComPtr<ID3D11Texture2D> depth;ComPtr<ID3D11DepthStencilView> dsv;
    ComPtr<ID3D11PixelShader> ps;ComPtr<ID3D11Buffer> cb;
};
Native Bind(Fixture& f,const Target& t,bool fullViewport=true) {
    Native n;
    D3D11_TEXTURE2D_DESC d{};d.Width=t.w;d.Height=t.h;d.MipLevels=d.ArraySize=1;d.Format=DXGI_FORMAT_D32_FLOAT;d.SampleDesc.Count=1;
    d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_DEPTH_STENCIL;
    Hr(f.device->CreateTexture2D(&d,nullptr,&n.depth),"depth");Hr(f.device->CreateDepthStencilView(n.depth.Get(),nullptr,&n.dsv),"DSV");
    ComPtr<ID3DBlob> code,errors;
    const char* src="float4 main():SV_Target{return float4(1,0,1,1);}";
    Hr(D3DCompile(src,std::strlen(src),"native",nullptr,nullptr,"main","ps_5_0",0,0,&code,&errors),"native PS");
    Hr(f.device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&n.ps),"native PS object");
    D3D11_BUFFER_DESC b{};b.ByteWidth=16;b.Usage=D3D11_USAGE_DEFAULT;b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    Hr(f.device->CreateBuffer(&b,nullptr,&n.cb),"native CB");
    ID3D11RenderTargetView* rtv=t.rtv.Get();
    f.context->OMSetRenderTargets(1,&rtv,n.dsv.Get());
    const D3D11_VIEWPORT v{0,0,static_cast<float>(fullViewport ? t.w : t.w/2),static_cast<float>(t.h),0,1};
    f.context->RSSetViewports(1,&v);
    f.context->PSSetShader(n.ps.Get(),nullptr,0);
    ID3D11Buffer* cb=n.cb.Get();f.context->PSSetConstantBuffers(0,1,&cb);
    return n;
}
void NativeIntact(Fixture& f,const Target& t,const Native& n) {
    ComPtr<ID3D11RenderTargetView> rtv[2];ComPtr<ID3D11DepthStencilView> dsv;
    ID3D11RenderTargetView* raw[2]{};ID3D11DepthStencilView* rawDsv=nullptr;
    f.context->OMGetRenderTargets(2,raw,&rawDsv);rtv[0].Attach(raw[0]);rtv[1].Attach(raw[1]);dsv.Attach(rawDsv);
    Check(rtv[0].Get()==t.rtv.Get() && !rtv[1] && dsv.Get()==n.dsv.Get(),"the native render targets are bound again");
    D3D11_VIEWPORT v[2]{};UINT count=2;f.context->RSGetViewports(&count,v);
    Check(count==1 && v[0].Width==static_cast<float>(t.w) && v[0].Height==static_cast<float>(t.h),"the native viewport is back");
    ComPtr<ID3D11PixelShader> ps;f.context->PSGetShader(&ps,nullptr,nullptr);
    Check(ps.Get()==n.ps.Get(),"the native pixel shader is back");
    ComPtr<ID3D11Buffer> cb;f.context->PSGetConstantBuffers(0,1,&cb);
    Check(cb.Get()==n.cb.Get(),"the native constant buffer is back");
    ComPtr<ID3D11RasterizerState> rs;f.context->RSGetState(&rs);
    Check(!rs,"the native (default) rasterizer state is back: no scissor left on");
}

// The lens drawn: inside (a pixel short of the edge) the source picture scaled up, outside untouched.
void LensChecks(const Pixels& before,const Pixels& after,float m,float tolerance,const char* what) {
    const float w=static_cast<float>(after.w),h=static_cast<float>(after.h);
    const crew::scopepip::Lens lens=crew::scopepip::LensFor(w,h,m);
    int inside=0,outside=0;
    for(UINT y=0;y<after.h;++y)for(UINT x=0;x<after.w;++x) {
        const float px=x+0.5f,py=y+0.5f,dx=px-lens.cx,dy=py-lens.cy,d=std::sqrt(dx*dx+dy*dy);
        const float* a=after.At(x,y);const float* b=before.At(x,y);
        if(d<lens.r-1.0f) {
            float sx,sy;crew::scopepip::Source(lens,px,py,&sx,&sy);
            // The gradient's value at px s: an 8-bit target's texel i holds i/(w-1) at centre i+0.5.
            const bool half=tolerance<0.01f;
            const float ex=half ? sx/w : (sx-0.5f)/(w-1.0f),ey=half ? sy/h : (sy-0.5f)/(h-1.0f);
            if(std::fabs(a[0]-ex)>tolerance || std::fabs(a[1]-ey)>tolerance || std::fabs(a[2]-b[2])>tolerance || a[3]!=b[3]) {
                std::fprintf(stderr,"%s: (%u,%u) got %g %g want %g %g\n",what,x,y,a[0],a[1],ex,ey);
                Check(false,"a lens pixel shows the centre's neighbourhood scaled up by the magnification");
            }
            ++inside;
        } else if(d>lens.r+1.0f) {
            if(std::memcmp(a,b,sizeof(float)*4)!=0) {
                std::fprintf(stderr,"%s: (%u,%u) outside the lens was changed\n",what,x,y);
                Check(false,"no pixel outside the lens is written");
            }
            ++outside;
        }
    }
    Check(inside>100 && outside>100,"both the lens and the view round it were sampled");
}
}  // namespace

int main() {
    Fixture f;
    Magnifier magnifier;
    // The game's world target (R16G16B16A16_FLOAT, docs/scope-pip-re.md §1: the viewport slot's 0xA targets), 4.2x.
    {
        const Target t=MakeTarget(f,96,64,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R16G16B16A16_FLOAT);
        const Native n=Bind(f,t);
        const Pixels before=Read(f,t);
        Check(magnifier.Composite(f.context.Get(),4.2f),"the lens is drawn on the game's half-float world target");
        NativeIntact(f,t,n);
        LensChecks(before,Read(f,t),4.2f,0.004f,"half 4.2x");
        // The same frame again at a different power: the copy is of the current picture, not of an older one.
        const Pixels once=Read(f,t);
        Check(magnifier.Composite(f.context.Get(),1.5f),"a second composite on the same target");
        const Pixels twice=Read(f,t);
        Check(!Same(once,twice),"the second pass magnified the already magnified picture (it copies the live target)");
    }
    // A typeless 8-bit target through a typed view, then a resized one: the private copy follows.
    {
        const Target t=MakeTarget(f,80,80,DXGI_FORMAT_R8G8B8A8_TYPELESS,DXGI_FORMAT_R8G8B8A8_UNORM);
        const Native n=Bind(f,t);
        const Pixels before=Read(f,t);
        Check(magnifier.Composite(f.context.Get(),2.0f),"a typeless target's typed view is sampled");
        NativeIntact(f,t,n);
        LensChecks(before,Read(f,t),2.0f,2.5f/255.0f+0.004f,"8-bit 2x");
        const Target big=MakeTarget(f,160,90,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R16G16B16A16_FLOAT);
        const Native nb=Bind(f,big);
        const Pixels b2=Read(f,big);
        Check(magnifier.Composite(f.context.Get(),8.5f),"a resized target gets a new private copy");
        NativeIntact(f,big,nb);
        LensChecks(b2,Read(f,big),8.5f,0.004f,"half 8.5x resized");
    }
    // What is not supported changes nothing and says so.
    {
        const Target t=MakeTarget(f,64,64,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R16G16B16A16_FLOAT);
        Native n=Bind(f,t);
        const Pixels before=Read(f,t);
        Check(!magnifier.Composite(f.context.Get(),1.0f),"no magnification: no lens");
        Check(!magnifier.Composite(f.context.Get(),NAN),"a NaN magnification: no lens");
        Check(Same(before,Read(f,t)),"nothing written without a magnification");
        n=Bind(f,t,false);
        Check(!magnifier.Composite(f.context.Get(),4.0f),"a partial viewport is refused");
        Check(Same(before,Read(f,t)),"nothing written on a partial viewport");
        const Target second=MakeTarget(f,64,64,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R16G16B16A16_FLOAT);
        n=Bind(f,t);
        ID3D11RenderTargetView* two[2]={t.rtv.Get(),second.rtv.Get()};
        f.context->OMSetRenderTargets(2,two,n.dsv.Get());
        Check(!magnifier.Composite(f.context.Get(),4.0f),"a second render target bound is refused (not the world's end)");
        Check(Same(before,Read(f,t)),"nothing written with two targets bound");
        f.context->OMSetRenderTargets(0,nullptr,nullptr);
        Check(!magnifier.Composite(f.context.Get(),4.0f),"no render target bound is refused");
        UINT quality=0;
        if(SUCCEEDED(f.device->CheckMultisampleQualityLevels(DXGI_FORMAT_R16G16B16A16_FLOAT,4,&quality)) && quality>0) {
            const Target ms=MakeTarget(f,64,64,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R16G16B16A16_FLOAT,4);
            Bind(f,ms);
            Check(!magnifier.Composite(f.context.Get(),4.0f),"a multisampled target is refused");
        }
        magnifier.Reset();
        n=Bind(f,t);
        Check(magnifier.Composite(f.context.Get(),3.0f),"after Reset the pass builds itself again");
        NativeIntact(f,t,n);
    }
    std::printf("scope_pip_gpu: %d checks passed\n",checks);
    return 0;
}
