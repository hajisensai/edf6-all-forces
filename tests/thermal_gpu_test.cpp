// Real Direct3D11 WARP execution of the production thermal shaders and resource/state contract.
// The fixture supplies real mesh draws, an opaque wall drawn later, and a different final colour target.
#include "thermal_gpu.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>

using Microsoft::WRL::ComPtr;
using crew::thermal::Renderer;
using crew::thermal::Polarity;
namespace {
int checks=0;
void Check(bool yes,const char* what) { ++checks;if(!yes){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);} }
void Hr(HRESULT result,const char* what) { if(FAILED(result)){std::fprintf(stderr,"HRESULT %08lX: %s\n",static_cast<unsigned long>(result),what);std::exit(1);} }
ComPtr<ID3DBlob> Compile(const char* source,const char* profile) {
    ComPtr<ID3DBlob> code,errors;
    const HRESULT result=D3DCompile(source,std::strlen(source),"thermal-test",nullptr,nullptr,"main",profile,D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&errors);
    if(FAILED(result) && errors)std::fprintf(stderr,"%s\n",static_cast<const char*>(errors->GetBufferPointer()));
    Hr(result,"test shader compilation");return code;
}
struct Image { ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11RenderTargetView> rtv; };
struct Fixture {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> vertices,indices;
    ComPtr<ID3D11DepthStencilState> depthState;
    ComPtr<ID3D11Texture2D> depth;
    ComPtr<ID3D11DepthStencilView> dsv;
    ComPtr<ID3D11RasterizerState> raster;
    Image scene,extra1,extra2,finalColour;
    float depthClear=1.0f;
    Fixture() {
        D3D_FEATURE_LEVEL level{};
        Hr(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context),"WARP device");
        Check(level>=D3D_FEATURE_LEVEL_11_0,"WARP supports shader model 5");
        const char* vertex="float4 main(float3 p:POSITION):SV_Position{return float4(p,1);}";
        const char* pixel="float4 main():SV_Target{return float4(0.2,0.6,0.3,1);}";
        const auto vc=Compile(vertex,"vs_5_0"),pc=Compile(pixel,"ps_5_0");
        Hr(device->CreateVertexShader(vc->GetBufferPointer(),vc->GetBufferSize(),nullptr,&vs),"VS");
        Hr(device->CreatePixelShader(pc->GetBufferPointer(),pc->GetBufferSize(),nullptr,&ps),"PS");
        const D3D11_INPUT_ELEMENT_DESC input={"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};
        Hr(device->CreateInputLayout(&input,1,vc->GetBufferPointer(),vc->GetBufferSize(),&layout),"input layout");
        // Prefix vertices and prefix indices deliberately make the production DrawIndexed offset contract observable.
        SetWallDepth(.3f);
        const std::uint16_t i[]={0,0,0,0,1,2,2,1,3,4,5,6,6,5,7};
        D3D11_BUFFER_DESC b{};b.ByteWidth=sizeof(i);b.Usage=D3D11_USAGE_IMMUTABLE;b.BindFlags=D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA data{};data.pSysMem=i;Hr(device->CreateBuffer(&b,&data,&indices),"indices");
        D3D11_DEPTH_STENCIL_DESC ds{};ds.DepthEnable=TRUE;ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;ds.DepthFunc=D3D11_COMPARISON_LESS;
        Hr(device->CreateDepthStencilState(&ds,&depthState),"scene depth state");
        D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=16;d.ArraySize=d.MipLevels=1;d.Format=DXGI_FORMAT_D32_FLOAT;
        d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_DEPTH_STENCIL;
        Hr(device->CreateTexture2D(&d,nullptr,&depth),"scene depth");Hr(device->CreateDepthStencilView(depth.Get(),nullptr,&dsv),"scene DSV");
        D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=TRUE;
        Hr(device->CreateRasterizerState(&r,&raster),"raster");
        scene=Colour();extra1=Colour();extra2=Colour();finalColour=Colour();
    }
    void SetWallDepth(float wall) {
        SetGeometry(.6f,wall);
    }
    void SetGeometry(float targetDepth,float wall) {
        const float v[][3]={{0,0,0},{0,0,0},{-.8f,.8f,targetDepth},{.8f,.8f,targetDepth},{-.8f,-.8f,targetDepth},{.8f,-.8f,targetDepth},
                            {0,.9f,wall},{.9f,.9f,wall},{0,-.9f,wall},{.9f,-.9f,wall}};
        D3D11_BUFFER_DESC b{};b.ByteWidth=sizeof(v);b.Usage=D3D11_USAGE_IMMUTABLE;b.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA data{};data.pSysMem=v;vertices.Reset();Hr(device->CreateBuffer(&b,&data,&vertices),"vertices");
        const UINT stride=sizeof(float)*3,offset=0;ID3D11Buffer* buffer=vertices.Get();
        context->IASetVertexBuffers(0,1,&buffer,&stride,&offset);
    }
    void SetDepthFormat(DXGI_FORMAT format) {
        context->OMSetRenderTargets(0,nullptr,nullptr);dsv.Reset();depth.Reset();
        D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=16;d.ArraySize=d.MipLevels=1;d.Format=format;
        d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_DEPTH_STENCIL;
        Hr(device->CreateTexture2D(&d,nullptr,&depth),"alternate scene depth");
        Hr(device->CreateDepthStencilView(depth.Get(),nullptr,&dsv),"alternate scene DSV");
    }
    void SetDepthComparison(D3D11_COMPARISON_FUNC function) {
        D3D11_DEPTH_STENCIL_DESC ds{};ds.DepthEnable=TRUE;ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;ds.DepthFunc=function;
        depthState.Reset();Hr(device->CreateDepthStencilState(&ds,&depthState),"alternate depth comparison");
        context->OMSetDepthStencilState(depthState.Get(),27);
        depthClear=(function==D3D11_COMPARISON_GREATER || function==D3D11_COMPARISON_GREATER_EQUAL) ? 0.0f : 1.0f;
    }
    Image Colour(unsigned size=16,DXGI_FORMAT format=DXGI_FORMAT_R8G8B8A8_UNORM) {
        Image out;D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=size;d.ArraySize=d.MipLevels=1;d.Format=format;
        d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_RENDER_TARGET;
        Hr(device->CreateTexture2D(&d,nullptr,&out.texture),"colour texture");
        Hr(device->CreateRenderTargetView(out.texture.Get(),nullptr,&out.rtv),"colour RTV");return out;
    }
    void Setup() {
        const float clear[]={.2f,.4f,.6f,1};context->ClearRenderTargetView(scene.rtv.Get(),clear);
        context->ClearDepthStencilView(dsv.Get(),D3D11_CLEAR_DEPTH,depthClear,0);
        ID3D11RenderTargetView* targets[]={scene.rtv.Get(),extra1.rtv.Get(),extra2.rtv.Get()};context->OMSetRenderTargets(3,targets,dsv.Get());
        context->OMSetDepthStencilState(depthState.Get(),27);context->OMSetBlendState(nullptr,nullptr,0xffffffff);
        context->RSSetState(raster.Get());const D3D11_VIEWPORT viewport={0,0,16,16,0,1};context->RSSetViewports(1,&viewport);
        const UINT stride=sizeof(float)*3,offset=0;ID3D11Buffer* buffer=vertices.Get();context->IASetVertexBuffers(0,1,&buffer,&stride,&offset);
        context->IASetIndexBuffer(indices.Get(),DXGI_FORMAT_R16_UINT,0);context->IASetInputLayout(layout.Get());
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);
    }
    void Target() { context->DrawIndexed(6,3,2); }
    void Layers() { context->DrawIndexed(12,3,2); }
    void Wall() { context->DrawIndexed(6,3,6); }
    void FinalTarget() {
        context->OMSetRenderTargets(0,nullptr,nullptr);context->CopyResource(finalColour.texture.Get(),scene.texture.Get());
        ID3D11RenderTargetView* target=finalColour.rtv.Get();context->OMSetRenderTargets(1,&target,nullptr);
    }
    std::array<unsigned char,4> Pixel(Image& image,unsigned x,unsigned y) {
        D3D11_TEXTURE2D_DESC d{};image.texture->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> copy;Hr(device->CreateTexture2D(&d,nullptr,&copy),"readback texture");context->CopyResource(copy.Get(),image.texture.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};Hr(context->Map(copy.Get(),0,D3D11_MAP_READ,0,&mapped),"readback");
        const auto p=static_cast<const unsigned char*>(mapped.pData)+mapped.RowPitch*y+x*4;
        const std::array<unsigned char,4> out={p[0],p[1],p[2],p[3]};context->Unmap(copy.Get(),0);return out;
    }
    void StateUnchanged(bool capture) {
        ComPtr<ID3D11VertexShader> actualVS;ComPtr<ID3D11PixelShader> actualPS;ComPtr<ID3D11InputLayout> actualLayout;
        context->VSGetShader(&actualVS,nullptr,nullptr);context->PSGetShader(&actualPS,nullptr,nullptr);context->IAGetInputLayout(&actualLayout);
        Check(actualVS.Get()==vs.Get() && actualPS.Get()==ps.Get() && actualLayout.Get()==layout.Get(),"shaders and input layout restored");
        ComPtr<ID3D11DepthStencilState> actualDepth;UINT reference=0;context->OMGetDepthStencilState(&actualDepth,&reference);
        Check(actualDepth.Get()==depthState.Get() && reference==27,"native depth state and stencil reference restored");
        ID3D11RenderTargetView* targets[3]{};ComPtr<ID3D11DepthStencilView> actualDSV;context->OMGetRenderTargets(3,targets,&actualDSV);
        Check(targets[0]==(capture ? scene.rtv.Get() : finalColour.rtv.Get()),"render target restored");
        Check(capture ? targets[1]==extra1.rtv.Get() && targets[2]==extra2.rtv.Get() && actualDSV.Get()==dsv.Get()
                      : !targets[1] && !targets[2] && !actualDSV,"MRT and DSV restored");
        for(auto target:targets)if(target)target->Release();
        UINT n=1;D3D11_VIEWPORT viewport{};context->RSGetViewports(&n,&viewport);
        Check(n==1 && viewport.Width==16 && viewport.Height==16,"viewport restored");
    }
};
void Grey(const std::array<unsigned char,4>& p,const char* what) {Check(p[0]==p[1] && p[1]==p[2] && p[3]==255,what);}
} // namespace
int main() {
    Fixture f;Renderer thermal;
    for(unsigned pass=0;pass<2;++pass) {
        f.Setup();Check(thermal.BeginView(f.context.Get(),pass+1),"begin thermal view");
        Check(thermal.CaptureIndexed(f.context.Get(),6,3,2),"capture a real indexed mesh with nonzero offsets");f.StateUnchanged(true);f.Target();
        f.Wall();f.FinalTarget();Check(thermal.Composite(f.context.Get(),pass ? Polarity::blackHot : Polarity::whiteHot),"compose thermal into different final RTV without bound DSV");
        f.StateUnchanged(false);
        const auto visible=f.Pixel(f.finalColour,4,8),occluded=f.Pixel(f.finalColour,10,8),environment=f.Pixel(f.finalColour,0,0);
        Grey(visible,"visible mesh is grey");Grey(occluded,"occluding wall is grey");Grey(environment,"environment is grey");
        Check(pass ? visible[0]<25 : visible[0]>230,"white-hot / black-hot polarity affects actual mesh pixels");
        Check(pass ? occluded[0]>130 && occluded[0]<240 : occluded[0]>15 && occluded[0]<125,"a wall drawn after capture hides heat via final scene depth");
        Check(pass ? environment[0]>130 && environment[0]<240 : environment[0]>15 && environment[0]<125,"background remains lower thermal contrast");
        Check(!thermal.Composite(f.context.Get(),Polarity::whiteHot),"a completed view cannot replay stale heat");
    }
    f.Setup();Check(thermal.BeginView(f.context.Get(),3),"begin no-target view");f.FinalTarget();
    Check(thermal.Composite(f.context.Get(),Polarity::whiteHot),"empty observed scene still has thermal background");
    const auto empty=f.Pixel(f.finalColour,4,8);Grey(empty,"empty scene is grayscale");Check(empty[0]<125,"no stale previous-frame heat in empty scene");
    f.Setup();Check(thermal.BeginView(f.context.Get(),4),"begin mismatch fixture");
    Check(thermal.CaptureIndexed(f.context.Get(),6,3,2),"capture before a final size mismatch");f.Target();
    auto compact=f.Colour(8);ID3D11RenderTargetView* target=compact.rtv.Get();f.context->OMSetRenderTargets(1,&target,nullptr);
    Check(!thermal.Composite(f.context.Get(),Polarity::whiteHot),"final viewport/resource mismatch fails closed");
    f.SetWallDepth(.6f-.000001f);f.Setup();Check(thermal.BeginView(f.context.Get(),5),"begin close-depth fixture");
    Check(thermal.CaptureIndexed(f.context.Get(),6,3,2),"capture before close occluder");f.Target();f.Wall();f.FinalTarget();
    Check(thermal.Composite(f.context.Get(),Polarity::whiteHot),"compose close-depth occluder");
    Check(f.Pixel(f.finalColour,10,8)[0]<125,"one-millionth depth difference cannot leak heat through a later wall");

    f.SetWallDepth(.6f);f.Setup();f.Wall();
    Check(thermal.BeginView(f.context.Get(),6),"begin coplanar LESS rejection fixture");
    Check(thermal.CaptureIndexed(f.context.Get(),6,3,2),"capture before a coplanar enemy native draw");f.Target();f.FinalTarget();
    Check(thermal.Composite(f.context.Get(),Polarity::whiteHot),"compose coplanar wall fixture");
    Check(f.Pixel(f.finalColour,4,8)[0]>230,"unoccluded part of coplanar fixture remains hot");
    Check(f.Pixel(f.finalColour,10,8)[0]<125,"a native LESS rejection at exactly the wall depth cannot revive as heat");

    const std::array<DXGI_FORMAT,4> formats={DXGI_FORMAT_D16_UNORM,DXGI_FORMAT_D24_UNORM_S8_UINT,
        DXGI_FORMAT_D32_FLOAT,DXGI_FORMAT_D32_FLOAT_S8X24_UINT};
    const std::array<float,3> depths={.1234567f,.61327f,.918273f};
    std::uint64_t token=7;
    for(const auto format:formats) {
        f.SetDepthFormat(format);
        for(const float targetDepth:depths) {
            f.SetGeometry(targetDepth,targetDepth*.5f);f.Setup();
            Check(thermal.BeginView(f.context.Get(),token++),"begin native depth-format quantization fixture");
            Check(thermal.CaptureIndexed(f.context.Get(),6,3,2),"capture before a quantized native depth write");f.Target();f.Wall();f.FinalTarget();
            Check(thermal.Composite(f.context.Get(),Polarity::whiteHot),"compose quantized depth format");
            const auto visible=f.Pixel(f.finalColour,4,8);
            if(visible[0]<=230)std::fprintf(stderr,"depth-format=%u z=%.9g visible=%u\n",
                static_cast<unsigned>(format),static_cast<double>(targetDepth),static_cast<unsigned>(visible[0]));
            Check(visible[0]>230,"D16/D24/D32/D32S8 depth comparison retains visible heat");
            Check(f.Pixel(f.finalColour,10,8)[0]<125,"quantized scene depth rejects an opaque wall");
        }
    }
    f.SetDepthFormat(DXGI_FORMAT_D32_FLOAT);
    for(unsigned reverse=0;reverse<2;++reverse) {
        f.SetDepthComparison(reverse ? D3D11_COMPARISON_GREATER : D3D11_COMPARISON_LESS);
        for(unsigned order=0;order<2;++order) {
            const float firstDepth=order ? .3f : .8f,secondDepth=order ? .8f : .3f;
            f.SetGeometry(firstDepth,.9f);f.Setup();
            Check(thermal.BeginView(f.context.Get(),token++),"begin nearest-depth ordering fixture");
            Check(thermal.CaptureIndexed(f.context.Get(),6,3,2),"capture first overlapping enemy");f.Target();
            f.SetGeometry(secondDepth,.9f);
            Check(thermal.CaptureIndexed(f.context.Get(),6,3,2),"capture second overlapping enemy");f.Target();f.FinalTarget();
            Check(thermal.Composite(f.context.Get(),Polarity::whiteHot),"compose MIN/MAX nearest depth");
            Check(f.Pixel(f.finalColour,4,8)[0]>230,"nearest hot pixel survives either target submission order and depth convention");
        }
    }
    f.SetDepthComparison(D3D11_COMPARISON_LESS);f.SetGeometry(.3f,.7f);f.Setup();
    Check(thermal.BeginView(f.context.Get(),token++),"begin overlapping layers in one native mesh");
    Check(thermal.CaptureIndexed(f.context.Get(),12,3,2),"capture near-first far-last primitives in one DrawIndexed");f.Layers();f.FinalTarget();
    Check(thermal.Composite(f.context.Get(),Polarity::whiteHot),"compose self-overlapping native mesh");
    Check(f.Pixel(f.finalColour,10,8)[0]>230,"a later far primitive cannot overwrite the nearer fragment in the heat mask");

    // The previous mask contains heat. Clear the next scene without targets,
    // then predicate all native resource commands off while Begin observes it.
    f.Setup();ComPtr<ID3D11Predicate> predicate;const D3D11_QUERY_DESC query={D3D11_QUERY_OCCLUSION_PREDICATE,0};
    Hr(f.device->CreatePredicate(&query,&predicate),"occlusion predicate");f.context->Begin(predicate.Get());f.context->End(predicate.Get());
    BOOL predicateResult=TRUE;HRESULT predicateStatus=S_FALSE;
    const auto predicateDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    do {
        predicateStatus=f.context->GetData(predicate.Get(),&predicateResult,sizeof(predicateResult),0);
        if(predicateStatus==S_FALSE)SwitchToThread();
    } while(predicateStatus==S_FALSE && std::chrono::steady_clock::now()<predicateDeadline);
    Hr(predicateStatus,"occlusion predicate data");
    Check(predicateStatus==S_OK && !predicateResult,"an empty occlusion interval issues a false native predicate");
    f.context->SetPredication(predicate.Get(),FALSE);
    Check(thermal.BeginView(f.context.Get(),token++),"begin and clear private mask under a false native predicate");
    ComPtr<ID3D11Predicate> actualPredicate;BOOL actualPredicateValue=TRUE;
    f.context->GetPredication(&actualPredicate,&actualPredicateValue);
    Check(actualPredicate.Get()==predicate.Get() && actualPredicateValue==FALSE,"mask clear restores native predicate and comparison value");
    f.context->SetPredication(nullptr,FALSE);f.FinalTarget();f.context->SetPredication(predicate.Get(),FALSE);
    Check(thermal.Composite(f.context.Get(),Polarity::whiteHot),"compose under native predication without skipping private GPU commands");
    actualPredicate.Reset();f.context->GetPredication(&actualPredicate,&actualPredicateValue);
    Check(actualPredicate.Get()==predicate.Get() && actualPredicateValue==FALSE,"composite restores native predication");
    f.context->SetPredication(nullptr,FALSE);
    Check(f.Pixel(f.finalColour,4,8)[0]<125,"a predicated native clear cannot leave heat from a previous view");

    // Begin is allowed before this camera binds its scene depth. A provisional
    // observation of another surface must not reject the real first mesh.
    f.Setup();ComPtr<ID3D11Texture2D> oldDepth=f.depth;ComPtr<ID3D11DepthStencilView> oldDSV=f.dsv;
    Check(thermal.BeginView(f.context.Get(),token++),"begin while a previous view DSV is still bound");
    f.SetDepthFormat(DXGI_FORMAT_D32_FLOAT);f.SetWallDepth(.3f);f.Setup();
    Check(thermal.ObserveSurface(f.context.Get()),"first authoritative scene observation replaces provisional depth");
    Check(thermal.CaptureIndexed(f.context.Get(),6,3,2),"capture after binding this camera actual depth");f.Target();f.FinalTarget();
    Check(thermal.Composite(f.context.Get(),Polarity::whiteHot),"compose after provisional depth replacement");
    Check(f.Pixel(f.finalColour,4,8)[0]>230,"provisional observation does not lock out the real scene mask");
    thermal.Reset();
    std::printf("thermal_gpu: %d real WARP shader, depth, polarity, state and lifecycle checks passed\n",checks);
}
