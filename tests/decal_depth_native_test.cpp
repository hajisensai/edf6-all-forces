// Runs the supported game's original and production-rewritten decal CS on an offline D3D11 WARP device.
// EDF.dll is mapped with DONT_RESOLVE_DLL_REFERENCES: no DllMain, game services, or game process runs.
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include "../src/decal_depth.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <initializer_list>
#include <vector>

namespace {
using Microsoft::WRL::ComPtr;
constexpr unsigned kWidth=8,kHeight=8;
constexpr std::size_t kShaderRva=0x1F68FF0;
int failures=0;

bool Check(bool condition,const char* message) {
    if(!condition){std::fprintf(stderr,"FAIL: %s\n",message);++failures;}
    return condition;
}
bool Result(HRESULT hr,const char* message) {
    if(SUCCEEDED(hr))return true;
    std::fprintf(stderr,"FAIL: %s (HRESULT 0x%08lX)\n",message,static_cast<unsigned long>(hr));
    ++failures;return false;
}
std::uint32_t ReadWord(const unsigned char* bytes) {
    std::uint32_t value=0;std::memcpy(&value,bytes,sizeof(value));return value;
}
// Bind this fixture to the exact production creation, draw and upload path, not merely a coincidental DXBC blob.
bool NativeBytes(const unsigned char* image,std::size_t at,std::initializer_list<unsigned char> bytes) {
    return std::memcmp(image+at,bytes.begin(),bytes.size())==0;
}
std::uint64_t Fingerprint(const unsigned char* bytes,std::size_t size) {
    std::uint64_t hash=14695981039346656037ull;
    for(std::size_t i=0;i<size;++i)hash=(hash^bytes[i])*1099511628211ull;
    return hash;
}
bool RelativeCall(const unsigned char* image,std::size_t site,std::size_t target) {
    std::int32_t delta=0;std::memcpy(&delta,image+site+1,sizeof(delta));
    return image[site]==0xE8 && static_cast<std::int64_t>(site)+5+delta==static_cast<std::int64_t>(target);
}
void CheckNativeBinding(const unsigned char* image) {
    Check(RelativeCall(image,0x2D23BF,0x2D28C0) && RelativeCall(image,0x2D291E,0x2CF120) &&
          RelativeCall(image,0x2CF17C,0x2D0940) &&
          NativeBytes(image,0x2D28D1,{0x48,0x85,0xC0,0x74,0x0A,0x83,0x78,0x08,0,0x0F,0x85,0xEF,0,0,0}),
          "the native effect accessor lazily reaches its constructor through the audited three-call creation chain");
    Check(NativeBytes(image,0x2D179B,{0x41,0xB9,0x10,0x10,0,0}) &&
          NativeBytes(image,0x2D17A1,{0x4C,0x8D,0x05}) &&
          NativeBytes(image,0x2D17AF,{0x49,0x8D,0x8F,0xB8,0x01,0,0}) &&
          RelativeCall(image,0x2D17B6,0x11614D0),
          "the production hook creates the 0x1010-byte decal CS in this effect's +0x1B8 wrapper");
    std::int32_t shaderOffset=0;std::memcpy(&shaderOffset,image+0x2D17A4,sizeof(shaderOffset));
    Check(0x2D17A8ll+shaderOffset==static_cast<std::int64_t>(kShaderRva),
          "the native R8 argument points to the exact DXBC profile rewritten by production");
    Check(NativeBytes(image,0x11614E4,{0x4D,0x8B,0xE1,0x4D,0x8B,0xE8}) &&
          NativeBytes(image,0x116153F,{0x49,0x8B,0x8F,0x80,0,0,0,0x48,0x8B,0x01}) &&
          NativeBytes(image,0x116155B,{0x4D,0x8B,0xC4,0x49,0x8B,0xD5,0xFF,0x90,0x90,0,0,0}),
          "the helper forwards bytes / length as RDX / R8 to ID3D11Device::CreateComputeShader at vtable +0x90");
    Check(NativeBytes(image,0x2D17BE,{0x41,0xB8,0xB0,0,0,0}) &&
          NativeBytes(image,0x2D17CB,{0x49,0x8D,0x8F,0xD8,0x01,0,0}) &&
          RelativeCall(image,0x2D17D2,0x111F650),
          "the effect creates the exact 176-byte native decal constant buffer");
    Check(NativeBytes(image,0x2D2B9E,{0x49,0x8B,0xF1}) &&
          NativeBytes(image,0x2D2D88,{0x0F,0x10,0x46,0x20}) &&
          NativeBytes(image,0x2D2DB4,{0x0F,0x10,0x46,0x20}) &&
          RelativeCall(image,0x2D2DEE,0x71810),
          "the native draw retains its R9 view and inverts that view's +0x20 matrix");
    Check(NativeBytes(image,0x2D2DF3,{
          0x0F,0x10,0x46,0x60,0x0F,0x29,0x45,0xC0,0x0F,0x10,0x4E,0x70,0x0F,0x29,0x4D,0xD0,
          0x0F,0x10,0x86,0x80,0,0,0,0x0F,0x29,0x45,0xE0,0x0F,0x10,0x8E,0x90,0,0,0,0x0F,0x29,0x4D,0xF0}) &&
          RelativeCall(image,0x2D2E21,0x71810),
          "the native draw copies all four projection rows at view +0x60..0x90 and calls the real inverse");
    // The whole straight matrix-preparation block is guarded, including both inverse calls, inverse(P)*inverse(V)
    // multiplication and its transpose. At upload base rbp+0x180, invVP starts at +0x30 (cb3), normal rotation at +0x70
    // (cb7). Therefore the homogeneous W column is cb6, whose z/w are the production shader's new coefficients.
    Check(Fingerprint(image+0x2D2D88,0x2D3015-0x2D2D88)==0x54935166809FFD58ull &&
          Fingerprint(image+0x2D2F50,0x2D3015-0x2D2F50)==0x8D023848D66EA2FBull,
          "the exact production inverse(P)*inverse(V) calculation, cb3..6 transpose and cb7..10 rotation source remain native");
    Check(NativeBytes(image,0x2D3015,{
          0x48,0x8D,0x8F,0xB8,0,0,0,0x4C,0x8D,0x85,0x80,0x01,0,0,0x49,0x8B,0xD7}) &&
          RelativeCall(image,0x2D3026,0x113F860) && RelativeCall(image,0x113F886,0x113D240) &&
          NativeBytes(image,0x113D240,{0x48,0x8B,0x49,0x08,0x4C,0x8B,0x11,0x49,0xFF,0xA2,0x80,0x01,0,0}),
          "the native draw uploads that exact rbp+0x180 buffer through ID3D11DeviceContext::UpdateSubresource");
    // The only native code executed by this test is this leaf matrix inverse. Its four branches stay inside
    // these 0x580 bytes, there are no calls / IAT references, and its only RIP data are the following constants.
    Check(Fingerprint(image+0x71810,0x580)==0xB2B0CF604E3E5A74ull &&
          NativeBytes(image,0x1C36A90,{0xFF,0xFF,0xFF,0x7F,0xFF,0xFF,0xFF,0x7F,0xFF,0xFF,0xFF,0x7F,0xFF,0xFF,0xFF,0x7F}) &&
          NativeBytes(image,0x1C36994,{0,0,0x80,0x3F}),
          "the callable native matrix inverse is the audited leaf SSE implementation with its original constants");
}
struct Chunk { const unsigned char* bytes=nullptr;std::size_t size=0; };
bool Chunks(const unsigned char* bytes,std::size_t size,std::vector<Chunk>& out) {
    if(size<32 || std::memcmp(bytes,"DXBC",4)!=0 || ReadWord(bytes+24)!=size)return false;
    const std::size_t count=ReadWord(bytes+28);
    if(count>(size-32)/4)return false;
    for(std::size_t i=0;i<count;++i) {
        const std::size_t offset=ReadWord(bytes+32+4*i);
        if(offset>size-8)return false;
        const std::size_t length=ReadWord(bytes+offset+4);
        if(length>size-offset-8)return false;
        out.push_back({bytes+offset,length+8});
    }
    return true;
}
struct Instruction { std::vector<std::uint32_t> words; };
bool Instructions(const Chunk& chunk,std::vector<Instruction>& out) {
    if(chunk.size<16 || (chunk.size-8)%4!=0 || ReadWord(chunk.bytes+8)!=0x50050 ||
       ReadWord(chunk.bytes+12)!=(chunk.size-8)/4)return false;
    for(std::size_t at=16;at<chunk.size;) {
        const std::size_t count=(ReadWord(chunk.bytes+at)>>24)&0x7F;
        if(!count || count>(chunk.size-at)/4)return false;
        Instruction instruction;instruction.words.resize(count);
        std::memcpy(instruction.words.data(),chunk.bytes+at,count*4);
        out.push_back(std::move(instruction));at+=count*4;
    }
    return true;
}
void CheckRewrite(const unsigned char* source,std::size_t size,const std::vector<unsigned char>& patched) {
    std::vector<Chunk> before,after;
    if(!Check(Chunks(source,size,before) && Chunks(patched.data(),patched.size(),after),"both DXBC containers are structurally valid"))return;
    if(!Check(before.size()==5 && after.size()==before.size(),"all five native DXBC chunks remain"))return;
    unsigned shex=0,madChanges=0,divChanges=0;
    for(std::size_t chunk=0;chunk<before.size();++chunk) {
        const auto& a=before[chunk];const auto& b=after[chunk];
        if(!Check(std::memcmp(a.bytes,b.bytes,4)==0,"native chunk order and FourCCs remain"))continue;
        if(std::memcmp(a.bytes,"SHEX",4)!=0) {
            Check(a.size==b.size && std::memcmp(a.bytes,b.bytes,a.size)==0,"every non-code DXBC chunk is byte-identical");continue;
        }
        ++shex;std::vector<Instruction> original,rewritten;
        if(!Check(Instructions(a,original) && Instructions(b,rewritten),"both native compute instruction streams parse"))continue;
        if(!Check(original.size()==rewritten.size(),"rewriting inserts no executable instructions"))continue;
        bool needsDiv=false;
        for(std::size_t i=0;i<original.size();++i) {
            const auto& oldWords=original[i].words;const auto& newWords=rewritten[i].words;
            if(needsDiv) {
                auto expected=oldWords;
                const bool isDiv=oldWords.size()==7 && oldWords[0]==0x0700000E &&
                                 oldWords[3]==0x4001 && oldWords[4]==0x42C80000;
                if(isDiv)expected[4]=0xBF800000;
                Check(isDiv && expected==newWords,"each depth divide changes only its numerator from 100 to -1");
                ++divChanges;needsDiv=false;continue;
            }
            if(oldWords==newWords)continue;
            const bool isMad=oldWords.size()==9 && oldWords[0]==0x09000032 &&
                             oldWords[5]==0x4001 && oldWords[6]==0x4479F99A &&
                             oldWords[7]==0x4001 && oldWords[8]==0xC47A0000;
            std::vector<std::uint32_t> expected;
            if(isMad) {
                expected.assign(oldWords.begin(),oldWords.begin()+5);expected[0]=0x0B000032;
                const std::uint32_t operands[]={0x20802A,0,6,0x20803A,0,6};
                expected.insert(expected.end(),std::begin(operands),std::end(operands));
            }
            Check(isMad && expected==newWords,"only a native depth mad changes to cb0[6].z / cb0[6].w");
            if(isMad){++madChanges;needsDiv=true;}
        }
        Check(!needsDiv,"the final depth mad has its matching divide");
    }
    Check(shex==1 && madChanges==5 && divChanges==5,"exactly the centre and four neighbour depth reconstructions change");
    Check(patched.size()==size+5*8,"the container grows only by the five constant-buffer operands");

    const std::vector<unsigned char> sentinel={0x12,0x34,0x56};auto unchanged=sentinel;
    Check(!crew::decaldepth::Rewrite(nullptr,size,&unchanged) && unchanged==sentinel,"a null shader is rejected without changing the output");
    Check(!crew::decaldepth::Rewrite(source,size,nullptr),"a null output is rejected");
    Check(!crew::decaldepth::Rewrite(source,size-1,&unchanged) && unchanged==sentinel,"an incomplete shader is rejected without changing the output");
    std::vector<unsigned char> foreign(source,source+size);foreign.back()^=1;
    Check(!crew::decaldepth::Rewrite(foreign.data(),foreign.size(),&unchanged) && unchanged==sentinel,"an unknown shader fingerprint is rejected without changing the output");
    Check(!crew::decaldepth::Rewrite(patched.data(),patched.size(),&unchanged) && unchanged==sentinel,"an already rewritten shader cannot be rewritten again");
}
struct Float4 { float x=0,y=0,z=0,w=0; };
static_assert(sizeof(Float4)==16,"the native CB uses float4 registers");
struct alignas(16) Matrix { float m[4][4]{}; };
using NativeInverse=void(__fastcall*)(Matrix*,const Matrix*);
static_assert(sizeof(Matrix)==64 && 3*sizeof(Float4)==48 && 7*sizeof(Float4)==112,
              "fixture matrix sizes and invVP / invV offsets match the native upload");
Matrix Multiply(const Matrix& a,const Matrix& b) {
    Matrix result;
    for(unsigned row=0;row<4;++row)for(unsigned col=0;col<4;++col)
        for(unsigned i=0;i<4;++i)result.m[row][col]+=a.m[row][i]*b.m[i][col];
    return result;
}
void TransposeInto(std::array<Float4,11>& cb,unsigned first,const Matrix& matrix) {
    for(unsigned row=0;row<4;++row) {
        const Float4 value={matrix.m[0][row],matrix.m[1][row],matrix.m[2][row],matrix.m[3][row]};
        cb[first+row]=value;
    }
}
struct Input {
    std::array<Float4,11> cb{};
    std::array<float,kWidth*kHeight> depth{};
    std::array<Float4,kWidth*kHeight> normal{},viewId{};
    Float4 expectedNormal{};
};
Input Plane(float nearPlane,float farPlane,float viewDepth,bool rotated,NativeInverse invert) {
    Input input;
    constexpr float xs=1.375f,ys=1.75f;
    const float q=farPlane/(farPlane-nearPlane),qn=nearPlane*q;
    const float depth=q-qn/viewDepth;
    input.cb[0]={static_cast<float>(kWidth),static_cast<float>(kHeight),1.0f/kWidth,1.0f/kHeight};
    input.cb[1]={2.0f/(xs*kWidth),-2.0f/(ys*kHeight),-1.0f/xs,1.0f/ys};
    Matrix inverseProjection;
    inverseProjection.m[0][0]=1.0f/xs;inverseProjection.m[1][1]=1.0f/ys;
    inverseProjection.m[2][3]=-1.0f/qn;inverseProjection.m[3][2]=1.0f;
    inverseProjection.m[3][3]=q/qn;
    Matrix projection;
    projection.m[0][0]=xs;projection.m[1][1]=ys;projection.m[2][2]=q;
    projection.m[2][3]=1;projection.m[3][2]=-qn;
    Matrix nativeInverse;invert(&nativeInverse,&projection);
    bool inverseMatches=true;
    for(unsigned row=0;row<4;++row)for(unsigned col=0;col<4;++col) {
        const float expected=inverseProjection.m[row][col];
        inverseMatches=inverseMatches && std::isfinite(nativeInverse.m[row][col]) &&
            std::fabs(nativeInverse.m[row][col]-expected)<=0.00001f*(1.0f+std::fabs(expected));
    }
    Check(inverseMatches,"the real native 0x71810 inverse agrees with the independently derived projection inverse");
    const Matrix identity=Multiply(projection,nativeInverse);bool inverseIdentity=true;
    for(unsigned row=0;row<4;++row)for(unsigned col=0;col<4;++col)
        inverseIdentity=inverseIdentity && std::fabs(identity.m[row][col]-(row==col ? 1.0f : 0.0f))<0.00003f;
    Check(inverseIdentity,"the real native projection inverse multiplies back to identity");
    inverseProjection=nativeInverse;
    Matrix inverseView;
    const float angle=rotated ? 0.63f : 0.0f,c=std::cos(angle),s=std::sin(angle);
    inverseView.m[0][0]=c;inverseView.m[0][2]=-s;inverseView.m[1][1]=1;
    inverseView.m[2][0]=s;inverseView.m[2][2]=c;inverseView.m[3][3]=1;
    inverseView.m[3][0]=rotated ? 125.0f : 0;inverseView.m[3][1]=rotated ? -45.0f : 0;
    inverseView.m[3][2]=rotated ? 3000.0f : 0;
    TransposeInto(input.cb,3,Multiply(inverseProjection,inverseView));
    // Native cb7..10 copy the raw VIEW rows, not a complete transposed inverse(V). Only cb7..9.xyz are read:
    // for this rigid view their rotation equals transpose(inverse(V)); the unused translation / W differ.
    Matrix view;invert(&view,&inverseView);
    for(unsigned row=0;row<4;++row)
        input.cb[7+row]={view.m[row][0],view.m[row][1],view.m[row][2],view.m[row][3]};
    input.expectedNormal={s,0,c,0};
    Check(input.cb[6].x==0 && input.cb[6].y==0 && input.cb[6].z==nativeInverse.m[2][3] &&
          input.cb[6].w==nativeInverse.m[3][3],
          "cb6.z / w are the native projection inverse's homogeneous coefficients even after view rotation / translation");
    for(unsigned y=0;y<kHeight;++y)for(unsigned x=0;x<kWidth;++x) {
        const auto i=static_cast<std::size_t>(y*kWidth+x);
        input.depth[i]=depth;
        input.normal[i]={0.1f+0.01f*x,-0.2f+0.01f*y,0.9f,-777};
        input.viewId[i]={100.0f+x,200.0f+y,300,5000.0f+static_cast<float>(i)};
    }
    return input;
}
struct Output {
    std::array<Float4,kWidth*kHeight> normal{},gbuffer{};
    std::array<float,kWidth*kHeight> depth{};
};
struct Gpu {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    bool Texture(DXGI_FORMAT format,UINT binds,const void* initial,UINT pitch,ComPtr<ID3D11Texture2D>& texture) const {
        D3D11_TEXTURE2D_DESC desc{};desc.Width=kWidth;desc.Height=kHeight;desc.MipLevels=1;desc.ArraySize=1;
        desc.Format=format;desc.SampleDesc.Count=1;desc.Usage=initial ? D3D11_USAGE_IMMUTABLE : D3D11_USAGE_DEFAULT;
        desc.BindFlags=binds;D3D11_SUBRESOURCE_DATA data{};data.pSysMem=initial;data.SysMemPitch=pitch;
        return Result(device->CreateTexture2D(&desc,initial ? &data : nullptr,&texture),"create the offline shader texture");
    }
    bool Readback(ID3D11Texture2D* texture,void* target,std::size_t rowBytes) const {
        D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;
        desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> staging;
        if(!Result(device->CreateTexture2D(&desc,nullptr,&staging),"create output readback texture"))return false;
        context->CopyResource(staging.Get(),texture);D3D11_MAPPED_SUBRESOURCE map{};
        if(!Result(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map),"map completed WARP output"))return false;
        for(unsigned y=0;y<kHeight;++y)std::memcpy(static_cast<unsigned char*>(target)+y*rowBytes,
            static_cast<const unsigned char*>(map.pData)+static_cast<std::size_t>(y)*map.RowPitch,rowBytes);
        context->Unmap(staging.Get(),0);return true;
    }
    bool Run(ID3D11ComputeShader* shader,const Input& input,Output& output) const {
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=sizeof(input.cb);desc.Usage=D3D11_USAGE_IMMUTABLE;
        desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;D3D11_SUBRESOURCE_DATA data{};data.pSysMem=input.cb.data();
        ComPtr<ID3D11Buffer> cb;
        if(!Result(device->CreateBuffer(&desc,&data,&cb),"create the native 176-byte constant buffer"))return false;
        std::array<ComPtr<ID3D11Texture2D>,3> sources,targets;
        const DXGI_FORMAT sourceFormats[]={DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R32G32B32A32_FLOAT,DXGI_FORMAT_R32G32B32A32_FLOAT};
        const void* sourceData[]={input.depth.data(),input.normal.data(),input.viewId.data()};
        const UINT sourcePitch[]={kWidth*sizeof(float),kWidth*sizeof(Float4),kWidth*sizeof(Float4)};
        std::array<ComPtr<ID3D11ShaderResourceView>,3> srvs;
        std::array<ComPtr<ID3D11UnorderedAccessView>,3> uavs;
        for(unsigned i=0;i<3;++i) {
            if(!Texture(sourceFormats[i],D3D11_BIND_SHADER_RESOURCE,sourceData[i],sourcePitch[i],sources[i]) ||
               !Result(device->CreateShaderResourceView(sources[i].Get(),nullptr,&srvs[i]),"create native SRV binding"))return false;
            const auto format=i==2 ? DXGI_FORMAT_R32_FLOAT : DXGI_FORMAT_R32G32B32A32_FLOAT;
            if(!Texture(format,D3D11_BIND_UNORDERED_ACCESS,nullptr,0,targets[i]) ||
               !Result(device->CreateUnorderedAccessView(targets[i].Get(),nullptr,&uavs[i]),"create native UAV binding"))return false;
        }
        ID3D11Buffer* buffer=cb.Get();ID3D11ShaderResourceView* views[]={srvs[0].Get(),srvs[1].Get(),srvs[2].Get()};
        ID3D11UnorderedAccessView* outputs[]={uavs[0].Get(),uavs[1].Get(),uavs[2].Get()};
        context->CSSetShader(shader,nullptr,0);context->CSSetConstantBuffers(0,1,&buffer);
        context->CSSetShaderResources(0,3,views);context->CSSetUnorderedAccessViews(0,3,outputs,nullptr);
        context->Dispatch(1,1,1);
        ID3D11ShaderResourceView* noSrvs[3]{};ID3D11UnorderedAccessView* noUavs[3]{};
        context->CSSetShaderResources(0,3,noSrvs);context->CSSetUnorderedAccessViews(0,3,noUavs,nullptr);
        context->CSSetShader(nullptr,nullptr,0);ID3D11Buffer* noCb=nullptr;context->CSSetConstantBuffers(0,1,&noCb);
        return Readback(targets[0].Get(),output.normal.data(),kWidth*sizeof(Float4)) &&
               Readback(targets[1].Get(),output.gbuffer.data(),kWidth*sizeof(Float4)) &&
               Readback(targets[2].Get(),output.depth.data(),kWidth*sizeof(float));
    }
};
bool Close(float actual,float expected,float tolerance) {
    return std::isfinite(actual) && std::fabs(actual-expected)<=tolerance;
}
void CheckPlane(const Input& input,const Output& original,const Output& patched,float nearPlane,float farPlane,float viewDepth,bool rotated) {
    // Hardware depth and the native CB are float32. Inverting depth near 1 loses approximately z^2/n ULPs.
    const float tolerance=0.0002f+8.0f*std::numeric_limits<float>::epsilon()*viewDepth*viewDepth/nearPlane;
    bool depthCorrect=true,stockCompatible=true,normalsCorrect=true,gbufferPreserved=true;
    float maximumError=0,oldError=0;
    for(std::size_t i=0;i<input.depth.size();++i) {
        depthCorrect=depthCorrect && Close(patched.depth[i],viewDepth,tolerance);
        maximumError=(std::max)(maximumError,std::fabs(patched.depth[i]-viewDepth));
        oldError=(std::max)(oldError,std::fabs(original.depth[i]-viewDepth));
        stockCompatible=stockCompatible && Close(patched.depth[i],original.depth[i],tolerance);
        const auto& normal=patched.normal[i];const auto& oldNormal=original.normal[i];
        const auto& gbuffer=patched.gbuffer[i];const auto& oldGbuffer=original.gbuffer[i];const auto& expected=input.normal[i];
        gbufferPreserved=gbufferPreserved && gbuffer.x==expected.x && gbuffer.y==expected.y && gbuffer.z==expected.z &&
                         gbuffer.w==input.viewId[i].w && normal.w==input.viewId[i].w &&
                         std::memcmp(&gbuffer,&oldGbuffer,sizeof(gbuffer))==0;
        const auto x=static_cast<unsigned>(i%kWidth),y=static_cast<unsigned>(i/kWidth);
        if(x>=2 && x<kWidth-2 && y>=2 && y<kHeight-2) {
            normalsCorrect=normalsCorrect && Close(normal.x,input.expectedNormal.x,0.00003f) &&
                Close(normal.y,input.expectedNormal.y,0.00003f) && Close(normal.z,input.expectedNormal.z,0.00003f) &&
                Close(normal.x,oldNormal.x,0.00003f) && Close(normal.y,oldNormal.y,0.00003f) && Close(normal.z,oldNormal.z,0.00003f);
        }
    }
    std::printf("plane n=%g f=%g z=%g view=%s: corrected error=%g (limit %g), native error=%g\n",
        static_cast<double>(nearPlane),static_cast<double>(farPlane),static_cast<double>(viewDepth),rotated ? "rotated+translated" : "identity",
        static_cast<double>(maximumError),static_cast<double>(tolerance),static_cast<double>(oldError));
    Check(depthCorrect,"the rewritten native shader reconstructs the draw's real view depth");
    Check(normalsCorrect,"interior plane normals retain their native world rotation and unit direction");
    Check(gbufferPreserved,"both shaders preserve G-buffer normal channels and the native object IDs");
    if(nearPlane==0.1f && farPlane==1000.0f)Check(stockCompatible,"stock projection remains compatible with native depth output");
    else Check(oldError>tolerance,"the original hard-coded depth formula demonstrably fails this custom projection");
}
} // namespace

