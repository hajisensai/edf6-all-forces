// Where a Vehicler's start vehicle stands (docs/newclass-re/vehicler.md §2.2 step 2), pure: tests/zw_dispatch_test.cpp.
// The host plans every start (offline this machine), one request after another, and keeps the footprints it planned this
// mission: a vehicle goes beside its player along the player's side axis (right = (fz, -fx), the stock matrix's row 0),
// first right then left, a step further out each time, kMaxShift steps a side. A spot is taken when its footprint (the
// vehicle's envelope turned to the player's heading) comes within kVehicleGap of a footprint planned before, within
// kPersonRadius of a person (a player standing there) or the ground there is not clear (the caller's test: standable, dry,
// level, nothing overhead). No spot: refused (the player walks in), never one that overlaps.
// Why: online the players' spawn points are a few metres apart; each one's vehicle made where it stands would overlap.
#pragma once
#include <cmath>

namespace crew::zwplace {
inline constexpr float kSideGap=1.5f;       // m between the player and its vehicle's side
inline constexpr float kVehicleGap=2.0f;    // m at least between two planned vehicles
inline constexpr float kPersonRadius=1.0f;  // m round a person no vehicle may cover
inline constexpr int kMaxShift=4;           // steps a side
inline constexpr int kCandidates=2*kMaxShift;

// A vehicle's footprint on the ground: its centre, its unit forward (fx, fz) and its half extents.
struct Footprint {
    float x=0,z=0,fx=0,fz=1,halfWidth=0,halfLength=0;
};
// The unit heading of (fx, fz); false when it has no length (nothing invented).
inline bool Unit(float& fx,float& fz) noexcept {
    const float len=std::sqrt(fx*fx+fz*fz);
    if(!std::isfinite(len) || len<1e-3f)return false;
    fx/=len;fz/=len;return true;
}
// Half the extent of `f` along unit axis (ax, az).
inline float Reach(const Footprint& f,float ax,float az) noexcept {
    const float alongForward=std::fabs(f.fx*ax+f.fz*az),alongRight=std::fabs(f.fz*ax-f.fx*az);
    return f.halfLength*alongForward+f.halfWidth*alongRight;
}
// Whether two footprints come within `gap` of each other (separating axes: each one's forward and right).
inline bool Overlap(const Footprint& a,const Footprint& b,float gap) noexcept {
    const float dx=b.x-a.x,dz=b.z-a.z;
    const float axes[4][2]={{a.fx,a.fz},{a.fz,-a.fx},{b.fx,b.fz},{b.fz,-b.fx}};
    for(const auto& axis:axes) {
        const float distance=std::fabs(dx*axis[0]+dz*axis[1]);
        if(distance>=Reach(a,axis[0],axis[1])+Reach(b,axis[0],axis[1])+gap)return false;
    }
    return true;
}
// Whether `f` comes within `radius` of point (px, pz).
inline bool Covers(const Footprint& f,float px,float pz,float radius) noexcept {
    const float dx=px-f.x,dz=pz-f.z;
    const float along=dx*f.fx+dz*f.fz,side=dx*f.fz-dz*f.fx;
    return std::fabs(along)<f.halfLength+radius && std::fabs(side)<f.halfWidth+radius;
}
// The i-th candidate centre (0 <= i < kCandidates) for a vehicle of `halfWidth` beside a player at (x, z) facing unit
// (fx, fz): even i right, odd i left, step i/2 out.
inline void Candidate(float x,float z,float fx,float fz,float halfWidth,int i,float& cx,float& cz) noexcept {
    const float side=(i%2)==0 ? 1.0f : -1.0f;
    const float offset=side*(halfWidth+kSideGap+static_cast<float>(i/2)*(2.0f*halfWidth+kVehicleGap));
    cx=x+fz*offset;cz=z-fx*offset;
}
// The spot for a vehicle of (halfWidth, halfLength) beside the player at (x, z) facing (fx, fz), or false: the first
// candidate whose footprint overlaps none of `taken` (count of them), covers none of `people` (pairs x, z; count of them)
// and whose ground `clear(const Footprint&)` passes. `out` gets it, heading the player's.
template<class Clear>
bool Place(float x,float z,float fx,float fz,float halfWidth,float halfLength,const Footprint* taken,int takenCount,
           const float (*people)[2],int peopleCount,Clear&& clear,Footprint* out) noexcept {
    if(!out || !std::isfinite(x+z) || !(halfWidth>0) || !(halfLength>0) || !Unit(fx,fz))return false;
    for(int i=0;i<kCandidates;++i) {
        Footprint f{};f.fx=fx;f.fz=fz;f.halfWidth=halfWidth;f.halfLength=halfLength;
        Candidate(x,z,fx,fz,halfWidth,i,f.x,f.z);
        bool free=true;
        for(int t=0;free && t<takenCount;++t)free=!Overlap(f,taken[t],kVehicleGap);
        for(int p=0;free && p<peopleCount;++p)free=!Covers(f,people[p][0],people[p][1],kPersonRadius);
        if(free && clear(static_cast<const Footprint&>(f))){*out=f;return true;}
    }
    return false;
}
}  // namespace crew::zwplace
