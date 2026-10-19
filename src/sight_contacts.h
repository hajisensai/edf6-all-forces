// Optical recognition policy. Native hostile lock points and LOS are supplied by sight_contacts.cpp;
// this bounded selection never represents heat, infrared imagery or a through-wall radar.
#pragma once
#include <cmath>
#include <cstring>

namespace crew {
// Game thread: a real, armed sensor seat with an explicitly supported thermal equipment trait.
// Shared by the optical collector and thermal renderer; legacy M163 optics stay optical-only.
bool VehicleHasThermal(const unsigned char* vehicle,unsigned seat) noexcept;
namespace vehiclecontacts {
constexpr int kCandidates=32;
constexpr float kRange=3000.0f;
constexpr float kForwardCos=0.34202014f; // 70 degrees: coarse filter before view projection/cropping
inline bool OpticalClass(const char* name) noexcept {
    if(!name)return false;
    constexpr const char* supported[]={"403_Tank","404_Tank","505_Tank","601_Tank","603_Flak","Car",
                                      "506_Helicopter","Helicopter409","Helicopter410","HelicopterBase"};
    for(const char* item:supported)if(std::strcmp(name,item)==0)return true;
    return false;
}
inline bool ThermalClass(const char* name) noexcept {
    if(!name)return false;
    constexpr const char* supported[]={"403_Tank","404_Tank","505_Tank","601_Tank","603_Flak",
                                      "506_Helicopter","Helicopter409"};
    for(const char* item:supported)if(std::strcmp(name,item)==0)return true;
    return false;
}
struct Candidate { const void* key; float at[3],range,cosAim; };
struct Scan {
    float eye[3]{},look[3]{};
    int count=0;
    Candidate point[kCandidates]{};
    void Add(const void* key,const float* at) noexcept {
        if(!key || !at)return;
        const float d[3]={at[0]-eye[0],at[1]-eye[1],at[2]-eye[2]};
        const float distance=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
        if(!std::isfinite(distance) || distance<1.0f || distance>kRange)return;
        const float aim=(d[0]*look[0]+d[1]*look[1]+d[2]*look[2])/distance;
        if(!std::isfinite(aim) || aim<kForwardCos)return;
        Candidate candidate{key,{at[0],at[1],at[2]},distance,aim};
        // Multiple weakpoints belong to one object. Keep the one closest to the view centre.
        for(int i=0;i<count;++i)if(point[i].key==key) {
            if(!Before(candidate,point[i]))return;
            for(int j=i;j<count-1;++j)point[j]=point[j+1];
            --count;break;
        }
        int index=0;
        while(index<count && !Before(candidate,point[index]))++index;
        if(index>=kCandidates)return;
        if(count<kCandidates)++count;
        for(int j=count-1;j>index;--j)point[j]=point[j-1];
        point[index]=candidate;
    }
    static bool Before(const Candidate& a,const Candidate& b) noexcept {
        return a.cosAim>b.cosAim || (a.cosAim==b.cosAim && a.range<b.range);
    }
};
// A map-only ray cannot hit the target's own hull. Even a wall immediately before its lock point occludes it.
inline bool Visible(float distance,float mapHit) noexcept {
    return std::isfinite(distance) && distance>=1.0f && std::isfinite(mapHit) && (mapHit<0.0f || mapHit>=distance-0.02f);
}
}} // namespace crew::vehiclecontacts
