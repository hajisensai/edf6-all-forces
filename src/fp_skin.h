// First-person topology, independent of GPU/game state. A triangle is an arm triangle only if all three vertices
// are weighted exclusively to the visible arm bones. Mixed shoulder/torso faces must not be stretched or collapsed.
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace crew::fpskin {
constexpr std::size_t kBones=256;
struct Mask { unsigned count=0; bool arm[kBones]{}; unsigned char side[kBones]{}; }; // 1 right, 2 left, 0 unspecified

inline bool Filter(const unsigned char* vertices,std::size_t bytes,unsigned stride,unsigned vertexOffset,
                   const std::uint16_t* indices,unsigned count,int base,const Mask& mask,
                   std::vector<std::uint16_t>* out) {
    out->clear();
    // Audited native soldier layouts: half4/float3 position, float4 weights, ubyte4 indices at the tail.
    if(!vertices || (stride!=60 && stride!=64) || count%3 || !indices || mask.count>kBones || !mask.count)return false;
    const std::size_t weights=stride-20,bones=stride-4;
    if(vertexOffset>bytes)return false;
    std::vector<unsigned char> arm((bytes-vertexOffset)/stride,0);
    for(std::size_t i=0;i<arm.size();++i) {
        const std::size_t at=vertexOffset+i*stride;
        if(at>bytes || bytes-at<stride)return false;
        float sum=0.0f;unsigned char visible=3;
        for(unsigned k=0;k<4;++k) {
            float w;std::memcpy(&w,vertices+at+weights+k*4,4);
            if(!std::isfinite(w) || w<0.0f || w>1.001f)return false;
            sum+=w;
            if(w>0.0f) {
                const unsigned b=vertices[at+bones+k];
                if(b>=mask.count)return false;
                visible=static_cast<unsigned char>(visible & (mask.arm[b] ? (mask.side[b] ? mask.side[b] : 3) : 0));
            }
        }
        if(std::fabs(sum-1.0f)>.01f)return false;
        arm[i]=visible;
    }
    for(unsigned i=0;i<count;i+=3) {
        unsigned char visible=3;
        for(unsigned k=0;k<3;++k) {
            const std::int64_t v=static_cast<std::int64_t>(indices[i+k])+base;
            if(v<0 || static_cast<std::uint64_t>(v)>=arm.size())return false;
            visible=static_cast<unsigned char>(visible & arm[static_cast<std::size_t>(v)]);
        }
        if(visible)out->insert(out->end(),indices+i,indices+i+3);
    }
    return true;
}
} // namespace crew::fpskin
