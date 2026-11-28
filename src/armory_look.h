// 整备「外观」页的游戏侧（armory_look.cpp）：读写原版外观弹窗与配色界面写的同一批 GameStatus 格子
// （armory_look_logic.h 的偏移，tools/armory_look_sigs.py 签名核对的代码）。只在游戏的 UI 线程调。
#pragma once
#include "armory_look_logic.h"

namespace crew::armorylook {
// 核对签名（src/armory_look_signatures.inc）；不符记 "ARMORY look REFUSED" 一行，本页不出动。
bool Install() noexcept;
bool Ready() noexcept;
int Outfit(int who) noexcept;       // GS 外观格（-1 随任务 .. 3）；读不到 -2
int WornOutfit(int who) noexcept;   // 0xD8700：随任务时按任务解析；读不到 -1
int Unlocked() noexcept;            // GS+0x300：1 解锁、0 未解锁、-1 读不到
bool ReadColours(int who,int cls,int outfit,armory::look::Palette* out) noexcept;
// 原版外观项的写法（0x8AD5C0..：GS+0x6E94 = outfit）。outfit -1..3。
bool WriteOutfit(int who,int outfit) noexcept;
// 原版配色界面的写法（0x882790：选中号）。
bool WriteColour(int who,int cls,int outfit,int ch,int idx) noexcept;
}  // namespace crew::armorylook
