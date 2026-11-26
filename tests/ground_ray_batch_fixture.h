// The navigation adapter uses each fixture's existing synthetic scene queries.
#pragma once
#include "../src/heli.h"
namespace crew {
float GroundRayBatch::Floor(const float* a,const float* b,float* hit) noexcept {
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
