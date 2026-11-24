// The game frame both plugins count by: the native scene step. xgs::game::System (*(EDF.dll+0x20B2958), vtable
// 0x1AE2278) increments +0xCE0 once before the scene's complete input / update traversal (0x1198FD0) and zeroes it when
// a scene starts (0x1193551); the scene itself is at +0x68. Reading it on the game thread avoids guessing frame
// boundaries from the order vehicles come round in: object enumeration is not a clock (a vehicle whose input runs twice
// in one step, or an export asked for it in between, made two frames of one).
// EDF6VehicleCrew (src/crew.cpp GameFrame) and EDF6AutoTurret (autoturret/src/plugin.cpp Frame) each keep a Clock.
#pragma once
#include "edf/layout.h"
#include "edf/memory.h"
#include "edf/patch.h"
#include <cstddef>
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

constexpr std::size_t kSystem=0x20B2958,kSystemScene=0x68,kSystemStep=0xCE0;
constexpr unsigned kSystemVtable=0x1AE2278;
// The increment (mov ecx,[r14+CE0] / lea eax,[rcx+1] / mov [r14+CE0],eax) and the reset (mov qword [rdi+CE0],0).
constexpr std::size_t kStepIncrement=0x1198FD0,kStepReset=0x1193551;
inline constexpr unsigned char kStepIncrementCode[]={0x41,0x8B,0x8E,0xE0,0x0C,0x00,0x00,0x8D,0x41,0x01,0x41,0x89,0x86,0xE0,0x0C,0x00,0x00};
inline constexpr unsigned char kStepResetCode[]={0x48,0xC7,0x87,0xE0,0x0C,0x00,0x00,0x00,0x00,0x00,0x00};

// The counter's code as above (checked once at load: the clock is off without it).
inline bool CounterAsExpected(const unsigned char* image) noexcept {
    __try {
        return edf::Matches(image,kStepIncrement,kStepIncrementCode,sizeof(kStepIncrementCode)) &&
               edf::Matches(image,kStepReset,kStepResetCode,sizeof(kStepResetCode));
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

// The System, its scene and the step count now; false while the System is not there (or not the System).
struct Step { const void* system; const void* scene; std::uint32_t count; };
inline bool ReadStep(const unsigned char* image,Step& out) noexcept {
    const auto system=edf::At<const unsigned char*>(image,kSystem);
    if(!edf::Readable(system,kSystemStep+4) || edf::At<const void*>(system,0)!=image+kSystemVtable)return false;
    out=Step{system,edf::At<const void*>(system,kSystemScene),edf::At<std::uint32_t>(system,kSystemStep)};
    return true;
}
// The clock moved on to the step now (game thread); unchanged while the step cannot be read.
inline void See(Clock& clock,const unsigned char* image) noexcept {
    Step s{};
    if(ReadStep(image,s))clock.Observe(s.system,s.scene,s.count);
}
}  // namespace gameframe
