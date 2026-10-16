#include "../src/fp_skin_gpu.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdio>
#include <array>
#include <cstdarg>
namespace crew { void Log(const char* format,...) noexcept { va_list a;va_start(a,format);std::vprintf(format,a);va_end(a);std::puts(""); } }

int main() {
    using namespace crew::fpskin;
    using Microsoft::WRL::ComPtr;
    int failed=0;
    const auto check=[&](bool ok,const char* text){std::printf("%s %s\n",ok ? "ok" : "FAIL",text);if(!ok)++failed;};
    std::array<unsigned char,5*60> vertices{};
    const auto skin=[&](unsigned i,unsigned a,unsigned b,float wa,float wb){
        std::memcpy(vertices.data()+i*60+40,&wa,4);std::memcpy(vertices.data()+i*60+44,&wb,4);
        vertices[i*60+56]=static_cast<unsigned char>(a);vertices[i*60+57]=static_cast<unsigned char>(b);
    };
    skin(0,1,0,1,0);skin(1,2,0,1,0);skin(2,1,2,.5f,.5f);skin(3,1,0,.5f,.5f);skin(4,0,0,1,0);
    const std::uint16_t indices[]={0,1,2,1,2,3,2,3,4};
    Mask mask{};mask.count=3;mask.arm[1]=mask.arm[2]=true;
    std::vector<std::uint16_t> out;
    check(Filter(vertices.data(),vertices.size(),60,0,indices,9,0,mask,&out) && out==std::vector<std::uint16_t>({0,1,2}),
          "the pure arm triangle survives; both torso/shoulder triangles are excluded");
    const auto original=vertices;
    mask.arm[1]=mask.arm[2]=false;
    check(Filter(vertices.data(),vertices.size(),60,0,indices,9,0,mask,&out) && out.empty(),"a riding body has no colour triangles");
    mask.arm[1]=mask.arm[2]=true;
    check(!Filter(vertices.data(),vertices.size(),44,0,indices,9,0,mask,&out),"an unknown vertex declaration is rejected");
    check(vertices==original,"filtering does not rewrite native vertex weights or positions");
    mask.side[1]=1;mask.side[2]=2;
    check(Filter(vertices.data(),vertices.size(),60,0,indices,9,0,mask,&out) && out.empty(),
          "a triangle bridging independently placed left/right arms is excluded too");
    mask.side[1]=mask.side[2]=0;
    std::array<unsigned char,5*64> wide{};
    for(unsigned i=0;i<5;++i)std::memcpy(wide.data()+i*64+44,vertices.data()+i*60+40,20);
    check(Filter(wide.data(),wide.size(),64,0,indices,9,0,mask,&out) && out==std::vector<std::uint16_t>({0,1,2}),
          "the Wing Diver float3-position layout uses the same topology rule");

    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
    const HRESULT hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context);
    if(FAILED(hr)){std::puts("WARP unavailable; CPU topology checks completed");return failed ? 1 : 77;}
    D3D11_BUFFER_DESC desc{};desc.ByteWidth=static_cast<UINT>(vertices.size());desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA data{};data.pSysMem=vertices.data();ComPtr<ID3D11Buffer> vb,ib;
    check(SUCCEEDED(device->CreateBuffer(&desc,&data,&vb)),"create native-style immutable vertex buffer");
    desc.ByteWidth=sizeof(indices);desc.BindFlags=D3D11_BIND_INDEX_BUFFER;data.pSysMem=indices;
    check(SUCCEEDED(device->CreateBuffer(&desc,&data,&ib)),"create original index buffer");
    if(!vb || !ib)return 1;
    ID3D11Buffer* raw=vb.Get();UINT stride=60,offset=0;context->IASetVertexBuffers(0,1,&raw,&stride,&offset);
    context->IASetIndexBuffer(ib.Get(),DXGI_FORMAT_R16_UINT,0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    Begin(mask);Submit(context.Get(),9,0,0);End();
    const auto stats=Last();
    check(stats.valid && stats.source==9 && stats.kept==3,"the GPU path submits only the three arm indices");
    ComPtr<ID3D11Buffer> after;DXGI_FORMAT format{};UINT afterOffset=1;context->IAGetIndexBuffer(&after,&format,&afterOffset);
    check(after.Get()==ib.Get() && format==DXGI_FORMAT_R16_UINT && afterOffset==0,"the original GPU binding is restored after the draw");
    Submit(context.Get(),9,0,0); // outside Begin/End: native third-person/shadow path
    after.Reset();context->IAGetIndexBuffer(&after,&format,&afterOffset);
    check(after.Get()==ib.Get(),"outside the first-person scope the original body remains bound");
    Clear();
    return failed ? 1 : 0;
}
