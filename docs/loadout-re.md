# 强制装备（兵种 + 武器 + 支援/载具）逆向笔记

EDF.dll TimeDateStamp `0x678CCB46`，地址均为 RVA（ImageBase `0x180000000`）。全部是静态分析，**未实测**。
置信度标记：**[确认]** = 有指令级证据；**[推断]** = 由多处证据推出但没跑过；**[猜测]** = 只是假设。

武器清单：`docs/weapons.csv`（1564 行，从游戏 Root.cpk 的 `WEAPON/WEAPONTABLE.SGO`、`WEAPONTEXT.EN/JA.SGO`、
`DEFAULTPACKAGE/CONFIG.SGO` 生成）。

---

## 0. 结论速览

- 本机（离线 / slot 0）当前装备就在 `GameStatus` 里，GameStatus* 全局在 `EDF+0x20B2890`。
  - 兵种：`GS + 0x6E90 + slot*0x3E60`（int，0 Ranger / 1 WingDiver / 2 AirRaider / 3 Fencer）
  - 武器：`GS + 0x6E98 + slot*0x3E60 + class*0x18 + i*4`（int，WeaponTable 行号；**每个兵种各记一套 6 格**）
- 武器 ID = `WEAPONTABLE.SGO` 的**行号**（0 起）。名字 ↔ ID 由 `0xE1BA0`（按 SGO 名查行号）/ `0xE1CF0`（按行号取行）完成。
- 菜单里确认装备时，UI 直接把选中的行号写进上面那块 GS 内存（`UiWeaponSelect` vfunc1 `0x7DD680`，`UiSoldierSelect` vfunc1 `0x7DB520`）。存档只是 GS 的序列化。
- 离线建人路径 `CreatePlayer` → `0x1DC450` → `0x591410` → `0x595C80`（从 GS 抄装备）→ `0x5A3F90`（造人+造武器），**全程没有“是否拥有”检查**。资源预加载 `PreloadPlayerResource` → `0x59DE50` 也读同一块 GS。
- Air Raider 的载具槽（slot 4）、支援槽（slot 3），Ranger 的“支援/载具”槽（slot 3）都在同一个 int 数组里，和武器同一种 ID。
- 最实用的强制方式：插件在任务脚本 `PreloadPlayerResource()` 之前把 GS 这几个 int 改掉（可在 `0x59DE50` 入口 hook），任务结束 / 存档前还原。
- 存档（`%LOCALAPPDATA%\EarthDefenceForce6\SAVE_DATA\<SteamID64>\saveslotNN\MAIN.GST`）是加密/压缩后的二进制（熵 ≈ 8.0），有校验，且走 Steam Cloud；本轮没拿到密钥，**不建议**走改档路线。

---

## 1. 内存里的本机装备在哪里、谁写它

### 1.1 GameStatus 布局（与装备相关部分）

`GS = *(uint8_t**)(EDF + 0x20B2890)` [确认：`0x595CA4 mov rax,[rip+...]` 解析到 `0x20B2890`，multislot `mission.h` 同值]。
`slot` = 本机玩家槽（0 = 1P，1 = 分屏 2P；`0xAC650`/`0xAC9E0` 都拒绝 `slot >= 2`）。每个 slot 跨度 `0x3E60`（= `0xF98*4`）。

| 偏移（+slot*0x3E60） | 类型 | 含义 | 证据 |
|---|---|---|---|
| `0x6E68 + class*4` | int | 每兵种一个计数（用途未定，MissionSync 发出去）[猜测：出击次数之类] | `0x78EC31` |
| `0x6E90` | int | 当前兵种 0..3 | `0x595CB5`、`0x7DB6B2`（UI 写）、`0xAC696`（`set_player_soldier`） |
| `0x6E94` | int | 外观/服装编号，-1..3（UI 里 `(n%5)-1`） | `0x7DB7D0`、`0xD8700`（<0 时取默认） |
| `0x6E98 + class*0x18 + i*4` | int[4][6] | 每兵种 6 格装备，值 = WeaponTable 行号，-1 = 空 | `0x595D80..0x595DAD`、`0x7DD8AB`、`0xACA88`、`0x78EB40..` |
| `0x6F88 + (slot*0xF98+class)*4` | int | 护甲拾取数（multislot armor.h 已详述） | `0x595CD9` |

