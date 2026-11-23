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
3. 快照整块写回：`memcpy([this+0x7F0] ?: GS+0x6E68, this+0x800, 0xDCE8)`（`0x8BD7E3..0x8BD7FE`）。**H**（代码）/ **M**（`+0x7F0` 何时非空：弱引用 `+0x7F8` 指向的另一份 GS 块，推测是分屏 2P 或别处打开界面时的目标；HQ 离线路径为空，写回 GS）
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
4. 写的兵种是该玩家当前兵种时，照 §4 广播消息 4；UI System 指针为空时不新建（原版会新建；插件不替游戏造 UI 系统），记一行日志。
5. 不存档（§3.6）。

## 6. 未知 / 未做

- 武器属性（伤害、弹数、换弹、射程、射速）：原版说明栏由 `0xD8A90` 按星级算，未逆向，`wlist::CompareStats` 目前只有等级一项有数据。**L**
- `this+0x7F0` 目标的确切来源（§3.3）；插件始终写 GS，联机房间里成员装备由 `MissionSync`（`0x78E9A0`）从 GS 打包（`docs/loadout-re.md` §6），所以 GS 是联机下也会被同步的那一份。**M**
- 原版在列表里对 NEW / UP 的处理（`0x8B51A0`）与清 NEW 位：插件页面暂不清 NEW。**M**
