# 新兵种资源槽与 HUD 逆向笔记（gauge）

对象：刻印剑士（兵种 5）的「以太槽」，载具手（兵种 4）的「后勤点」。
EDF.dll Steam 0x678CCB46，地址都是 RVA。这一轮只读，没开游戏，没改代码。
置信度：[确认] = 汇编逐条核过；[推断] = 由调用关系或数据推出来，没逐条核；[猜测] = 没有直接证据。

## 1. 原版能量字段在 GameObject 上，不属于 PaleWing

- [确认] GameObject ctor `0x545670`，在 `0x546272..0x54628D` 写入：
  - +0x2FC = 0（过热 / 紧急充能标志，dword）
  - +0x300、+0x304、+0x308 = 100.0f（+0x304 是上限，+0x308 是当前值）
- [确认] 通用加能 `0x5478A0` 的指令序列是 `addss [rcx+0x308]; minss [rcx+0x304]; maxss 0; store`。调用方是 SupportUnitBullet01 slot1 `0x2AC928`（能量补给弹）。
- [确认] 通用扣能 `0x54A2E0`。唯一调用点 `0x5804EC`，也就是 PaleWing 的 slot 31 `0x5804B0`。这一层会把扣能量乘以 +0x2384（参数 0xCE）。
- [确认] 士兵类里只有 PaleWing 的代码写 +0x2FC / +0x308：`0x580374`（slot 54）、`0x58037F`、`0x5807CE`、`0x580DD8`。
- 结论：所有士兵对象都带这三个字段，默认值 100/100/0，只是其他兵种没有代码去动它们。

## 2. PaleWing 的能量逻辑（作对照，不直接复用）

- [确认] slot 82 `0x5806B0` 负责装载参数。它通过 `0x592940(soldier,id)` 读装备 / SGO 参数，用 `0x5927C0` 取上限，然后写入：

  | 字段 | 含义 | 来源 |
  |---|---|---|
  | +0x304、+0x308 | 上限、当前值 | max |
  | +0x2374 | 常规充能 / 帧 | 参数 0xD0 × scale |
  | +0x2378 | 紧急充能 / 帧 | 参数 0xD1 × (max / const) |
  | +0x237C | 飞行耗能 / 帧 | 参数 0xCD × scale |
  | +0x2380 | 冲刺耗能 | 参数 0xC9 |
  | +0x2384 | 耗能倍率 | 参数 0xCE |

- [确认] 每帧逻辑在 slot 5 `0x5808B0` 的 `0x580D3A..0x580DD8`：
  1. 若 +0x20B0 > 0：先减 1，再通过 vtable+0xF8（slot 31）扣 +0x237C。
  2. 若 +0x2FC == 1（紧急状态）：加 +0x2378；满了就清掉 +0x2FC 和 +0x20B0。
  3. 否则，若字节 +0x2388 置位：加 +0x2374。
  4. 最后 clamp 到 [0, max]，写回 +0x308。
- [确认] 飞行函数 `0x57EF10`：
  - +0x2FC == 1 时取消飞行（清 +0x1EF0 等，状态机 `0x7A8CD0` 切到 4）。
  - 否则起飞时 +0x20B0 = 45；+0x1EF1 为 0 时保持 ≥20，否则保持 ≥1。
  - 设置点：`0x57D4D9`、`0x583894`、`0x583DD4`。
- [确认] +0x2388 = 1 的写入点：`0x57CCC8`、`0x5836E1`、`0x583CF8`（都是冲刺的出口）。没找到清零的地方。
- [确认] 冲刺扣能：slot 31 `0x5804B0` → `0x54A2E0`，扣 +0x2380。扣到 0 时 +0x2FC 置 1（见 docs/npc-mobility-re.md §3.3）。
- [确认] 能量武器充能（docs/stock-payload-re.md §3）：`0x693E34` 在 EnergyChargeRequire[1] ≥ 0.0011 时，经 owner 的 vtable slot 31 扣能。非 PaleWing 的士兵，slot 31 不一定会扣 +0x308 [推断]。

## 3. 原版能量 HUD 与兵种无关，只看士兵 SGO 的 gauge_setting

