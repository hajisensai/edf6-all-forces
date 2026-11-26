#include "native_body_math.h"
#include "crew.h"
#include "perf_probe.h"
#include "edf/memory.h"
#include "edf/patch.h"
#include <cfloat>
#include <cstring>
#include <emmintrin.h>

namespace crew {
namespace {
constexpr unsigned kTransformBox=0x652F0,kBoxArguments=0x109BFD,kBoxCall=0x109C09,kSignMask=0x1C36A90;
constexpr unsigned kBasis=0x511C0;
constexpr unsigned kBoxMerge=kBoxArguments,kBoxMergeEnd=0x109C9D,kBoxMergeCall=kBoxMerge+7;
// Exact straight-line region through the last store to the union; the native body update follows it.
constexpr unsigned char kBoxMergeStock[]={
    0x48,0x8D,0x53,0x20,0x4C,0x8D,0x43,0x60,0x48,0x8D,0x4B,0x40,0xE8,0xE2,0xB6,0xF5,
    0xFF,0x0F,0x10,0x43,0x40,0x4C,0x8D,0x4D,0x20,0x0F,0x10,0x63,0x50,0x4C,0x8D,0x43,
    0x60,0x0F,0x10,0x5B,0x40,0x0F,0x28,0xD0,0x0F,0x28,0x4D,0x00,0x0F,0x5C,0xD4,0x0F,
    0xC6,0xC0,0xFF,0x0F,0x58,0xE3,0x0F,0xC6,0xDB,0xFF,0xC7,0x44,0x24,0x20,0x01,0x00,
    0x00,0x00,0x0F,0xC6,0xD2,0x93,0xF3,0x0F,0x10,0xD0,0x0F,0xC6,0xE4,0x93,0x0F,0xC6,
    0xD2,0x39,0xF3,0x0F,0x10,0xE3,0x0F,0x5D,0xCA,0x0F,0x29,0x55,0x20,0x0F,0x28,0x55,
    0x10,0x0F,0x28,0xC1,0x0F,0xC6,0xC1,0x55,0xF3,0x0F,0x11,0x45,0x04,0xF3,0x0F,0x11,
    0x4D,0x00,0x0F,0xC6,0xC9,0xAA,0xF3,0x0F,0x11,0x4D,0x08,0x0F,0xC6,0xE4,0x39,0x0F,
    0x5F,0xD4,0x0F,0x29,0x65,0x20,0xF3,0x0F,0x11,0x55,0x10,0x0F,0x28,0xC2,0x0F,0xC6,
    0xC2,0x55,0xF3,0x0F,0x11,0x45,0x14,0x0F,0xC6,0xD2,0xAA,0xF3,0x0F,0x11,0x55,0x18,
};
static_assert(sizeof(kBoxMergeStock)==kBoxMergeEnd-kBoxMerge);
constexpr unsigned char kBoxSig[]={0x48,0x83,0xEC,0x18,0xF3,0x0F,0x10,0x1D,0x94,0x17,0xBD,0x01};
constexpr unsigned char kBoxArgumentsSig[]={0x48,0x8D,0x53,0x20,0x4C,0x8D,0x43,0x60,0x48,0x8D,0x4B,0x40};
constexpr unsigned char kBasisSig[]={0x48,0x8B,0xC4,0x53,0x48,0x81,0xEC,0xD0,0x00,0x00,0x00};
struct BasisCall {
    unsigned arguments,call;
    unsigned char argumentSig[5],callSig[5];
};
// Each mode builds its own 4x4 scratch matrix and normalizes it before passing it to a different consumer.
constexpr BasisCall kAttachedBasisCalls[]={
    {0x6ED902,0x6ED9EB,{0x48,0x8D,0x4C,0x24,0x20},{0xE8,0xD0,0x37,0x96,0xFF}},
    {0x6EDB05,0x6EDBF4,{0x48,0x8D,0x4C,0x24,0x60},{0xE8,0xC7,0x35,0x96,0xFF}},
};
constexpr unsigned kAvoidanceSift=0xD5E020,kAvoidanceSiftEnd=0xD5E0F7;
struct AvoidanceCall {
    unsigned call;
    unsigned char stock[10];
};
constexpr AvoidanceCall kAvoidanceCalls[]{
    {0xD5F5A5,{0x8B,0xD7,0x48,0x8B,0xCB,0xE8,0x76,0xEA,0xFF,0xFF}},
    {0xD5F604,{0x75,0x08,0x48,0x8B,0xCB,0xE8,0x17,0xEA,0xFF,0xFF}},
};
static_assert(sizeof(kAttachedBasisCalls)/sizeof(kAttachedBasisCalls[0])+
              sizeof(kAvoidanceCalls)/sizeof(kAvoidanceCalls[0])+3==kNativeBodyMathSites);

struct AvoidanceNeighbor { std::uint64_t handle; float distanceSquared; std::uint32_t padding; };
struct AvoidanceHeap { AvoidanceNeighbor* entries; int count; };
static_assert(sizeof(AvoidanceNeighbor)==0x10 && offsetof(AvoidanceNeighbor,distanceSquared)==8 &&
              offsetof(AvoidanceHeap,count)==8);

int __fastcall SiftAvoidanceNeighbors(AvoidanceHeap* heap,int root) noexcept {
    const int inner=heap->count/2;
    if(root>=inner)return root;
    auto* const entries=heap->entries;
    const auto handle=entries[root].handle;
    const float distance=entries[root].distanceSquared;
    int hole=root;
    while(hole<inner) {
        const int left=hole*2+1,right=left+1;
        // Stock picks the right child on a tie or unordered comparison.
        const int child=right<heap->count && !(entries[left].distanceSquared>entries[right].distanceSquared) ?
            right : left;
        if(!(entries[child].distanceSquared>distance))break;
        entries[hole].handle=entries[child].handle;
        entries[hole].distanceSquared=entries[child].distanceSquared;
        hole=child;
    }
    if(hole!=root) {
        // The native swap only moves the handle and distance. Leave the trailing word in its original slot.
        entries[hole].handle=handle;
        entries[hole].distanceSquared=distance;
    }
    return hole;
}

unsigned InstallAvoidanceHeap() noexcept {
    const auto* const stock=image+kAvoidanceSift;
    constexpr unsigned bytes=kAvoidanceSiftEnd-kAvoidanceSift;
    if(!edf::Readable(stock,bytes))return 0;
    std::uint64_t fingerprint=14695981039346656037ull;
    for(unsigned i=0;i<bytes;++i)fingerprint=(fingerprint^stock[i])*1099511628211ull;
    if(fingerprint!=0xD8C5803BAE3519BBull)return 0;
    unsigned installed=0;
    for(const auto& site:kAvoidanceCalls) {
        if(!edf::Matches(image,site.call-5,site.stock,sizeof(site.stock)))continue;
        bool changed=false;
        edf::RedirectCall(image+site.call,image+kAvoidanceSift,reinterpret_cast<void*>(&SiftAvoidanceNeighbors),changed);
        installed+=changed;
    }
    return installed;
}

constexpr unsigned kStreamCall=0xE782CB,kStockStream=0xE776F0,kContactCommand=0x12FE960;
constexpr unsigned char kStreamCaller[]={0x4C,0x8D,0x4D,0x07,0x49,0x8B,0xD6,0x49,0x8B,0xCC,0xE8,0x20,0xF4,0xFF,0xFF};
constexpr unsigned char kStockStreamSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18};
constexpr unsigned char kContactCommandSig[]={0x0F,0xB6,0x42,0x03,0x48,0x8B,0x4C,0xC1,0x08,0x48,0x8B,0x01,0x48,0xFF,0x60,0x18};
constexpr unsigned kStreamPayload=0x20;

struct ContactRange {
    unsigned char* block;
    std::uint16_t offset,firstCount;
    int count;
    const ContactRange* next;
};
static_assert(offsetof(ContactRange,count)==0xC && offsetof(ContactRange,next)==0x10);
using ContactStreamFn=void(__fastcall*)(void*,void*,const ContactRange*,void*);
using ContactCommandFn=void(__fastcall*)(void*,unsigned char*);

template<bool tracing>
void ContactRanges(void* storage,void* dispatcher,const ContactRange* range,void* writer) {
    const auto stock=reinterpret_cast<ContactStreamFn>(image+kStockStream);
    for(;range;range=range->next) {
        int inBlock=range->firstCount;
        if(!range->count || !inBlock)continue;
        auto* block=range->block;
        if(!block || range->count<inBlock) {
            stock(storage,dispatcher,range,writer);
            return;
        }
        auto* command=block+kStreamPayload+range->offset;
        int remaining=range->count-inBlock;
        for(;;) {
            for(int i=0;i<inBlock;++i) {
                if(command[2]&1) {
                    const unsigned kind=command[3];
                    if constexpr(tracing)perfprobe::ObserveContactCommand(kind);
                    auto* const handler=edf::At<void*>(dispatcher,(kind+1)*sizeof(void*));
                    edf::At<ContactCommandFn>(edf::At<const void*>(handler,0),0x18)(handler,command);
                } else {
                    // Deferred records still use the original writer and its allocation path.
                    const auto offset=static_cast<std::uint16_t>(command-(block+kStreamPayload));
                    const ContactRange single{block,offset,1,1,nullptr};
                    stock(storage,dispatcher,&single,writer);
                }
                // Subscribers run first: read the record length after they have returned.
                command+=edf::At<std::uint16_t>(command,0);
            }
            if(!remaining)break;
            block=edf::At<unsigned char*>(block,8);
            if(!block)break;
            const int available=edf::At<std::uint16_t>(block,0);
            inBlock=available<remaining ? available : remaining;
            if(!inBlock)break;
            remaining-=inBlock;
            command=block+kStreamPayload;
            _mm_prefetch(edf::At<const char*>(block,8),_MM_HINT_NTA);
            _mm_prefetch(reinterpret_cast<const char*>(block+0x80),_MM_HINT_NTA);
        }
    }
}

void __fastcall DispatchContactRanges(void* storage,void* dispatcher,const ContactRange* range,void* writer) {
    if(perfprobe::ContactCommandsActive())ContactRanges<true>(storage,dispatcher,range,writer);
    else ContactRanges<false>(storage,dispatcher,range,writer);
}

bool InstallContactStream() noexcept {
    if(!edf::Matches(image,kStreamCall-10,kStreamCaller,sizeof(kStreamCaller)) ||
       !edf::Matches(image,kStockStream,kStockStreamSig,sizeof(kStockStreamSig)) ||
       !edf::Matches(image,kContactCommand,kContactCommandSig,sizeof(kContactCommandSig)))return false;
    bool changed=false;
    edf::RedirectCall(image+kStreamCall,image+kStockStream,reinterpret_cast<void*>(&DispatchContactRanges),changed);
    return changed;
}

constexpr unsigned kClingGate=0x6DAC9B,kClingBody=0x6DACD5;
constexpr unsigned char kClingFirstCall[]={0xE8,0x45,0x26,0xAD,0x00};
constexpr unsigned char kClingDecodeCall[]={0xE8,0xAB,0x77,0x6F,0x00};
constexpr unsigned char kClingGateStock[]={
    0x80,0x78,0x28,0x00,0x0F,0x85,0x87,0x01,0x00,0x00,0x0F,0xB6,0x43,0x10,
    0x84,0xC0,0x0F,0x84,0x7B,0x01,0x00,0x00,0x3C,0x02,0x76,0x08,0x3C,0x04,
    0x0F,0x85,0x6F,0x01,0x00,0x00,
};
constexpr unsigned char kClingGatePatched[]={
    0x48,0x89,0x44,0x24,0x30, // Save the first lookup in the existing body slot.
    0x80,0x78,0x28,0,0x75,0x10,0x0F,0xB6,0x43,0x10,0x84,0xC0,0x74,0x08,
    0x3C,0x02,0x76,0x0B,0x3C,0x04,0x74,0x07,0xE9,0x71,0x01,0x00,0x00,0x90,0x90,
};
constexpr unsigned char kClingBodyStock[]={
    0x48,0x8B,0x0D,0x7C,0x7C,0x9D,0x01,0x4C,0x8B,0xE0,0x48,0x83,0xC1,0x10,0x41,0x83,
    0x7F,0x20,0x00,0x48,0x0F,0x44,0xF5,0x8B,0x14,0x1E,0xE8,0xEC,0x25,0xAD,0x00,
};
constexpr unsigned char kClingBodyPatched[]={
    0x4C,0x8B,0xE0,0x48,0x8B,0x44,0x24,0x30,0xEB,0x15, // Preserve the manifold result, then reuse the body.
    0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,
    0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,
};
static_assert(sizeof(kClingGateStock)==sizeof(kClingGatePatched));
static_assert(sizeof(kClingBodyStock)==sizeof(kClingBodyPatched));

bool InstallClingBodyReuse() noexcept {
    if(!edf::Matches(image,kClingGate-5,kClingFirstCall,sizeof(kClingFirstCall)) ||
       !edf::Matches(image,kClingBody-5,kClingDecodeCall,sizeof(kClingDecodeCall)) ||
       !edf::Matches(image,kClingGate,kClingGateStock,sizeof(kClingGateStock)) ||
       !edf::Matches(image,kClingBody,kClingBodyStock,sizeof(kClingBodyStock)))return false;
    // Nothing between the two lookups changes the event side or body mapping: the call only decodes its manifold.
    if(!edf::PatchCode(image+kClingGate,kClingGateStock,kClingGatePatched,sizeof(kClingGateStock)))return false;
    if(edf::PatchCode(image+kClingBody,kClingBodyStock,kClingBodyPatched,sizeof(kClingBodyStock)))return true;
    edf::PatchCode(image+kClingGate,kClingGatePatched,kClingGateStock,sizeof(kClingGateStock));
    return false;
}

inline __m128 XyzMask() noexcept { return _mm_castsi128_ps(_mm_set_epi32(0,-1,-1,-1)); }

inline __m128 KeepW(__m128 xyz,__m128 original) noexcept {
    const auto mask=XyzMask();
    return _mm_or_ps(_mm_and_ps(mask,xyz),_mm_andnot_ps(mask,original));
}

struct BoxVectors { __m128 centre,half; };

// The verified call passes disjoint centre/extent, matrix and output records. Preserve the native y+x+z(+w) sums.
__forceinline BoxVectors BoxTransform(const float* box,const float* matrix) noexcept {
    const auto centre=_mm_loadu_ps(box),half=_mm_loadu_ps(box+4);
    const auto m0=_mm_loadu_ps(matrix),m1=_mm_loadu_ps(matrix+4),m2=_mm_loadu_ps(matrix+8),m3=_mm_loadu_ps(matrix+12);
    const auto absBits=_mm_castsi128_ps(_mm_set1_epi32(0x7fffffff));
    const auto hx=_mm_and_ps(_mm_mul_ps(_mm_shuffle_ps(half,half,0x00),m0),absBits);
    const auto hy=_mm_and_ps(_mm_mul_ps(_mm_shuffle_ps(half,half,0x55),m1),absBits);
    const auto hz=_mm_and_ps(_mm_mul_ps(_mm_shuffle_ps(half,half,0xAA),m2),absBits);
    const auto x=_mm_mul_ps(_mm_shuffle_ps(centre,centre,0x00),m0);
    const auto y=_mm_mul_ps(_mm_shuffle_ps(centre,centre,0x55),m1);
    const auto z=_mm_mul_ps(_mm_shuffle_ps(centre,centre,0xAA),m2);
    const auto w=_mm_mul_ps(_mm_shuffle_ps(centre,centre,0xFF),m3);
    return {_mm_add_ps(_mm_add_ps(_mm_add_ps(y,x),z),w),
            KeepW(_mm_add_ps(_mm_add_ps(hy,hx),hz),_mm_set1_ps(1.0f))};
}

void __fastcall TransformBox(float* out,const float* box,const float* matrix) noexcept {
    const auto transformed=BoxTransform(box,matrix);
    _mm_storeu_ps(out+4,transformed.half);
    _mm_storeu_ps(out,transformed.centre);
}

void __fastcall TransformAndMergeBox(float* bounds,float* proxy) noexcept {
    const auto transformed=BoxTransform(proxy+0x20/sizeof(float),proxy+0x60/sizeof(float));
    const auto lo=_mm_loadu_ps(bounds),hi=_mm_loadu_ps(bounds+4);
    const auto shrink=_mm_sub_ps(transformed.centre,transformed.half);
    const auto grow=_mm_add_ps(transformed.half,transformed.centre);
    _mm_storeu_ps(proxy+0x50/sizeof(float),transformed.half);
    _mm_storeu_ps(proxy+0x40/sizeof(float),transformed.centre);
    _mm_storeu_ps(bounds,KeepW(_mm_min_ps(lo,shrink),lo));
    _mm_storeu_ps(bounds+4,KeepW(_mm_max_ps(hi,grow),hi));
}

bool InstallBoxMerge() noexcept {
    if(!edf::Matches(image,kBoxMerge,kBoxMergeStock,sizeof(kBoxMergeStock)))return false;
    auto* const site=image+kBoxMerge;
    auto* const target=edf::AllocateNearThunk(site,reinterpret_cast<void*>(&TransformAndMergeBox));
    if(!target)return false;
    unsigned char code[]={
        0x48,0x8D,0x4D,0x00,       // lea rcx,[rbp]      bounds
        0x48,0x8B,0xD3,            // mov rdx,rbx        proxy
        0xE8,0,0,0,0,              // call fused transform and union
        0x4C,0x8D,0x4D,0x20,       // lea r9,[rbp+0x20]  native position arguments
        0x4C,0x8D,0x43,0x60,       // lea r8,[rbx+0x60]
        0xC7,0x44,0x24,0x20,1,0,0,0,
        0xE9,0,0,0,0,              // resume the native body update
    };
    constexpr unsigned call=kBoxMergeCall-kBoxMerge;
    constexpr auto resume=static_cast<std::int32_t>(kBoxMergeEnd-kBoxMerge-sizeof(code));
    const auto delta=reinterpret_cast<std::intptr_t>(target)-reinterpret_cast<std::intptr_t>(site+call+5);
    if(delta>=INT32_MIN && delta<=INT32_MAX) {
        const auto rel32=static_cast<std::int32_t>(delta);
        std::memcpy(code+call+1,&rel32,sizeof(rel32));
        std::memcpy(code+sizeof(code)-sizeof(resume),&resume,sizeof(resume));
        if(edf::PatchCode(site,kBoxMergeStock,code,sizeof(code)))return true;
    }
    VirtualFree(target,0,MEM_RELEASE);
    return false;
}

inline bool Normalize3(__m128& v) noexcept {
    v=KeepW(v,_mm_set1_ps(1.0f));
    const auto squared=_mm_mul_ps(v,v);
    const auto xy=_mm_add_ss(_mm_shuffle_ps(squared,squared,0x55),squared);
    const auto total=_mm_add_ss(xy,_mm_shuffle_ps(squared,squared,0xAA));
    if((_mm_movemask_ps(_mm_cmple_ss(total,_mm_set_ss(FLT_MAX)))&1)==0)return false;
    if(_mm_cvtss_f32(total)!=0.0f) {
        const auto len=_mm_sqrt_ss(total);
        v=_mm_div_ps(v,_mm_shuffle_ps(len,len,0));
    }
    return true;
}

inline __m128 Cross3(__m128 a,__m128 b) noexcept {
    const auto mask=XyzMask();
    a=_mm_and_ps(a,mask);b=_mm_and_ps(b,mask);
    const auto ayzx=_mm_shuffle_ps(a,a,_MM_SHUFFLE(3,0,2,1)),azxy=_mm_shuffle_ps(a,a,_MM_SHUFFLE(3,1,0,2));
    const auto byzx=_mm_shuffle_ps(b,b,_MM_SHUFFLE(3,0,2,1)),bzxy=_mm_shuffle_ps(b,b,_MM_SHUFFLE(3,1,0,2));
    return _mm_sub_ps(_mm_mul_ps(ayzx,bzxy),_mm_mul_ps(azxy,byzx));
}

void __fastcall NormalizeAttachedBasis(float* matrix) noexcept {
    const auto savedX=_mm_loadu_ps(matrix),savedY=_mm_loadu_ps(matrix+4),savedZ=_mm_loadu_ps(matrix+8);
    auto x=savedX;
    if(Normalize3(x)) {
        auto y=Cross3(savedZ,x);
        if(Normalize3(y)) {
            auto z=Cross3(x,y);
            if(Normalize3(z)) {
                _mm_storeu_ps(matrix,KeepW(x,savedX));
                _mm_storeu_ps(matrix+4,KeepW(y,savedY));
                _mm_storeu_ps(matrix+8,KeepW(z,savedZ));
                return;
            }
        }
    }
    // Keep the stock handling of overflowing or unordered intermediates, before writing any row.
    reinterpret_cast<void(__fastcall*)(float*)>(image+kBasis)(matrix);
}
}

