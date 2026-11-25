#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace crew::enemy_native::pose {

enum class Kind : std::uint32_t { deiroi=1, large, timeShip, squid, ufo515 };

struct State {
    Kind kind=Kind::deiroi;
    std::uint32_t profile=0;
    float position[3]{},rotation[4]{};
    float currentAngles[3]{},targetAngles[3]{};
    float linearVelocity[3]{},accumulatedVelocity[3]{};
    float targetRotation[4]{},auxiliaryRotation[4]{},rotationStep[4]{},rotationDelta[4]{};
    float deformation[16]{};
    float phase=0,scale=0,bank=0;
};

// One host sample on the wire: the state plus when it was taken (the host's GameFrame(), low 32 bits) and how far the body
// actually moved per logic frame since the previous sample (measured from positions, enemy_control_pose_interp.h
// MeasureMotion). The guest places samples on the host's frame timeline and extrapolates with motion
// (enemy_control_pose_interp.h Track); the native linearVelocity's units differ per class and are only written back.
struct Packet {
    State state{};
    std::uint32_t senderFrame=0;
    float motion[3]{};
};

inline constexpr std::uint32_t kWireMagic=0x45534F50u; // "POSE" in little endian.
// Version 2 (net-9, room protocol 17): senderFrame and motion[3] after the 16-byte header. A version-1 frame is rejected.
inline constexpr std::uint32_t kWireVersion=2u;
inline constexpr std::size_t kFloatCount=54;
inline constexpr std::size_t kHeaderBytes=32;
inline constexpr std::size_t kWireBytes=kHeaderBytes+kFloatCount*4;
// 5 m per logic frame is 300 m/s; MeasureMotion reports a larger step as zero (a teleport, not motion).
inline constexpr float kMaxMotion=5.0f;
static_assert(kWireBytes==248 && sizeof(float)==4 && std::numeric_limits<float>::is_iec559,
              "pose wire requires IEEE-754 binary32");

namespace codec_detail {
inline std::uint32_t Bits(float value) noexcept {
    std::uint32_t result=0;
    std::memcpy(&result,&value,sizeof result);
    return result;
}
inline float Float(std::uint32_t value) noexcept {
    float result=0;
    std::memcpy(&result,&value,sizeof result);
    return result;
}
inline void Write32(unsigned char* out,std::uint32_t value) noexcept {
    for(unsigned i=0;i<4;++i)out[i]=static_cast<unsigned char>((value>>(i*8u))&0xFFu);
}
inline std::uint32_t Read32(const unsigned char* in) noexcept {
    std::uint32_t value=0;
    for(unsigned i=0;i<4;++i)value|=static_cast<std::uint32_t>(in[i])<<(i*8u);
    return value;
}
template<std::size_t N>
inline void Append(float* out,std::size_t& at,const float (&values)[N]) noexcept {
    for(float value:values)out[at++]=value;
}
template<std::size_t N>
inline void Extract(const float* in,std::size_t& at,float (&values)[N]) noexcept {
    for(float& value:values)value=in[at++];
}
inline void Flatten(const State& state,float (&out)[kFloatCount]) noexcept {
    std::size_t at=0;
    Append(out,at,state.position);Append(out,at,state.rotation);
    Append(out,at,state.currentAngles);Append(out,at,state.targetAngles);
    Append(out,at,state.linearVelocity);Append(out,at,state.accumulatedVelocity);
    Append(out,at,state.targetRotation);Append(out,at,state.auxiliaryRotation);
    Append(out,at,state.rotationStep);Append(out,at,state.rotationDelta);
    Append(out,at,state.deformation);
    out[at++]=state.phase;out[at++]=state.scale;out[at]=state.bank;
}
inline void Expand(const float (&in)[kFloatCount],State& state) noexcept {
    std::size_t at=0;
    Extract(in,at,state.position);Extract(in,at,state.rotation);
    Extract(in,at,state.currentAngles);Extract(in,at,state.targetAngles);
    Extract(in,at,state.linearVelocity);Extract(in,at,state.accumulatedVelocity);
    Extract(in,at,state.targetRotation);Extract(in,at,state.auxiliaryRotation);
    Extract(in,at,state.rotationStep);Extract(in,at,state.rotationDelta);
    Extract(in,at,state.deformation);
    state.phase=in[at++];state.scale=in[at++];state.bank=in[at];
}
inline void Mark(bool (&used)[kFloatCount],std::size_t first,std::size_t count) noexcept {
    for(std::size_t i=first;i<first+count;++i)used[i]=true;
}
inline bool Unit(const float* quaternion) noexcept {
    double lengthSquared=0;
    for(std::size_t i=0;i<4;++i) {
        const double value=static_cast<double>(quaternion[i]);
        if(!std::isfinite(value))return false;
        lengthSquared+=value*value;
    }
    return std::fabs(lengthSquared-1.0)<=1.0e-3;
}
} // namespace codec_detail

