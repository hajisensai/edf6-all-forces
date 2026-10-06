// VehicleWeaponAim is embedded at seat+0xE0, not a pointer stored there.
// EDF.dll 0x6459D0 adds 0xE0 and the seat stride before calling its vtable.
#pragma once
#include "edf/layout.h"
#include <cmath>

namespace crew::seataim {
inline unsigned char* Object(unsigned char* seat) noexcept { return seat+edf::kSeatAim; }
inline const unsigned char* Object(const unsigned char* seat) noexcept { return seat+edf::kSeatAim; }

// Called after the destination's stock aim step, before the vehicle fires. Apply
// each changed axis to its bones as the stock axis step does, not just its angle.
template<class Apply> void Follow(const unsigned char* source,unsigned char* dest,Apply apply) noexcept {
    for(int i=0;i<2;++i) {
        const std::size_t off=edf::kAimAxes+static_cast<std::size_t>(i)*edf::kAxisStride;
        const float angle=edf::At<float>(source,off+edf::kAxisAngle);
        const float lo=edf::At<float>(dest,off+edf::kAxisMin),hi=edf::At<float>(dest,off+edf::kAxisMax);
        if(!std::isfinite(angle) || !std::isfinite(lo) || !std::isfinite(hi) || !(hi>lo))continue;
        edf::Put<float>(dest,off+edf::kAxisAngle,angle<lo ? lo : angle>hi ? hi : angle);
        edf::Put<float>(dest,off+0xC,0.0f);
        apply(dest+off);
    }
}
}  // namespace crew::seataim
