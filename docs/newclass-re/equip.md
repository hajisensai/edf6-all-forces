# 新兵种（4 载具手 / 5 刻印剑士）逆向：装备 / 兵种选择 / 类别

EDF.dll Steam 0x678CCB46，地址均为 RVA。置信度：[确认]=汇编核过；[推断]=由多处证据推出；[猜测]/[待查]=未核。

## 1. 兵种数、槽、类别全部来自 CONFIG.SGO

- `0xE34D0` 兵种数 = `[[cfg+0x20]+4]`（cfg=GS+0x130，cfg+0x20 = SoldierInit 节点），即 SoldierInit 数组长度。[确认]
- `0xE3680` 槽数 = SoldierInit[class][4] 元素数。[确认]
- `0xE2450(cfg,out,class,slot)`：out+0 槽名、+8 默认武器名、+0x10 vector<int> 类别、+0x30 可重复。[确认]
- `0xE3580` 取 SoldierInit[class][1]（名字键）；`0x959170(i)` = E3580 + `0x7A3500` 本地化 → 兵种显示名。[确认]
- `0xE07D0` 取 SoldierInit[class][5] 颜色表。[确认]
- 3D 预览 `0x93B320` 用 E3680；模型路径 `0xE3420` / `0xE34E0(tables,class,outfit)` 取自 SoldierInit。[推断]

## 2. 类别号 → 兵种：数据给出，不按百位推

- `0xE2AD0` 读 WeaponTable(cfg+0x188)、`'SoldierInit'`(0x17682A0)→cfg+0x20、`'SoldierWeaponCategory'`(0x17682B8)→哈希表 cfg+0x28（`0xE0320` 插入，节点+0x18 信息，+0x20 初值 -1）。[确认]
- `0xE2CB5..0xE2D70`：遍历每兵种每槽每类别，`0xE2D49 call 0xE00C0` 查节点，`0xE2D51 mov [rcx+0x20],ebx` 写 SoldierInit[class][0]。即 category→兵种表完全由 SoldierInit 槽的类别列表建出。[确认]
- 所以 400–4xx / 500–5xx 不会被原版当成 0–3 号兵种，前提：SoldierWeaponCategory 里登记这些号、并只挂到兵种 4/5 的槽上。[确认，建表处]；map+0x20 的全部读者未逐一核。[推断]
- `0xE35B0(cfg,out,cat)` 按类别取节点（+0x18），调用者 `0x7DC079`、`0x8B4D73`（武器选择界面取类别名再 0x7A3500 本地化）、`0x8D68A9`、`0x8D7C3E`、`0x93DA14`。[确认]

## 3. GameStatus / 存档已为 10 个兵种预留空间

- `0xDEDA0`（rcx=GS+0x6E68，每本机槽 base+0x28+slot*0x3E60）：+0x28 兵种=0、+0x2C 外观=-1；+0x30 起 10×0x18 填 -1（10 兵种 × 6 槽武器）；+0xF8 起 10 项护甲；+0x128 起颜色 10 兵种×4 外观×2×0xC4=0x3D40，正好到 0x3E60 块尾；块头 10 个 int 计数清零。[确认]
- 汇编扫描：GS+0x6EF8..0x6F87（兵种 4..9 的区间）没有直接偏移访问者。[确认]
- 推论：兵种 4、5 的装备/护甲/颜色已在 GS 与 MAIN.GST（0xDCE8 字节）里，存档格式不用改。[推断]

## 4. 默认装备与旧存档

- `0xDC170` 新档：2 本机槽 × E34D0 × E3680，按 E2450 默认名经 `0xE1BA0` 写 `GS+0x6E68+4*(r14+6*(class+2)+i)`。[确认]
- `0xDC550`（DC170 末尾、UnlockDownloadContents `0x70FF87` 调用，每次读档后都跑）：`0xDC7DC..0xDC8C5` 对每兵种每槽，值为 -1 或未持有（GS+0xEB50 flags）就写回 SoldierInit 默认。[确认]
- 所以加 SoldierInit 条目后旧存档兵种 4/5 会自动补默认装备（默认武器必须是已持有/可持有行）。[推断]
- `0xDB920` / `0xDC0C0`：2×兵种数×4 调 `0xDF1B0`，按 SoldierInit[class][5] 初始化颜色块（base+slot*0x3E60+0x160+(class*4+k)*0x188）。[确认]

