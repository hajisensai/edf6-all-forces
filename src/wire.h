// The plugin's online messages' common codec (plugin_net.h channels; gravity_net_protocol.cpp, plugin_net_logic.h's
// roster): fixed little-endian fields written and read in order, with the checks every channel made by hand before (a
// float finite, an ID in the native canonical form). Pure: no game, no transport.
//  - Writer zero-fills its buffer first: a field never written (reserved, padding) goes out as zero.
//  - Reader fails sticky: a read past the end, a float not finite (or past its bound), an ID not canonical leaves Ok()
//    false, and every later read returns zero; a decoder reads everything, then asks Ok() once.
// Native IDs (support_soldier.h ReadNativeObjectId, command_identity.h): 32 bytes, bytes 20..23 zero (the NetId's padding,
// never identity), or all zero (none given). Object references (net_object_ref.h: a player by its mission slot, anything
// else by its NetId): Ref, which also takes a player's; every field that names an object a player may be (a caller, a
// grabber, a target, a focus) goes by Ref, never Id.
#pragma once
#include "net_object_ref.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace crew::wire {
constexpr std::size_t kIdBytes=32;
constexpr float kMostCoordinate=1.0e7f;   // a position or velocity component past this is not a world's

inline bool CanonicalId(const unsigned char* id) noexcept {
    if(!id)return false;
    for(std::size_t i=20;i<24;++i)if(id[i])return false;
    return true;
}
inline bool ZeroId(const unsigned char* id) noexcept {
    if(!id)return true;
    for(std::size_t i=0;i<kIdBytes;++i)if(id[i])return false;
    return true;
}

class Writer {
public:
    Writer(void* bytes,std::size_t size) noexcept : p_(static_cast<unsigned char*>(bytes)),size_(bytes ? size : 0) {
        if(p_)std::memset(p_,0,size_);
        ok_=p_!=nullptr;
    }
    Writer& U8(std::uint8_t v) noexcept { if(Room(1))p_[at_++]=v; return *this; }
    Writer& U16(std::uint16_t v) noexcept { if(Room(2)){p_[at_++]=static_cast<unsigned char>(v);p_[at_++]=static_cast<unsigned char>(v>>8);} return *this; }
    Writer& U32(std::uint32_t v) noexcept { if(Room(4))for(int i=0;i<4;++i)p_[at_++]=static_cast<unsigned char>(v>>(8*i)); return *this; }
    Writer& F(float f) noexcept {
        if(!std::isfinite(f)){ok_=false;return *this;}
        std::uint32_t v;std::memcpy(&v,&f,4);return U32(v);
    }
    Writer& V3(const float* v) noexcept { const float z[3]{};if(!v)v=z;return F(v[0]).F(v[1]).F(v[2]); }
    // A native ID (null: all zero); a non-canonical one fails the message.
    Writer& Id(const unsigned char* id) noexcept {
        if(id && !CanonicalId(id)){ok_=false;return *this;}
        if(Room(kIdBytes)){if(id)std::memcpy(p_+at_,id,kIdBytes);at_+=kIdBytes;}
        return *this;
    }
    // An object reference (net_object_ref.h; null: none). One that is not Valid (`allowNone`, `playerOnly`) fails the message.
    Writer& Ref(const unsigned char* ref,bool allowNone=true,bool playerOnly=false) noexcept {
        if(!netref::Valid(ref,allowNone,playerOnly)){ok_=false;return *this;}
        if(Room(kIdBytes)){if(ref)std::memcpy(p_+at_,ref,kIdBytes);at_+=kIdBytes;}
        return *this;
    }
    Writer& Bytes(const void* b,std::size_t n) noexcept { if(Room(n)){if(b)std::memcpy(p_+at_,b,n);at_+=n;} return *this; }
    Writer& Skip(std::size_t n) noexcept { if(Room(n))at_+=n; return *this; }
    Writer& Seek(std::size_t at) noexcept { if(at>size_)ok_=false;else at_=at; return *this; }
    bool Ok() const noexcept { return ok_; }
    std::size_t Used() const noexcept { return at_; }
private:
    bool Room(std::size_t n) noexcept { if(!ok_ || n>size_-at_){ok_=false;return false;} return true; }
    unsigned char* p_=nullptr;
    std::size_t size_=0,at_=0;
    bool ok_=false;
};

