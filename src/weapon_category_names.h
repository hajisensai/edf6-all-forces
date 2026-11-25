#pragma once
// 武器类别（WEAPONTABLE 行 +0x10，docs/weapons.csv 的 category / category_name）的短名，给整备武器页的类别标签用。
// 类别号与英文内部名取自 docs/weapons.csv（游戏数据，证据 H）；各语言短名是本插件自己起的（证据 L：原版选择界面的
// 类别标签文字没有逆出来，不保证与原版用词一致）。顺序同 hudtext::Lang：en, zhCN, zhTW, ja。
#include <cstddef>

namespace crew::wlist {

struct CategoryName {
    int category;
    const wchar_t* name[4];
};

inline constexpr CategoryName kCategoryNames[]={
    // Ranger
    {0,{L"Assault",L"突击步枪",L"突擊步槍",L"アサルト"}},
    {1,{L"Shotgun",L"霰弹枪",L"霰彈槍",L"ショットガン"}},
    {2,{L"Sniper",L"狙击枪",L"狙擊槍",L"スナイパー"}},
    {3,{L"Rocket",L"火箭筒",L"火箭筒",L"ロケラン"}},
    {4,{L"Missile",L"导弹",L"導彈",L"ミサイル"}},
    {5,{L"Grenade",L"榴弹",L"榴彈",L"グレネード"}},
    {6,{L"Special",L"特殊",L"特殊",L"特殊"}},
    {7,{L"Support",L"支援装备",L"支援裝備",L"補助装備"}},
    {8,{L"Vehicle",L"载具",L"載具",L"ビークル"}},
    {9,{L"Vehicle",L"载具",L"載具",L"ビークル"}},
    {10,{L"Vehicle",L"载具",L"載具",L"ビークル"}},
    {20,{L"Throw",L"投掷",L"投擲",L"投てき"}},
    {21,{L"Bomb",L"设置炸弹",L"設置炸彈",L"設置爆弾"}},
    {22,{L"Turret",L"设置枪",L"設置槍",L"設置銃"}},
    {23,{L"Throw 2",L"投掷 2",L"投擲 2",L"投てき2"}},
    // Wing Diver
    {100,{L"Short",L"近距离",L"近距離",L"近距離"}},
    {101,{L"Laser",L"激光",L"雷射",L"レーザー"}},
    {102,{L"Thunder",L"雷电",L"雷電",L"雷撃"}},
    {103,{L"Particle",L"粒子",L"粒子",L"粒子"}},
    {104,{L"Long",L"远距离",L"遠距離",L"狙撃"}},
    {105,{L"Explosive",L"爆炸",L"爆炸",L"爆撃"}},
    {106,{L"Homing",L"追踪",L"追蹤",L"誘導"}},
    {110,{L"Grenade",L"榴弹",L"榴彈",L"グレネード"}},
    {111,{L"Short",L"近距离",L"近距離",L"近距離"}},
    {112,{L"Trap",L"陷阱",L"陷阱",L"トラップ"}},
    {113,{L"Shield",L"护盾",L"護盾",L"シールド"}},
    {114,{L"Special",L"特殊",L"特殊",L"特殊"}},
    {115,{L"Homing",L"追踪",L"追蹤",L"誘導"}},
    {116,{L"Cannon",L"炮",L"砲",L"キャノン"}},
    {120,{L"Core",L"核心",L"核心",L"コア"}},
    // Fencer
    {200,{L"Hammer",L"近战打击",L"近戰打擊",L"近接打撃"}},
    {201,{L"Spear",L"近战突刺",L"近戰突刺",L"近接刺突"}},
    {202,{L"Shield",L"盾",L"盾",L"盾"}},
    {203,{L"Auto",L"机炮",L"機砲",L"機関砲"}},
    {204,{L"Cannon",L"火炮",L"火砲",L"火砲"}},
    {205,{L"Missile",L"导弹",L"導彈",L"ミサイル"}},
    {206,{L"Booster",L"推进器",L"推進器",L"ブースター"}},
    {207,{L"Shield+",L"护盾强化",L"護盾強化",L"盾強化"}},
    {208,{L"Aiming",L"瞄准",L"瞄準",L"照準"}},
    {209,{L"Actuator",L"动力",L"動力",L"機動"}},
    // Air Raider
    {302,{L"Engineer",L"工兵",L"工兵",L"工兵"}},
    {303,{L"Bomb",L"炸弹",L"炸彈",L"爆弾"}},
    {305,{L"Special",L"特殊",L"特殊",L"特殊"}},
    {306,{L"Vehicle",L"载具",L"載具",L"ビークル"}},
    {307,{L"Vehicle",L"载具",L"載具",L"ビークル"}},
    {308,{L"Vehicle",L"载具",L"載具",L"ビークル"}},
    {309,{L"Vehicle",L"载具",L"載具",L"ビークル"}},
    {310,{L"Artillery",L"炮击",L"砲擊",L"砲撃"}},
    {311,{L"Gunship",L"武装直升机",L"武裝直升機",L"ガンシップ"}},
    {312,{L"Attacker",L"空爆",L"空爆",L"空爆"}},
    {313,{L"Missile",L"导弹",L"導彈",L"ミサイル"}},
    {314,{L"Satellite",L"卫星",L"衛星",L"衛星"}},
    {320,{L"Vehicle",L"载具",L"載具",L"ビークル"}},
    {330,{L"Drone",L"无人机",L"無人機",L"ドローン"}},
    {331,{L"Drone bomb",L"无人机炸弹",L"無人機炸彈",L"爆撃ドローン"}},
    {332,{L"Tracer",L"追踪无人机",L"追蹤無人機",L"追尾ドローン"}},
    {333,{L"Gun drone",L"射击无人机",L"射擊無人機",L"射撃ドローン"}},
    {334,{L"Counter",L"反击无人机",L"反擊無人機",L"迎撃ドローン"}},
};

// nullptr：表里没有（别的 mod 的新类别），调用方显示类别号。
inline const wchar_t* CategoryShortName(int category,int lang) noexcept {
    for(const CategoryName& c:kCategoryNames)
        if(c.category==category)return c.name[lang>=0 && lang<4 ? lang : 0];
    return nullptr;
}

}  // namespace crew::wlist
