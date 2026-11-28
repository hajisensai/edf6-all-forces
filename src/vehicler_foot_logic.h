// The Vehicler on foot (docs/newclass-re/vehicler.md §4, §5 M5; src/vehicler_foot.cpp), the pure part (tests/vehicler_foot_test.cpp;
// the EDF.dll side tests/vehicler_foot_native_audit.py):
//  - No dash. The Ranger's dash starts in AssultSoldier's frame (vtable 0x17CDF28 slot 5, 0x550C30) at 0x550C56: `cmp byte
//    [soldier+0xD7B],0; je` past the dash start (0x5518A0 with the action 0x552040). +0xD7B is the dash button of this frame,
//    written by the input fill of slot 4 (0x572DF0 -> 0x570650: 0x570855) and nothing else in slot 5 before the check. The
//    class SGO has no say (the dash's numbers 100 / 0x65..0x67 are the accessories' custom_parameter, docs/newclass-re/motion.md
//    §3), so the plugin wraps slot 5: for a Vehicler the byte reads 0 over the stock frame and is put back after it. Roll
//    (its own button and action) is untouched.
//  - The Gyrojet's damage that grows with flight. The bullet core's update 0x235D50 ends with the stock falloff gate 0x235E7B:
//    `movss xmm0,[0.99]; comiss xmm0,[core+0xA10]; jbe` past `call 0x231080; movss [core+0x780],xmm0`. 0x231080 is
//    D * (a + (1 - a) * (1 - t)^b) (D = core+0xA0C * core+0x9A0, a = core+0xA10, b = core+0xA18, t = clamp(age core+0xAF8 /
//    life core+0xA08, 0, 1)): AmmoDamageReduce [a, b]. Stock only runs it for a < 0.99 (no stock weapon has a > 1: the RE note
//    counted every WEAPON/*.SGO). The gate is widened to a > 1 too, so AmmoDamageReduce [5, 2] starts at D and grows to 5 D.
//    Every stock weapon (a <= 1) is gated as before; the growth is the weapon's own data, so it holds online wherever the
//    round is simulated (each machine runs its own copy of a fired round from the same SGO).
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace crew::vehicler_foot {

// ---- the sites (checked by vehicler_foot.cpp before anything is hooked; tests/vehicler_foot_native_audit.py reads this table) ----
struct Print { std::size_t rva; const char* hex; const char* what; };
inline constexpr Print kPrints[]={
    {0x550C30,"4053 4881ece0000000 488bd9 e86f9f0400","AssultSoldier slot 5 (frame) head: call 0x59ABB0 (SoldierBase frame)"},
    {0x550C41,"f7839c030000fdffffff 7557 f683d005000004 754e 80bb7b0d000000 7445",
     "slot 5 dash gate: state, +0x5D0 bit 2, then `cmp byte [rbx+0xD7B],0; je` past the dash"},
    {0x550C5F,"33d2 488bcb 448d4201 e8d3000400 84c0 7433 c744242800000000 488d05c0130000",
     "slot 5 dash start: the action 0x552040 (lea) handed to 0x5518A0"},
    {0x570855,"88837b0d0000","slot 4 input fill: +0xD7B (the dash button) written"},
    {0x235E7B,"f30f100555835601 0f2f87100a0000 7610","bullet core falloff gate: 0.99 > AmmoDamageReduce[0] (core+0xA10), else skip"},
    {0x235E8C,"488bcf e8ecb1ffff f30f118780070000","falloff refresh: core+0x780 = 0x231080(core)"},
    {0x235E9C,"4c8b742430","after the refresh (the gate's skip target)"},
    {0x179E1D8,"a4707d3f","the gate's 0.99f"},
    {0x231080,"4883ec48 660f6e91f80a0000","the falloff 0x231080 head (age core+0xAF8)"},
};
constexpr std::size_t kMostPrintBytes=40;
constexpr const Print* Find(std::size_t rva) noexcept {
    for(const Print& p:kPrints)
        if(p.rva==rva)return &p;
    return nullptr;
}
constexpr int HexDigit(char c) noexcept { return c>='0' && c<='9' ? c-'0' : c>='a' && c<='f' ? c-'a'+10 : -1; }
// A print's bytes (pairs of lowercase hex digits, spaces between): their count, or 0 when malformed or over `cap`.
constexpr std::size_t Decode(const char* hex,unsigned char* out,std::size_t cap) noexcept {
    std::size_t n=0;
    for(std::size_t i=0;hex[i];) {
        if(hex[i]==' ') { ++i; continue; }
        const int hi=HexDigit(hex[i]),lo=hex[i+1] ? HexDigit(hex[i+1]) : -1;
        if(hi<0 || lo<0 || n>=cap)return 0;
        out[n++]=static_cast<unsigned char>(hi*16+lo);
        i+=2;
    }
    return n;
}

// ---- the dash ----
constexpr std::size_t kSoldierVtable=0x17CDF28,kFrameSlot=5,kStockFrame=0x550C30;
constexpr std::size_t kDashButton=0xD7B;
constexpr int kVehiclerType=4;   // soldier_type::kVehicler
// Whether a soldier of this type (soldier::ZwType) has its dash button held back over its frame.
constexpr bool DashHeldBack(int zwType) noexcept { return zwType==kVehiclerType; }

// ---- the falloff gate ----
constexpr std::size_t kGate=0x235E7B,kGateSize=17,kRefresh=0x235E8C,kSkip=0x235E9C;
constexpr float kStockGate=0.99f;   // 0x179E1D8
// Whether the bullet core's falloff is refreshed this frame for AmmoDamageReduce[0] = `a`: the stock rule (0.99 > a, an ordered
// compare, so NaN skips) or growth (a > 1).
constexpr bool Refreshes(float a) noexcept { return kStockGate>a || a>1.0f; }
// 0x231080's value for damage `d` (core+0xA0C * core+0x9A0), AmmoDamageReduce [a, b], `age` of `life` frames.
inline float Falloff(float d,float a,float b,int age,int life) noexcept {
    float t=static_cast<float>(age)/static_cast<float>(life);
    t=0.0f>t ? 0.0f : (1.0f<t ? 1.0f : t);
    const float da=d*a;
    return std::pow(1.0f-t,b)*(d-da)+da;
}

// The code the gate's 17 bytes jump to (vehicler_foot.cpp: RedirectToCode, Branch::kJump). rdi = the core, as at the site; only
// xmm0 is written (the stock gate writes it too, and neither target reads it: 0x235E8C sets rcx and calls, 0x235E9C restores
// registers and returns). Both ways back are absolute jumps, so the page may sit anywhere.
//    0  f3 0f 10 05 <54>          movss  xmm0,[rip+54]      0.99f (offset 62)
//    8  0f 2f 87 10 0a 00 00      comiss xmm0,[rdi+0xA10]
//   15  77 1f                     ja     refresh (48)       stock: 0.99 > a
//   17  f3 0f 10 87 10 0a 00 00   movss  xmm0,[rdi+0xA10]
//   25  0f 2f 05 <34>             comiss xmm0,[rip+34]      1.0f (offset 66)
//   32  77 0e                     ja     refresh (48)       growth: a > 1
//   34  ff 25 00000000 <skip>     jmp    [skip]
//   48  ff 25 00000000 <refresh>  jmp    [refresh]
//   62  0.99f   66  1.0f
constexpr std::size_t kCaveSize=70;
inline void BuildCave(unsigned char (&out)[kCaveSize],std::uint64_t skip,std::uint64_t refresh) noexcept {
    constexpr unsigned char head[34]={0xF3,0x0F,0x10,0x05,54,0,0,0, 0x0F,0x2F,0x87,0x10,0x0A,0x00,0x00, 0x77,0x1F,
                                      0xF3,0x0F,0x10,0x87,0x10,0x0A,0x00,0x00, 0x0F,0x2F,0x05,34,0,0,0, 0x77,0x0E};
    std::memcpy(out,head,sizeof(head));
    const unsigned char jmp[6]={0xFF,0x25,0,0,0,0};
    std::memcpy(out+34,jmp,6);std::memcpy(out+40,&skip,8);
    std::memcpy(out+48,jmp,6);std::memcpy(out+54,&refresh,8);
    const float gate=kStockGate,one=1.0f;
    std::memcpy(out+62,&gate,4);std::memcpy(out+66,&one,4);
}

}  // namespace crew::vehicler_foot