全局（不分 slot）每把武器一条 12 字节记录，按武器 ID 索引：

| 偏移 | 类型 | 含义 | 证据 |
|---|---|---|---|
| `GS + 0xEB50 + id*12` | u32 flags | bit0 = 已拥有，bit1 = 用过（`SetUsedWeapon`），bit2 = 新获得 [推断] | `0xAF28C`/`0xAF2C3`（unlock_weapon）、`0x70FB7F`（SetUsedWeapon） |
| `GS + 0xEB54 + id*12` | u8[8] | 该武器各项属性的星级 | `0xAF2BB`、`0x5A4397`（造武器时传入） |

最多 `0x800` 条（`0xDF720` 的循环上限）。

`GS + 0x14FF4`：本机玩家数（`SetUsedWeapon` 循环上限）。`GS + 0x14C78`：联机 4 条 `0xD4` 字节装备记录（multislot `mission.h`）。

### 1.2 各兵种的槽位（`CONFIG.SGO` → `SoldierInit`）[确认]

槽数由 `0xE3680(GS+0x130, class)` 从 CONFIG.SGO 的 SoldierInit[class][4] 的元素个数取得。

| class | 槽 i | 名称 | 允许的 category | 默认 |
|---|---|---|---|---|
| 0 Ranger | 0 | Weapon_Slot1 | 0–6 | AssultRifle01 |
|  | 1 | Weapon_Slot2 | 0–6 | aGrenadeLauncher01 |
|  | 2 | Weapon_SlotSupport2 | 20,23,21,22 | aHandGrenade01 |
|  | 3 | Weapon_SlotSupport1 | **7（支援装备）, 8,9,10（Ranger 载具）** | aSupportNone |
| 1 WingDiver | 0,1 | Slot1/2 | 100–106 | pRapier01 / pPulse01 |
|  | 2 | PaleSupport2 | 110,111,113,112,116,115,114 | pEnergyShield01 |
|  | 3 | PaleSupport1（核心） | 120 | pSupportNone |
| 2 AirRaider | 0,1,2 | Slot1/2/3 | 310–314, 303, 305 | eLimpetGun01 / eDrone_attacker01 / eDrone_airstrike01 |
|  | 3 | AirRaiderSupport | 302, 330–334, 331 | eDrone_sa01 |
|  | 4 | **SlotVehicle** | **306,307,308,309,320** | eVehicleNone |
| 3 Fencer | 0..3 | Slot1L/1R/2L/2R | 200–205 | hShield01/hGatling01/hShield01/hPileBanker01 |
|  | 4,5 | FencerSupport ×2 | 206–209 | hSupportNone0 / hSupportNone1 |

注意 **category 的百位 ≠ 兵种编号**：2xx 是 Fencer（class 3），3xx 是 Air Raider（class 2）。CSV 的 `class_id` 已按 SoldierInit 换算好。

### 1.3 谁写这块内存

- **装备界面确认武器**：`UiWeaponSelect` vtable `0x17F4528` 第 1 槽 `0x7DD680`，在 `0x7DD8AB`：
  `[GS + 0x6E68 + 4*((slot)*0xF98 + 6*(class+2) + i)] = 选中列表项的 id`，其中 slot = `this+0x10C`、i = `this+0x110`。[确认]
- **兵种选择**：`UiSoldierSelect` vtable `0x17F43D0` 第 1 槽 `0x7DB520`：`0x7DB6B2` 写 class，`0x7DB7D0` 写外观。改兵种后调 `0x7DA480` 把该兵种的 6 格读回 UI，**负值会被改写成 0**。[确认]
- **读档**：GS 由 `0xD62C0` 一带序列化/反序列化（`0xD6461 lea rcx,[r14+0x6E68]`）[推断]。
- **开发者命令**（零售版里仍注册着，`0xA1F90` 注册表，名字是 UTF-16）：
  `set_player_soldier 0xAC650`、`set_weapon 0xAC9E0`（**按 SGO 名**查行号再写进当前兵种的槽，查不到打印「武器が見つからない (%ls)」）、
  `set_armor_count 0xAB2C0`、`unlock_weapon 0xAF150`、`unlock_weapon_one 0xAF330`、`unlock_weapon_index 0xAF120`、`weapon_status 0xAFC60`。
  这几个处理函数是本笔记布局的旁证；调用它们需要命令参数结构（`[ctx+8]` 指向参数，+0 slot，+0x10 槽号，+0x28 名字），本轮没把参数结构完全还原，**插件直接写内存更简单**。[确认/参数布局为推断]

