// The backpack weapons a support soldier may carry (the user, 2026-10-18: 「让npc可以使用背包位武器如突击的手雷，飞翼的短剑
// 等」; docs/npc-backpack-re.md). Pure data: src/support_loadout.h (the preset grammar, the variant, the file name),
// src/room_menu.h (the in-game menu), src/npc_backpack.h (when a soldier uses one) and tools/support_loadout.py (the
// generator; tests/support_loadout_ini_test.py keeps its PACKS equal to this table and, with the game, checks every row
// against WEAPON/WEAPONTABLE.SGO and the weapon's own SGO).
//
// A player's backpack is the class's sub weapon slots (CONFIG.SGO SoldierInit, the player templates' soldier_sub_weapon_slot,
// docs/loadout-re.md §1.2): the Ranger's slot 2 (Weapon_SlotSupport2, categories 20-23) and the Wing Diver's slot 2
// (PaleSupport2, 110-116) are the weapons its backpack button throws or swings. The table holds the base game's ones whose
// use an AI can judge from the target alone:
//   thrown  a ballistic round thrown at a group (Ranger 20 hand grenades, 23 thermite / binary rounds; Wing Diver 110 the
//           plasma grenades), its arc from the weapon's own speed and gravity;
//   direct  a straight round at a target in its reach (Wing Diver 111 the sabers, 116 the spears).
// Left out: the Ranger's 21 bombs / impulses (a second press detonates) and 22 sentry guns, the Aerial Reversers (they
// heal), the Wing Diver's 112 traps, 113 shields, 114 specials and 115 guided grenades (a lock-on), every DLC (acquire 3)
// and mission pack (MPACK) row, the Fencer's 206-209 and the Air Raider's support slot (passive accessories, calls).
// `code` is the soldier variant's byte (support_loadout.h LookVariant bits 50-57): a code once given is never reused.
// `lift`: the weapon's FireVector y / z (the throw leaves above the look by atan(lift)); `area`: the radius (m) a round
// without a blast burns or gasses (kept off friends), 0 for a blast round (its own radius is the weapon's +0x8B0).
#pragma once
#include <cstdint>
#include <cwchar>
#include <cwctype>

