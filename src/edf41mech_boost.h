// The pure part of src/edf41mech_boost.cpp (the 406 Begaruta's jet group: slots the old begaruta_boost parser never
// builds): the code it checks and the stubs it writes. No Windows, no game: tests/edf41mech_boost_test.cpp runs it.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace crew::mechboost {
// The jet group (vehicle+0x1BB0) holds four slots at +0x20 + i*0xA0, a slot's emitter at its +0x50. Two loops write
// through every slot's emitter unchecked; each one's body is replaced by a jump to a stub that runs the same
// instructions with the emitter's write behind a null test.
//
// 1. The group's state step 0x6482A0 (rax = the slot, xmm0 = its intensity, dl = lit), 0x64833A..0x648355:
//    mov r8,[rax+0x50]; movss [r8+0x3EC],xmm0; test dl,dl; je 0x648356; mov dword [r8+0x3F0],1.0
constexpr std::size_t kStepWrites=0x64833A,kStepNext=0x648356;
inline constexpr unsigned char kStepWritesCode[]={0x4C,0x8B,0x40,0x50,0xF3,0x41,0x0F,0x11,0x80,0xEC,0x03,0x00,0x00,0x84,0xD2,
    0x74,0x0B,0x41,0xC7,0x80,0xF0,0x03,0x00,0x00,0x00,0x00,0x80,0x3F};
static_assert(kStepWrites+sizeof(kStepWritesCode)==kStepNext,"the writes end where the step's loop advances");
// 2. The update's boost stop (0x638EA6, edx == 2: the group's intensity +0x1BB8 to 0), 0x638EF0..0x638F08, the loop's
//    head (its jne at 0x638F12 comes back here): mov rcx,[rax+0x50]; movss xmm0,[rax+0x40]; mulss xmm0,[rdi+0x1BB8];
//    movss [rcx+0x3EC],xmm0
constexpr std::size_t kStopWrites=0x638EF0,kStopNext=0x638F09;
inline constexpr unsigned char kStopWritesCode[]={0x48,0x8B,0x48,0x50,0xF3,0x0F,0x10,0x40,0x40,0xF3,0x0F,0x59,0x87,0xB8,0x1B,0x00,
    0x00,0xF3,0x0F,0x11,0x81,0xEC,0x03,0x00,0x00};
static_assert(kStopWrites+sizeof(kStopWritesCode)==kStopNext,"the write ends where the stop's loop advances");
// What the loops walk, and what the old parser builds (checked, not patched):
// 0x64831A lea rax,[rcx+0x20]; lea r9,[rcx+0x2A0]  (the step: four slots)
constexpr std::size_t kStepLoop=0x64831A;
inline constexpr unsigned char kStepLoopCode[]={0x48,0x8D,0x41,0x20,0x4C,0x8D,0x89,0xA0,0x02,0x00,0x00};
// 0x638EE0 lea rax,[rcx+0x1BD0]; cmp rax,rdx; je 0x638F14  (the stop: vehicle+0x1BD0 .. rdx = vehicle+0x1E50)
constexpr std::size_t kStopLoop=0x638EE0;
inline constexpr unsigned char kStopLoopCode[]={0x48,0x8D,0x81,0xD0,0x1B,0x00,0x00,0x48,0x3B,0xC2,0x74,0x28};
// 0x640CB3 cmp rsi,2; jl 0x640480  (the old begaruta_boost parser 0x640340: two slots built)
constexpr std::size_t kOldLoop=0x640CB3;
inline constexpr unsigned char kOldLoopCode[]={0x48,0x83,0xFE,0x02,0x0F,0x8C,0xC3,0xF7,0xFF,0xFF};
constexpr int kSlots=4,kOldSlots=2;
static_assert((0x2A0-0x20)/0xA0==kSlots && (0x1E50-0x1BD0)/0xA0==kSlots,"both loops walk four slots");

constexpr std::size_t kJumpBack=14;   // jmp [rip+0]; dq back
// The step's stub: mov r8,[rax+0x50]; test r8,r8; jz done; <the stock writes, from movss on>; done: jmp back
constexpr std::size_t kStepStubSize=4+3+2+(sizeof(kStepWritesCode)-4)+kJumpBack;
// The stop's stub: <the stock loads and the multiply>; test rcx,rcx; jz done; movss [rcx+0x3EC],xmm0; done: jmp back
// (xmm0 is computed as the stock computes it, emitter or not: every register leaves the stub as it leaves the stock body)
constexpr std::size_t kStopStubSize=17+3+2+8+kJumpBack;

inline void JumpBack(std::uint64_t back,unsigned char* out) noexcept {
    out[0]=0xFF;out[1]=0x25;out[2]=out[3]=out[4]=out[5]=0;
    std::memcpy(out+6,&back,8);
}
inline void StepStub(std::uint64_t back,unsigned char* out) noexcept {
    unsigned char* p=out;
    std::memcpy(p,kStepWritesCode,4);p+=4;                                   // mov r8,[rax+0x50]
    *p++=0x4D;*p++=0x85;*p++=0xC0;                                            // test r8,r8
    *p++=0x74;*p++=static_cast<unsigned char>(sizeof(kStepWritesCode)-4);     // jz done
    std::memcpy(p,kStepWritesCode+4,sizeof(kStepWritesCode)-4);p+=sizeof(kStepWritesCode)-4;
    JumpBack(back,p);
}
inline void StopStub(std::uint64_t back,unsigned char* out) noexcept {
    unsigned char* p=out;
    std::memcpy(p,kStopWritesCode,17);p+=17;                                  // the emitter, the intensity, the multiply
    *p++=0x48;*p++=0x85;*p++=0xC9;                                            // test rcx,rcx
    *p++=0x74;*p++=8;                                                         // jz done
    std::memcpy(p,kStopWritesCode+17,8);p+=8;                                 // movss [rcx+0x3EC],xmm0
    JumpBack(back,p);
}
// The patch over a loop body: jmp rel32 to its stub, the rest int3 (never reached: the stub returns past the body).
inline bool Jump(std::uint64_t at,std::uint64_t stub,std::size_t size,unsigned char* out) noexcept {
    const std::int64_t rel=static_cast<std::int64_t>(stub)-static_cast<std::int64_t>(at+5);
    if(size<5 || rel<INT32_MIN || rel>INT32_MAX)return false;
    const std::int32_t rel32=static_cast<std::int32_t>(rel);
    out[0]=0xE9;
    std::memcpy(out+1,&rel32,4);
    std::memset(out+5,0xCC,size-5);
    return true;
}
}  // namespace crew::mechboost
