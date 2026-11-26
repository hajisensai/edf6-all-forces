// The navigation adapter uses each fixture's existing synthetic scene queries.
#pragma once
#include "../src/heli.h"
#include "../src/ground_body_rays.h"
namespace ground_ray_fixture {
inline bool bodyAvailable=false,bodyClear=false;
inline float bodyMaxWidth=0;
}
namespace crew {
bool GroundBodyRays::Capture(const float* lower,const float* upper) noexcept {
    const float width=ground_ray_fixture::bodyMaxWidth;
    return ground_ray_fixture::bodyAvailable &&
           (width<=0.0f || (upper[0]-lower[0]<=width && upper[2]-lower[2]<=width));
}
bool GroundBodyRays::Unobstructed(const float*,const float*) const noexcept {return ground_ray_fixture::bodyClear;}
float GroundRayBatch::Floor(const float* a,const float* b,float* hit,const GroundBodyRays*) noexcept {
    return MapFloorRay(a,b,hit);
}
bool GroundRayBatch::Clear(const float* a,const float* b) noexcept {
    float hit[3]{};
    return MapRay(a,b,hit)<0.0f;
}
Sea GroundRayBatch::Water(float x,float z,float* surface) noexcept {
    return SeaAt(x,z,surface);
}
}