int wmain(int argc,wchar_t** argv) {
    if(argc<2 || !argv[1][0] || GetFileAttributesW(argv[1])==INVALID_FILE_ATTRIBUTES) {
        std::puts("SKIP: pass the supported EDF.dll path (read-only mapping; offline WARP only)");return 77;
    }
    const HMODULE module=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    if(!Check(module!=nullptr,"EDF.dll maps without executing its entrypoint"))return 1;
    struct Unmap { HMODULE module;~Unmap(){FreeLibrary(module);} } unmap{module};
    const auto image=reinterpret_cast<const unsigned char*>(module);
    const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    if(!Check(dos->e_magic==IMAGE_DOS_SIGNATURE && dos->e_lfanew>0,"native DOS header is valid"))return 1;
    const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(image+dos->e_lfanew);
    if(!Check(nt->Signature==IMAGE_NT_SIGNATURE && nt->FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64 &&
        nt->FileHeader.TimeDateStamp==0x678CCB46 && nt->OptionalHeader.SizeOfImage==0x22CE000 &&
        kShaderRva+crew::decaldepth::kNativeSize<=nt->OptionalHeader.SizeOfImage,"supported EDF.dll x64 shader profile"))return 1;
    CheckNativeBinding(image);
    if(failures)return 1;
    const auto invert=reinterpret_cast<NativeInverse>(const_cast<unsigned char*>(image)+0x71810);
    const auto source=image+kShaderRva;
    const std::vector<unsigned char> original(source,source+crew::decaldepth::kNativeSize);
    std::vector<unsigned char> patched;
    if(!Check(crew::decaldepth::Rewrite(source,original.size(),&patched),"production Rewrite accepts the exact native shader"))return 1;
    CheckRewrite(source,original.size(),patched);
    Check(std::memcmp(source,original.data(),original.size())==0,"rewriting never modifies the original native mapping");
    if(failures)return 1;
    Gpu gpu;D3D_FEATURE_LEVEL level{};const D3D_FEATURE_LEVEL requested[]={D3D_FEATURE_LEVEL_11_0};
    const HRESULT hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,requested,1,D3D11_SDK_VERSION,
        &gpu.device,&level,&gpu.context);
    if(FAILED(hr)){std::puts("SKIP: WARP is unavailable; native container checks passed");return 77;}
    ComPtr<ID3D11ComputeShader> nativeShader,patchedShader;
    if(!Result(gpu.device->CreateComputeShader(source,original.size(),nullptr,&nativeShader),"D3D11 accepts the exact native DXBC") ||
       !Result(gpu.device->CreateComputeShader(patched.data(),patched.size(),nullptr,&patchedShader),"D3D11 accepts production-rewritten DXBC and its retail checksum"))return 1;
    const float nearPlanes[]={0.1f,0.03f,0.5f},farPlanes[]={1000,20000,35000},distances[]={2,20,100};
    for(const float nearPlane:nearPlanes)for(const float farPlane:farPlanes)for(const float distance:distances)
        for(const bool rotated:{false,true}) {
            const auto input=Plane(nearPlane,farPlane,distance,rotated,invert);Output native{},corrected{};
            if(!gpu.Run(nativeShader.Get(),input,native) || !gpu.Run(patchedShader.Get(),input,corrected))return 1;
            CheckPlane(input,native,corrected,nearPlane,farPlane,distance,rotated);
        }
    if(failures){std::fprintf(stderr,"%d native decal checks failed\n",failures);return 1;}
    std::puts("ok: production DXBC rewrite and 54 original/rewritten WARP projection fixtures");return 0;
}