unsigned InstallNativeBodyMath() noexcept {
    if(!image)return 0;
    unsigned installed=0;
    if(edf::Matches(image,kTransformBox,kBoxSig,sizeof(kBoxSig)) &&
       edf::Matches(image,kBoxArguments,kBoxArgumentsSig,sizeof(kBoxArgumentsSig)) &&
       edf::At<std::uint32_t>(image,kSignMask)==0x7fffffffu) {
        if(InstallBoxMerge())++installed;
        else {
            bool changed=false;
            edf::RedirectCall(image+kBoxCall,image+kTransformBox,reinterpret_cast<void*>(&TransformBox),changed);
            installed+=changed;
        }
    }
    installed+=InstallAvoidanceHeap();
    installed+=InstallClingBodyReuse();
    installed+=InstallContactStream();
    if(!edf::Matches(image,kBasis,kBasisSig,sizeof(kBasisSig)))return installed;
    for(const auto& site:kAttachedBasisCalls) {
        if(!edf::Matches(image,site.arguments,site.argumentSig,sizeof(site.argumentSig)) ||
           !edf::Matches(image,site.call,site.callSig,sizeof(site.callSig)))continue;
        bool changed=false;
        edf::RedirectCall(image+site.call,image+kBasis,reinterpret_cast<void*>(&NormalizeAttachedBasis),changed);
        installed+=changed;
    }
    return installed;
}
}
