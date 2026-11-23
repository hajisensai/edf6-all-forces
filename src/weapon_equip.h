// 整备界面武器页的游戏侧：读武器表 / 持有位 / 等级上限 / 当前装备，写一个槽（docs/weapon-select-re.md）。
// 列表与判定的纯逻辑在 weapon_list_logic.h；这里只取运行时事实、按同一判定写 GS，再像原版一样给 3D 预览发消息 4。
//
// 线程：除 InstallWeaponEquip 外都只在游戏的 UI 更新里调（菜单 OnUpdate 链，即原版选择界面运行的那个线程）。
// 不打补丁、不挂钩：只调用签名核过的原生函数（weapon_equip_signatures.inc，tools/weapon_equip_sigs.py）。
#pragma once
#include "weapon_list_logic.h"
#include <array>

namespace crew::wequip {

// 核对签名（不改游戏）。不符时记 "WEQUIP REFUSED ..."，之后所有调用返回失败 / -1。
bool InstallWeaponEquip() noexcept;
bool Ready() noexcept;

inline constexpr int kPlayers=2;   // GS 里的两份装备（1P / 分屏 2P，步长 0x3E60）

// 运行时武器表（GS+0x130）。表还没载入（[cfg+0x188] 为空）或未就绪：-1 / false。
int RowCount() noexcept;
int FindRow(const wchar_t* id) noexcept;                 // 区分大小写，-1 不在表里
struct RowFacts {
    int category=-1;
    int level=0;                   // 行 +0x20：游戏显示与封顶用的等级
    wchar_t id[64]{};              // 行 +0 的 WEAPONTABLE 名（截断到 63 字）
};
bool ReadRow(int row,RowFacts* out) noexcept;

bool Owned(int row) noexcept;                             // WeaponRecord flags bit0（weapon_records.h，含搬家后的行）
int LevelCap() noexcept;                                  // -1：不封顶（GS+0x14FC8 为 0，或 0xD8850 返回 -1）
int CurrentClass(int player) noexcept;                    // GS+0x6E90+player*0x3E60；-1 读不到
// 某兵种 6 个槽当前的行（-1 空；该兵种没有的槽也是 GS 里的原值）。
bool ReadEquipped(int player,int cls,std::array<int,wlist::kSlotsMost>* out) noexcept;

// 当前表的目录：生成数据按名字找行（表里没有的不列）；表里有、生成数据里没有的行（别的 mod 追加）作为来源
// kSourceOther 的单行系列，名字用它的 WEAPONTABLE 名。表未就绪时返回 false。
bool BuildCatalog(wlist::Catalog* out);
// player 在 cls 兵种的装备、持有位快照（按当前 RowCount）与等级上限；favourites 不动（由页面从 ini 读）。
bool ReadLoadout(int player,int cls,wlist::Loadout* out);

struct EquipResult {
    wlist::Verdict verdict=wlist::Verdict::absent;   // ok 才写；same 不写也算成功
    bool written=false;            // GS+0x6E98 那一格已写成 row
    bool previewNotified=false;    // cls 是该玩家当前兵种：已广播消息 4 {player, slot, row, cls}
};
// 写一个槽：先按表里读到的类别 / 等级、持有位、上限、本兵种其它槽，用 wlist::CheckEquipRow 判定（规则同原版
// HUiHQWeaponSelect），ok 才写 GS+0x6E98+player*0x3E60+cls*0x18+slot*4，然后像原版确认键一样经 UI 系统广播消息 4。
// 不存档：原版确认路径上也没有存档 / 脏标记（docs/weapon-select-re.md §4），装备随游戏自己的下一次存档写盘。
EquipResult Equip(int player,int cls,int slot,int row) noexcept;

}  // namespace crew::wequip
