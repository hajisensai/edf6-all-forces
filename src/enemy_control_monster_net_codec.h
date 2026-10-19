// Accepted Monster projectile outcomes. Padded native structs and local pointers never cross the wire.
#pragma once
#include "enemy_control_projectile_native.h"
#include <cstring>

namespace crew::enemy_native::monster_wire {
inline constexpr std::uint32_t kTag=0x31504E4Du; // MNP1
inline constexpr std::uint8_t kVersion=1;
inline constexpr unsigned kMaxBatch=1;
inline constexpr std::size_t kMaxBytes=512,kHeaderBytes=8;
inline constexpr std::size_t kEventBytes=8+8+1+16*4+16*4+kProjectileWords*4+3*4+kProjectileBytes+2+32+32+4+1+1+1;
static_assert(kHeaderBytes+kEventBytes*kMaxBatch<=kMaxBytes,"complete accepted projectile fits the reliable envelope");
struct Event {
    std::uint64_t serial=0,digest=0;
    std::uint8_t family=0; // 1 = Monster501/King, 2 = Monster504
    NativeProjectileSample sample{};
};
inline bool Valid(const Event& event) noexcept {
    return event.serial && event.digest && (event.family==1 || event.family==2) && event.sample.resourceMask<=3 && ValidNativeProjectileSample(event.sample);
}
inline std::size_t EventBytes(const Event& event) noexcept {return Valid(event) ? kEventBytes : 0;}
inline std::size_t Bytes(const Event* events,unsigned count) noexcept {
    if(!events || !count || count>kMaxBatch)return 0;
    std::size_t size=kHeaderBytes;std::uint64_t previous=0;
    for(unsigned i=0;i<count;++i) {
        const auto bytes=EventBytes(events[i]);if(!bytes || events[i].serial<=previous)return 0;
        previous=events[i].serial;size+=bytes;
    }
    return size<=kMaxBytes ? size : 0;
}
namespace detail {
inline void Put(unsigned char*& out,std::uint64_t value,unsigned bytes) noexcept {
    for(unsigned i=0;i<bytes;++i)*out++=static_cast<unsigned char>(value>>(8*i));
}
inline std::uint64_t Get(const unsigned char*& in,unsigned bytes) noexcept {
    std::uint64_t value=0;for(unsigned i=0;i<bytes;++i)value|=std::uint64_t(*in++)<<(8*i);return value;
}
inline void PutFloat(unsigned char*& out,float value) noexcept {
    std::uint32_t bits=0;std::memcpy(&bits,&value,4);Put(out,bits,4);
}
inline float GetFloat(const unsigned char*& in) noexcept {
    const auto bits=static_cast<std::uint32_t>(Get(in,4));float value=0;std::memcpy(&value,&bits,4);return value;
}
inline void Write(unsigned char*& out,const Event& event) noexcept {
    Put(out,event.serial,8);Put(out,event.digest,8);Put(out,event.family,1);
    const auto& sample=event.sample;
    for(float value:sample.matrix)PutFloat(out,value);
    for(float value:sample.muzzleMatrix)PutFloat(out,value);
    for(auto value:sample.scalar)Put(out,value,4);
    Put(out,sample.random,4);Put(out,sample.shot,4);Put(out,sample.flags,4);
    for(auto value:sample.bytes)Put(out,value,1);
    Put(out,sample.scalar178,2);
    for(auto value:sample.target)Put(out,value,1);
    for(auto value:sample.extra)Put(out,value,1);
    Put(out,sample.lock,4);Put(out,sample.hasMuzzle ? 1u : 0u,1);Put(out,sample.extraObject ? 1u : 0u,1);
    Put(out,sample.resourceMask,1);
}
inline bool Read(const unsigned char*& in,Event& event) noexcept {
    event.serial=Get(in,8);event.digest=Get(in,8);event.family=static_cast<std::uint8_t>(Get(in,1));
    auto& sample=event.sample;
    for(float& value:sample.matrix)value=GetFloat(in);
    for(float& value:sample.muzzleMatrix)value=GetFloat(in);
    for(auto& value:sample.scalar)value=static_cast<std::uint32_t>(Get(in,4));
    sample.random=static_cast<std::uint32_t>(Get(in,4));sample.shot=static_cast<std::uint32_t>(Get(in,4));
    sample.flags=static_cast<std::uint32_t>(Get(in,4));
    for(auto& value:sample.bytes)value=static_cast<unsigned char>(Get(in,1));
    sample.scalar178=static_cast<std::uint16_t>(Get(in,2));
    for(auto& value:sample.target)value=static_cast<unsigned char>(Get(in,1));
    for(auto& value:sample.extra)value=static_cast<unsigned char>(Get(in,1));
    sample.lock=static_cast<std::uint32_t>(Get(in,4));
    const auto muzzle=Get(in,1),extra=Get(in,1),resources=Get(in,1);
    if(muzzle>1 || extra>1 || resources>3)return false;
    sample.hasMuzzle=muzzle!=0;sample.extraObject=extra!=0;sample.resourceMask=static_cast<unsigned char>(resources);return true;
}
} // namespace detail
inline bool Encode(const Event* events,unsigned count,void* bytes,std::size_t capacity,std::size_t* used) noexcept {
    if(used)*used=0;const auto needed=Bytes(events,count);
    if(!bytes || !used || !needed || needed>capacity)return false;
    auto* out=static_cast<unsigned char*>(bytes);detail::Put(out,kTag,4);detail::Put(out,kVersion,1);
    detail::Put(out,count,1);detail::Put(out,0,2);
    for(unsigned i=0;i<count;++i)detail::Write(out,events[i]);
    *used=needed;return true;
}
inline bool Decode(const void* bytes,std::size_t size,Event* events,unsigned capacity,unsigned* count) noexcept {
    if(count)*count=0;
    if(!bytes || !events || !count || size<kHeaderBytes || size>kMaxBytes)return false;
    const auto* in=static_cast<const unsigned char*>(bytes);
    if(detail::Get(in,4)!=kTag || detail::Get(in,1)!=kVersion)return false;
    const auto total=static_cast<unsigned>(detail::Get(in,1));
    if(!total || total>kMaxBatch || total>capacity || detail::Get(in,2) || size!=kHeaderBytes+total*kEventBytes)return false;
    Event pending[kMaxBatch]{};std::uint64_t previous=0;
    for(unsigned i=0;i<total;++i) {
        if(!detail::Read(in,pending[i]) || !Valid(pending[i]) || pending[i].serial<=previous)return false;
        previous=pending[i].serial;
    }
    for(unsigned i=0;i<total;++i)events[i]=pending[i];
    *count=total;return true;
}
inline std::uint64_t Hash(const Event& event) noexcept {
    if(!Valid(event))return 0;
    unsigned char bytes[kEventBytes]{};auto* out=bytes;detail::Write(out,event);
    std::uint64_t hash=1469598103934665603ull;
    for(auto value:bytes){hash^=value;hash*=1099511628211ull;}
    return hash;
}
} // namespace crew::enemy_native::monster_wire