class Reader {
public:
    Reader(const void* bytes,std::size_t size) noexcept : p_(static_cast<const unsigned char*>(bytes)),size_(bytes ? size : 0),ok_(bytes!=nullptr) {}
    std::uint8_t U8() noexcept { return Room(1) ? p_[at_++] : 0; }
    std::uint16_t U16() noexcept {
        if(!Room(2))return 0;
        const std::uint16_t v=static_cast<std::uint16_t>(p_[at_]|(p_[at_+1]<<8));at_+=2;return v;
    }
    std::uint32_t U32() noexcept {
        if(!Room(4))return 0;
        std::uint32_t v=0;for(int i=0;i<4;++i)v|=static_cast<std::uint32_t>(p_[at_++])<<(8*i);
        return v;
    }
    // A finite float of magnitude at most `most`; anything else fails the message (0 returned).
    float F(float most=kMostCoordinate) noexcept {
        const std::uint32_t v=U32();float f;std::memcpy(&f,&v,4);
        if(!std::isfinite(f) || std::fabs(f)>most){ok_=false;return 0.0f;}
        return ok_ ? f : 0.0f;
    }
    void V3(float* out,float most=kMostCoordinate) noexcept { for(int i=0;i<3;++i){const float f=F(most);if(out)out[i]=f;} }
    // A native ID: canonical (or all zero); anything else fails the message (zeros written).
    void Id(unsigned char* out) noexcept {
        if(!Room(kIdBytes)){if(out)std::memset(out,0,kIdBytes);return;}
        const unsigned char* id=p_+at_;at_+=kIdBytes;
        if(!CanonicalId(id)){ok_=false;if(out)std::memset(out,0,kIdBytes);return;}
        if(out)std::memcpy(out,id,kIdBytes);
    }
    // An object reference (net_object_ref.h): one not Valid (`allowNone`, `playerOnly`) fails the message (zeros written).
    void Ref(unsigned char* out,bool allowNone=true,bool playerOnly=false) noexcept {
        if(!Room(kIdBytes)){if(out)std::memset(out,0,kIdBytes);return;}
        const unsigned char* ref=p_+at_;at_+=kIdBytes;
        if(!netref::Valid(ref,allowNone,playerOnly)){ok_=false;if(out)std::memset(out,0,kIdBytes);return;}
        if(out)std::memcpy(out,ref,kIdBytes);
    }
    void Bytes(void* out,std::size_t n) noexcept {
        if(!Room(n)){if(out)std::memset(out,0,n);return;}
        if(out)std::memcpy(out,p_+at_,n);
        at_+=n;
    }
    Reader& Skip(std::size_t n) noexcept { if(Room(n))at_+=n; return *this; }
    Reader& Seek(std::size_t at) noexcept { if(at>size_)ok_=false;else at_=at; return *this; }
    void Fail() noexcept { ok_=false; }
    bool Ok() const noexcept { return ok_; }
    std::size_t Used() const noexcept { return at_; }
    std::size_t Size() const noexcept { return size_; }
private:
    bool Room(std::size_t n) noexcept { if(!ok_ || n>size_-at_){ok_=false;return false;} return true; }
    const unsigned char* p_=nullptr;
    std::size_t size_=0,at_=0;
    bool ok_=false;
};

// The first two words of a channel's message (plugin_net_logic.h): its magic and version, 0 when too short.
inline std::uint32_t HeadWord(const void* bytes,std::size_t size,std::size_t word) noexcept {
    if(!bytes || size<(word+1)*4)return 0;
    Reader r(bytes,size);r.Skip(word*4);return r.U32();
}
}  // namespace crew::wire
