# 原版选武器界面与「写一个槽」（整备界面武器页）

EDF.dll TimeDateStamp `0x678CCB46`，地址均为 RVA。证据等级：**H** = 反汇编逐条核对且有离线审计（`tests/weapon_equip_native_audit.py`）钉住；**M** = 反汇编读过、推断合理但没有实机验证；**L** = 猜测。全部为静态逆向（2026-11-24），没有开游戏验证。

实现：`src/weapon_equip.{h,cpp}`（游戏侧）、`src/weapon_list_logic.h`（判定与列表的纯逻辑）、`tools/gen_weapon_series.py` → `src/weapon_series.inc`（全部武器行与系列）。签名：`tools/weapon_equip_sigs.py` → `src/weapon_equip_signatures.inc`，不符时插件记 `WEQUIP REFUSED` 并关闭这一块（不打补丁，只调用原生函数）。

## 1. 数据位置

| 内容 | 位置 | 等级 | 证据 |
| --- | --- | --- | --- |
| GS | `*(EDF+0x20B2890)` | H | `docs/loadout-re.md` §1；`0x8BC85A` |
| 当前兵种 | `GS+0x6E90 + player*0x3E60` | H | `docs/loadout-re.md` §1.1；`0x8BB966` 读快照 `+0x828`（= GS 块 `0x6E68+0x28+...` 的同位） |
| 装备 | `GS+0x6E98 + player*0x3E60 + class*0x18 + slot*4`，值是 WeaponTable 行号，-1 空 | H | `docs/loadout-re.md` §1.2 |
| 持有位 | `WeaponRecord(gs,row)` flags bit0（bit1 用过、bit2 NEW、bit3 UP） | H | `docs/loadout-re.md` §8、§8.8（记录可能搬家，一律经 `crew::WeaponRecord`） |
| 武器表 | `cfg=GS+0x130`；`[cfg+0x188]` 为空表示未载入；RowCount `0xE23F0`、FindRow `0xE1BA0`（区分大小写，-1）、GetRow `0xE1CF0`（不查上界） | H | `docs/loadout-re.md` §8.1 |
| 行的显示等级 | Row `+0x20 = max(0, (int)(col4 * 0.25f * 100.0f))`，常量 `0x1765A14`=0.25、`0x1765A68`=100.0 | H | `0xE20B9..0xE20DD`，审计核对两个常量与 rip 相对引用 |
| 等级上限 | `0xD8850(GS)` 返回整数上限（-1 无）；只在字节 `GS+0x14FC8` 非 0 时生效；判定 `(unsigned)level > (unsigned)cap` | H | `0x8BC850..0x8BC879`；`docs/weapon-level-cap-re.md` |
| NEW / UP | 同一记录 flags：`(flags&6)==4` NEW；否则 bit3 UP（bit1 用过不影响 UP）。记录按武器行、不分玩家，1P / 2P 读同一份 | H | §7 |
| 说明栏文字表 | WEAPONTEXT `text_table` 句柄 `GS+0xD0 {doc*, u32 节点 +0xD8}`，为空时 `0xD9350(GS)` 载入 | H | `0xD8ACF..0xD8AE0`；§8 |
| 武器星级 | 记录 `+4` 起 8 字节（`WeaponRecord`，与 flags 同一条） | H | `0xD8A90` 传给 `0xD7CE0`；`weapon_records` 也改写这一处 |

## 2. 原版列表 `HUiHQWeaponSelect`（构造 / 列表构建 `0x8B4280`）

- 打开时把整块 `GS+0x6E68`（`0xDCE8` 字节）拷成快照（`0x8B499A`，插件在 `0x8B49B1` 改调 `UiSnapHook`，见 `docs/loadout-re.md` §8.8），之后只读快照。**H**
- 只列持有（bit0）的行；每项从表行抄类别（`+0x10` → 项 `+0`）与显示等级（`+0x20` → 项 `+4`）（`0x8B50BA..0x8B50F9`）。**H**
- 槽信息 `0xE2450(cfg, out, class, slot)`：`out+0x30` = SoldierInit 槽第 4 项（可重复），存到界面 `+0xE528`；第 5 项 → `out+0x31`。**H**（读法）/ **M**（第 5 项含义）
- 重复规则（`0x8B5145..0x8B517B`）：`+0xE528` 为 0 时，本兵种其它槽装着的行收集在 `+0xE530` 的 vector；某项的行在里面且不等于本槽当前行，项状态 `+0x4F4` = 2（灰掉）。**H**
- 上限谓词 `0x8BC850` 只在联机时挂上（`0x8B43D3..0x8B43F6`）；离线 `GS+0x14FC8` 为 0，同样不封顶。**H**