inline bool Valid(const State& state) noexcept {
    if(state.profile==0)return false;
    float fields[kFloatCount]{};
    codec_detail::Flatten(state,fields);
    bool used[kFloatCount]{};
    codec_detail::Mark(used,0,7); // XYZ and the actual physical orientation.
    switch(state.kind) {
    case Kind::deiroi:
        codec_detail::Mark(used,10,2);codec_detail::Mark(used,13,3);
        break;
    case Kind::large:
        codec_detail::Mark(used,7,2);codec_detail::Mark(used,10,2);
        codec_detail::Mark(used,13,6);codec_detail::Mark(used,23,12);
        if(!codec_detail::Unit(state.auxiliaryRotation) || !codec_detail::Unit(state.rotationStep) ||
           !codec_detail::Unit(state.rotationDelta))return false;
        break;
    case Kind::timeShip:
        codec_detail::Mark(used,13,10);codec_detail::Mark(used,35,18);
        if(!codec_detail::Unit(state.targetRotation))return false;
        if(state.deformation[3]!=0 || state.deformation[7]!=0 || state.deformation[11]!=0 ||
           state.deformation[15]!=1)return false;
        break;
    case Kind::squid:
        codec_detail::Mark(used,7,12);
        break;
    case Kind::ufo515:
        codec_detail::Mark(used,7,2);codec_detail::Mark(used,13,6);used[53]=true;
        break;
    default:
        return false;
    }
    if(!codec_detail::Unit(state.rotation))return false;
    for(std::size_t i=0;i<kFloatCount;++i) {
        if(!used[i]) {
            if(codec_detail::Bits(fields[i])!=0)return false; // Includes negative zero.
        } else if(!std::isfinite(fields[i]) || std::fabs(fields[i])>(i<3 ? 1.0e6f : 1.0e7f)) {
            return false;
        }
    }
    return true;
}

inline bool ValidMotion(const float (&motion)[3]) noexcept {
    double lengthSquared=0;
    for(float value:motion) {
        if(!std::isfinite(value))return false;
        lengthSquared+=static_cast<double>(value)*value;
    }
    return lengthSquared<=static_cast<double>(kMaxMotion)*kMaxMotion;
}

inline bool Valid(const Packet& packet) noexcept {return Valid(packet.state) && ValidMotion(packet.motion);}

inline bool Encode(const Packet& packet,void* buffer,std::size_t bytes) noexcept {
    if(buffer==nullptr || bytes!=kWireBytes || !Valid(packet))return false;
    const State& state=packet.state;
    float fields[kFloatCount]{};
    codec_detail::Flatten(state,fields);
    auto* out=static_cast<unsigned char*>(buffer);
    codec_detail::Write32(out,kWireMagic);
    codec_detail::Write32(out+4,kWireVersion);
    codec_detail::Write32(out+8,static_cast<std::uint32_t>(state.kind));
    codec_detail::Write32(out+12,state.profile);
    codec_detail::Write32(out+16,packet.senderFrame);
    for(std::size_t i=0;i<3;++i)codec_detail::Write32(out+20+i*4,codec_detail::Bits(packet.motion[i]));
    for(std::size_t i=0;i<kFloatCount;++i)codec_detail::Write32(out+kHeaderBytes+i*4,codec_detail::Bits(fields[i]));
    return true;
}

inline bool Decode(const void* buffer,std::size_t bytes,Packet* out) noexcept {
    if(buffer==nullptr || out==nullptr || bytes!=kWireBytes)return false;
    const auto* in=static_cast<const unsigned char*>(buffer);
    if(codec_detail::Read32(in)!=kWireMagic || codec_detail::Read32(in+4)!=kWireVersion)return false;
    Packet candidate{};
    candidate.state.kind=static_cast<Kind>(codec_detail::Read32(in+8));
    candidate.state.profile=codec_detail::Read32(in+12);
    candidate.senderFrame=codec_detail::Read32(in+16);
    for(std::size_t i=0;i<3;++i)candidate.motion[i]=codec_detail::Float(codec_detail::Read32(in+20+i*4));
    float fields[kFloatCount]{};
    for(std::size_t i=0;i<kFloatCount;++i)fields[i]=codec_detail::Float(codec_detail::Read32(in+kHeaderBytes+i*4));
    codec_detail::Expand(fields,candidate.state);
    if(!Valid(candidate))return false;
    *out=candidate;
    return true;
}

} // namespace crew::enemy_native::pose
