// The new classes' player object and per-type tables, the pure part (no game): the type numbers and what the script boundary
// sees (docs/plan-2026-11-28-new-classes-impl.md §1), the nameplate letters and colours, the damage colour stub's bytes, the
// stock vtables the clone is built from, and the EDF.dll sites (src/soldier_sites.inc) decoded. src/soldier_factory.cpp and
// src/soldier_type_patches.cpp use it; tests/soldier_type_test.cpp checks it, tests/soldier_native_audit.py checks the same
// sites and tables against the game's EDF.dll.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace crew::soldier_type {

// ---- type numbers (plan §1) ----
constexpr int kVehicler=4,kRuneBlade=5;   // ZwVehicler (V), ZwRuneBlade (S)
constexpr int kStockTypes=4,kTypes=6;
constexpr bool IsNewType(int type) noexcept { return type==kVehicler || type==kRuneBlade; }
// What the mission scripts see (GetPlayerSoldierType, native 1005): both new classes are Rangers to them (plan §1: 0), so the
// tutorials' per-class branches run the Ranger's. Every other value passes as it is.
constexpr int ScriptType(int type) noexcept { return IsNewType(type) ? 0 : type; }

// ---- the HUD nameplate table (0x806FE0: entries of 0x30 bytes, a std::wstring at +0, RGBA floats at +0x20) ----
constexpr std::size_t kPlateStride=0x30,kPlateColour=0x20,kPlateAlpha=0x2C;
struct Plate { int type; wchar_t letter; float r,g,b; };
constexpr Plate kPlates[2]={{kVehicler,L'V',1.0f,0.55f,0.0f},{kRuneBlade,L'S',0.6f,0.4f,1.0f}};
constexpr const Plate* PlateOf(int type) noexcept {
    for(const Plate& p:kPlates)
        if(p.type==type)return &p;
    return nullptr;
}

// ---- the damage number colour (0x81F6F0) ----
// Stock: [this + 0x920 + type*0x30 + column*0x10], rows 0..3 for the four classes, row 4 the in-vehicle colours (so type 4 read
// those and type 5 read past the table). The new classes read the plugin's rows instead: the nameplate colour, opaque, in all
// three columns.
constexpr int kDamageColumns=3;
struct DamageRows { float rgba[2][kDamageColumns][4]; };
constexpr DamageRows MakeDamageRows() noexcept {
    DamageRows d{};
    for(int k=0;k<2;++k)
        for(int c=0;c<kDamageColumns;++c) {
            d.rgba[k][c][0]=kPlates[k].r;
            d.rgba[k][c][1]=kPlates[k].g;
            d.rgba[k][c][2]=kPlates[k].b;
            d.rgba[k][c][3]=1.0f;
        }
    return d;
}
constexpr std::size_t kDamageSite=0x81F844,kDamageSiteSize=22,kDamageBack=0x81F85A;
// The stub the 22 bytes at kDamageSite jump to (eax = the type, rbx = the column; rax, rcx, rdx and the flags are dead there:
// 0x81F85A sets rax, the function returns soon after):
//     lea ecx,[rax-4] / cmp ecx,1 / ja stock
//     lea edx,[rcx+rcx*2] / add rdx,rbx / shl rdx,4 / mov rax,table / movups xmm0,[rax+rdx] / jmp [rip] back
//   stock: the 22 bytes as they were / jmp [rip] back
// Position independent (absolute jumps), so it runs wherever AllocateNearCode puts it. Returns its size, 0 when `cap` is short.
constexpr std::size_t kDamageAbsJump=14;                          // jmp [rip] + the 8-byte address
constexpr std::size_t kDamagePluginPath=3+3+4+10+4+kDamageAbsJump; // lea, add, shl, mov rax imm64, movups, jmp back
constexpr std::size_t kDamageStockAt=3+3+2+kDamagePluginPath;      // the ja's target
static_assert(kDamagePluginPath<0x80,"the ja is a rel8");
constexpr std::size_t kDamageStubSize=kDamageStockAt+kDamageSiteSize+kDamageAbsJump;
inline std::size_t BuildDamageStub(unsigned char* out,std::size_t cap,const unsigned char* stock,std::uint64_t table,
                                   std::uint64_t back) noexcept {
    if(cap<kDamageStubSize)return 0;
    std::size_t n=0;
    auto put=[&](std::initializer_list<unsigned char> bytes) { for(unsigned char b:bytes)out[n++]=b; };
    auto put64=[&](std::uint64_t v) { std::memcpy(out+n,&v,8); n+=8; };
    put({0x8D,0x48,0xFC});          // lea ecx,[rax-4]
    put({0x83,0xF9,0x01});          // cmp ecx,1
    put({0x77,static_cast<unsigned char>(kDamagePluginPath)});   // ja stock
    put({0x8D,0x14,0x49});          // lea edx,[rcx+rcx*2]
    put({0x48,0x01,0xDA});          // add rdx,rbx
    put({0x48,0xC1,0xE2,0x04});     // shl rdx,4
    put({0x48,0xB8}); put64(table); // mov rax,table
    put({0x0F,0x10,0x04,0x10});     // movups xmm0,[rax+rdx]
    put({0xFF,0x25,0,0,0,0}); put64(back);
    std::memcpy(out+n,stock,kDamageSiteSize); n+=kDamageSiteSize;
    put({0xFF,0x25,0,0,0,0}); put64(back);
    return n;
}