SoldierInit（CONFIG.SGO，`[槽名, 默认, [类别], 可重复, 支援槽]`，`gen_weapon_series.py SLOTS`，`--names` 与审计对游戏核对）：**H**

| 兵种 | 槽 | 类别 | 可重复 |
| --- | --- | --- | --- |
| 游击兵 0 | 0、1 | 0–6 | 否 |
|  | 2 | 20、23、21、22 | 否 |
|  | 3 | 7、8、9、10 | 否 |
| 飞翼 1 | 0、1 | 100–106 | 否 |
|  | 2 | 110、111、113、112、116、115、114 | 否 |
|  | 3 | 120 | 否 |
| 空袭 2 | 0–2 | 310–314、303、305 | 否 |
|  | 3 | 302、330、332、333、334、331 | 否 |
|  | 4 | 306、307、308、309、320 | 否 |
| 重装 3 | 0–3 | 200–205 | **是** |
|  | 4、5 | 206–209 | 否 |

## 3. 原版确认（`0x8BD6F0(this, evt)`）

1. 选中项 = `list[this+0xE548].items[this+0xE54C]`（每项 0x90 字节）；项 `+0x54 & 6` 非 0 拒绝（跳 `0x8BDA66`）。**H**
2. 项 `+0` 的行号写进快照的槽（`0x8BD7A0 mov [rbx+rdx*4+0x800], r8d`）。**H**
3. 快照整块写回：`memcpy([this+0x7F0] ?: GS+0x6E68, this+0x800, 0xDCE8)`（`0x8BD7E3..0x8BD7FE`）。**H**。
   `+0x7F0` 在原版里始终为空，确认总是写 `GS+0x6E68`（**H**）：
   - 构造 `0x8B4280` 对传入参数 `dynamic_cast` 成 `HUiHQWeaponSelect::InitParam`（RTTI `.?AUInitParam@HUiHQWeaponSelect@ui@@`，
     vtable `0x1805838`，COL `0x1CAA848`），把 InitParam `+0x28` 拷到 `+0x7B0`（玩家）、`+0xE8` 到 `+0x7B4`（槽）、
     `+0xF0 / +0xF8` 的弱引用拷到 `+0x7F0 / +0x7F8`；弱引用为空或 lock 失败时快照取自 `GS+0x6E68`（`0x8B4921`、`0x8B4993`）。
   - InitParam 全程序只有一处构造：`0x8AA250` 里的 `0x8AA2C9`。它把 `+0xF0 / +0xF8` 清零（`movdqa [rbp+0x60], xmm0`）后再没写过，
     只填 `+0x28 = [r15+0x798]`（玩家）与 `+0xE8 = edx`（槽）。1P / 2P 都走这条路径。
   - 所以插件直接写 GS（§5）与原版确认的写入目标相同，联机下成员装备也由 `MissionSync`（`0x78E9A0`）从 GS 打包（`docs/loadout-re.md` §6）。
4. 调 `0x811FC0` 与若干文本 / UI 处理（取弱引用、刷新说明文字）。**M**
5. `0x8BDA4D call 0x8BB920(this, &[this+0x7B0] 玩家, _, &[this+0x7B4] 槽, &行)` 发消息 4；`0x8BDA52` 置 `this+0xE628 = 1`（界面自己的预览标志，`0x8BCBE0` 清掉；**不是**存档标志）；`0x8BDA5C call 0x7E2EB0` 关窗（vtable `+0x38` 再 `+0x40`）。**H**
6. **确认路径上没有存档 / 自动存档 / 脏标记调用**：上面就是全部写操作。装备只在 GS 里，随游戏自己的下一次存档（任务开始 / 返回据点等的 MAIN.GST 存档作业）写盘。**M**（没找到任何调用；「何时存档」没有逐一追）

旧版 `UiWeaponSelect`（`0x7DD680`）直接写 GS（`0x7DD8AB`），再放音效 `0x5010101` 并调 `0x11A4450`。**M**（只作对照，插件不用）

