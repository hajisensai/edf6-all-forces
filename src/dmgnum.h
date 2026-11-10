// The damage numbers' colours and the allies' numbers (README 伤害数字, docs/damage-numbers-re.md), pure: no Windows, no
// game (tools/dmgnum_check.cpp). src/dmgnum.cpp does the hooking.
//
// The stock: the one damage function 0x547C30 builds a 0x40-byte notice after it takes the HP off, {attacker, victim,
// GameDamageInfo*, signed HP change, fill colour (1,1,1,1), second colour (0,0,0,1)}, and hands it to the attacker's
// slot 21 (0x54823D). Only a player's (0x54AAC0: attacker+0x340 set) or a vehicle a player sits in (0x62F4F0, the notice
// re-sent with the rider as attacker) reaches the cameras, and only the camera following that player passes it to the
// HUD (0xF7550); the HUD copies the two colours into the number it draws (0x820710 -> the entry +0x00 / +0x10 -> the
// number layer's slots 8 / 9). So:
//  - a weak spot / friendly fire colour is the notice's fill colour, picked where the stock writes it (0x54820F) from
//    what the damage function itself decided: its friendly fire flag ([rsp+0x20], set at 0x547DE6 when the attacker's
//    team is friendly to the victim's) and its hit part multiplier ([rsp+0x44], the victim's slot 17: a weak spot's
//    weak_damage_multiply, a humanoid's head, 1 for a body without parts; read at 0x548148);
//  - an NPC's or a support unit's hit is shown by handing the same notice, the local player as its attacker, to the
//    stock player path 0x54AAC0 (what 0x62F4F0 does for a vehicle's rider).
#pragma once
#include "damage_stats.h"
#include <cstdint>

