// The support vehicles' and aircraft's firepower by level (docs/feature-npc-weapon-tier.md §4.1; the maintainer,
// 2026-11-10: 「援军载具和战机的火力 改成根据等级来。这个等级是类似联机模式的那个，等级会随着难度变化」).
// Pure: the level is the stock online story's weapon level cap at the mission's difficulty and place in its list (what
// EDF.dll 0xE2710 makes of it), the damage multiplier the one a player's request of that level has on its vehicle's stock
// request curve (tools/call_weapons.py request_tier). The data are the stock game's (Steam EDF.dll 0x678CCB46 and its
// Root.cpk); tools/selftest.py support_level_tables_match_the_game reads them off the game and compares.
#pragma once
#include <cmath>
#include <cstdint>

namespace crew::supportlevel {
constexpr int kDifficulties=5;   // GS+0x4C: 0 easy .. 4 inferno

// DEFAULTPACKAGE/CONFIG.SGO ModeList GameMode_OnlineScenario, member 7 [difficulty] element 6: the online weapon level cap
// as raw levels (the UI level / 25), interpolated by the mission's place in the list (docs/weapon-level-cap-re.md §1).
struct CapRow { int n; float at[4]; };
inline constexpr CapRow kOnlineCap[kDifficulties]={
    {2,{0.1f,1.1f}},{2,{0.1f,1.1f}},{2,{0.2f,2.1f}},{2,{1.4f,3.1f}},{4,{2.4f,3.2f,3.999f,3.999f}},
};

// EDF.dll 0xE2710 (cfg, difficulty, progress) on a row of floats, step for step: the progress held to [0, 1]
// (0xE275B / 0xE276D), x = (n - 1) * progress, i = floorf(x) (0xE278A, the CRT floorf); the last element from i + 1 >= n on
// (0xE279D), else lerp(at[i], at[i + 1], x - i) (0xE27DA..0xE27E6); then x 0.25 x 100 truncated (0xE27EA..0xE27FA): the UI
// level. -1 (no cap) for a negative element, as there (not in the stock online story's table).
inline int CapLevel(const CapRow& row,float progress) noexcept {
    if(row.n<=0 || row.n>4)return -1;
    if(!(progress>=0.0f))progress=0.0f;   // 0 > progress, and a NaN (the game's comiss falls through to its clamp too)
    else if(progress>1.0f)progress=1.0f;
    const float x=static_cast<float>(row.n-1)*progress;
    const float f=std::floor(x);
    const int i=static_cast<int>(f);
    float v;
    if(i+1>=row.n)v=row.at[row.n-1];
    else {
        const float a=row.at[i],b=row.at[i+1];
        if(a<0.0f || b<0.0f)return -1;
        v=(x-f)*(b-a)+a;
    }
    v*=0.25f;v*=100.0f;
    return static_cast<int>(v);
}

// The support level now: the online story's cap at this difficulty and progress (0xD7B60: the mission's row / (rows - 1)).
// -1: a difficulty out of range.
inline int SupportLevel(int difficulty,float progress) noexcept {
    if(difficulty<0 || difficulty>=kDifficulties)return -1;
    return CapLevel(kOnlineCap[difficulty],progress);
}

// A stock request family's (level, damage multiplier) by level: a request's vehicle setup [0][1] (Ammo_CustomParameter[4]
// [3][0][1]) at its WEAPONTABLE level (column 4, raw: the UI level / 25).
struct Point { float level,damage; };
enum class Curve : std::uint8_t { eros, blacker, grape };
// The N9 Eros requests (tools/call_weapons.py EROS_REQUESTS: EWEAPON394..399): every aircraft the plugin's player
// requests bring is on it (request_family of a jet or heli call), so every support aircraft is too.
inline constexpr Point kEros[]={{0.44f,1.3f},{0.88f,2.7f},{1.5f,5.0f},{2.73f,14.0f},{3.16f,22.0f},{3.42f,25.0f}};
// The Blacker's (the V505 tank's line: pylib/vcobjects.py GROUND_VEHICLES['drill'].family, EWEAPON418/421/425/428/433):
// the ground support tank (V505_TANK_MISSION).
inline constexpr Point kBlacker[]={{0.24f,0.7f},{0.64f,1.7f},{1.02f,3.0f},{1.92f,7.0f},{3.12f,23.0f}};
// The Grape's (V507 rescue tank: EWEAPON400/404/408/411/414/415): the ground support transport (V507_RESCUETANK_AI).
inline constexpr Point kGrape[]={{0.24f,1.3f},{0.725f,1.8f},{1.57f,4.8f},{2.35f,9.0f},{2.9f,17.0f},{3.07f,22.0f}};

struct Points { const Point* at; int n; };
constexpr Points PointsOf(Curve c) noexcept {
    return c==Curve::blacker ? Points{kBlacker,static_cast<int>(sizeof(kBlacker)/sizeof(kBlacker[0]))} :
           c==Curve::grape ? Points{kGrape,static_cast<int>(sizeof(kGrape)/sizeof(kGrape[0]))} :
           Points{kEros,static_cast<int>(sizeof(kEros)/sizeof(kEros[0]))};
}
constexpr const char* CurveName(Curve c) noexcept {
    return c==Curve::blacker ? "blacker" : c==Curve::grape ? "grape" : "eros";
}

// tools/call_weapons.py request_tier's damage: linear between the points, held past both ends.
inline float TierDamage(Curve c,float rawLevel) noexcept {
    const Points p=PointsOf(c);
    if(rawLevel<=p.at[0].level)return p.at[0].damage;
    for(int i=1;i<p.n;++i) {
        if(rawLevel<=p.at[i].level) {
            const Point& a=p.at[i-1];const Point& b=p.at[i];
            return a.damage+(b.damage-a.damage)*(rawLevel-a.level)/(b.level-a.level);
        }
    }
    return p.at[p.n-1].damage;
}

// What a support vehicle's weapons deal per their SGO damage at UI level `level` (SupportLevel): a request's multiplier.
inline float LevelDamage(Curve c,int level) noexcept {
    return TierDamage(c,static_cast<float>(level)/25.0f);
}
}  // namespace crew::supportlevel
