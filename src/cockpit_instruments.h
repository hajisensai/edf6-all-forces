// Live cockpit geometry. Field order and digit segment order are shared with pylib/cockpit_instruments.py.
#pragma once
#include "fp_view.h"

namespace crew {
namespace instruments {
enum Field { hp,hull,fuel,speed,altitude,heading,slot,ammo,warn,count };
struct Readings { float value[count]; float share[count]; bool contact[6]; float at[6][2]; };

inline bool Segment(float value,int place,int segment) noexcept {
    constexpr unsigned masks[]={0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x07,0x7F,0x6F};
    if(place<0 || place>4 || segment<0 || segment>6)return false;
    if(!std::isfinite(value) || value<0.0f || value>99999.0f)return segment==6; // unavailable / overflow: dashes
    int div=1;for(int i=0;i<place;++i)div*=10;
    const int number=static_cast<int>(value+0.5f);
    if(place && number<div)return false;
    return (masks[(number/div)%10]&(1u<<segment))!=0;
}

inline fpview::M4 Pose(const fpview::M4& original,wchar_t mode,int field,int place,int segment,
                      const Readings& readings,bool ready) noexcept {
    if(field<0 || field>=count)return original;
    if(mode==L't') {
        if(field>=6 || !ready || !readings.contact[field])return fpview::Collapse(original.m+12);
        return fpview::Mul(fpview::Translate(readings.at[field][0],readings.at[field][1],0),original);
    }
    const float v=ready ? readings.value[field] : -1.0f;
    if(mode==L'd')return Segment(v,place,segment) ? original : fpview::Collapse(original.m+12);
    if(mode==L'b') {
        const float s=ready ? readings.share[field] : 0.0f;
        const float k=std::isfinite(s) ? fpview::Clamp(s,0.0f,1.0f) : 0.0f;
        fpview::M4 out=original;
        for(int i=0;i<3;++i)out.m[i]*=k;
        return out;
    }
    if(mode==L'n') {
        if(!std::isfinite(v) || v<0.0f)return fpview::Collapse(original.m+12);
        const float maximum=field==speed ? 2000.0f : field==altitude ? 3000.0f : 360.0f;
        const float a=(-135.0f+270.0f*fpview::Clamp(v/maximum,0.0f,1.0f))*0.01745329252f;
        const float axis[3]={0,0,1};
        return fpview::Mul(fpview::Rotation(axis,std::cos(a),-std::sin(a)),original);
    }
    return original;
}
} // namespace instruments
} // namespace crew
