// The stock decal compute shader linearises hardware depth with near=0.1/far=1000, even though its constant
// buffer already carries this draw's inverse view-projection. Only the five depth-reconstruction samples
// are changed here. All resource declarations, normal reconstruction, IDs, UAV writes and other chunks stay native.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace crew::decaldepth {
constexpr std::size_t kNativeSize=0x1010;
constexpr std::uint64_t kNativeFingerprint=0x185D549A9D3195B7ull;

inline std::uint32_t Word(const unsigned char* at) noexcept {
    std::uint32_t out=0;std::memcpy(&out,at,4);return out;
}
inline void PutWord(unsigned char* at,std::uint32_t value) noexcept { std::memcpy(at,&value,4); }
inline std::uint32_t Rotate(std::uint32_t value,unsigned count) noexcept {
    return (value<<count)|(value>>(32-count));
}
// DXBC's retail container checksum uses MD5's compression and Microsoft's final-block padding, not the standard
// MD5 digest. Format reference: Microsoft DirectXShaderCompiler lib/DxilHash/DxilHash.cpp, ComputeHashRetail.
// This independently written loop hashes the container from byte 20 (the version word) to its end.
inline void Compress(const unsigned char* block,std::array<std::uint32_t,4>& state) noexcept {
    constexpr std::uint32_t t[]={
        0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
        0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
        0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
        0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
        0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
        0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
        0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
        0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391};
    constexpr unsigned shifts[4][4]={{7,12,17,22},{5,9,14,20},{4,11,16,23},{6,10,15,21}};
    std::uint32_t a=state[0],b=state[1],c=state[2],d=state[3];
    for(unsigned i=0;i<64;++i) {
        std::uint32_t f=0;unsigned g=0;
        if(i<16){f=(b&c)|(~b&d);g=i;}
        else if(i<32){f=(d&b)|(~d&c);g=(5*i+1)%16;}
        else if(i<48){f=b^c^d;g=(3*i+5)%16;}
        else {f=c^(b|~d);g=(7*i)%16;}
        const std::uint32_t next=b+Rotate(a+f+t[i]+Word(block+g*4),shifts[i/16][i%4]);
        a=d;d=c;c=b;b=next;
    }
    state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;
}
inline std::array<std::uint32_t,4> Checksum(const unsigned char* data,std::size_t size) noexcept {
    std::array<std::uint32_t,4> state={0x67452301,0xefcdab89,0x98badcfe,0x10325476};
    const std::size_t complete=size&~std::size_t(63),remaining=size-complete;
    for(std::size_t at=0;at<complete;at+=64)Compress(data+at,state);
    unsigned char tail[128]{};
    if(remaining>=56) {
        std::memcpy(tail,data+complete,remaining);tail[remaining]=0x80;
        PutWord(tail+64,static_cast<std::uint32_t>(size)<<3);
        PutWord(tail+124,1u|(static_cast<std::uint32_t>(size)<<1));
        Compress(tail,state);Compress(tail+64,state);
    } else {
        PutWord(tail,static_cast<std::uint32_t>(size)<<3);
        std::memcpy(tail+4,data+complete,remaining);tail[4+remaining]=0x80;
        PutWord(tail+60,1u|(static_cast<std::uint32_t>(size)<<1));Compress(tail,state);
    }
    return state;
}
inline bool Rewrite(const void* source,std::size_t size,std::vector<unsigned char>* result) noexcept {
    if(!source || !result || size!=kNativeSize)return false;
    const auto bytes=static_cast<const unsigned char*>(source);
    std::uint64_t fingerprint=14695981039346656037ull;
    for(std::size_t i=0;i<size;++i)fingerprint=(fingerprint^bytes[i])*1099511628211ull;
    if(fingerprint!=kNativeFingerprint || std::memcmp(bytes,"DXBC",4)!=0 || Word(bytes+24)!=size || Word(bytes+28)!=5)return false;
    const auto originalChecksum=Checksum(bytes+20,size-20);
    if(std::memcmp(originalChecksum.data(),bytes+4,16)!=0)return false;
    try {
        std::vector<unsigned char> rewritten(bytes,bytes+52);unsigned changes=0;
        for(unsigned chunk=0;chunk<5;++chunk) {
            const std::size_t offset=Word(bytes+32+chunk*4);
            if(offset>size-8)return false;
            const std::size_t length=Word(bytes+offset+4);
            if(length>size-offset-8)return false;
            PutWord(rewritten.data()+32+chunk*4,static_cast<std::uint32_t>(rewritten.size()));
            if(std::memcmp(bytes+offset,"SHEX",4)!=0) {
                rewritten.insert(rewritten.end(),bytes+offset,bytes+offset+8+length);continue;
            }
            if(length%4 || length<8)return false;
            std::vector<std::uint32_t> words(length/4);std::memcpy(words.data(),bytes+offset+8,length);
            if(words[0]!=0x50050 || words[1]!=words.size())return false; // native cs_5_0
            std::vector<std::uint32_t> code(words.begin(),words.begin()+2);
            for(std::size_t i=2;i<words.size();) {
                const std::size_t count=(words[i]>>24)&0x7f;
                if(!count || count>words.size()-i)return false;
                const auto at=words.data()+i;
                if(count==9 && at[0]==0x09000032 && at[5]==0x4001 && at[6]==0x4479f99a && at[7]==0x4001 && at[8]==0xc47a0000) {
                    // mad z,d,999.9,-1000 -> mad w,d,CB0[6].z,CB0[6].w.
                    // The CPU transposes inverse(P)*inverse(V) into cb3..6; an affine inverse(V) preserves its
                    // homogeneous W column. Thus 1/w is the draw's positive view depth for any near/far plane.
                    if(i+16>words.size() || words[i+9]!=0x0700000e || words[i+12]!=0x4001 || words[i+13]!=0x42c80000)return false;
                    code.push_back(0x0b000032);code.insert(code.end(),at+1,at+5);
                    const std::uint32_t coefficients[]={0x20802a,0,6,0x20803a,0,6};
                    code.insert(code.end(),coefficients,coefficients+6);
                    // The original negated div remains: -(-1/w) = 1/w.
                    words[i+13]=0xbf800000;++changes;
                } else code.insert(code.end(),at,at+count);
                i+=count;
            }
            code[1]=static_cast<std::uint32_t>(code.size());
            const std::size_t begin=rewritten.size(),newLength=code.size()*4;
            rewritten.resize(begin+8+newLength);std::memcpy(rewritten.data()+begin,"SHEX",4);
            PutWord(rewritten.data()+begin+4,static_cast<std::uint32_t>(newLength));
            std::memcpy(rewritten.data()+begin+8,code.data(),newLength);
        }
        if(changes!=5)return false;
        PutWord(rewritten.data()+24,static_cast<std::uint32_t>(rewritten.size()));
        const auto checksum=Checksum(rewritten.data()+20,rewritten.size()-20);
        std::memcpy(rewritten.data()+4,checksum.data(),16);result->swap(rewritten);return true;
    } catch(...) { return false; }
}
} // namespace crew::decaldepth