---

## 2. 武器 ID 格式与名称映射

- **ID = WeaponTable 行号（0 起）** [确认]：
  - `0x5A3F90` 循环里 `esi = weapon[i]` → `0xE1CF0(GS+0x130, &row, esi)` 取该行，再拿行里的路径 (`app:/weapon/xxx.sgo`) 交给 `0x58F360` 造武器。
  - `0xE1CF0` 用 `rsi*4` 直接索引行数组；`0xE1BA0` 逐行比较名字、返回行号（默认装备、`set_weapon` 都靠它把 SGO 名换成 ID）。
  - edf6-jaeger 的 `build.py` 也写明「Saves refer to weapons by row index」，删行后存档装着它会进主菜单崩溃。
- WeaponTable 来自 CONFIG.SGO `WeaponTable = 'app:/Weapon/WeaponTable.sgo'`，经 EDFModLoader 时 `Mods\WEAPON\WEAPONTABLE.SGO` 会覆盖它。**当前游戏目录 Mods\WEAPON 里没有 WEAPONTABLE.SGO**（只有 WEAPONTEXT.*，与表逐行对齐），所以现在 ID 就是原版 1564 行。装了加行的 mod（如 jaeger，追加在末尾）后，原有 ID 不变，新行在后面。
- 行格式（`table` 数组每行 9 列）：`[0] SGO 名, [1] 路径, [2] category, [3] 1/3（3 = MPACK 系列）, [4] 等级（0..1.x 的小数）, [5] 获取方式（0 普通 / 1 初始装备 / 3 DLC，见 jaeger build.py）, [6] 6 个难度的掉落参数, [7], [8]`。
- 名字：`WEAPONTEXT.<LANG>.SGO` 的 `text_table[id][0]`，与 WeaponTable 逐行对齐。
- **CSV**：`D:\APP\edf6-heli-npc\docs\weapons.csv`（UTF-8 BOM），列：
  `id, class_id, class, category, category_name, slot_indices, sgo_name, name_en, name_ja, level_raw, acquire, col3, col7, col8`。
  `slot_indices` = 这把武器能放进该兵种的哪些槽（`|` 分隔）。
  “空槽”请用各兵种的 None 行，不要写 -1：Ranger `aSupportNone`(305)、WingDiver 核心无空值（120 类必选）、Fencer `hSupportNone0`(866)/`hSupportNone1`(867)、AirRaider `eVehicleNone`(1262)。
  生成脚本见本文末尾。

---

## 3. 插件强制装备的做法

### 3.1 离线建人数据流 [确认]

```
任务脚本 MISSION.AC:
  Main():            PreloadPlayerResource();      // 0x1B8CC0 → 0x59DE50(slot)
  Main_usercode():   CreatePlayer("プレイヤー");     // 0x1B21E0 → 0x1BCC50(mode=0)
                     (CreatePlayer_NoWeapon = 0x1B2200 mode=1, CreatePlayer_InitWeapon = 0x1B21F0 mode=2)
→ 0x1DC450  每个玩家：联机 → 0x591130(index,...) 读装备记录；离线 → 0x591410(slot, transform, mode)
→ 0x595C80  建 PlayerSpec：+0 slot, +8 class(=GS+0x6E90), +0xC 外观, +0x10 护甲数, +0x18 vector<int> 武器（从 GS+0x6E98.. 抄）
→ 0x5A3F90  造人；mode 0 用 spec 里的武器，星级取 GS+0xEB54+id*12；
            mode 2 无视存档，用 SoldierInit 默认武器 + 4 星（0x0404040404040404 与表上限取小）
```

主流程 `MAINSCRIPT.AS::PlayMission_Common()`：`MissionSync("Sync")`（联机装备记录由 `0x78E9A0` 从 GS 打包）→ `SetUsedWeapon()`（`0x70FAF0`，给已装备武器置 flags bit1）→ `Mission()`（跑任务脚本）。重试（`MISSION_RESULT_RETRY`）会再跑一遍 `Mission()`，即再走一次 Preload + CreatePlayer。

### 3.2 推荐 hook 点

