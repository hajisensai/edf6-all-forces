#include "fp_skin_gpu.h"
#include "crew.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <array>

namespace crew::fpskin {
namespace {
using Microsoft::WRL::ComPtr;
thread_local bool active=false;
thread_local Mask selected{};
thread_local Stats last{};
constexpr std::size_t kSlots=32,kMostBytes=16u*1024u*1024u;
struct Entry {
    ComPtr<ID3D11Buffer> vb,ib,filtered;
    UINT stride=0,vo=0,io=0,first=0,count=0;
    INT base=0;
    Mask mask{};
    bool valid=false;
    UINT kept=0;
};
std::array<Entry,kSlots> entries;
std::size_t replaceAt=0;
SRWLOCK cacheLock=SRWLOCK_INIT;

bool SameMask(const Mask& a,const Mask& b) noexcept {
    return a.count==b.count && std::memcmp(a.arm,b.arm,sizeof(a.arm))==0 && std::memcmp(a.side,b.side,sizeof(a.side))==0;
}

bool Read(ID3D11Device* device,ID3D11Buffer* source,std::vector<unsigned char>& bytes) {
    D3D11_BUFFER_DESC desc{};source->GetDesc(&desc);
    if(!desc.ByteWidth || desc.ByteWidth>kMostBytes)return false;
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    desc.MiscFlags=0;desc.StructureByteStride=0;
    ComPtr<ID3D11Buffer> staging;
    if(FAILED(device->CreateBuffer(&desc,nullptr,&staging)))return false;
    ComPtr<ID3D11DeviceContext> immediate;device->GetImmediateContext(&immediate);
    immediate->CopyResource(staging.Get(),source);
    bytes.resize(desc.ByteWidth);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if(FAILED(immediate->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)))return false;
    std::memcpy(bytes.data(),mapped.pData,desc.ByteWidth);
    immediate->Unmap(staging.Get(),0);
    return true;
}

Entry* Get(ID3D11DeviceContext* context,ID3D11Buffer* vb,ID3D11Buffer* ib,UINT stride,UINT vo,UINT io,
           UINT count,UINT first,INT base) {
    for(Entry& e:entries)
        if(e.vb.Get()==vb && e.ib.Get()==ib && e.stride==stride && e.vo==vo && e.io==io &&
           e.count==count && e.first==first && e.base==base && SameMask(e.mask,selected))return &e;
    Entry& e=entries[replaceAt++%kSlots];e=Entry{};
    e.vb=vb;e.ib=ib;e.stride=stride;e.vo=vo;e.io=io;e.count=count;e.first=first;e.base=base;e.mask=selected;
    const auto failed=[&e](const char* why){Log("FP skin unavailable: %s (stride %u, indices %u)",why,e.stride,e.count);return &e;};
    if(context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return failed("uncached deferred draw context");
    ComPtr<ID3D11Device> device;context->GetDevice(&device);
    std::vector<unsigned char> vertices,indices;
    if(!Read(device.Get(),vb,vertices) || !Read(device.Get(),ib,indices))return failed("buffer readback");
    const std::size_t start=static_cast<std::size_t>(io)+static_cast<std::size_t>(first)*2;
    if(start%2 || start>indices.size() || static_cast<std::size_t>(count)*2>indices.size()-start)return failed("index span");
    std::vector<std::uint16_t> kept;
    if(!Filter(vertices.data(),vertices.size(),stride,vo,reinterpret_cast<const std::uint16_t*>(indices.data()+start),
               count,base,selected,&kept))return failed("unsupported skin layout");
    e.kept=static_cast<UINT>(kept.size());
    if(e.kept) {
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=e.kept*2;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA data{};data.pSysMem=kept.data();
        if(FAILED(device->CreateBuffer(&desc,&data,&e.filtered)))return failed("filtered index buffer");
    }
    e.valid=true;
    Log("FP skin: stride %u, indices %u -> %u (arm triangles only)",stride,count,e.kept);
    return &e;
}

} // namespace

void Submit(ID3D11DeviceContext* context,unsigned count,unsigned first,int base) {
    if(!active){context->DrawIndexed(count,first,base);return;}
    last=Stats{count,0,false};
    ComPtr<ID3D11Buffer> vb,ib;UINT stride=0,vo=0,io=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    context->IAGetVertexBuffers(0,1,&vb,&stride,&vo);context->IAGetIndexBuffer(&ib,&format,&io);
    if(!vb || !ib || format!=DXGI_FORMAT_R16_UINT)return;
    ComPtr<ID3D11Buffer> filtered;UINT kept=0;bool valid=false;
    AcquireSRWLockExclusive(&cacheLock);
    try {
        const Entry* e=Get(context,vb.Get(),ib.Get(),stride,vo,io,count,first,base);
        if(e){filtered=e->filtered;kept=e->kept;valid=e->valid;}
    } catch(const std::bad_alloc&) { Log("FP skin: allocation failed"); }
    ReleaseSRWLockExclusive(&cacheLock);
    // An unsupported layout never falls back to stretched body geometry. The weapon draw remains independent.
    last=Stats{count,kept,valid};
    if(!valid || !kept)return;
    context->IASetIndexBuffer(filtered.Get(),DXGI_FORMAT_R16_UINT,0);
    context->DrawIndexed(kept,0,base);
    context->IASetIndexBuffer(ib.Get(),format,io);
}
void Begin(const Mask& mask) noexcept { selected=mask;active=true; }
void End() noexcept { active=false; }
bool Active() noexcept { return active; }
MaskState SaveMask() noexcept { return MaskState{active,selected}; }
void RestoreMask(const MaskState& before) noexcept { selected=before.selected;active=before.active; }

void Clear() noexcept {
    active=false;AcquireSRWLockExclusive(&cacheLock);
    for(Entry& e:entries)e=Entry{};replaceAt=0;
    ReleaseSRWLockExclusive(&cacheLock);
}
Stats Last() noexcept { return last; }
} // namespace crew::fpskin