- [确认] 字符串：

  | 字符串 | 地址 |
  |---|---|
  | `TextWingEnergy` | 0x17FD320 |
  | `WingChargeGuage` | 0x17FD340 |
  | `bar_energy` | 0x17FD360 |
  | `bar_WingCharge` | 0x17FD378 |
  | `EnergyGuage_Base` | 0x17FD4E0 |
  | `%.0f` | 0x17FD508 |
  | `WingChargeGuage_Base` | 0x17FD518 |
  | `app:/ui/lyt_HudEnergyGuage_R.sgo` | 0x17FD660 |
  | WeaponGuageR / WeaponGuageL / SubWeaponGuageR 布局 | 0x17FD6B0 / 0x17FD6F0 / 0x17FD730 |

- [确认] `HuiHudWeaponObject`（vtable 0x17FD208，ctor `0x82E170`，工厂 `0x7FDA40`）由士兵 HUD 构建函数 `0x7FE5D0` 创建，调用点 `0x7FE931`。
- [确认] 构建函数读士兵 SGO（soldier+0x100）的参数 `gauge_setting`（字符串 0x17F6770，lea 在 `0x7FE7B6`）。`0x7FE8CA` 测第一个元素，`setne [rbp-0x18]` 把结果写进创建结构的 +0x20。
- [确认] 布局在 `0x82E3E7..0x82E3FC` 选定：
  - 右槽（r12d == 0）且 flag ≠ 0 → `lyt_HudEnergyGuage_R`
  - 右槽且 flag == 0 → WeaponGuageR
  - 左槽 → WeaponGuageL
- [确认] Root.cpk 里各兵种的 gauge_setting：
  - P505_RANGER = [0,1]
  - P506_WINGDIVER = [1,1]
  - P507_FENCER = [0,1]
  - P508_AIRRADER = [0,1]
- [推断] `HUiHudWeapon` ctor `0x82D070` 在 `0x82DB46` / `0x82DC38` 写 `+0xA78 = byte [param+0x40]`，来源推测也是 gauge_setting。`0x8323C8`、`0x83540A` 等处读取 +0xA78。
- [确认] HUiHudWeapon slot 2 `0x831A40` 在 `0x8323B1..0x8323BB` 调 `0x8353D0(this, soldier+0x2E0)`。r13 是士兵，依据是 `0x832385` 处对 +0x1550 的载具判断。`0x8353D0` 里 r15 = soldier+0x2E0：
  - `[r15+0x28] / [r15+0x24]`（即 +0x308 / +0x304）算 bar_WingCharge 的比例：`0x83638A..0x836396` 计算，`0x83646A` 写入。
  - `%.0f` 显示 +0x308（`0x836512`）。
  - `[r15+0x1C] == 1`（即 +0x2FC 紧急状态）的判断在 `0x83606A`、`0x836589`、`0x8365D4`，用来切过热样式。
  - bar_energy 用 this+0x9D4 / +0x9D8 / +0x9DC，是武器充能，与士兵能量无关。
- 结论 [确认]：原版能量槽 HUD 只读士兵 GameObject 的 +0x2FC / +0x304 / +0x308，开关是士兵 SGO 的 gauge_setting[0]，与兵种 ID 和 PaleWing vtable 都无关。新兵种用自己的 SGO 把 gauge_setting 设成 [1,1]，就能得到原版能量槽外观，数值由插件写。
- [猜测] 布局里的标签（TextWingEnergy 之类）多半是图片或文本资源。要改成「以太」字样，得改 lyt sgo 或做一个新布局；不改就显示原版翼装能量的样子。没核实是否只是贴图。

## 4. 原版空降兵 pt 不是士兵资源（后勤点不能沿用）

- [确认]（docs/plan-2026-10-26-map-support-pt.md）pt 记在每把武器上：ReloadType = 2。
  - 每帧 `0x693EAC..0x693F1E` 读团队分 `[[0x20B2978]+0x38] + team*0x38 + 0x30`（float），减去 weapon+0xE80 存的上次值，截断后从 weapon+0xE68 里扣。超过 1500 的部分按 (x−1500)^0.8 + 1500 压缩。
  - 武器生成 `0x68FC85` / `0x69823C` 时记下当前分数。
  - 团队分的写入方仍没找到：搜 imul 0x38 + 0x30 的写入无果；`vanilla.py refs 20b2978` 只列出 40 行，疑似被截断（`0x693EC4` 的读取不在列表里）。
- 这个分数是伤害积分，不是击杀数。插件现有的 SPTS（`src/support_points*.{h,cpp}`、`support_points_net.h`）是房主维护的团队池，在 `kAfterHudPublish` 时 tick。

## 5. 击杀事件来源