namespace crew {
enum class PackUse : std::uint8_t { thrown, direct };
struct PackWeapon {
    std::uint8_t code;
    const wchar_t* sgo;         // WEAPONTABLE.SGO's name; the file app:/weapon/<sgo>.sgo
    std::uint8_t soldierClass;  // support_call.h SupportSoldierClass: 0 Ranger, 1 Wing Diver
    PackUse use;
    std::int16_t category;      // WEAPONTABLE.SGO column 2
    float lift;
    float area;
    const wchar_t* label;       // the weapon's English name (WEAPONTEXT.EN.SGO)
};
inline constexpr PackWeapon kPackWeapons[]={
    {1,L"aHandGrenade01",0,PackUse::thrown,20,0.125f,0.0f,L"MG11"},
    {2,L"aWeapon221",0,PackUse::thrown,20,0.125f,0.0f,L"MG11J"},
    {3,L"aWeapon222",0,PackUse::thrown,20,0.125f,0.0f,L"T5 Mouse"},
    {4,L"aWeapon223",0,PackUse::thrown,20,0.0f,0.0f,L"DNG1"},
    {5,L"aWeapon224",0,PackUse::thrown,20,0.125f,0.0f,L"Firecracker 1"},
    {6,L"aWeapon225",0,PackUse::thrown,20,0.125f,0.0f,L"MG12"},
    {7,L"aWeapon226",0,PackUse::thrown,20,0.125f,0.0f,L"MG13"},
    {8,L"aWeapon227",0,PackUse::thrown,20,0.125f,0.0f,L"T44 Mouse"},
    {9,L"aWeapon228",0,PackUse::thrown,20,0.125f,0.0f,L"MG13JA"},
    {10,L"aWeapon229",0,PackUse::thrown,20,0.125f,0.0f,L"MG14"},
    {11,L"aWeapon230",0,PackUse::thrown,20,0.125f,0.0f,L"MG14J"},
    {12,L"aWeapon231",0,PackUse::thrown,20,0.125f,0.0f,L"Firecracker 2"},
    {13,L"aWeapon232",0,PackUse::thrown,20,0.125f,0.0f,L"T6 Mouse"},
    {14,L"aWeapon233",0,PackUse::thrown,20,0.0f,0.0f,L"DNG2"},
    {15,L"aWeapon234",0,PackUse::thrown,20,0.125f,0.0f,L"MG15"},
    {16,L"aWeapon235",0,PackUse::thrown,20,0.1f,0.0f,L"MG20"},
    {17,L"aWeapon236",0,PackUse::thrown,20,0.125f,0.0f,L"Firecracker 3"},
    {18,L"aWeapon237",0,PackUse::thrown,20,0.1f,0.0f,L"TJ444 Mouse"},
    {19,L"aWeapon238",0,PackUse::thrown,20,0.1f,0.0f,L"MG21J"},
    {20,L"aWeapon239",0,PackUse::thrown,20,0.1f,0.0f,L"MG21"},
    {21,L"aWeapon240",0,PackUse::thrown,20,0.0f,0.0f,L"DNG3"},
    {22,L"aWeapon241",0,PackUse::thrown,20,0.125f,0.0f,L"Firecracker 4"},
    {23,L"aWeapon242",0,PackUse::thrown,20,0.1f,0.0f,L"MG22"},
    {24,L"aWeapon243",0,PackUse::thrown,20,0.125f,0.0f,L"Bound Grenade"},
    {25,L"aWeapon244",0,PackUse::thrown,20,0.07f,0.0f,L"MG30"},
    {26,L"aWeapon245",0,PackUse::thrown,20,0.125f,0.0f,L"TJO Mouse"},
    {27,L"aWeapon246",0,PackUse::thrown,20,0.125f,0.0f,L"Firecracker 0"},
    {28,L"aWeapon247",0,PackUse::thrown,20,0.0f,0.0f,L"DNG9"},
    {29,L"aWeapon248",0,PackUse::thrown,20,0.07f,0.0f,L"MG40"},
    {30,L"aWeapon249",0,PackUse::thrown,20,0.07f,0.0f,L"TZ6 Mouse"},
    {31,L"aWeapon250",0,PackUse::thrown,20,0.05f,0.0f,L"MG50"},
    {32,L"aWeapon251",0,PackUse::thrown,20,0.0f,0.0f,L"DNG10"},
    {33,L"aWeapon252",0,PackUse::thrown,20,0.125f,0.0f,L"Firecracker 1000"},
    {34,L"aWeapon253",0,PackUse::thrown,20,0.05f,0.0f,L"TZ8 Mouse"},
    {35,L"aWeapon254",0,PackUse::thrown,23,0.125f,8.0f,L"Thermite Round"},
    {36,L"aWeapon255",0,PackUse::thrown,23,0.125f,8.0f,L"Nanothermite Round"},
    {37,L"aWeapon256",0,PackUse::thrown,23,0.1f,8.0f,L"M2 Thermite Round"},
    {38,L"aWeapon257",0,PackUse::thrown,23,0.07f,8.0f,L"BX Nanothermite Round"},
    {39,L"aWeapon258",0,PackUse::thrown,23,0.07f,8.0f,L"Metathermite Round"},
    {40,L"aWeapon259",0,PackUse::thrown,23,0.05f,8.0f,L"M3 Thermite Round"},
    {41,L"aWeapon260",0,PackUse::thrown,23,0.05f,8.0f,L"M9 Thermite Round"},
    {42,L"aWeapon266",0,PackUse::thrown,23,0.125f,8.0f,L"A61 Binary Round"},
    {43,L"aWeapon267",0,PackUse::thrown,23,0.1f,8.0f,L"A62 Binary Round"},
    {44,L"aWeapon268",0,PackUse::thrown,23,0.07f,8.0f,L"A71 Binary Round"},
    {45,L"aWeapon269",0,PackUse::thrown,23,0.05f,8.0f,L"A72 Binary Round"},
    {46,L"pHandGrenade01",1,PackUse::thrown,110,0.0f,0.0f,L"Blink Ball α"},
    {47,L"pWeapon197",1,PackUse::thrown,110,0.0f,0.0f,L"Starmine"},
    {48,L"pWeapon198",1,PackUse::thrown,110,0.0f,0.0f,L"Blink Ball"},
    {49,L"pWeapon199",1,PackUse::thrown,110,0.0f,0.0f,L"Split Ball"},
    {50,L"pWeapon200",1,PackUse::thrown,110,0.0f,0.0f,L"Blink Ball B"},
    {51,L"pWeapon201",1,PackUse::thrown,110,0.0f,0.0f,L"Starmine 2"},
    {52,L"pWeapon202",1,PackUse::thrown,110,0.0f,0.0f,L"Blink Ball C"},
    {53,L"pWeapon203",1,PackUse::thrown,110,0.0f,0.0f,L"Blink Fork"},
    {54,L"pWeapon204",1,PackUse::thrown,110,0.0f,0.0f,L"Starmine F"},
    {55,L"pWeapon205",1,PackUse::thrown,110,0.0f,0.0f,L"Blink Ball D"},
    {56,L"pWeapon206",1,PackUse::thrown,110,0.0f,0.0f,L"Split Ball AT"},
    {57,L"pWeapon207",1,PackUse::thrown,110,0.0f,0.0f,L"Starmine Z"},
    {58,L"pWeapon208",1,PackUse::thrown,110,0.0f,0.0f,L"Blink Ball FZ"},
    {59,L"pWeapon216",1,PackUse::direct,111,0.0f,0.0f,L"Handy Saber"},
    {60,L"pWeapon217",1,PackUse::direct,111,0.0f,0.0f,L"Short Saber"},
    {61,L"pWeapon218",1,PackUse::direct,111,0.0f,0.0f,L"Handy Saber 2"},
    {62,L"pWeapon219",1,PackUse::direct,111,0.0f,0.0f,L"Handy Saber 3"},
    {63,L"pWeapon220",1,PackUse::direct,111,0.0f,0.0f,L"Short Saber 2"},
    {64,L"pWeapon221",1,PackUse::direct,111,0.0f,0.0f,L"Power Saber"},
    {65,L"pWeapon222",1,PackUse::direct,111,0.0f,0.0f,L"Handy Saber S"},
    {66,L"pWeapon223",1,PackUse::direct,111,0.0f,0.0f,L"Valiant Saber"},
    {67,L"pWeapon238",1,PackUse::direct,116,0.0f,0.0f,L"Pulsar Spear M1"},
    {68,L"pWeapon239",1,PackUse::direct,116,0.0f,0.0f,L"Glow Spear"},
    {69,L"pWeapon240",1,PackUse::direct,116,0.0f,0.0f,L"Pulsar Spear M2"},
    {70,L"pWeapon241",1,PackUse::direct,116,0.0f,0.0f,L"Pulsar Spear M3"},
    {71,L"pWeapon242",1,PackUse::direct,116,0.0f,0.0f,L"Power Spear"},
    {72,L"pWeapon243",1,PackUse::direct,116,0.0f,0.0f,L"Pulsar Spear M4"},
    {73,L"pWeapon244",1,PackUse::direct,116,0.0f,0.0f,L"Glow Spear S"},
    {74,L"pWeapon245",1,PackUse::direct,116,0.0f,0.0f,L"Power Spear S3"},
};
inline constexpr int kPackWeaponCount=static_cast<int>(sizeof(kPackWeapons)/sizeof(kPackWeapons[0]));
static_assert(kPackWeaponCount<256,"a code is one byte of the variant");

constexpr const PackWeapon* PackOf(int code) noexcept {
    for(const auto& p:kPackWeapons)if(p.code==code)return &p;
    return nullptr;
}
// The row named [a, b) (the WEAPONTABLE name, any case), nullptr when none.
inline const PackWeapon* PackNamed(const wchar_t* a,const wchar_t* b) noexcept {
    for(const auto& p:kPackWeapons) {
        const wchar_t* s=p.sgo;const wchar_t* c=a;
        for(;c<b && *s && std::towlower(*c)==std::towlower(*s);++c,++s){}
        if(c==b && !*s)return &p;
    }
    return nullptr;
}
// Whether a soldier of class `soldierClass` (0 Ranger, 1 Wing Diver, 2 Fencer) may carry pack `code` (0: none, always).
constexpr bool PackFits(int code,int soldierClass) noexcept {
    if(!code)return true;
    const PackWeapon* p=PackOf(code);
    return p && p->soldierClass==soldierClass;
}
// The next (step +1) / previous (-1) choice for a soldier of `soldierClass` in the cycle none, its packs in table order.
inline int CyclePack(int code,int soldierClass,int step) noexcept {
    int list[kPackWeaponCount+1];int n=0,at=0;
    list[n++]=0;
    for(const auto& p:kPackWeapons)if(p.soldierClass==soldierClass){if(p.code==code)at=n;list[n++]=p.code;}
    if(n==1)return 0;
    at=((at+step)%n+n)%n;
    return list[at];
}
}  // namespace crew
