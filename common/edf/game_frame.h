// The game frame both plugins count by: the native scene step. xgs::game::System (*(EDF.dll+0x20B2958), vtable
// 0x1AE2278) increments +0xCE0 once before the scene's complete input / update traversal (0x1198FD0) and zeroes it when
// a scene starts (0x1193551); the scene itself is at +0x68. Reading it on the game thread avoids guessing frame
// boundaries from the order vehicles come round in: object enumeration is not a clock (a vehicle whose input runs twice
// in one step, or an export asked for it in between, made two frames of one).
// EDF6VehicleCrew (src/crew.cpp GameFrame) keeps a Clock; EDF6AutoTurret (autoturret/src/plugin.cpp Frame) a RunClock.
// The step goes on while the game is paused: the sub-step loop increments +0xCE0 first and tests the pause bits
// (+0xCD8 & +0xCDC, 0x11990C7) after, a paused sub-step only moving the camera (0x119953B). The scene time +0xCE4 is
// what stops: 0x1198E07 adds sub-steps x 1/60 s (the constant at 0x179F358) once per call, only when not paused; the
// reset at 0x1193551 (a qword) zeroes it with the step. RunClock counts the steps that scene time backs.
#pragma once
#include "edf/layout.h"
#include "edf/memory.h"
#include "edf/patch.h"
#include <cmath>
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

// The steps the scene really ran: a step-count move only counts as far as scene time (+0xCE4 / the step's seconds)
// moved with it. Paused steps (the count moves, the time does not) are not frames; a call running two sub-steps adds
// both steps' time before the first, and the credit carries it to the second.
struct RunClock {
    const void* system=nullptr;
    const void* scene=nullptr;
    std::uint32_t native=0;
    float ran=0.0f;          // scene time last turned into credit
    std::uint32_t credit=0;  // run steps the step count has not caught up with yet
    std::uint64_t logical=1;
    bool bound=false;
    // Credit left once the count caught up: the rest of one call's sub-steps, never more (a call whose later sub-steps
    // were paused after the time was added leaves at most these).
    static constexpr std::uint32_t kMaxLeftover=8;
    void Reset() noexcept { system=scene=nullptr;bound=false;++logical; }
    void Observe(const void* sys,const void* world,std::uint32_t step,float run,float stepSeconds) noexcept {
        if(!std::isfinite(run) || !(stepSeconds>0.0f))return;
        const std::uint32_t delta=step-native;
        if(!bound || system!=sys || scene!=world || delta>=0x80000000u || !(run>=ran)) {
            // A new scene (or its reset: step and time zeroed together): one frame on, a new baseline.
            system=sys;scene=world;native=step;ran=run;credit=0;bound=true;++logical;return;
        }
        const double steps=(static_cast<double>(run)-static_cast<double>(ran))/static_cast<double>(stepSeconds);
        if(steps>=0.5) {
            const double whole=std::floor(steps+0.5),room=static_cast<double>(0xFFFFFFFFu-credit);
            credit+=whole>=room ? 0xFFFFFFFFu-credit : static_cast<std::uint32_t>(whole);
            ran=run;
        }
        const std::uint32_t advance=delta<credit ? delta : credit;
        logical+=advance;credit-=advance;native=step;
        if(credit>kMaxLeftover)credit=kMaxLeftover;
    }
};

constexpr std::size_t kSystem=0x20B2958,kSystemScene=0x68,kSystemStep=0xCE0;
constexpr unsigned kSystemVtable=0x1AE2278;
// The increment (mov ecx,[r14+CE0] / lea eax,[rcx+1] / mov [r14+CE0],eax) and the reset (mov qword [rdi+CE0],0).
constexpr std::size_t kStepIncrement=0x1198FD0,kStepReset=0x1193551;
// The scene time: +0xCE4 += sub-steps (r12) x the step's seconds (xmm6, loaded from 0x179F358), when not paused.
constexpr std::size_t kSystemRunTime=0xCE4,kRunTimeAdd=0x1198E07,kStepSecondsLoad=0x1198DF6,kStepSeconds=0x179F358;
inline constexpr unsigned char kRunTimeAddCode[]={0x0F,0x57,0xD2,0xF3,0x49,0x0F,0x2A,0xD4,0xF3,0x0F,0x59,0xD6,
    0xF3,0x41,0x0F,0x58,0x96,0xE4,0x0C,0x00,0x00,0xF3,0x41,0x0F,0x11,0x96,0xE4,0x0C,0x00,0x00};
// movss xmm6,[rip+0x60655A] (0x179F358) / test [r14+CDC],eax / jne: the pause test right before the add.
inline constexpr unsigned char kStepSecondsLoadCode[]={0xF3,0x0F,0x10,0x35,0x5A,0x65,0x60,0x00,
    0x41,0x85,0x86,0xDC,0x0C,0x00,0x00,0x75,0x31};
inline constexpr unsigned char kStepIncrementCode[]={0x41,0x8B,0x8E,0xE0,0x0C,0x00,0x00,0x8D,0x41,0x01,0x41,0x89,0x86,0xE0,0x0C,0x00,0x00};
inline constexpr unsigned char kStepResetCode[]={0x48,0xC7,0x87,0xE0,0x0C,0x00,0x00,0x00,0x00,0x00,0x00};

// The counter's code as above (checked once at load: the clock is off without it).
inline bool CounterAsExpected(const unsigned char* image) noexcept {
    __try {
        return edf::Matches(image,kStepIncrement,kStepIncrementCode,sizeof(kStepIncrementCode)) &&
               edf::Matches(image,kStepReset,kStepResetCode,sizeof(kStepResetCode));
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
// The scene time's code as above, and its step a sane length (RunClock is off without them).
inline bool RunTimeAsExpected(const unsigned char* image) noexcept {
    __try {
        if(!edf::Matches(image,kRunTimeAdd,kRunTimeAddCode,sizeof(kRunTimeAddCode)) ||
           !edf::Matches(image,kStepSecondsLoad,kStepSecondsLoadCode,sizeof(kStepSecondsLoadCode)))return false;
        const float seconds=edf::At<float>(image,kStepSeconds);
        return seconds>0.001f && seconds<1.0f;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

// The System, its scene and the step count now; false while the System is not there (or not the System).
struct Step { const void* system; const void* scene; std::uint32_t count; float run; };
inline bool ReadStep(const unsigned char* image,Step& out) noexcept {
    const auto system=edf::At<const unsigned char*>(image,kSystem);
    if(!edf::Readable(system,kSystemRunTime+4) || edf::At<const void*>(system,0)!=image+kSystemVtable)return false;
    out=Step{system,edf::At<const void*>(system,kSystemScene),edf::At<std::uint32_t>(system,kSystemStep),
             edf::At<float>(system,kSystemRunTime)};
    return true;
}
// The clock moved on to the step now (game thread); unchanged while the step cannot be read.
inline void See(Clock& clock,const unsigned char* image) noexcept {
    Step s{};
    if(ReadStep(image,s))clock.Observe(s.system,s.scene,s.count);
}
// The run clock moved on to the scene's run steps now (game thread; RunTimeAsExpected checked at load).
inline void See(RunClock& clock,const unsigned char* image) noexcept {
    Step s{};
    if(ReadStep(image,s))clock.Observe(s.system,s.scene,s.count,s.run,edf::At<float>(image,kStepSeconds));
}
}  // namespace gameframe