namespace dmgnum {

struct Rgba { float r,g,b,a; };
inline constexpr Rgba kStockFill{1.0f,1.0f,1.0f,1.0f};     // the stock's two constants (0x1765B90, 0x1765B30)
inline constexpr Rgba kStockSecond{0.0f,0.0f,0.0f,1.0f};

// 0xRRGGBB (the ini's form) as an opaque colour.
inline Rgba FromRgb(std::uint32_t rgb) noexcept {
    return Rgba{static_cast<float>((rgb>>16)&0xFFu)/255.0f,static_cast<float>((rgb>>8)&0xFFu)/255.0f,
                static_cast<float>(rgb&0xFFu)/255.0f,1.0f};
}

enum class Kind : std::uint8_t { normal, weak, friendly };
// A part multiplier above this is a weak spot (a float written by the game: 1 exactly for no part; armour below 1).
inline constexpr float kWeakAbove=1.001f;
// The victims that are a weak spot as a whole: a separate part object the boss takes its weak-spot damage through,
// whose slot 17 accepts the hit without scaling it (docs/damage-numbers-re.md §3: EDF6_GrandMotherParts_Weak 0x37C7A0).
inline constexpr const char* kWeakPartClasses[]={".?AVEDF6_GrandMotherParts_Weak@@"};
inline bool WeakPartClass(const char* rtti) noexcept {
    if(!rtti)return false;
    for(const char* c:kWeakPartClasses) {
        const char* a=rtti;
        const char* b=c;
        while(*a && *a==*b){++a;++b;}
        if(*a==*b)return true;
    }
    return false;
}
// What the hit was, from the damage function's own facts. Friendly fire first: a misfire on an ally's weak part is
// still a misfire. A multiplier that is not a number (unreadable) counts as none.
inline Kind Classify(bool friendlyFire,float partMultiplier,bool weakPart) noexcept {
    if(friendlyFire)return Kind::friendly;
    return weakPart || partMultiplier>kWeakAbove ? Kind::weak : Kind::normal;   // NaN compares false: normal
}

struct Colors {
    bool tint=true;                 // ini DamageNumberColors: weak spots and friendly fire in their own colours
    std::uint32_t weak=0xFFC800;    // ini DamageNumberWeakColor
    std::uint32_t friendly=0xFF4040;   // ini DamageNumberFriendlyColor
    std::uint32_t ally=0xFFFFFF;    // ini DamageNumberAllyColor: an NPC's / support unit's normal hit (0xFFFFFF: stock white)
};
// The fill colour of a hit of kind `k`; `allyHit` when it is an NPC's (shown by the plugin, not the stock).
inline Rgba Fill(const Colors& c,Kind k,bool allyHit) noexcept {
    if(c.tint && k==Kind::friendly)return FromRgb(c.friendly);
    if(c.tint && k==Kind::weak)return FromRgb(c.weak);
    return allyHit ? FromRgb(c.ally) : kStockFill;
}

// Whether this machine's screen gets a number the stock would not show: the hit of an NPC soldier (squad), a friendly
// object that is no soldier and no vehicle (ally: a sentry, a bomber), or a vehicle nobody plays in (support: an NPC
// tank, the plugin's helicopters and jets). Mine and my vehicle's the stock shows; another player's go to their screen
// (the stock sends them to their camera); an enemy's and the world's are no one's numbers.
inline bool AllyNumber(dmgstat::Side s) noexcept {
    return s==dmgstat::Side::squad || s==dmgstat::Side::ally || s==dmgstat::Side::support;
}
// An ally's number is worth showing: HP changed (a zero is friendly fire the difficulty nullified, or a hit the target
// shrugged off: the stock shows the player's zeros, an ally's would only crowd the screen).
inline bool WorthShowing(float change) noexcept { return change<0.0f || change>0.0f; }

// The shim the stock's colour stores jump to (0x54820F .. 0x548225: movaps xmm0,[fill]; movaps [rbp+20h],xmm0; movaps
// xmm1,[second]; movaps [rbp+30h],xmm1). There rcx is the attacker (live: the stock tested it at 0x54820A), rbp the
// notice, rdi the victim, r13 the GameDamageInfo, xmm6 the signed HP change, rsp the damage function's frame (16-byte
// aligned). It calls Hit(notice, victim, gdi, frame, attacker, change), which writes both colours, keeps rcx (the stock
// stores it next, 0x548225) and returns to 0x548225. xmm0 / xmm1 / the other volatile registers are dead there (the
// next call, 0x54823D, takes rcx / rdx only and clobbers the rest).
inline constexpr unsigned kStubHook=32,kStubBack=53;   // offsets of the two absolute addresses
inline constexpr unsigned char kStub[]={
    0x51,                               // push rcx                  (rsp = 8 mod 16)
    0x48,0x83,0xEC,0x38,                // sub rsp,38h               (aligned; 0x20 home + two stack arguments)
    0x48,0x89,0x4C,0x24,0x20,           // mov [rsp+20h],rcx         5th: the attacker
    0xF3,0x0F,0x11,0x74,0x24,0x28,      // movss [rsp+28h],xmm6      6th: the HP change
    0x48,0x89,0xE9,                     // mov rcx,rbp               the notice
    0x48,0x89,0xFA,                     // mov rdx,rdi               the victim
    0x4D,0x89,0xE8,                     // mov r8,r13                the GameDamageInfo
    0x4C,0x8D,0x4C,0x24,0x40,           // lea r9,[rsp+40h]          the damage function's rsp
    0x48,0xB8,0,0,0,0,0,0,0,0,          // mov rax,Hit               (its address at offset 32)
    0xFF,0xD0,                          // call rax
    0x48,0x83,0xC4,0x38,                // add rsp,38h
    0x59,                               // pop rcx
    0xFF,0x25,0,0,0,0,                  // jmp [rip]
    0,0,0,0,0,0,0,0,                    // 0x548225                  (offset 53)
};
static_assert(sizeof(kStub)==kStubBack+8,"the stub's layout");

}  // namespace dmgnum