## 5. 兵种选择界面 HUiHQSoldierSelect（vtable 0x1805788，init 0x8A8CE0，OnUpdate 0x8AB310）

- 注意：`UiSoldierSelect 0x7DB520 / 0x7DA480 / 0x7DD680` 是调试文本菜单，不是玩家看到的界面。[确认]
- 按钮：`0x8A97CC..0x8A9803` 调 E34D0 得 n，循环 n 次 `0x845BB0(this, L"ButtonSolder"@0x1805878)` 按 LYT 模板建按钮。[确认]
- 文字：`0x8A9896..0x8A995F` 对 i<min(n, vector[+0x780..+0x788] 元素数) 调 0x959170(i)→`0x9532C0(this+0x770,i,名)`。[确认]；+0x780 向量来源（LYT 里的位置表？）与 6 个按钮能否摆下未核。[待查]
- 当前兵种从 `[GS+0x6E68+slot*0x3E60+0x28]` 读入 +0x79C（`0x8A9988`）；外观经 `0xD86C0` → +0x7A0。[确认]
- 槽行 `0x8AA830`：对 i<E3680 生成行（E3550/E2450 取槽名），再加 2 行（类型 2/3）；行向量 `0x8A8FEC` 固定 8×0x50。每兵种最多 6 槽 + 2，新兵种 2/3 槽没问题。[确认]
- `0x8AA250` 构造 HUiHQWeaponSelect::InitParam（玩家=[r15+0x798]，槽=edx），`0x8AA2BA call 0x8BDE80` 刷新预览。[确认]
- 外观弹窗 `0x8AB5A9..0x8ABC10`（5 项，写 GS+0x6E94，见 edf41-outfits.md §3）；`0x8ABB6F` 一带 cmp 3 / mov 3 / mov 4 未分析。[待查]
- 改兵种写 GS 在 `0x8AC6E0..0x8ADC90`（多处 imul 0x3E60，`0x8AD9A1` 读 +0x6E90），未细读。[待查]

## 6. 武器选择 HUiHQWeaponSelect（ctor 0x8B4280）

- 类别只经 E3680/E2450/E35B0 取得，数据驱动（docs/weapon-select-re.md）。快照整块 0xDCE8，每兵种装备在快照+0x830+class*0x18（class 4/5 落在预留区内）。[确认]

## 7. 其它 cmp 4

- 联机预载 `0x5959E1` / `0x59DCEA` 的 cmp 4 是玩家数（EDF6Coop 已改），与兵种无关。[确认]

## 8. 插件 / 工具里硬编码 4 兵种（都要改成表驱动）

- `src/player_slots.h:19` kClasses=4、kSlotsMost=6，static_assert SlotsOf(4)==0
- `src/weapon_page_logic.h:32` kClasses=4，Texts::cls {R,W,A,F}
- `src/loadout_preset.h:34` kClasses=4、kSlots=6（预设存档格式要升版本）
- `src/outfit41_logic.h:20` kClasses=4
- `src/armory.cpp:446` kClassNames
- `src/airstrike.cpp:500-509` for cls<4 + kSlots[cls]
- `src/weapon_equip.h:61` 注释兵种 0..3
- `tools/gen_weapon_series.py` SLOTS 硬编码原版 SoldierInit，`--names` 校验 have==SLOTS
- `tools/ported_weapons.py` SOLDIER_CAS 按 category//100 映射兵种 → 4xx/5xx 错
- 可直接通用：`src/weapon_list_logic.h` CheckEquipRow（规则表驱动）；`src/weapon_equip.cpp` Equip/SetClassSlots/SetClass；armory 整备界面武器页（兵种分页 Z/C）。

## 9. 未覆盖（交给其它主题）

- LYT（HUiHQSoldierSelect 布局）能否放 6 个按钮。
- 按兵种分的其它数据：任务通关记录、DEFP_*.MST、成就、结算界面、HUD、兵种能力（飞行/冲刺/护甲 cap）、角色模型/动作。
- map+0x20 全部读者。