1. **`0x59DE50`（PreloadPlayerResource 的每玩家函数，`ecx = slot`）入口**：把目标兵种写进 `GS+0x6E90`，把目标武器写进 `GS+0x6E98+class*0x18+i*4`（只写该兵种的槽数 4/4/5/6）。这样**预加载的模型/武器资源**和之后 `CreatePlayer` 拿到的装备是一致的。[推断：0x59DE50 只被 PreloadPlayerResource 与 `0x225E30` 调用]
   - 不在预加载前改、只在 CreatePlayer 前改也“能跑”，但新武器的 SGO/模型没被预加载，会走运行时同步加载或出问题——**未验证，避免**。
2. 备选：在 `0x591410` 入口再写一次（保险），`mode != 0` 时不处理（脚本要求 NoWeapon / InitWeapon 时尊重脚本）。
3. **还原**：GS 会被结算后的自动存档写回存档（`Result_Offline` 存档），所以测试结束要还原。建议在 `0x591410` 返回后还原（玩家对象已建好，离线任务内没发现再读 GS 装备的路径 [推断]）；重试时 0x59DE50 会再次触发、再写一次。如果只是测试机、不在乎存档，也可以不还原。

最简伪代码（MinHook）：

```cpp
constexpr uint32_t kGameStatus = 0x20B2890, kPreloadPlayer = 0x59DE50;
struct Loadout { int cls; int weapons[6]; int count; };   // count = 4/4/5/6
void Apply(uint8_t* gs, int slot, const Loadout& l) {
    uint8_t* p = gs + slot * 0x3E60;
    *(int*)(p + 0x6E90) = l.cls;
    for (int i = 0; i < l.count; ++i) *(int*)(p + 0x6E98 + l.cls * 0x18 + i * 4) = l.weapons[i];
}
void __fastcall PreloadPlayerHook(int slot) { Apply(*(uint8_t**)(base + kGameStatus), slot, wanted); orig(slot); }
```

校验（插件侧必须做，游戏不做）：`0 <= id < 表行数`；`category(id)` 在该兵种该槽的允许列表里（CSV 的 `slot_indices`）；不写 -1。`0xE1CF0` 用 id 直接当下标，越界/-1 会读野指针（-1 被零扩展成 0xFFFFFFFF）。

### 3.3 拥有/解锁检查

- **离线建人路径不检查拥有**：`0x595C80`/`0x5A3F90`/`0x58F360` 里没有读 `flags bit0`。[确认（就这条路径而言）]
- 未拥有的武器星级字节多半是 0（从没拾取过），所以按 0 星属性造出来 [推断]；要固定属性可同时写 `GS+0xEB54+id*12` 的 8 个字节（上限见 `unlock_weapon` 里 `min(arg,10)`）。这会进存档，记得还原。
- UI 才是限制拥有的地方（列表只列已拥有）；联机还有额外限制：加入房间时 `0xD92B0` 内容拥有检查（`Lobby_WarnWeaponEquip`），以及按难度的护甲/武器等级上限（`UnlockArmorWeaponLimit` 解除，`GS+0x6E58`）。联机不在本次范围内。
- `SetUsedWeapon` 会给强制装备的武器置 bit1（“用过”），进存档；无害但会留痕。

### 3.4 载具 / 支援

- Air Raider：slot 3 = 支援（302 生命/330–334 无人机/331 炸弹），slot 4 = **载具**（306–309、320），与武器同一数组、同一 ID 空间。[确认]
- Ranger：slot 3 同时容纳支援装备（7）和 Ranger 载具（8/9/10，例如 `eVehicle_Tank01` = Blacker E1 在 category 8）。
- WingDiver、Fencer 没有载具槽。
- 呼叫载具在任务内还受“呼叫点数”机制约束，本笔记未涉及。

### 3.5 联机（仅备注）

联机时每个玩家从 `GS+0x14C78+index*0xD4` 的记录建人（`0x591130`），记录由 `0x78E9A0` 在 `MissionSync("Sync")` 时从 GS 打包：记录 `+0 class, +4 外观, +8..+0x1C 武器[6], +0x20 护甲数, +0xA4+i*8 星级`[推断]。要在联机下强制，需在 `0x78E9A0` 之前改 GS，所有人看到的都是改后的装备。

---

## 4. 改存档路线

