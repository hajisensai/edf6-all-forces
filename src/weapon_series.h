// 武器系列数据：tools/gen_weapon_series.py 生成的 weapon_series.inc 的类型（规则见生成器文档）。
// 纯数据，无游戏依赖；列表逻辑见 weapon_list_logic.h。
#pragma once
#include <cstdint>

namespace crew::wlist {

inline constexpr int kNameLangs=4;   // hudtext::Lang 顺序：en, zhCN, zhTW, ja

// 来源（kWeaponRows.source）；kSourceOther 只给运行时表里有、生成数据里没有的行（别的 mod 追加的）。
enum Source : std::uint8_t { kSourceEdf6=0, kSourceEdf5=1, kSourceEdf41=2, kSourceOther=3 };
inline constexpr int kSources=4;

enum RowFlag : std::uint8_t { kRowCall=1, kRowInitial=2, kRowDlc=4 };

struct WeaponRowInfo {
    const wchar_t* id;            // WEAPONTABLE 名（FindRow 0xE1BA0 的键，区分大小写）
    std::uint16_t index;          // 只装本插件时的表下标（运行时一律按 id 查）
    std::uint8_t source;
    std::uint8_t cls;
    std::int16_t category;
    std::uint16_t level;          // 游戏显示 / 封顶用的等级（GetRow 的 +0x20）
    std::uint16_t series;         // kWeaponSeries 下标
    std::uint8_t slots;           // bit i：本兵种第 i 槽收这一类
    std::uint8_t flags;           // RowFlag
    const wchar_t* name[kNameLangs];
};

struct WeaponSeriesInfo {
    std::uint8_t source;
    std::uint8_t cls;
    std::int16_t category;
    std::uint16_t first;          // kWeaponRows 下标
    std::uint16_t count;
    const wchar_t* label[kNameLangs];
};

inline constexpr int kSlotCategoriesMost=8;
struct WeaponSlotRule {
    std::uint8_t cls;
    std::uint8_t slot;
    bool allowDuplicate;          // CONFIG.SGO SoldierInit 槽第 4 项（0xE2450 读到 +0x30）
    std::int16_t categories[kSlotCategoriesMost];   // -1 结尾填充
};

#include "weapon_series.inc"

}  // namespace crew::wlist
