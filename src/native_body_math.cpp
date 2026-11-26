#include "native_body_math.h"
#include "crew.h"
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
static_assert(sizeof(kAttachedBasisCalls)/sizeof(kAttachedBasisCalls[0])+1==kNativeBodyMathSites);

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