- 位置：`C:\Users\<user>\AppData\Local\EarthDefenceForce6\SAVE_DATA\<SteamID64>\saveslot00..03\`：`MAIN.GST`（56 KB，即 GameStatus）、`COMMON.CFG`、`DEFP_*.MST`（各模式任务进度）、`TROPHY.DAT`；`SYSTEM\` 下是系统设置；`BACKUP\saveslotNN_<时间戳>` 是游戏自动备份；每个目录都有 `steam_autocloud.vdf`（Steam Cloud 同步）。
- 格式：`MAIN.GST` 头 48 字节无 magic，整文件熵 7.997 bit/byte → **加密（或压缩+加密）**。EDF.dll 里有 AES S-box（`0x18DA810`），导入 `CryptAcquireContextW/CryptGenRandom`；存档任务 `JobSaveData`（vtable `0x17F1098`，run `0x7C1670` / `0x7C1CB0`），校验函数 `0x7C1270`（hasher 对象，返回 `~crc` 形式的 32 位值）。密钥/算法本轮**没追到**。
- 结论：可行性低、风险高（校验、Steam Cloud 覆盖、版本差异），而且存档本身就是 GS 的镜像，内存路线能做的它都能做。**不推荐**。若一定要做：先用插件在内存里设好装备，再让游戏自己存档，相当于“用游戏当存档编辑器”。

---

## 5. 关键地址表

| RVA | 是什么 |
|---|---|
| `0x20B2890` | `GameStatus*` 全局 |
| `0x1B8CC0` | 脚本 `PreloadPlayerResource()` |
| `0x59DE50` | 每玩家预加载（读 GS 兵种+武器，提交资源请求 `0x7A3780`） |
| `0x1B21E0/0x1B2200/0x1B21F0` | 脚本 `CreatePlayer` / `_NoWeapon` / `_InitWeapon`（mode 0/1/2 → `0x1BCC50`） |
| `0x1DC450` | 单个玩家创建（离线/联机分流） |
| `0x591410` | 离线本机建人 `(slot, transform, mode)` |
| `0x591130` | 联机按记录建人 `(index, transform, mode)` |
| `0x595C80` | 从 GS 构造 PlayerSpec |
| `0x5A3F90` | 造人+造武器 |
| `0xE3680` | `(GS+0x130, class)` → 槽数 |
| `0xE34D0` | 兵种数 |
| `0xE2450` | `(GS+0x130, &out, class, i)` → 默认武器名 |
| `0xE1BA0` | `(GS+0x130, name)` → 行号 |
| `0xE1CF0` | `(GS+0x130, &row, id)` → 行 |
| `0x70FAF0` | 脚本 `SetUsedWeapon()` |
| `0x78E9A0` | MissionSync 打包本机装备记录 |
| `0x7DD680` | `UiWeaponSelect` 写入选中武器 |
| `0x7DB520` | `UiSoldierSelect` 写兵种/外观 |
| `0x7DA480` | UI 读回装备（负值改 0） |
| `0xAC650 / 0xAC9E0 / 0xAB2C0` | 开发者命令 set_player_soldier / set_weapon / set_armor_count |
| `0xAF150 / 0xAF330 / 0xAF120` | 开发者命令 unlock_weapon / _one / _index |

---

## 6. weapons.csv 生成方式

（一次性脚本，未落盘、未提交到任何仓库；按下面的逻辑可复现。）


逻辑：`gamefs.read('DEFAULTPACKAGE','CONFIG.SGO')` 用 multislot `tools/sgo.py`（大端 SGO）解析 `SoldierInit` / `SoldierWeaponCategory`；
`WEAPONTABLE.SGO`、`WEAPONTEXT.{EN,JA}.SGO` 用 jaeger `dsgo.py` 解析；category → (class, 槽) 由 SoldierInit 反查。
如果装了改 WeaponTable 的 mod，应改读 `Mods\WEAPON\WEAPONTABLE.SGO` / `WEAPONTEXT.*` 重新生成（行号会变）。

## 7. 未验证 / 待实测

1. 只在 CreatePlayer 前改、不经预加载时会发生什么（同步加载还是失败）。
2. 未拥有、0 星的武器实际属性是否正常。
3. `0x591410` 返回后立即还原 GS，任务内是否还有路径读 GS 装备（HUD、结算画面的“使用武器”等）。
4. 外观 `+0x6E94` 跨兵种的取值是否需要一并调整（UI 换兵种时会同时写）。