1. [确认] 原版消息 0x10000006（docs/kill-counter-re.md）：
   - ApplyDamage `0x547C30` 在 HP ≤ 0 的那一刻给攻击者发这条消息，SoldierBase slot 9 `0x598FD0` 接收。
   - 只有 soldier+0x340（pad 对象）非空才会走 `0x5499F0` → `0xCF640`（成就计数），所以只算本地玩家。
   - 载具武器的 owner 是载具（SetWeaponObject `0x633330`），所以载具击杀到不了士兵。用它做载具手的后勤点会漏掉主要来源，不可用。
2. [确认] 插件已有的 `0x547C30` 头部 detour（`src/damagestats.cpp` 的 DamageHook → Record）：
   - 击杀条件是调用前没死、调用后死了：`!deadBefore && obj[kDead] != 0`（kDead = +0x2E8）。
   - 攻击者来自 GDI+0x10，控制块 +0x8 > 0 表示还活着。
   - 弹体在生成钩（SpawnHook）里打标签，结算期间（Settling）`TagOf` 能按标签找到发射者。载具武器能归到座位上的乘员（见 docs/damage-stats-re.md §5–7）。
   - 例外 [确认，见同文档]：部分部件类会自己写 +0x2E8，要排除部件，只算本体。
3. [推断] 联机时伤害可能只在房主侧真正结算，客人本地的 detour 不一定能看到每次击杀。所以后勤点应由房主判定并下发，和 SPTS 一样。这一点未实测。

## 6. 插件现有 HUD 工具（docs/hud-re.md）

- [确认] GaugeHook 把 `0x8040E0`（HudPlayer_FollowerDurability）里的调用改到 `0x804300`，然后调 `HudDraw`。
- [确认] 方块用 `0xC2FB0(drawer = *(EDF+0x2139A78), ctx, m[16], rgba, 5, xyz, 4, tex = 0)`；文字走游戏字体链。
- [确认] 数据流：游戏线程生成快照（seqlock），绘制线程读。
- [确认] 隐藏原版槽：根节点 +0x1F8 = 0（`stockgauge.cpp`、`stock_armor_hud.h`）。
- [确认] 文本放 `src/hudtext.inc`（四种语言，已有 `overheat` 词条）；离线预览用 `tools/hud_view`。

## 7. 方案

### 以太槽：复用原版字段和 HUD，逻辑归插件
- 新兵种自己的士兵 SGO 设 `gauge_setting = [1,1]`，原版能量槽自然出现（第 3 节）。
- 插件在新兵种克隆的 vtable 里接管 slot 5（每帧更新，或在原 slot 5 之后调）：
  - 落地时（地面标志，候选 +0x1264 [推断]）按参数给 +0x308 充能。
  - 踏阵每一步、R3 七星、M4 坠星砸地时扣 +0x308。
  - 不够时拒绝动作；归零时设 +0x2FC = 1，HUD 自动显示过热样式，满了再清零。
  - 不调用、也不依赖 PaleWing 的 slot 5 / 31 / 82 代码。
- 若刻印剑士要用能量充能型武器，克隆的 slot 31 要实现「扣 +0x308」，否则 `0x693E34` 扣的不是这个槽。
- 参数（上限、落地充能速度、各动作耗能）写进新兵种 SGO 或插件表。不要用 0xCD..0xD1 这几个 PaleWing 参数号的语义。
- 联机：+0x308 / +0x2FC 是本地状态。客人看别人的槽需要同步，可走 plugin_net 新通道，或依赖 Coop 现有的士兵属性同步 [猜测：Coop 是否同步 +0x308 未核实]。

### 后勤点：插件自己的资源、自己画
- 击杀来源：在 damagestats 的 Record 旁加一个击杀订阅。判定用同一套前后状态差；攻击者按弹体标签 / 座位乘员归到载具手玩家。
- 由房主计数并下发：仿 SPTS 那样走 `src/plugin_net.h` 的通道 + 能力位，满了在房主侧触发呼叫替换载具。
- HUD 用 HudDraw 加一条进度条和数字，文本进 hudtext.inc。不要借原版 pt 的武器槽，它是每把武器的、按伤害积分。
- 不用 0x10000006：漏载具击杀，而且依赖 pad 对象。

## 8. 未决问题
- 联机时伤害结算在哪一侧（决定后勤点只能房主计、还是客人也能本地计）。
- 团队分写入方未找到（只影响「改用分数」这一备选）。
- 能量槽布局的字样能否换成「以太」：需要看 lyt_HudEnergyGuage_R.sgo 的结构。
- +0xA78 的来源只是 [推断]；地面标志 +0x1264 是 [推断]。
- 全部未实测。
