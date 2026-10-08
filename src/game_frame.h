#pragma once
#include <cstdint>

namespace gameframe {
// Extend the native scene-step ordinal; object enumeration is not a clock.
struct Clock {
    const void* system=nullptr;
    const void* scene=nullptr;
    std::uint32_t native=0;
    std::uint64_t logical=1;
    bool bound=false;
    void Reset() noexcept { system=scene=nullptr;bound=false;++logical; }
    void Observe(const void* sys,const void* world,std::uint32_t step) noexcept {
        if(!bound || system!=sys || scene!=world) {
            system=sys;scene=world;native=step;bound=true;++logical;return;
        }
        const std::uint32_t delta=step-native;
        // A small unsigned delta also handles the native 32-bit wrap. A backwards
        // reset in the same allocation starts a new baseline, never billions of frames.
        if(delta)logical+=delta<0x80000000u ? delta : 1;
        native=step;
    }
};
}