## 4. 消息 4（3D 预览换某槽武器）

- 发送 `0x8BB920`：`this+0xE638 = 行`；载荷 `{player, slot, row, class}`，class 取快照 `+0x828 + player*0x3E60`（即当前兵种）；`0x119D1B0(System, 4, &payload)`，`System = [EDF+0x20B2960] - 0x88`（为空时它会新建；与 `0x881FE0()` 返回的同一个）。**H**
- 调用者：`0x8BB221`（`0x8BAB90` 里，光标移动时预览）、`0x8BCBCA`（`0x8BCB90`）、`0x8BDA4D`（确认）。另有通用发送 `0x8BDF00`（无直接调用者）。**H**
- 接收：`UiHQ_3DModelView` OnMessage `0x93B320`，跳表 `0x93C2E4[id-3]`，id 4 → `0x93B895`（换该槽武器，`docs/room-3d-preview-re.md`）。**H**（跳表）/ **M**（处理细节）

## 5. 插件的写法（`wequip::Equip`）

1. 表就绪、行 < RowCount；GetRow 读该行类别与显示等级；读持有位、上限（`GS+0x14FC8` 为 0 视为 -1）与本兵种 6 槽。
2. `wlist::CheckEquipRow`：槽存在 → 类别在该槽表里 → 本槽已是它（same，不写）→ 持有 → 未超上限（无符号比较）→ 不可重复的槽不与本兵种其它槽重复。与 §2 的原版规则一一对应；离线测试 `weapon_list_logic_test`。
3. ok 才写 `GS+0x6E98+...`（只写这一格，不像原版整块拷回——原版的整块来自同一 GS 块的快照，其余字段不变，效果相同）。
4. `wequip::Equip` 自己**不发**消息 4。插件里消息 4 只有一个发送者 `roompreview::ShowPlayerWeapon`（`src/room_preview.cpp`
   `SendWeapon`）：不经 UI System 广播，直接发给已认领的 `UiHQ_3DModelView`（载荷 `{player, slot, row, 0}`，在视图自己的
   Update 里发；视图正显示别的兵种 / 支援士兵预览时丢弃），每槽一个待发值（同帧多槽，后写的覆盖）。写成功、且写的是该玩家
   当前兵种时由调用方（整备武器页 `WeaponsGame::Equip`、整备方案 `WritePlayer`）调它。H（代码）；视图收到后模型是否立即
   换枪：**M**（§4 接收侧细节 M，未实机）
5. 不存档（§3.6）。
6. 玩家装备 `GS+0x6E98` 与兵种 `GS+0x6E90` 插件里只经 `weapon_equip.cpp` 写：一格用 `Equip`，整备方案切换用
   `SetClassSlots`（一次一个兵种的几槽，规则同上，重复按「写完以后」判，两槽互换也能写；判定是纯逻辑
   `wlist::PlanClassSlots`，离线测试 `weapon_list_logic_test` ClassSlots）与 `SetClass`。**例外**：`src/loadout.cpp`
   （支援士兵借玩家槽生成、任务后还原的临时换装）仍直接读写这两处，不属于整备界面，未并入。H（代码）
7. 签名：原先核对的 UI System 广播 `0x119D1B0` 已不调用，从 `weapon_equip_signatures.inc` 去掉；原版消息 4 发送者
   `0x8BB939` 仍核对（ShowPlayerWeapon 载荷格式的依据），`weapon_equip_native_audit.py` 照旧核对原版确认路径调它。

## 6. 未知 / 未做

- 说明栏的星级条（每种类型的平均星数 / 上限，§8.4）没有做，插件页面只对比文字。
- 属性按武器**当前**星级读（与原版说明栏相同）；没有「满星时」的预测。
- 以上全部为静态逆向 + 离线测试，未实机核对页面显示与原版说明栏逐字一致。**M**
- 整备方案切换现在也按原版规则判等级上限与重复（以前只判找得到 / 持有 / 类别）：超上限或会重复的槽留原值，日志
  `ARMORY the player's loadout switched ... refused by the select rules`。H（代码）
- 3D 预览只能显示玩家**当前兵种**（视图只有一个模型，`kViewShownClass` 不等于要显示的兵种时消息 4 被丢弃）：看别的兵种的槽时
  详情列不留洞、显示文字提示。方案切换改了兵种时，视图是否随 `GS+0x6E90` 重建成新兵种，未知。**L**

