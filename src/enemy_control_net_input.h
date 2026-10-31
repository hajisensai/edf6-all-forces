// Canonical semantic input only. Native pointers, raw pad memory and function/CAS addresses never cross the wire.
#pragma once
#include "enemy_control_types.h"
#include "net_object_ref.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace crew::enemy_control_net {
constexpr std::size_t kInputBytes=4+5*4+3*4+enemy_control::kConcurrentAbilities*8+32;
constexpr std::uint8_t kInputVersion=4;   // 4: the focus is an object reference (a player by its slot)
inline bool ValidSemanticInput(const enemy_control::Input& input) noexcept {
    for(float value:input.move)if(!std::isfinite(value) || std::fabs(value)>1.0f)return false;
    for(float value:input.look)if(!std::isfinite(value) || std::fabs(value)>1.0f)return false;
    // Each peer receives the same absolute aim sample. Relative look remains a native axis, not a replayed delta.
    constexpr float pi=3.14159265358979323846f;
    if(!std::isfinite(input.aimYaw) || !std::isfinite(input.aimPitch))return false;
    if(input.absoluteAim) {
        if(std::fabs(input.aimYaw)>pi || std::fabs(input.aimPitch)>1.2f)return false;
    } else if(input.aimYaw!=0.0f || input.aimPitch!=0.0f)return false;
    // The focus is an object reference (net_object_ref.h): an enemy player by its slot, the rest by NetId, or none.
    if(!crew::netref::Valid(input.focusRef,true))return false;
    if(!input.absoluteAim) {
        for(float value:input.move)if(value!=0)return false;
        for(float value:input.look)if(value!=0)return false;
        for(const auto& channel:input.active)if(channel.id || channel.phase)return false;
        for(auto byte:input.focusRef)if(byte)return false;
    }
    bool ended=false;
    for(unsigned i=0;i<enemy_control::kConcurrentAbilities;++i) {
        const auto& channel=input.active[i];
        if(!channel.id){if(channel.phase)return false;ended=true;continue;}
        if(ended)return false; // Canonical packed list, matching the core sampler/validator.
        if(channel.phase!=1 && channel.phase!=2)return false;
        for(unsigned j=0;j<i;++j)if(input.active[j].id==channel.id)return false;
    }
    return true;
}
inline void Put32(unsigned char*& out,std::uint32_t value) noexcept {
    for(unsigned i=0;i<4;++i)*out++=static_cast<unsigned char>(value>>(i*8));
}
inline std::uint32_t Get32(const unsigned char*& in) noexcept {
    std::uint32_t value=0;for(unsigned i=0;i<4;++i)value|=static_cast<std::uint32_t>(*in++)<<(i*8);return value;
}
inline void PutFloat(unsigned char*& out,float value) noexcept {
    std::uint32_t bits=0;std::memcpy(&bits,&value,4);Put32(out,bits);
}
inline float GetFloat(const unsigned char*& in) noexcept {
    const auto bits=Get32(in);float value=0;std::memcpy(&value,&bits,4);return value;
}
inline bool EncodeInput(const enemy_control::Input& input,void* bytes,std::size_t size) noexcept {
    if(!bytes || size!=kInputBytes || !ValidSemanticInput(input))return false;
    auto* out=static_cast<unsigned char*>(bytes);
    *out++=kInputVersion;*out++=0;*out++=0;*out++=0;
    for(float value:input.move)PutFloat(out,value);
    for(float value:input.look)PutFloat(out,value);
    Put32(out,input.absoluteAim ? 1u : 0u);PutFloat(out,input.aimYaw);PutFloat(out,input.aimPitch);
    for(const auto& channel:input.active){Put32(out,channel.id);Put32(out,channel.phase);}
    for(auto byte:input.focusRef)*out++=byte;
    // Input.sequence/seed belong to the authenticated host's envelope, never the controller's payload.
    return out==static_cast<unsigned char*>(bytes)+size;
}
inline bool DecodeInput(const void* bytes,std::size_t size,enemy_control::Input* out) noexcept {
    if(!out)return false;*out={};
    if(!bytes || size!=kInputBytes)return false;
    auto* in=static_cast<const unsigned char*>(bytes);
    if(*in++!=kInputVersion || *in++ || *in++ || *in++)return false;
    enemy_control::Input input;
    for(float& value:input.move)value=GetFloat(in);
    for(float& value:input.look)value=GetFloat(in);
    const auto flags=Get32(in);if(flags>1)return false;
    input.absoluteAim=flags!=0;input.aimYaw=GetFloat(in);input.aimPitch=GetFloat(in);
    for(auto& channel:input.active){channel.id=Get32(in);channel.phase=Get32(in);}
    for(auto& byte:input.focusRef)byte=*in++;
    if(!ValidSemanticInput(input))return false;
    *out=input;return true;
}
inline bool SameChannels(const enemy_control::Input& a,const enemy_control::Input& b) noexcept {
    for(unsigned i=0;i<enemy_control::kConcurrentAbilities;++i)
        if(a.active[i].id!=b.active[i].id || a.active[i].phase!=b.active[i].phase)return false;
    return true;
}
inline bool HasPress(const enemy_control::Input& input) noexcept {
    for(const auto& channel:input.active)if(channel.id && channel.phase==2)return true;
    return false;
}
} // namespace crew::enemy_control_net
