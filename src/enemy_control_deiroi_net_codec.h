// Explicit little-endian Deiroi part events. Native pointers and padded structs never cross the wire.
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace crew::enemy_native::deiroi_wire {
constexpr std::uint32_t kMagic=0x31474445u;
constexpr unsigned kMaxBatch=12;
constexpr std::size_t kMaxBytes=512;
constexpr std::size_t kHeaderBytes=8;
constexpr std::size_t kControlBytes=42;
constexpr std::size_t kShotBytes=121;
constexpr std::size_t kEntityBytes=161;
constexpr std::uint8_t kVersion=1;

enum class Kind : std::uint8_t { control=1,shot=2,entity=3 };
struct Event {
    Kind kind{};
    std::uint64_t serial{};
    std::uint32_t key{};
    std::uint8_t phase{};
    std::uint64_t commandSequence{},seed{};
    std::uint32_t shotIndex{},rng90{},rng94{};
    // Actual InitParam words: +90 team, +94 speed, +98 pattern, +9C power, +1A0/+1A4.
    std::uint32_t actual[6]{};
    float point[3]{},matrix[16]{};
    std::uint32_t entityTeam{};
    unsigned char targetActorId[32]{};
    std::uint32_t lockOrdinal{};
};

inline bool Valid(const Event& event) noexcept {
    const auto group=event.key>>16,part=event.key&0xFFFFu;
    if(!event.serial || group>32 || part>=(group==32 ? 64u : 128u))return false;
    if(event.kind==Kind::control) {
        if((group==32 ? (event.phase<6 || event.phase>7) : event.phase>5) ||
           (event.phase<2 && (!event.commandSequence || !event.seed)))return false;
        for(float value:event.point)if(!std::isfinite(value))return false;
        return true;
    }
    if((event.kind!=Kind::shot && event.kind!=Kind::entity) || event.shotIndex>0x7FFFFFFEu)return false;
    if(event.kind==Kind::entity) {
        unsigned targetBits=0;
        for(unsigned i=0;i<32;++i) {
            const auto value=event.targetActorId[i];
            if(i>=20 && i<24 && value)return false;
            targetBits|=value;
        }
        if(!targetBits && event.lockOrdinal)return false;
    }
    float speed=0,power=0;
    std::memcpy(&speed,event.actual+1,sizeof(speed));std::memcpy(&power,event.actual+3,sizeof(power));
    if(!std::isfinite(speed) || !std::isfinite(power))return false;
    for(float value:event.matrix)if(!std::isfinite(value))return false;
    if(std::fabs(event.matrix[15]-1.0f)>1.0e-4f)return false;
    // Keep native scale, fan patterns and roll. Only reject a collapsed rotation frame.
    double rows[3][3]{},norms[3]{};
    for(unsigned i=0;i<3;++i) {
        for(unsigned j=0;j<3;++j) {
            rows[i][j]=event.matrix[i*4+j];
            norms[i]+=rows[i][j]*rows[i][j];
        }
        if(!(norms[i]>0.0) || !std::isfinite(norms[i]))return false;
    }
    const double determinant=rows[0][0]*(rows[1][1]*rows[2][2]-rows[1][2]*rows[2][1])
        -rows[0][1]*(rows[1][0]*rows[2][2]-rows[1][2]*rows[2][0])
        +rows[0][2]*(rows[1][0]*rows[2][1]-rows[1][1]*rows[2][0]);
    const double scale=std::sqrt(norms[0]*norms[1]*norms[2]);
    return std::isfinite(determinant) && std::isfinite(scale) && std::fabs(determinant)>scale*1.0e-8;
}
inline std::size_t EventBytes(const Event& event) noexcept {
    if(!Valid(event))return 0;
    return event.kind==Kind::control ? kControlBytes : event.kind==Kind::entity ? kEntityBytes : kShotBytes;
}

namespace detail {
inline void Put(unsigned char*& out,std::uint64_t value,unsigned bytes) noexcept {
    for(unsigned i=0;i<bytes;++i)*out++=static_cast<unsigned char>(value>>(i*8));
}
inline std::uint64_t Get(const unsigned char*& in,unsigned bytes) noexcept {
    std::uint64_t value=0;
    for(unsigned i=0;i<bytes;++i)value|=static_cast<std::uint64_t>(*in++)<<(i*8);
    return value;
}
inline void PutFloat(unsigned char*& out,float value) noexcept {
    std::uint32_t bits=0;std::memcpy(&bits,&value,sizeof(bits));Put(out,bits,4);
}
inline float GetFloat(const unsigned char*& in) noexcept {
    const auto bits=static_cast<std::uint32_t>(Get(in,4));
    float value=0;std::memcpy(&value,&bits,sizeof(value));return value;
}
} // namespace detail

inline bool Encode(const Event* events,unsigned count,void* bytes,std::size_t capacity,std::size_t* used) noexcept {
    if(used)*used=0;
    if(!events || !bytes || !count || count>kMaxBatch)return false;
    std::size_t needed=kHeaderBytes;
    std::uint64_t previous=0;
    for(unsigned i=0;i<count;++i) {
        const auto size=EventBytes(events[i]);
        if(!size || events[i].serial<=previous)return false;
        previous=events[i].serial;needed+=size;
    }
    if(needed>capacity || needed>kMaxBytes)return false;
    auto* out=static_cast<unsigned char*>(bytes);
    detail::Put(out,kMagic,4);*out++=kVersion;*out++=static_cast<unsigned char>(count);
    *out++=0;*out++=0;
    for(unsigned i=0;i<count;++i) {
        const auto& event=events[i];
        *out++=static_cast<unsigned char>(event.kind);
        detail::Put(out,event.serial,8);detail::Put(out,event.key,4);
        if(event.kind==Kind::control) {
            *out++=event.phase;detail::Put(out,event.commandSequence,8);detail::Put(out,event.seed,8);
            for(float value:event.point)detail::PutFloat(out,value);
        } else {
            detail::Put(out,event.shotIndex,4);detail::Put(out,event.seed,8);
            detail::Put(out,event.rng90,4);detail::Put(out,event.rng94,4);
            for(auto word:event.actual)detail::Put(out,word,4);
            for(float value:event.matrix)detail::PutFloat(out,value);
            if(event.kind==Kind::entity) {
                detail::Put(out,event.entityTeam,4);
                for(auto byte:event.targetActorId)*out++=byte;
                detail::Put(out,event.lockOrdinal,4);
            }
        }
    }
    if(used)*used=needed;
    return true;
}

inline bool Decode(const void* bytes,std::size_t size,Event* events,unsigned capacity,unsigned* count) noexcept {
    if(count)*count=0;
    if(!bytes || !events || !count || size<kHeaderBytes || size>kMaxBytes)return false;
    auto* in=static_cast<const unsigned char*>(bytes);
    if(detail::Get(in,4)!=kMagic || *in++!=kVersion)return false;
    const auto total=static_cast<unsigned>(*in++);
    if(!total || total>kMaxBatch || total>capacity || *in++ || *in++)return false;
    const auto* end=static_cast<const unsigned char*>(bytes)+size;
    Event pending[kMaxBatch]{};
    std::uint64_t previous=0;
    for(unsigned i=0;i<total;++i) {
        if(in==end)return false;
        auto& event=pending[i];
        event.kind=static_cast<Kind>(*in);
        const auto needed=event.kind==Kind::control ? kControlBytes : event.kind==Kind::shot ? kShotBytes :
            event.kind==Kind::entity ? kEntityBytes : 0;
        if(!needed || static_cast<std::size_t>(end-in)<needed)return false;
        ++in;event.serial=detail::Get(in,8);event.key=static_cast<std::uint32_t>(detail::Get(in,4));
        if(event.kind==Kind::control) {
            event.phase=*in++;event.commandSequence=detail::Get(in,8);event.seed=detail::Get(in,8);
            for(float& value:event.point)value=detail::GetFloat(in);
        } else {
            event.shotIndex=static_cast<std::uint32_t>(detail::Get(in,4));event.seed=detail::Get(in,8);
            event.rng90=static_cast<std::uint32_t>(detail::Get(in,4));
            event.rng94=static_cast<std::uint32_t>(detail::Get(in,4));
            for(auto& word:event.actual)word=static_cast<std::uint32_t>(detail::Get(in,4));
            for(float& value:event.matrix)value=detail::GetFloat(in);
            if(event.kind==Kind::entity) {
                event.entityTeam=static_cast<std::uint32_t>(detail::Get(in,4));
                for(auto& byte:event.targetActorId)byte=*in++;
                event.lockOrdinal=static_cast<std::uint32_t>(detail::Get(in,4));
            }
        }
        if(!Valid(event) || event.serial<=previous)return false;
        previous=event.serial;
    }
    if(in!=end)return false;
    for(unsigned i=0;i<total;++i)events[i]=pending[i];
    *count=total;return true;
}
} // namespace crew::enemy_native::deiroi_wire