## 7. NEW / UP（原版 `0x8B51A0` 列表构建，`0x8B519A..0x8B51D5`）

- 每项的状态从快照 `this+0x84E8 + row*0xC` 读记录 flags（快照基址 `this+0x800` 对应 `GS+0x6E68`，即 `GS+0xEB50` 的记录，
  按武器行、不乘玩家步长，1P / 2P 同一份）：`(flags&6)==4` → 1（NEW）；bit1 为 0 时 bit3 → 4（UP）否则 0；bit1 为 1 时
  `2 + bit3`（用过；带 bit3 仍是 UP）。**H**
- 原版选择界面只读这些位，不清。清除只在出击时的 `SetUsedWeapon`（`0x70FAF0`，经 `0xDF600` / `0xDF620`）。**H**
- 插件：`wlist::BadgeOf(flags)`（NEW 优先，其次 bit3 UP），`wequip::Badge(row)` 经 `crew::WeaponRecord` 读；系列行有一把
  NEW 就标 NEW，否则有 UP 标 UP。只读不写，与原版一致。

## 8. 武器属性（原版说明栏 `0xD8A90`）

### 8.1 数据

- `0xD8A90(GS, row, ...)`：取 `GS+0xD0` 文字表（空则 `0xD9350(GS)` 载入，§1），`row >=` 表项数时不显示；
  `text_table[row] = [名字, 说明, 属性表]`，项数不足 3 不显示属性。**H**
- 属性表每项 `[标签, 格式, 参数...]`；参数是数组时为 `[基础值, 曲线|类型<<2, 星级下标, 最大星, d, 指数, 是否小数]`。**H**
- `0xD7CE0(out16, node*, stars[8])` 按星级算出一个参数：`{f32 值; u32 类型; u8 星; u8 上限; u32 是否小数 @+0xC}`。**H**
- SGO 节点访问：子节点 `0x52DE0`（不查界）、数值 `0x52F50`、子项数 `0x53060`、宽字符串 `0x551E0`、是数组 `0xD91C0`。**H**

### 8.2 格式（`0xD9B80`）

- 节点数 <2 无值；=2 原样输出格式；>2 时格式里 `$d`（d 为数字）换成参数 d（子节点 d+2，经 `0xD7CE0`）；`$` 后跟非数字时两个字符
  都丢掉（`0xD9E04`）。**H**
- 类型 3：在 `0x1767DE0` 的 21 项 `{wchar* 标签, f32 下界}`（S++ 0.0005 … L 1.4、Z 1e7）里 upper_bound 取评级，越界取 Z（`0xD9EC0`）。**H**
- 类型 6 / 7：`60 / (float)round(v)`（射速换算）；是否小数为 0：`"%d"`（`0x1765CE0`）of round(v)；否则 v>=1 `"%.1f"`、
  v>=0.1 `"%.2f"`、其余 `"%.3f"`（`0x1767FC0 / FB0 / FA0`）。round = |x|+0.5 截断、符号跟 x（`0xD9F49`）。**H**

### 8.3 插件

- `wequip::ReadSpec(row)`：照 8.1 的路径读（SEH 内只碰 POD，最多 48 行，超出记日志不显示；每行取前 10 个参数，`$d` 只有一位数字），参数值调原版 `0xD7CE0`
  （与原版同值），曲线取数组的第 2 项（`curve` 方向：0 越大越好，1 越小越好，其余不判）。
- `wlist::FormatSpecLine` / `FormatSpecParam` 复现 8.2（纯逻辑，`weapon_list_logic_test` Specs）；`wlist::CompareSpecs` 按标签配对
  （候选的行序，只在装着那把里有的接在后面），▲▼= 由曲线方向判。页面每行只读一次，`Refresh` 清缓存。
- 签名：上面每个被调函数与 `0xD8AC8`、`0xD9E04`、`0xD9EC0`、`0xD9F49` 的分支进 `weapon_equip_signatures.inc`；
  `weapon_equip_native_audit.py` 核对评级表 21 项（标签与 f32 下界）与四个格式串都与 `kSpecRanks` / 源码一致，且被 `0xD9B80` 引用。

### 8.4 没做

- 说明栏的星级条：原版按类型对参数的星 / 上限求和平均，插件页面不画。
