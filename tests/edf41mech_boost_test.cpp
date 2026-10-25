// src/edf41mech_boost.h: the stubs that replace the 406 Begaruta jet group's two slot loops (the state step
// 0x64833A..0x648355, the boost stop 0x638EF0..0x638F08) run the stock instructions for a built slot and skip the
// emitter's write for an unbuilt one (null emitter, what the old begaruta_boost parser leaves in slots 2 and 3), and the
// patches jump to them. The stubs are run for real: a few bytes put the registers where the stock loops have them, the
// stub's way back is an epilogue.
#include "edf41mech_boost.h"
#include <Windows.h>
#include <cstdio>
#include <cstring>

namespace {
int failures=0,checks=0;
void Check(bool ok,const char* what) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}
using namespace crew::mechboost;

void Bytes() {
    unsigned char s[kStepStubSize];
    StepStub(0x1122334455667788ull,s);
    Check(!std::memcmp(s,kStepWritesCode,4),"step: the emitter load is the stock mov r8,[rax+0x50]");
    Check(s[4]==0x4D && s[5]==0x85 && s[6]==0xC0 && s[7]==0x74,"step: then test r8,r8; jz");
    Check(!std::memcmp(s+9,kStepWritesCode+4,sizeof(kStepWritesCode)-4),"step: then the stock writes byte for byte");
    Check(9+s[8]==kStepStubSize-kJumpBack,"step: the null jump lands on the way back");
    std::uint64_t back=0;
    std::memcpy(&back,s+kStepStubSize-8,8);
    Check(s[kStepStubSize-14]==0xFF && s[kStepStubSize-13]==0x25 && back==0x1122334455667788ull,"step: jmp [rip] back past the body");

    unsigned char t[kStopStubSize];
    StopStub(0x99AABBCCDDEEFF00ull,t);
    Check(!std::memcmp(t,kStopWritesCode,17),"stop: the stock loads and multiply first, emitter or not");
    Check(t[17]==0x48 && t[18]==0x85 && t[19]==0xC9 && t[20]==0x74,"stop: then test rcx,rcx; jz");
    Check(!std::memcmp(t+22,kStopWritesCode+17,8),"stop: then the stock write");
    Check(22+t[21]==kStopStubSize-kJumpBack,"stop: the null jump lands on the way back");
    std::memcpy(&back,t+kStopStubSize-8,8);
    Check(back==0x99AABBCCDDEEFF00ull,"stop: back past the body");

    unsigned char p[32];
    Check(Jump(0x7FF600000000ull+kStepWrites,0x7FF600100000ull,sizeof(kStepWritesCode),p) && p[0]==0xE9,"a near stub: jmp rel32");
    std::int32_t rel=0;
    std::memcpy(&rel,p+1,4);
    Check(0x7FF600000000ull+kStepWrites+5+rel==0x7FF600100000ull,"the jump reaches the stub");
    bool pad=true;
    for(std::size_t i=5;i<sizeof(kStepWritesCode);++i)pad=pad && p[i]==0xCC;
    Check(pad,"the rest of the body int3");
    Check(!Jump(0x100000000000ull,0x7FF600000000ull,sizeof(kStepWritesCode),p),"a stub out of rel32 reach is refused");
}

unsigned char* Page() {
    return static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
}

// The step: rax = the slot, dl = lit, xmm0 = the intensity (a function's rcx, edx, fourth argument xmm3).
using StepRun=void(*)(unsigned char* slot,int lit,float,float intensity);
void Step() {
    unsigned char* mem=Page();
    if(!mem){Check(false,"executable page");return;}
    const unsigned char pro[]={0x48,0x89,0xC8,0x0F,0x28,0xC3};   // mov rax,rcx; movaps xmm0,xmm3
    std::memcpy(mem,pro,sizeof(pro));
    mem[0x200]=0xC3;                                             // the way back: ret
    StepStub(reinterpret_cast<std::uint64_t>(mem+0x200),mem+sizeof(pro));
    const auto run=reinterpret_cast<StepRun>(mem);
    alignas(16) unsigned char slot[0xA0]{};
    alignas(16) unsigned char emitter[0x400]{};
    run(slot,1,0.0f,0.75f);
    Check(true,"step: an unbuilt slot is skipped, no fault");
    unsigned char* e=emitter;
    std::memcpy(slot+0x50,&e,8);
    run(slot,1,0.0f,0.75f);
    float v=0,lit=0;
    std::memcpy(&v,emitter+0x3EC,4);std::memcpy(&lit,emitter+0x3F0,4);
    Check(v==0.75f && lit==1.0f,"step: a built slot gets the intensity and the lit flag as stock");
    std::memset(emitter,0,sizeof(emitter));
    run(slot,0,0.0f,0.25f);
    std::memcpy(&v,emitter+0x3EC,4);std::memcpy(&lit,emitter+0x3F0,4);
    Check(v==0.25f && lit==0.0f,"step: not lit, only the intensity");
    VirtualFree(mem,0,MEM_RELEASE);
}

// The stop: rax = the slot, rdi = the vehicle (the group's intensity at +0x1BB8); returns xmm0 as the stub leaves it.
using StopRun=float(*)(unsigned char* slot,unsigned char* vehicle);
void Stop() {
    unsigned char* mem=Page();
    if(!mem){Check(false,"executable page");return;}
    const unsigned char pro[]={0x57,0x48,0x89,0xC8,0x48,0x89,0xD7};   // push rdi; mov rax,rcx; mov rdi,rdx
    std::memcpy(mem,pro,sizeof(pro));
    mem[0x200]=0x5F;mem[0x201]=0xC3;                                  // the way back: pop rdi; ret
    StopStub(reinterpret_cast<std::uint64_t>(mem+0x200),mem+sizeof(pro));
    const auto run=reinterpret_cast<StopRun>(mem);
    alignas(16) unsigned char slot[0xA0]{};
    alignas(16) unsigned char emitter[0x400]{};
    static unsigned char vehicle[0x1C00]{};
    const float share=0.5f,group=0.8f;
    std::memcpy(slot+0x40,&share,4);std::memcpy(vehicle+0x1BB8,&group,4);
    const float unbuilt=run(slot,vehicle);
    Check(unbuilt==share*group,"stop: an unbuilt slot is skipped, xmm0 still the stock product");
    unsigned char* e=emitter;
    std::memcpy(slot+0x50,&e,8);
    const float built=run(slot,vehicle);
    float v=0;
    std::memcpy(&v,emitter+0x3EC,4);
    Check(built==share*group && v==share*group,"stop: a built slot gets the slot's share of the group's intensity");
    VirtualFree(mem,0,MEM_RELEASE);
}
}  // namespace

int main() {
    Bytes();
    Step();
    Stop();
    std::printf("edf41mech_boost: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
