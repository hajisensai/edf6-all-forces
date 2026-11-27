#include "native_bone_range.h"
#include <immintrin.h>

namespace crew::native_bones {
namespace {
inline __m256 RepeatRow(const float* row) noexcept {
    const auto v=_mm_loadu_ps(row);
    return _mm256_insertf128_ps(_mm256_castps128_ps256(v),v,1);
}

// Keep the original addition chain, with two rows in separate lanes; no FMA or affine shortcuts.
inline __m256 ComposeRows(__m256 local,__m256 p0,__m256 p1,__m256 p2,__m256 p3) noexcept {
    const auto x=_mm256_mul_ps(_mm256_shuffle_ps(local,local,0x00),p0);
    const auto y=_mm256_mul_ps(_mm256_shuffle_ps(local,local,0x55),p1);
    const auto z=_mm256_mul_ps(_mm256_shuffle_ps(local,local,0xAA),p2);
    const auto w=_mm256_mul_ps(_mm256_shuffle_ps(local,local,0xFF),p3);
    return _mm256_add_ps(_mm256_add_ps(_mm256_add_ps(y,x),z),w);
}
}

void ApplyAvx(unsigned char* records,int begin,int end) noexcept {
    // The original iteration order lets each descendant read its parent's updated world pose.
    auto* const limit=records+static_cast<std::ptrdiff_t>(end)*kStride;
    for(auto* record=records+static_cast<std::ptrdiff_t>(begin)*kStride;record!=limit;record+=kStride) {
        if(record[kOwnedLocally]!=1)continue;
        const auto index=edf::At<std::int32_t>(record,kParent);
        const auto* const parent=reinterpret_cast<const float*>(records+static_cast<std::ptrdiff_t>(index)*kStride+kWorld);
        const auto* const local=reinterpret_cast<const float*>(record+kLocal);
        auto* const world=reinterpret_cast<float*>(record+kWorld);
        const auto p0=RepeatRow(parent),p1=RepeatRow(parent+4),p2=RepeatRow(parent+8),p3=RepeatRow(parent+12);
        _mm256_storeu_ps(world,ComposeRows(_mm256_loadu_ps(local),p0,p1,p2,p3));
        _mm256_storeu_ps(world+8,ComposeRows(_mm256_loadu_ps(local+8),p0,p1,p2,p3));
    }
    _mm256_zeroupper();
}
}