// ---- the script boundary sites (6 bytes: call [rax+0x298], then `mov esi,eax`) ----
constexpr std::size_t kScriptSites[2]={0x1B5095,0x222813};   // GetPlayerSoldierType 0x1B5030, BVM native 1005 0x222740
constexpr std::size_t kScriptSiteSize=6;
constexpr unsigned char kTypeCall[kScriptSiteSize]={0xFF,0x90,0x98,0x02,0x00,0x00};

// ---- the player object (AssultSoldier, docs/newclass-re/spawn.md) ----
constexpr std::size_t kCreate=0x54FE50,kCtor=0x54FF10,kDtor=0x5505B0,kDelete=0x12D85EC,kStockSize=0x2090;
constexpr std::size_t kVtable=0x17CDF28,kTable120=0x17CE220,kTable1940=0x17CE2D8;
constexpr std::size_t kAt120=0x120,kAt1940=0x1940;
constexpr int kSlotType=83;    // +0x298
constexpr int kMainSlots=94,kSlots120=22,kSlots1940=2;
// The three vtables as the file has them (the clone's slots go to these, not to the live tables).
constexpr std::uint32_t kMainVtable[kMainSlots]={
    0x59A770,0x5505B0,0x54BE10,0x59A780,0x550A30,0x550C30,0x596820,0x596B50,0x54A290,0x598FD0,0x54AA50,0x54A970,0x266BE0,0x599960,
    0x59BCD0,0x54A330,0x599650,0x54AA30,0x5985C0,0x5727B0,0x572770,0x54AAC0,0x54ABD0,0x266BB0,0x598E10,0x266970,0x54A320,0x266BF0,
    0x598390,0x571540,0x598520,0x54A2E0,0x550A10,0x550890,0x5A3600,0x5A3630,0x54C800,0x54CAA0,0x54C900,0x59C3D0,0x59D2B0,0x577020,
    0x54CBD0,0x54CB60,0x575CD0,0x550A40,0x5508E0,0x598FB0,0x597E80,0x598F00,0x550680,0x550720,0x59FDB0,0x592180,0x595F50,0x596130,
    0x595F40,0x59C2E0,0x59C3B0,0x59BCF0,0x5998E0,0x599710,0x551020,0x570D80,0x570D60,0x57B250,0x57BCA0,0x579970,0x552E20,0x57A9B0,
    0x57A430,0x57A6E0,0x5790A0,0x5A1920,0x5A1590,0x57AC60,0x552EA0,0x5A1540,0x578FB0,0x5A1660,0x578850,0x578790,0x599A30,0x550A20,
    0x550C20,0x5A1BA0,0x59A880,0x598F80,0x5508D0,0x590710,0x590280,0x58FF70,0x59FB10,0x59CE80};
constexpr std::uint32_t kVtable120[kSlots120]={
    0x550414,0x127010,0x572D10,0x549C40,0x774920,0x125A70,0x125180,0x59FA80,0x592130,0x1251A0,0x1251B0,0x125230,0x125A90,0x127D30,
    0x773B50,0x5510E0,0x773DA0,0x577C30,0x7739E0,0x549C50,0x56DA30,0x1256A0};
constexpr std::uint32_t kVtable1940[kSlots1940]={0x550420,0x597F00};

// The plugin's part of the object: past the stock 0x2090 bytes (16-aligned), destroyed by the clone's destructor.
constexpr std::uint32_t kStateMagic=0x5A57534Fu;   // "OSWZ"

// ---- the sites (src/soldier_sites.inc) ----
struct Site { std::size_t rva; const char* hex; const char* what; };
struct Patch { std::size_t rva; const char* from; const char* to; const char* what; };
#define SOLDIER_PRINT(rva,hex,what) Site{rva,hex,what},
#define SOLDIER_PATCH(rva,from,to,what)
inline constexpr Site kPrints[]={
#include "soldier_sites.inc"
};
#undef SOLDIER_PRINT
#undef SOLDIER_PATCH
#define SOLDIER_PRINT(rva,hex,what)
#define SOLDIER_PATCH(rva,from,to,what) Patch{rva,from,to,what},
inline constexpr Patch kPlatePatches[]={
#include "soldier_sites.inc"
};
#undef SOLDIER_PRINT
#undef SOLDIER_PATCH

constexpr std::size_t kMostSiteBytes=48;
constexpr int HexDigit(char c) noexcept {
    return c>='0' && c<='9' ? c-'0' : c>='a' && c<='f' ? c-'a'+10 : -1;
}
// Decodes a site's byte string (pairs of lowercase hex digits, spaces between). Its size, or 0 when it is malformed or longer
// than `cap`.
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
constexpr const Site* FindPrint(std::size_t rva) noexcept {
    for(const Site& s:kPrints)
        if(s.rva==rva)return &s;
    return nullptr;
}

}  // namespace crew::soldier_type
