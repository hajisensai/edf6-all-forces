// EDF.dll 678CCB46. These weapons are SceneObjects, not GameObjectBase members.
#include "pvp_controlled_npc.h"
#include "memory.h"
#include "npc_native_hooks.h"
#include <array>
#include <cstring>

namespace crew { namespace npc_pvp_detail {
using controlled_pvp::Emitter;using controlled_pvp::EmitterKind;using controlled_pvp::Life;
// Exact primary slots read from the supported PE file; checked in the native audit.
constexpr std::uint32_t kActor0Slots[]={
    0x59A770,0x5505B0,0x54BE10,0x59A780,0x550A30,0x550C30,0x596820,0x596B50,0x54A290,0x598FD0,0x54AA50,0x54A970,
    0x266BE0,0x599960,0x59BCD0,0x54A330,0x599650,0x54AA30,0x5985C0,0x5727B0,0x572770,0x54AAC0,0x54ABD0,0x266BB0,
    0x598E10,0x266970,0x54A320,0x266BF0,0x598390,0x571540,0x598520,0x54A2E0,0x550A10,0x550890,0x5A3600,0x5A3630,
    0x54C800,0x54CAA0,0x54C900,0x59C3D0,0x59D2B0,0x577020,0x54CBD0,0x54CB60,0x575CD0,0x550A40,0x5508E0,0x598FB0,
    0x597E80,0x598F00,0x550680,0x550720,0x59FDB0,0x592180,0x595F50,0x596130,0x595F40,0x59C2E0,0x59C3B0,0x59BCF0,
    0x5998E0,0x599710,0x551020,0x570D80,0x570D60,0x57B250,0x57BCA0,0x579970,0x552E20,0x57A9B0,0x57A430,0x57A6E0,
    0x5790A0,0x5A1920,0x5A1590,0x57AC60,0x552EA0,0x5A1540,0x578FB0,0x5A1660,0x578850,0x578790,0x599A30,0x550A20,
    0x550C20,0x5A1BA0,0x59A880,0x598F80,0x5508D0,0x590710,0x590280,0x58FF70,0x59FB10,0x59CE80,
};
constexpr std::uint32_t kActor1Slots[]={
    0x59A770,0x57E680,0x54BE10,0x59A780,0x580520,0x5808B0,0x5803D0,0x580460,0x54A290,0x598FD0,0x54AA50,0x54A970,
    0x266BE0,0x599960,0x59BCD0,0x54A330,0x599650,0x54AA30,0x5985C0,0x5727B0,0x572770,0x54AAC0,0x54ABD0,0x266BB0,
    0x598E10,0x266970,0x54A320,0x266BF0,0x598390,0x571540,0x598520,0x5804B0,0x550A10,0x550890,0x5A3600,0x5A3630,
    0x54C800,0x54CAA0,0x54C900,0x59C3D0,0x59D2B0,0x577020,0x54CBD0,0x54CB60,0x575CD0,0x580530,0x598280,0x598FB0,
    0x597E80,0x598F00,0x550680,0x550720,0x582E40,0x57FCC0,0x5802A0,0x5803C0,0x595F40,0x59C2E0,0x59C3B0,0x59BCF0,
    0x5998E0,0x599710,0x5817A0,0x570D80,0x570D60,0x57B250,0x57BCA0,0x579970,0x57A1C0,0x584310,0x584290,0x57A6E0,
    0x5790A0,0x584390,0x583240,0x57AC60,0x57B7A0,0x5A1540,0x578FB0,0x5A1660,0x578850,0x578790,0x5806B0,0x580510,
    0x550C20,0x584400,0x59A880,0x598F80,0x5508D0,0x57EE70,0x57EE20,0x57EDD0,0x582DF0,0x59CE80,
};
constexpr std::uint32_t kActor2Slots[]={
    0x59A770,0x5661B0,0x54BE10,0x59A780,0x572DF0,0x567780,0x596820,0x567080,0x54A290,0x567250,0x54AA50,0x54A970,
    0x266BE0,0x599960,0x59BCD0,0x54A330,0x599650,0x54AA30,0x5671B0,0x5727B0,0x572770,0x54AAC0,0x54ABD0,0x266BB0,
    0x598E10,0x266970,0x54A320,0x266BF0,0x598390,0x571540,0x598520,0x54A2E0,0x550A10,0x550890,0x5A3600,0x5A3630,
    0x54C800,0x54CAA0,0x54C900,0x59C3D0,0x59D2B0,0x577020,0x54CBD0,0x54CB60,0x575CD0,0x574D80,0x598280,0x598FB0,
    0x597E80,0x598F00,0x550680,0x550720,0x59FDB0,0x592180,0x595F50,0x596130,0x595F40,0x59C2E0,0x59C3B0,0x59BCF0,
    0x567680,0x567640,0x567DB0,0x570D80,0x570D60,0x57B250,0x57BCA0,0x579970,0x57A1C0,0x57A9B0,0x57A430,0x57A6E0,
    0x5790A0,0x5A1920,0x5692A0,0x57AC60,0x57B7A0,0x5A1540,0x578FB0,0x5A1660,0x578850,0x578790,0x567690,0x567240,
    0x550C20,0x5A1BA0,0x59A880,0x598F80,0x566F90,0x590710,0x590280,0x566290,0x59FB10,0x59CE80,
};
constexpr std::uint32_t kWeapon0Slots[]={
    0x118A620,0x69A850,0x1256E0,0x69AB30,0x693280,0x69ABB0,0x125270,0x125280,0x125690,0x22F700,0x1256C0,0x22F710,
    0x681D30,0x6819E0,0x6874B0,0x6874C0,0x69AA10,0x69A8F0,0x681D20,0x693470,0x6874D0,0x681CB0,0x691FA0,0x69AAA0,
    0x681CC0,0x6819D0,0x6922A0,0x692100,0x682270,0x6822F0,0x6922F0,0x681C90,0x681950,0x681940,0x682250,0x682260,
    0x695240,0x681960,0x681970,0x6874E0,0x6874F0,
};
constexpr std::uint32_t kWeapon1Slots[]={
    0x118A620,0x69E010,0x1256E0,0x69AB70,0x693280,0x69E100,0x125270,0x125280,0x125690,0x22F700,0x1256C0,0x22F710,
    0x681D30,0x6819E0,0x6874B0,0x6874C0,0x69E050,0x681A10,0x681D20,0x693470,0x6874D0,0x681CB0,0x691FA0,0x69AAA0,
    0x681CC0,0x6819D0,0x6922A0,0x692100,0x682270,0x6822F0,0x69E080,0x681C90,0x681950,0x681940,0x682250,0x69E1E0,
    0x695240,0x681960,0x681970,0x6874E0,0x6874F0,
};
constexpr std::uint32_t kWeapon2Slots[]={
    0x118A620,0x6AF8C0,0x1256E0,0x6AFAA0,0x693280,0x6AFAB0,0x125270,0x125280,0x125690,0x22F700,0x1256C0,0x22F710,
    0x681D30,0x6819E0,0x6AF920,0x6AF970,0x6AF980,0x681A10,0x681D20,0x693470,0x6874D0,0x681CB0,0x691FA0,0x6AFA00,
    0x681CC0,0x6819D0,0x6AF9B0,0x692100,0x682270,0x6822F0,0x6922F0,0x681C90,0x681950,0x681940,0x682250,0x682260,
    0x695240,0x681960,0x681970,0x6874E0,0x6874F0,
};
constexpr std::uint32_t kWeapon3Slots[]={
    0x118A620,0x6AA710,0x1256E0,0x6AAE10,0x693280,0x6AAE20,0x125270,0x125280,0x125690,0x22F700,0x1256C0,0x22F710,
    0x681D30,0x6819E0,0x6AAAA0,0x6AAB10,0x6AAB20,0x681A10,0x6AADF0,0x6AADE0,0x6874D0,0x681CB0,0x691FA0,0x6AACD0,
    0x6AAD60,0x6AAAC0,0x6AAB90,0x692100,0x682270,0x6822F0,0x6AABE0,0x681C90,0x681950,0x681940,0x682250,0x682260,
    0x695240,0x681960,0x681970,0x6874E0,0x6874F0,
};
constexpr std::uint32_t kShieldBulletSlots[]={
    0x118A620,0x2A51E0,0x1256E0,0x2A55E0,0x1256D0,0x2A5600,0x125270,0x125280,0x125690,0x22F700,0x1256C0,0x22F710,
    0x22F390,0x22F0B0,
};
struct Type {const char* name;unsigned table;const std::uint32_t* slots=nullptr;unsigned count=0;bool actor=false;};
constexpr Type kActors[]={{"AssultSoldier",0x17CDF28,kActor0Slots,94,true},{"PaleWing",0x17D0FF8,kActor1Slots,94,true},{"HeavyArmor",0x17CF5B8,kActor2Slots,94,true}};
constexpr Type kWeapons[]={{"Weapon_BasicShoot",0x17E36E0,kWeapon0Slots,41},{"Weapon_HeavyShoot",0x17E3F18,kWeapon1Slots,41},{"Weapon_Swing",0x17E5950,kWeapon2Slots,41},{"Weapon_Shield",0x17E51A0,kWeapon3Slots,41}};
constexpr Type kBasic=kWeapons[0],kShieldBullet={"ShieldBashBullet01",0x17A5658,kShieldBulletSlots,14};
// Complete supported-PE primary WeaponBase census for the neutral existing-emitter API only.
constexpr std::uint32_t kShared0Slots[]={
    0x118A620,0x68F7D0,0x1256E0,0x6934C0,0x693280,0x12DA74A,0x125270,0x125280,0x125690,0x22F700,0x1256C0,0x22F710,
    0x681D30,0x6819E0,0x6874B0,0x6874C0,0x681A50,0x681A10,0x681D20,0x693470,0x6874D0,0x681CB0,0x691FA0,0x681CA0,
    0x681CC0,0x6819D0,0x6922A0,0x692100,0x682270,0x6822F0,0x6922F0,0x681C90,0x681950,0x681940,0x682250,0x682260,
    0x695240,0x681960,0x681970,0x6874E0,0x6874F0,
};
constexpr std::uint32_t kShared1Slots[]={
    0x118A620,0x6992F0,0x1256E0,0x6934C0,0x693280,0x699610,0x125270,0x125280,0x125690,0x22F700,0x1256C0,0x22F710,
    0x681D30,0x6819E0,0x6874B0,0x6874C0,0x681A50,0x681A10,0x681D20,0x693470,0x6874D0,0x681CB0,0x691FA0,0x681CA0,
    0x681CC0,0x6819D0,0x6922A0,0x692100,0x682270,0x6822F0,0x6922F0,0x681C90,0x681950,0x681940,0x682250,0x682260,
    0x695240,0x681960,0x681970,0x6874E0,0x6874F0,
};
constexpr std::uint32_t kShared2Slots[]={
    0x118A620,0x69A810,0x1256E0,0x69AB30,0x693280,0x69ABB0,0x125270,0x125280,0x125690,0x22F700,0x1256C0,0x22F710,
    0x681D30,0x6819E0,0x6874B0,0x6874C0,0x69AA10,0x69A8F0,0x681D20,0x693470,0x6874D0,0x681CB0,0x691FA0,0x69AAA0,
    0x681CC0,0x6819D0,0x6922A0,0x692100,0x682270,0x6822F0,0x6922F0,0x681C90,0x681950,0x681940,0x682250,0x682260,
    0x695240,0x681960,0x681970,0x6874E0,0x6874F0,
};
constexpr std::uint32_t kShared4Slots[]={
    0x118A620,0x69A890,0x1256E0,0x69AB70,0x693280,0x69AD20,0x125270,0x125280,0x125690,0x22F700,0x1256C0,0x22F710,
    0x681D30,0x6819E0,0x6874B0,0x6874C0,0x69AA10,0x681A10,0x681D20,0x693470,0x6874D0,0x681CB0,0x691FA0,0x69AAA0,
    0x681CC0,0x6819D0,0x6922A0,0x692100,0x682270,0x6822F0,0x6922F0,0x681C90,0x681950,0x681940,0x682250,0x682260,
    0x695240,0x681960,0x681970,0x6874E0,0x6874F0,
};
constexpr std::uint32_t kShared5Slots[]={
    0x118A620,0x69BFE0,0x1256E0,0x69C430,0x693280,0x69C440,0x125270,0x125280,0x125690,0x22F700,0x1256C0,0x22F710,
    0x681D30,0x6819E0,0x69C210,0x69C240,0x69C260,0x681A10,0x681D20,0x693470,0x6874D0,0x681CB0,0x691FA0,0x69C3A0,
    0x681CC0,0x6819D0,0x69C2E0,0x692100,0x69C660,0x69C6E0,0x6922F0,0x69C330,0x69C0B0,0x69C090,0x69C4D0,0x69C510,
    0x695240,0x681960,0x681970,0x6874E0,0x6874F0,
};
constexpr std::uint32_t kShared6Slots[]={
    0x118A620,0x681850,0x1256E0,0x681D40,0x693280,0x681E50,0x125270,0x125280,0x125690,0x22F700,0x1256C0,0x22F710,
    0x681D30,0x6819E0,0x681980,0x681A20,0x681A50,0x681A10,0x681D20,0x693470,0x681CD0,0x681CB0,0x691FA0,0x681CA0,
    0x681CC0,0x6819D0,0x6922A0,0x681A60,0x682270,0x6822F0,0x681B30,0x681C90,0x681950,0x681940,0x682250,0x682260,
    0x695240,0x681960,0x681970,0x682130,0x682150,
};
constexpr std::uint32_t kShared7Slots[]={
    0x118A620,0x683AE0,0x1256E0,0x684130,0x693280,0x684150,0x125270,0x125280,0x125690,0x22F700,0x1256C0,0x22F710,
    0x681D30,0x6819E0,0x683BD0,0x683C40,0x683C70,0x681A10,0x681D20,0x693470,0x6840F0,0x681CB0,0x691FA0,0x681CA0,
    0x681CC0,0x6819D0,0x6922A0,0x683E90,0x682270,0x6822F0,0x683F60,0x681C90,0x681950,0x681940,0x682250,0x682260,
    0x695240,0x681960,0x681970,0x684560,0x6845B0,
};
constexpr std::uint32_t kShared8Slots[]={
    0x118A620,0x685580,0x1256E0,0x685A30,0x693280,0x685A40,0x125270,0x125280,0x125690,0x22F700,0x1256C0,0x22F710,
    0x681D30,0x6819E0,0x685730,0x685960,0x69AA10,0x6857B0,0x681D20,0x693470,0x6859F0,0x681CB0,0x691FA0,0x69AAA0,
    0x681CC0,0x6819D0,0x6922A0,0x685970,0x682270,0x6822F0,0x6922F0,0x681C90,0x681950,0x681940,0x682250,0x682260,
    0x695240,0x681960,0x681970,0x685E30,0x685E50,
};
constexpr std::uint32_t kShared9Slots[]={
    0x118A620,0x687470,0x1256E0,0x6934C0,0x693280,0x12DA74A,0x125270,0x125280,0x125690,0x22F700,0x1256C0,0x22F710,
    0x681D30,0x6819E0,0x6874B0,0x6874C0,0x681A50,0x681A10,0x681D20,0x693470,0x6874D0,0x681CB0,0x691FA0,0x681CA0,
    0x681CC0,0x6819D0,0x6922A0,0x692100,0x682270,0x6822F0,0x6922F0,0x681C90,0x681950,0x681940,0x682250,0x682260,
    0x695240,0x681960,0x681970,0x6874E0,0x6874F0,
};
constexpr std::uint32_t kShared10Slots[]={
    0x118A620,0x69D270,0x1256E0,0x69D4F0,0x693280,0x69D520,0x125270,0x125280,0x125690,0x22F700,0x1256C0,0x22F710,
    0x681D30,0x6819E0,0x6874B0,0x6874C0,0x69D320,0x681A10,0x681D20,0x693470,0x6874D0,0x681CB0,0x691FA0,0x681CA0,
    0x681CC0,0x6819D0,0x6922A0,0x692100,0x682270,0x6822F0,0x69D3C0,0x681C90,0x681950,0x681940,0x682250,0x69D8D0,
    0x695240,0x681960,0x681970,0x6874E0,0x6874F0,
};
constexpr std::uint32_t kShared12Slots[]={
    0x118A620,0x69E740,0x1256E0,0x69E910,0x693280,0x69E920,0x125270,0x125280,0x125690,0x22F700,0x1256C0,0x22F710,
    0x681D30,0x6819E0,0x6874B0,0x6874C0,0x69E7C0,0x681A10,0x681D20,0x693470,0x6874D0,0x681CB0,0x691FA0,0x69E880,
    0x681CC0,0x6819D0,0x69E840,0x692100,0x682270,0x6822F0,0x6922F0,0x681C90,0x681950,0x681940,0x682250,0x682260,
    0x695240,0x681960,0x681970,0x6874E0,0x6874F0,
};
constexpr std::uint32_t kShared13Slots[]={
    0x118A620,0x69FC10,0x1256E0,0x6A0000,0x693280,0x6A0010,0x125270,0x125280,0x125690,0x22F700,0x1256C0,0x22F710,
    0x681D30,0x6819E0,0x69FD30,0x69FD80,0x69FDB0,0x681A10,0x681D20,0x693470,0x6874D0,0x69FFE0,0x691FA0,0x69FF00,
    0x681CC0,0x6819D0,0x69FDD0,0x692100,0x682270,0x6822F0,0x69FE20,0x681C90,0x681950,0x681940,0x682250,0x682260,
    0x695240,0x69FCB0,0x69FCE0,0x6874E0,0x6874F0,
};
constexpr std::uint32_t kShared14Slots[]={
    0x118A620,0x6A2380,0x1256E0,0x6A3BE0,0x693280,0x6A3C30,0x125270,0x125280,0x125690,0x22F700,0x1256C0,0x22F710,
    0x681D30,0x6819E0,0x6874B0,0x6874C0,0x681A50,0x681A10,0x681D20,0x693470,0x6874D0,0x681CB0,0x691FA0,0x681CA0,
    0x681CC0,0x6819D0,0x6A2FE0,0x692100,0x682270,0x6822F0,0x6A3110,0x681C90,0x681950,0x681940,0x682250,0x682260,
    0x695240,0x681960,0x681970,0x6874E0,0x6874F0,
};
constexpr std::uint32_t kShared15Slots[]={
    0x118A620,0x6A23D0,0x1256E0,0x6A3C00,0x693280,0x6A3D20,0x125270,0x125280,0x125690,0x22F700,0x1256C0,0x22F710,
    0x681D30,0x6819E0,0x6874B0,0x6874C0,0x681A50,0x681A10,0x681D20,0x693470,0x6874D0,0x681CB0,0x691FA0,0x681CA0,
    0x681CC0,0x6819D0,0x6A2FF0,0x692100,0x682270,0x6822F0,0x6A3190,0x681C90,0x681950,0x681940,0x682250,0x682260,
    0x695240,0x681960,0x681970,0x6874E0,0x6874F0,
};
constexpr std::uint32_t kShared16Slots[]={
    0x118A620,0x6A6B00,0x1256E0,0x69AB70,0x693280,0x69AD20,0x125270,0x125280,0x125690,0x22F700,0x1256C0,0x22F710,
    0x681D30,0x6819E0,0x6874B0,0x6874C0,0x69AA10,0x681A10,0x681D20,0x6A6B40,0x6874D0,0x681CB0,0x691FA0,0x69AAA0,
    0x681CC0,0x6819D0,0x6922A0,0x692100,0x682270,0x6822F0,0x6922F0,0x681C90,0x681950,0x681940,0x682250,0x682260,
    0x695240,0x681960,0x681970,0x6874E0,0x6874F0,
};
constexpr std::uint32_t kShared17Slots[]={
    0x118A620,0x6A6ED0,0x1256E0,0x6A7070,0x693280,0x6A7080,0x125270,0x125280,0x125690,0x22F700,0x1256C0,0x22F710,
    0x681D30,0x6819E0,0x6A6F30,0x6A6F70,0x6A6F80,0x681A10,0x681D20,0x693470,0x6874D0,0x681CB0,0x691FA0,0x6A7000,
    0x681CC0,0x6819D0,0x6A6FB0,0x692100,0x682270,0x6822F0,0x6922F0,0x681C90,0x681950,0x681940,0x682250,0x682260,
    0x695240,0x681960,0x681970,0x6874E0,0x6874F0,
};
constexpr std::uint32_t kShared18Slots[]={
    0x118A620,0x6A77A0,0x1256E0,0x69AB70,0x693280,0x6A78A0,0x125270,0x125280,0x125690,0x22F700,0x1256C0,0x22F710,
    0x681D30,0x6819E0,0x6874B0,0x6874C0,0x69AA10,0x681A10,0x681D20,0x693470,0x6874D0,0x681CB0,0x691FA0,0x69AAA0,
    0x681CC0,0x6819D0,0x6922A0,0x692100,0x6A7A10,0x6A7A30,0x6922F0,0x6A7830,0x6A7800,0x6A77F0,0x6A7910,0x6A7920,
    0x695240,0x681960,0x681970,0x6874E0,0x6874F0,
};
constexpr std::uint32_t kShared19Slots[]={
    0x118A620,0x6A83A0,0x1256E0,0x6A8740,0x693280,0x6A8760,0x125270,0x125280,0x125690,0x22F700,0x1256C0,0x22F710,
    0x681D30,0x6819E0,0x6A8540,0x6A8570,0x681A50,0x681A10,0x681D20,0x693470,0x6874D0,0x681CB0,0x691FA0,0x681CA0,
    0x681CC0,0x6819D0,0x6A85C0,0x692100,0x682270,0x6822F0,0x6A8610,0x681C90,0x681950,0x681940,0x682250,0x682260,
    0x695240,0x681960,0x681970,0x6874E0,0x6874F0,
};
constexpr std::uint32_t kShared21Slots[]={
    0x118A620,0x6ACDA0,0x1256E0,0x6ADC20,0x693280,0x6ADC70,0x125270,0x125280,0x125690,0x22F700,0x1256C0,0x22F710,
    0x6ADBF0,0x6819E0,0x6AD330,0x6AD600,0x6AD610,0x6AD370,0x681D20,0x6ADB20,0x6874D0,0x681CB0,0x691FA0,0x6ADAC0,
    0x681CC0,0x6819D0,0x6922A0,0x692100,0x682270,0x6822F0,0x6ADA40,0x681C90,0x681950,0x681940,0x682250,0x682260,
    0x695240,0x681960,0x681970,0x6874E0,0x6874F0,
};
constexpr std::uint32_t kShared22Slots[]={
    0x118A620,0x6AEA30,0x1256E0,0x6ADC20,0x693280,0x6AED40,0x125270,0x125280,0x125690,0x22F700,0x1256C0,0x22F710,
    0x6ADBF0,0x6819E0,0x6AD330,0x6AD600,0x6AEAC0,0x6AD370,0x681D20,0x6AEC80,0x6874D0,0x681CB0,0x691FA0,0x6ADAC0,
    0x681CC0,0x6819D0,0x6922A0,0x6AEBA0,0x682270,0x6822F0,0x6AEC00,0x681C90,0x681950,0x681940,0x682250,0x682260,
    0x695240,0x681960,0x681970,0x6AED90,0x6AEE70,
};
constexpr std::uint32_t kShared24Slots[]={
    0x118A620,0x6B01C0,0x1256E0,0x6B0360,0x693280,0x6B0370,0x125270,0x125280,0x125690,0x22F700,0x1256C0,0x22F710,
    0x681D30,0x6819E0,0x6B0220,0x6B0260,0x681A50,0x681A10,0x681D20,0x693470,0x6874D0,0x681CB0,0x691FA0,0x6B0300,
    0x681CC0,0x6819D0,0x6B0270,0x692100,0x6B03E0,0x6822F0,0x6922F0,0x6B02C0,0x681950,0x681940,0x682250,0x682260,
    0x695240,0x681960,0x681970,0x6874E0,0x6874F0,
};
constexpr std::uint32_t kShared25Slots[]={
    0x118A620,0x6B1100,0x1256E0,0x6B2630,0x693280,0x6B2750,0x125270,0x125280,0x125690,0x22F700,0x1256C0,0x22F710,
    0x681D30,0x6819E0,0x6874B0,0x6874C0,0x6B2390,0x681A10,0x681D20,0x693470,0x6874D0,0x681CB0,0x691FA0,0x681CA0,
    0x681CC0,0x6819D0,0x6922A0,0x692100,0x6B2AB0,0x6822F0,0x6B2470,0x6B2580,0x681950,0x681940,0x682250,0x682260,
    0x695240,0x681960,0x681970,0x6874E0,0x6874F0,
};
constexpr std::uint32_t kShared26Slots[]={
    0x118A620,0x6B31F0,0x1256E0,0x6B3230,0x693280,0x6B3240,0x125270,0x125280,0x125690,0x22F700,0x1256C0,0x22F710,
    0x681D30,0x6819E0,0x6874B0,0x6874C0,0x6B3640,0x681A10,0x681D20,0x693470,0x6874D0,0x681CB0,0x691FA0,0x681CA0,
    0x681CC0,0x6819D0,0x6922A0,0x692100,0x682270,0x6822F0,0x6922F0,0x681C90,0x681950,0x681940,0x682250,0x682260,
    0x695240,0x681960,0x681970,0x6874E0,0x6874F0,
};
constexpr std::uint32_t kShared27Slots[]={
    0x118A620,0x6B35C0,0x1256E0,0x6B3660,0x693280,0x6B3850,0x125270,0x125280,0x125690,0x22F700,0x1256C0,0x22F710,
    0x681D30,0x6819E0,0x6874B0,0x6874C0,0x6B3640,0x681A10,0x681D20,0x693470,0x6874D0,0x681CB0,0x691FA0,0x681CA0,
    0x681CC0,0x6819D0,0x6922A0,0x692100,0x682270,0x6822F0,0x6922F0,0x681C90,0x681950,0x681940,0x682250,0x682260,
    0x695240,0x681960,0x681970,0x6874E0,0x6874F0,
};
constexpr std::uint32_t kShared28Slots[]={
    0x118A620,0x6B3F70,0x1256E0,0x6B3660,0x693280,0x6B3850,0x125270,0x125280,0x125690,0x22F700,0x1256C0,0x22F710,
    0x681D30,0x6819E0,0x6874B0,0x6874C0,0x6B3640,0x681A10,0x681D20,0x693470,0x6874D0,0x681CB0,0x691FA0,0x681CA0,
    0x681CC0,0x6819D0,0x6922A0,0x692100,0x682270,0x6822F0,0x6922F0,0x681C90,0x681950,0x681940,0x682250,0x682260,
    0x695240,0x681960,0x681970,0x6874E0,0x6874F0,
};
constexpr Type kSharedWeapons[]={
    {"WeaponBase",0x17E25E8,kShared0Slots,41},
    {"Weapon_Accessory",0x17E3328,kShared1Slots,41},
    {"Weapon_BasicSemiAuto",0x17E3830,kShared2Slots,41},
    {"Weapon_BasicShoot",0x17E36E0,kWeapon0Slots,41},
    {"Weapon_BasicShoot_Base",0x17E3590,kShared4Slots,41},
    {"Weapon_ChargeShoot",0x17E3AF0,kShared5Slots,41},
    {"Weapon_Drone_Area",0x17E1CB0,kShared6Slots,41},
    {"Weapon_Drone_LaserMarker",0x17E1F00,kShared7Slots,41},
    {"Weapon_Drone_MarkerShoot",0x17E2130,kShared8Slots,41},
    {"Weapon_Drone_Switch",0x17E22B8,kShared9Slots,41},
    {"Weapon_Gatling",0x17E3D50,kShared10Slots,41},
    {"Weapon_HeavyShoot",0x17E3F18,kWeapon1Slots,41},
    {"Weapon_HomingShoot",0x17E40A0,kShared12Slots,41},
    {"Weapon_ImpactHammer",0x17E42A0,kShared13Slots,41},
    {"Weapon_LaserMarker",0x17E45E8,kShared14Slots,41},
    {"Weapon_LaserMarkerCallFire",0x17E4750,kShared15Slots,41},
    {"Weapon_MarkerShooter",0x17E4908,kShared16Slots,41},
    {"Weapon_PileBanker",0x17E4A80,kShared17Slots,41},
    {"Weapon_PreChargeShoot",0x17E4DC8,kShared18Slots,41},
    {"Weapon_RadioContact",0x17E4FB0,kShared19Slots,41},
    {"Weapon_Shield",0x17E51A0,kWeapon3Slots,41},
    {"Weapon_Sub",0x17E5440,kShared21Slots,41},
    {"Weapon_SubDrone",0x17E57D8,kShared22Slots,41},
    {"Weapon_Swing",0x17E5950,kWeapon2Slots,41},
    {"Weapon_Throw",0x17E5B60,kShared24Slots,41},
    {"Weapon_VehicleMaser",0x17E5E40,kShared25Slots,41},
    {"Weapon_VehicleRailGun",0x17E5FA8,kShared26Slots,41},
    {"Weapon_VehicleShoot",0x17E6120,kShared27Slots,41},
    {"Weapon_VehicleSwingShoot",0x17E6320,kShared28Slots,41}
};
constexpr unsigned kWeaponLimit=2,kHeldLimit=15,kWalkLimit=512;
struct Context {unsigned actor,weapons,held;};
constexpr auto Contexts() noexcept {
    std::array<Context,3*kWeaponLimit*kHeldLimit> result{};unsigned n=0;
    for(unsigned a=0;a<3;++a)for(unsigned w=1;w<=kWeaponLimit;++w)for(unsigned h=1;h<=kHeldLimit;++h)result[n++]={a,w,h};
    return result;
}
constexpr auto kContexts=Contexts();
struct Anchor {unsigned rva,size;unsigned char bytes[16];};
constexpr Anchor kAnchors[]={
    {0x68D588,4,{0x49,0x8B,0x45,0x30}},
    {0x68D594,4,{0x48,0x8B,0x48,0x30}},
    {0x68D59D,4,{0x48,0x8B,0x40,0x28}},
    {0x68D5AD,7,{0x48,0x8D,0x8E,0x38,0x08,0,0}},
    {0x69E073,5,{0xE9,0x98,0xC9,0xFF,0xFF}},
    {0x69AA18,3,{0x45,0x33,0xC0}},
    {0x69AA27,5,{0xE8,0x94,0x62,0xFF,0xFF}},
    {0x6AF98B,7,{0x48,0x8B,0x81,0x38,0x16,0,0}},
    {0x6AF992,8,{0x48,0x83,0xC0,0x10,0x49,0xC1,0xE0,0x06}},
    {0x6AF9A8,5,{0xE9,0x13,0x13,0xFE,0xFF}},
    {0x6AF4E6,7,{0x44,0x88,0xA5,0xB8,0,0,0}},
    {0x6AAB28,3,{0x45,0x33,0xC0}},
    {0x6AAB3C,5,{0xE8,0x7F,0x61,0xFE,0xFF}},
    {0x690D54,5,{0xE8,0x77,0x62,0,0}},
    {0x2A4C13,8,{0x45,0x33,0xC0,0xE8,0xA5,0x9D,0xF8,0xFF}},
    {0x22EA31,5,{0x40,0x84,0xFF,0x74,0x29}},
    {0x2A4C34,7,{0x49,0x8D,0xBE,0x90,0x0E,0,0}},
    {0x2A4D8B,12,{0x48,0x8D,0x56,0x30,0x48,0x8B,0xCF,0xE8,0x59,0xBA,0xF8,0xFF}},
    {0x2308EB,3,{0x8B,0x47,0x60}},
    {0x2308F4,3,{0x89,0x43,0x60}},
    {0x2A5656,6,{0x8B,0x93,0xF0,0x0E,0,0}},
    {0x2A5663,5,{0xE8,0xF8,0x21,0xF9,0xFF}},
    {0x5A106A,7,{0x48,0x8B,0x99,0x50,0x19,0,0}},
    {0x5A1074,7,{0x48,0x8B,0x81,0x60,0x19,0,0}},
    {0x58F602,12,{0x8B,0x86,0x14,0x03,0,0,0x89,0x83,0x14,0x02,0,0}},
    {0x68AD54,11,{0x48,0x8B,0x43,0x30,0x48,0x89,0x86,0x20,0x01,0,0}},
    {0x68AD8D,12,{0x48,0x8B,0xD6,0x48,0x8B,0x4B,0x30,0xE8,0x87,0x01,0xB0,0}},
    {0x68CDDF,6,{0x89,0x86,0x68,0x03,0,0}},
    {0x697083,7,{0x41,0x8B,0x87,0x68,0x03,0,0}},
    {0x6976CF,15,{0x41,0x8B,0x87,0x14,0x02,0,0,0x41,0x89,0x84,0x24,0x90,0,0,0}},
    {0x69749F,13,{0x0F,0x84,0xFD,0,0,0,0x49,0x8B,0xBF,0x20,0x0F,0,0}},
    {0x6974F8,7,{0x83,0xB9,0x30,0x0C,0,0,1}},
    {0x697501,11,{0x8B,0x81,0x34,0x0C,0,0,0xC1,0xE8,0x0E,0x24,1}},
    {0x69750E,6,{0x48,0x8B,0x01,0xFF,0x50,0x60}},
    {0x69759D,5,{0xE9,0x4F,0x09,0,0}},
    {0x697B1A,7,{0x4D,0x8D,0xB7,0x20,0x0F,0,0}},
    {0x697BE1,8,{0x8D,0x48,0x20,0xE8,0xC7,0x09,0xC4,0}},
    {0x697BE9,8,{0x48,0x8B,0x4D,0x30,0x48,0x89,0x48,0x10}},
    {0x697C01,4,{0x49,0xFF,0x46,0x08}},
    {0x697EB4,11,{0xFF,0xC7,0x89,0x7C,0x24,0x44,0x3B,0x7D,0x88,0x74,0x10}},
    {0x690F4D,9,{0x83,0xB8,0x30,0x0C,0,0,1,0x74,0x4D}},
    {0x6938E3,7,{0x48,0xFF,0x8E,0x28,0x0F,0,0}},
    {0x232015,6,{0x8B,0x83,0,0x0A,0,0}},
    {0x232023,6,{0x89,0x83,0x54,0x07,0,0}},
};
bool Ready() noexcept {
    if(!image)return false;
    for(const auto& a:kAnchors)if(!Readable(image+a.rva,a.size) || std::memcmp(image+a.rva,a.bytes,a.size))return false;
    return true;
}
bool Same(const ObjRef& a,const ObjRef& b) noexcept{return a.obj==b.obj && a.ctrl==b.ctrl;}
bool Team(int v) noexcept{return v>=-1 && v<7;}
Life Identity(const ObjRef& r) noexcept {
    if(!r.obj || !r.ctrl || !Readable(r.ctrl,16))return Life::invalid;
    if(At<LONG>(r.ctrl,8)<=0)return Life::gone;
    if(!Readable(r.obj,kSelfCtrl+8))return Life::invalid;
    if(At<const void*>(r.obj,kSelfCtrl)!=r.ctrl || (At<unsigned char>(r.obj,0x18)&4u))return Life::gone;
    return At<const void*>(r.obj,kSelf)==r.obj ? Life::live : Life::invalid;
}
bool TypeReady(const Type& type) noexcept {
    if(!image || !Readable(image+type.table-8,(static_cast<std::size_t>(type.count)+1)*8))return false;
    for(unsigned i=0;i<type.count;++i) {
        const void* actual=At<const void*>(image+type.table,i*8);
        if(actual!=image+type.slots[i] && (!type.actor || i!=7 || !NpcOwnsNativeThink(image+type.table,actual)))return false;
    }
    const auto* col=At<const unsigned char*>(image+type.table-8,0);
    if(!Readable(col,24) || At<unsigned>(col,0)!=1 || At<unsigned>(col,4)!=0 || At<unsigned>(col,8)!=0 || image+At<unsigned>(col,20)!=col)return false;
    const auto* name=image+At<unsigned>(col,12)+16;const auto n=std::strlen(type.name);
    return Readable(name,n+7) && !std::memcmp(name,".?AV",4) && !std::memcmp(name+4,type.name,n) && !std::memcmp(name+4+n,"@@",3);
}
bool TypeIs(const void* object,const Type& type) noexcept {
    return Readable(object,8) && At<const void*>(object,0)==image+type.table && TypeReady(type);
}
int WeaponKind(const void* object) noexcept {for(unsigned i=0;i<4;++i)if(TypeIs(object,kWeapons[i]))return static_cast<int>(i);return -1;}
int SharedWeaponKind(const void* object) noexcept {for(unsigned i=0;i<sizeof(kSharedWeapons)/sizeof(kSharedWeapons[0]);++i)if(TypeIs(object,kSharedWeapons[i]))return static_cast<int>(i);return -1;}
bool SwingCount(const void* object) noexcept {
    if(!Readable(object,0x1654))return false;
    const auto* rows=At<const unsigned char*>(object,0x1638);const auto count=At<std::size_t>(object,0x1648),cap=At<std::size_t>(object,0x1640);
    const int selected=At<int>(object,0x1650);
    if(!count || count>kWalkLimit || count>cap || !Readable(rows,count*0xC0) || selected < -1 || (selected>=0 && static_cast<std::size_t>(selected)>=count))return false;
    // slot16 passes each combo+10 as FireOptions; native ctor clears its +18 override flag.
    for(std::size_t i=0;i<count;++i)if(At<unsigned char>(rows+i*0xC0,0x28)!=0)return false;
    return true;
}
int BaseOf(const void* object,unsigned descriptor) noexcept {
    if(!Readable(object,8))return -1;
    const auto* table=At<const unsigned char*>(object,0);
    if(reinterpret_cast<std::uintptr_t>(table)<8 || !Readable(table-8,8))return -1;
    const auto* col=At<const unsigned char*>(table-8,0);
    if(!Readable(col,24) || At<unsigned>(col,0)!=1 || At<unsigned>(col,4)!=0 || At<unsigned>(col,8)!=0 || image+At<unsigned>(col,20)!=col)return -1;
    const auto* h=image+At<unsigned>(col,16);if(!Readable(h,16))return -1;
    const auto total=At<unsigned>(h,8);const auto* array=image+At<unsigned>(h,12);
    if(!total || total>128 || !Readable(array,total*4))return -1;
    for(unsigned i=0;i<total;++i) {
        const auto* base=image+At<unsigned>(array,i*4);if(!Readable(base,24))return -1;
        if(At<unsigned>(base,0)==descriptor && At<int>(base,8)==0 && At<int>(base,12)==-1)return 1;
    }
    return 0;
}
bool BulletType(const void* object) noexcept {return BaseOf(object,0x2022E80)==1;}
const Context* GetContext(void* p) noexcept {for(const auto& c:kContexts)if(p==&c)return &c;return nullptr;}
bool WeakOwner(const void* object,unsigned at,const ObjRef& owner) noexcept {
    return Readable(static_cast<const unsigned char*>(object)+at,16) && At<const void*>(object,at)==owner.obj && At<const void*>(object,at+8)==owner.ctrl;
}
Emitter Binding(const ObjRef& owner,const ObjRef& storage,unsigned at) noexcept {
    return {EmitterKind::ownerTeamCache,owner,static_cast<const unsigned char*>(storage.obj)+at,
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(storage.ctrl)),storage};
}
bool Append(Emitter* out,unsigned cap,unsigned& n,const Emitter& e) noexcept {
    for(unsigned i=0;i<n;++i)if(out[i].object==e.object)return false;
    if(n==cap)return false;out[n++]=e;return true;
}
bool SceneContains(const ObjRef& owner,const void* object) noexcept {
    const auto* entries=At<const unsigned char*>(owner.obj,0x48);
    const auto count=At<std::size_t>(owner.obj,0x58),cap=At<std::size_t>(owner.obj,0x50);
    if(count>4096 || count>cap || (count && !Readable(entries,count*8)))return false;
    unsigned found=0;for(std::size_t i=0;i<count;++i)if(At<const void*>(entries,i*8)==object)++found;
    return found==1;
}
bool EmptyGobs(const ObjRef& root) noexcept {
    if(!Readable(root.obj,0x560) || At<std::size_t>(root.obj,0x558)!=0)return false;
    const auto* head=At<const unsigned char*>(root.obj,0x550);
    return Readable(head,16) && At<const void*>(head,0)==head && At<const void*>(head,8)==head;
}
// 697B1A..697C1A: 0x20-byte nodes with the BulletBase weak at +10/+18.
// 690EF0 and 69749F..69759D keep the entire surviving mode-1 batch without creating another.
bool Held(const Context& c,const ObjRef& owner,const ObjRef& weapon,Emitter* out,unsigned cap,unsigned& n,bool neutral=false) noexcept {
    const auto* w=static_cast<const unsigned char*>(weapon.obj);const auto* head=At<const unsigned char*>(w,0xF20);
    const auto expected=At<std::size_t>(w,0xF28);if(expected>kWalkLimit || !Readable(head,16))return false;
    const auto* node=At<const unsigned char*>(head,0);const auto* prev=head;unsigned walked=0,live=0;
    while(node!=head) {
        if(walked>=expected || !Readable(node,0x20) || At<const void*>(node,8)!=prev)return false;
        const ObjRef bullet{At<const void*>(node,0x10),At<const void*>(node,0x18)};
        if(bullet.obj || bullet.ctrl) {
            if(!bullet.ctrl || !Readable(bullet.ctrl,16))return false;
            if(At<LONG>(bullet.ctrl,8)>0) {
                const auto life=Identity(bullet);if(life==Life::invalid)return false;
                if(life==Life::gone){++walked;prev=node;node=At<const unsigned char*>(node,0);continue;}
                if(!Readable(bullet.obj,0xC35))return false;
                const bool shield=TypeIs(bullet.obj,kShieldBullet);
                if(shield) {
                    // ShieldBash constructs no ordinary bullet core. Its filter uses a separate InitData at E90.
                    if((!neutral && WeaponKind(w)!=3) || !Readable(bullet.obj,0xEF4) || !WeakOwner(bullet.obj,0xE98,owner) || !Team(At<int>(bullet.obj,0xEF0)))return false;
                } else {
                    if((!neutral && WeaponKind(w)==3) || !BulletType(bullet.obj) || !WeakOwner(bullet.obj,0xAE8,owner) || !WeakOwner(bullet.obj,0x880,owner) ||
                       !Team(At<int>(bullet.obj,0xB40)) || !Team(At<int>(bullet.obj,0x894)))return false;
                }
                // F20 also retains ordinary FireType-1/2 projectiles. They keep their per-shot team snapshot.
                // 6974F8..697511 calls keepalive only for mode 1 with stop bit 14 clear.
                const bool keepalive=At<int>(bullet.obj,0xC30)==1 && !(At<unsigned>(bullet.obj,0xC34)&(1u<<14));
                if(keepalive) {
                    if(++live>c.held)return false;
                    if(shield) {
                        if(!Append(out,cap,n,Binding(owner,bullet,0xEF0)))return false;
                    } else {
                        for(unsigned at:{0xB40u,0x894u})if(!Append(out,cap,n,Binding(owner,bullet,at)))return false;
                    }
                }
            }
        }
        ++walked;prev=node;node=At<const unsigned char*>(node,0);
    }
    return walked==expected && At<const void*>(head,8)==prev;
}
bool Current(const Context& c,const ObjRef& root,Emitter* out,unsigned cap,unsigned& n,const Emitter* sought=nullptr,ObjRef* holder=nullptr) noexcept {
    if(Identity(root)!=Life::live || !TypeIs(root.obj,kActors[c.actor]) || !Readable(root.obj,0x1A18) || !EmptyGobs(root))return false;
    if(At<std::size_t>(root.obj,0x1A10)!=0)return false; // no unproved backpack/sub-weapon path
    const auto count=At<std::size_t>(root.obj,0x1960),reserved=At<std::size_t>(root.obj,0x1958);
    const auto* list=At<const unsigned char*>(root.obj,0x1950);
    if(count>c.weapons || count>reserved || (count && !Readable(list,count*8)))return false;
    for(std::size_t i=0;i<count;++i) {
        const auto* w=At<const unsigned char*>(list,i*8);if(!w || !Readable(w,0xF30))return false;
        const ObjRef weapon{w,At<const void*>(w,kSelfCtrl)};
        const int kind=WeaponKind(w);
        if(Identity(weapon)!=Life::live || kind<0 || (c.actor!=2 && kind!=0) || !WeakOwner(w,0x838,root) ||
           At<const void*>(w,0x120)!=root.obj || At<const void*>(w,0x38)!=root.obj || !SceneContains(root,w) || At<unsigned>(w,0x208)!=0 ||
           At<int>(w,0x368)<=0 || static_cast<unsigned>(At<int>(w,0x368))>c.held || (kind==2 && !SwingCount(w)))return false;
        for(std::size_t j=0;j<i;++j)if(At<const void*>(list,j*8)==w)return false;
        const unsigned before=n;
        if(!Team(At<int>(w,0x214)) || !Append(out,cap,n,Binding(root,weapon,0x214)) || !Held(c,root,weapon,out,cap,n))return false;
        if(sought && holder)for(unsigned j=before;j<n;++j)if(out[j].object==sought->object && Same(out[j].storage,sought->storage))*holder=weapon;
    }
    // Scene attachment and the Soldier weapon roster are separate native containers.
    // No live WeaponBase or GameObject child may silently escape the typed roster.
    const auto* children=At<const unsigned char*>(root.obj,0x48);const auto childCount=At<std::size_t>(root.obj,0x58),childCap=At<std::size_t>(root.obj,0x50);
    if(childCount>4096 || childCount>childCap || (childCount && !Readable(children,childCount*8)))return false;
    for(std::size_t i=0;i<childCount;++i) {
        const auto* object=At<const unsigned char*>(children,i*8);if(!object || !Readable(object,kSelfCtrl+8))return false;
        const ObjRef ref{object,At<const void*>(object,kSelfCtrl)};const auto life=Identity(ref);
        if(life==Life::invalid)return false;if(life==Life::gone)continue;
        bool listed=false;for(std::size_t j=0;j<count;++j)if(At<const void*>(list,j*8)==object)listed=true;
        if(listed)continue;
        const int weapon=BaseOf(object,0x2045768),gob=BaseOf(object,0x2006400),bullet=BaseOf(object,0x2022E80);
        if(weapon<0 || gob<0 || bullet<0 || weapon || gob || bullet)return false;
    }
    return true;
}
bool Collect(void* user,const ObjRef& root,ObjRef* members,unsigned mc,unsigned* count,Emitter* emitters,unsigned ec,unsigned* emitted) noexcept {
    if(count)*count=0;if(emitted)*emitted=0;
    if(!count || !emitted || !members || !mc || !emitters)return false;
    __try {
        const auto* c=GetContext(user);unsigned n=0;
        if(!c || !Ready() || !Current(*c,root,emitters,ec,n))return false;
        members[0]=root;*count=1;*emitted=n;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool Shape(const Emitter& e,bool neutral=false) noexcept {
    if(e.kind!=EmitterKind::ownerTeamCache || !e.member || !e.member.ctrl || !e.storage || !e.storage.ctrl || e.incarnation!=static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(e.storage.ctrl)))return false;
    const auto* storage=static_cast<const unsigned char*>(e.storage.obj);
    if(e.object==storage+0x214)return (neutral ? SharedWeaponKind(storage)>=0 : WeaponKind(storage)>=0) && Readable(storage,0xF30) && At<const void*>(storage,0x120)==e.member.obj && WeakOwner(storage,0x838,e.member);
    if(TypeIs(storage,kShieldBullet))return e.object==storage+0xEF0 && Readable(storage,0xEF4) && WeakOwner(storage,0xE98,e.member);
    return (e.object==storage+0xB40 || e.object==storage+0x894) && Readable(storage,0xC35) && BulletType(storage) &&
        WeakOwner(storage,0xAE8,e.member) && WeakOwner(storage,0x880,e.member);
}
Life ReadStorage(const Emitter& e,int* team,bool neutral=false) noexcept {
    if(!team || !Ready())return Life::invalid;
    const auto life=Identity(e.storage);if(life!=Life::live)return life;
    if(!Shape(e,neutral))return Life::invalid;
    const int value=At<int>(e.object,0);if(!Team(value))return Life::invalid;*team=value;return Life::live;
}
Life Read(void* user,const Emitter& e,int* team) noexcept {
    if(!team)return Life::invalid;
    __try {
        const auto life=ReadStorage(e,team);if(life!=Life::live)return life;
        int saved=-1;
        if(!controlled_pvp::ReadEmitterBaseline(e,&saved)) {
            const auto* c=GetContext(user);if(!c || Identity(e.member)!=Life::live)return Life::invalid;
            Emitter current[controlled_pvp::kMaxEmitters]{};unsigned count=0;
            if(!Current(*c,e.member,current,controlled_pvp::kMaxEmitters,count))return Life::invalid;
            bool found=false;for(unsigned i=0;i<count;++i)if(current[i].object==e.object && current[i].incarnation==e.incarnation && Same(current[i].storage,e.storage) && Same(current[i].member,e.member) && current[i].kind==e.kind)found=true;
            if(!found)return Life::invalid;
        }
        return Life::live;
    } __except(EXCEPTION_EXECUTE_HANDLER){return Life::invalid;}
}
bool Write(void* user,const Emitter& e,int team) noexcept {
    __try {
        int current=-1;if(!Team(team) || Read(user,e,&current)!=Life::live || !Readable(e.object,4,true))return false;
        if(current!=team)Put<int>(const_cast<void*>(e.object),0,team);
        return Read(user,e,&current)==Life::live && current==team;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool MemberBaseline(void* user,const ObjRef& root,const ObjRef& member,int team,int last,int* out,int* outLast) noexcept {
    __try {
        const auto* c=GetContext(user);
        if(!c || !out || !outLast || !Team(team) || !Team(last) || !Same(root,member) || Identity(member)!=Life::live || !TypeIs(member.obj,kActors[c->actor]))return false;
        *out=team;*outLast=last;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool EmitterBaseline(void* user,const Emitter& e,int ownerTeam,int,int* out) noexcept {
    if(!out)return false;
    __try {
        int current=-1;if(Read(user,e,&current)!=Life::live)return false;
        if(controlled_pvp::ReadEmitterBaseline(e,out))return true;
        if(!controlled_pvp::HasTemporaryTeam(e.member)){*out=current;return true;}
        const auto* c=GetContext(user);if(!c || !Team(ownerTeam))return false;
        if(e.object==static_cast<const unsigned char*>(e.storage.obj)+0x214){*out=ownerTeam;return true;}
        // A new held bullet inherits the real weapon cache, which can differ from the body's baseline.
        Emitter currentEmitters[controlled_pvp::kMaxEmitters]{};unsigned count=0;ObjRef holder{};
        if(!Current(*c,e.member,currentEmitters,controlled_pvp::kMaxEmitters,count,&e,&holder) || !holder)return false;
        const Emitter source=Binding(e.member,holder,0x214);
        return controlled_pvp::ReadEmitterBaseline(source,out); // source is captured before its bullets
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
} // namespace npc_pvp_detail
bool PvpNativeWeaponEmitters(const ObjRef& owner,const void* raw,controlled_pvp::Emitter* out,unsigned capacity,unsigned* count) noexcept {
    if(!out || !count || *count>capacity)return false;
    const unsigned saved=*count;
    __try {
        using namespace npc_pvp_detail;
        if(!Ready() || Identity(owner)!=Life::live || !Readable(raw,0xF30)){*count=saved;return false;}
        const auto* w=static_cast<const unsigned char*>(raw);const ObjRef weapon{w,At<const void*>(w,kSelfCtrl)};const int kind=SharedWeaponKind(w);
        // This API enumerates an already-existing bounded native container, with no spawn prediction.
        // FireOptions/FireCount can vary. FireType 1/2 ordinary retained bullets are validated, but keep their shot snapshot.
        if(Identity(weapon)!=Life::live || kind<0 || !WeakOwner(w,0x838,owner) || At<const void*>(w,0x120)!=owner.obj ||
           At<const void*>(w,0x38)!=owner.obj || !Team(At<int>(w,0x214))){*count=saved;return false;}
        unsigned n=saved;const Context context{0,1,kWalkLimit};
        if(!Append(out,capacity,n,Binding(owner,weapon,0x214)) || !Held(context,owner,weapon,out,capacity,n,true)){*count=saved;return false;}
        *count=n;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){*count=saved;return false;}
}
controlled_pvp::Life PvpNativeReadWeaponEmitter(const controlled_pvp::Emitter& e,int* team) noexcept {
    __try {return npc_pvp_detail::ReadStorage(e,team,true);} __except(EXCEPTION_EXECUTE_HANDLER){return controlled_pvp::Life::invalid;}
}
bool PvpNativeWriteWeaponEmitter(const controlled_pvp::Emitter& e,int team) noexcept {
    __try {
        int current=-1;if(!npc_pvp_detail::Team(team) || npc_pvp_detail::ReadStorage(e,&current,true)!=controlled_pvp::Life::live || !Readable(e.object,4,true))return false;
        if(current!=team)Put<int>(const_cast<void*>(e.object),0,team);
        return npc_pvp_detail::ReadStorage(e,&current,true)==controlled_pvp::Life::live && current==team;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool PvpControlledNpcProof(const char* name,unsigned maxWeapons,unsigned maxHeld,controlled_pvp::SpawnProof* out) noexcept {
    if(out)*out={};if(!out || !name)return false;
    __try {
        if(!npc_pvp_detail::Ready() || !maxWeapons || maxWeapons>npc_pvp_detail::kWeaponLimit || !maxHeld || maxHeld>npc_pvp_detail::kHeldLimit)return false;
        for(const auto& c:npc_pvp_detail::kContexts)if(c.weapons==maxWeapons && c.held==maxHeld && !std::strcmp(name,npc_pvp_detail::kActors[c.actor].name)) {
            if(!npc_pvp_detail::TypeReady(npc_pvp_detail::kActors[c.actor]))return false;
            for(unsigned i=0;i<(c.actor==2 ? 4u : 1u);++i)if(!npc_pvp_detail::TypeReady(npc_pvp_detail::kWeapons[i]))return false;
            if(c.actor==2 && !npc_pvp_detail::TypeReady(npc_pvp_detail::kShieldBullet))return false;
            out->maxMembers=1;out->maxEmitters=maxWeapons*(1+2*maxHeld);
            out->hooks={const_cast<npc_pvp_detail::Context*>(&c),&npc_pvp_detail::Collect,&npc_pvp_detail::Read,&npc_pvp_detail::Write,&npc_pvp_detail::MemberBaseline,&npc_pvp_detail::EmitterBaseline};return true;
        }
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
} // namespace crew
