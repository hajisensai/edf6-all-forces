# 新兵种（载具手 4 / 刻印剑士 5）静态逆向：motion（模型、动作集、近战、移动）

适用版本：EDF.dll Steam 版（TimeDateStamp 0x678CCB46），文中地址均为 RVA。本文只做了静态分析，没有开游戏。

置信度标记：
- [确认]：看过汇编或数据；
- [推断]：从代码结构推出来，没有实机验证；
- [猜测]：只是方向。

委托方提供的线索「玩家近战走 0x6C7CF0 近战 helper」已被证伪，见 §2.1。

## 0. 结论速览

1. **兵种由什么决定**：模型 SGO 里的 `xgs_scene_object_class`，加上该 SGO 的 `animation_model` 里的 CAS 动作集。建人和预载都按「兵种号 + 外观号」查 CONFIG.SGO 的 `SoldierInit`。这张表只有 0..3 四项，所有读取点都不查边界 [确认]。
2. **近战武器没有自己的命中判定代码**：玩家的近战武器（剑兵的刀、锤、枪）都靠 CAS 事件驱动 [确认]。
   - CAS 动画里的数据事件「武器」先到 SoldierBase 0x590880，再转给武器第 23 槽（vtable+0xB8），按事件参数 `name` 分发。
   - `name` 为「発射」时调 0x690BB0，生成 AmmoClass 子弹（SlashWave / ShockWave / EfsBullet / PileBunker），伤害由子弹负责。
   - `name` 为「キャンセル」时打开连段取消窗口。
3. **移动参数在哪**：不在兵种 SGO 里，而在装备的 Weapon_Accessory 的 custom_parameter（格式为 [[属性号, 值], …]）里 [确认]。
   - 各兵种的刷新函数用 0x592940（浮点）、0x592A70（整型）、0x592B80（向量）取值，写进士兵字段。
   - 基础跳跃速度是代码常量 (0,10,0)，位置 0x5669A8。
   - 剑兵冲刺和推进跳用属性 300..305。
   - 飞翼飞行和能量用属性 200..210。
4. **以哪个原版类为底**：
   - 刻印剑士最适合以 HeavyArmor（剑兵）C++ 类为底。原因：它已有「左右手武器组 → 0x2003 → 冲刺 0x569D50 / 推进跳 0x566BE0→0x569810→0x569A40」这条链，也有「ブースター」CAS 事件；刀、锤、枪的武器类（Weapon_Swing / Weapon_ImpactHammer / Weapon_PileBanker）原生就是给它写的。
   - 载具手以 AssultSoldier 或 Engineer 为底，因为它们的 CAS 有翻滚和摩托驾驶座状态。
   - 两者都只借底层能力；兵种号、模型、CAS、武器表全部新做 [推断]。

## 1. 新模型与新动作集：怎么登记、怎么预载

### 1.1 兵种描述表（CONFIG.SGO `SoldierInit`）[确认，数据]

每项的字段依次为：
- id
- 名字键
- 4 个外观的战斗 SGO
- [基础装甲, 系数]
- 槽列表 [槽名, 默认武器, 允许分类, ?, ?]
- [CustomColor SGO, 4 个菜单 SGO, TypeSelectImage.rab, dds 列表]

原版只有 4 项。

读取点如下（取自 docs/loadout-re.md，已复核）：

| 地址 | 作用 |
|---|---|
| 0xE34E0 | `SoldierInit[class][2][outfit]`，返回战斗 SGO 路径，不查边界 |
| 0xE3680 | 槽数 |
| 0xE34D0 | 兵种数 |
| 0xD8730 | 预载路径 |
| 0xE3420 | HQ 3D 视图路径 |

以下位置也都按 4 个兵种排布，兵种号直接用 4、5 会越界 [确认]：
- GS+0x6E90：兵种号；
- GS+0x6E94：外观号；
- GS+0x6E98：按兵种排的 6 格武器；
- 颜色块：按 (outfit + class*4) 索引。

### 1.2 建人 0x5A3F90 [确认]

1. 调 0xE34E0(GS+0x130, desc+8 兵种, desc+0xC 外观)，得到 SGO 路径。
2. 调 0x11945E0(DAT_20B2958, …, path, InitParam vtbl 0x17D2498) 生成对象。工厂按 SGO 的 `xgs_scene_object_class` 字符串选择 C++ 类。
3. `__RTDynamicCast` 到 SoldierBase。
4. 写 +0x128 远端 / 本机位。
5. 0xDE250 取装甲，写 +0x2F4 / +0x2F8。
6. 0xE3680 取槽数，0xE1CF0 取武器行，再经 0x58F360 逐把生成武器。

整个 dll 里只有 0x2AC7E0（判 PaleWing，疑似拾取）按具体兵种类做 dynamic_cast，其余兵种差异全走虚函数或 GS 兵种号 [确认]。

### 1.3 预载 0x59DE50 [确认]

- 流程：先把 0xD8730(GS, slot, class) 得到的 SGO 路径提交给 0x7A3780(DAT_20B29A8, path, 2)，再逐槽提交武器路径。
- 0x7A3780 只是往加载命令队列写一条 {2, type, param4, len, wchar 路径}，路径长度上限 0x400。
- SGO 依赖的 mrab / cas / shkt / efarc 由 SGO 加载器递归带出 [推断]。旁证：4.1 外观在 2026-10-18 实机跑通过「预载换路径 0x59DE91 + 建人换路径 0x5A3FFC」（docs/edf41-outfits.md）。
- 联机预载在 0x59DC90，换路径点 0x59DD27；HQ 视图在 0x93B426 / 0x93BD03 [确认，引自 edf41-outfits，地址已复核]。

### 1.4 模型 SGO 必须带的字段 [确认，数据，对照 P505 / P506 / P507 / P508]

- `xgs_scene_object_class`：取值为 AssultSoldier / PaleWing / HeavyArmor / Engineer 之一。
- `animation_model`：[[mrab, mdb, lod 表], CAS 路径, MAB blob, 0, {efarc}]。四个原版 CAS 是 EDF6ArmySoldier.cas、EDF6_wingDiver.cas、EDF6HeavyArmor.cas、EDF6Engineer.cas。
- `soldier_weapon_slot`：剑兵是双持槽 weapon_r / weapon_l，后缀 "_r" / "_l"。Weapon_Swing 会把这个后缀拼进动画名。
- 共有字段：`human_*` 系列、ragdoll shkt、`appeal_animation`、`game_object_vehicle_mask`（剑兵为 4）。
- 剑兵专有：`heavy_booster_setting`，含 4 个 MAB 名（ブースターL1/R1/L2/R2）。
- 飞翼专有：`pale_wing_hair_setting`、`gauge_setting [1,1]`。

### 1.5 CAS 动作集的硬约束 [确认]

| 解析函数 | 查找对象 | 名字缺失时的后果 |
|---|---|---|
| 0x1166FB0 | 播放器（player） | 崩溃 |
| 0x1167170 | 变量 | 返回 -1，setter 0x116DE10 拿 -1 去写，造成越界写 |
| 0x11664F0(casctl, playerIdx, name) | 动画（mcanm） | 返回 -1 |

- 武器在构造时就按名字解析动画：
  - ImpactHammer 在 0x69ECA0 解析，查不到时打印 "Weapon_ImpactHammer( charge animation not found : %ls)"（0x17E43F0），然后拿 -1 继续跑；
  - Swing 在 0x6AEFF0 解析。
- 因此新 CAS 必须覆盖两类名字：基类（选作底层的那个原版类）引用的全部播放器和变量名，以及新兵种所有可装武器引用的动画名。
- EDF6 只接受 CAS 0x204 / CANM 0x300。仓库里的 pylib/cas_graft.py、pylib/cas_legacy.py 能把原版的必需条目补进新 CAS。

### 1.6 新类怎么挂（建议）[推断]

有两种做法：
- **方案 A（简单）**：新 SGO 的 `xgs_scene_object_class` 仍写 HeavyArmor / AssultSoldier，沿用原版类实现。差异全部靠新 SGO、新 CAS、新武器表和插件逻辑。
- **方案 B（能改虚函数）**：参照 edf41-classes，由插件注册新工厂名，调原版 ctor 后换成克隆的 vtable，用于覆盖个别槽，例如 Think（第 7 槽：SoldierBase 0x596B50，HeavyArmor 0x567080）和第 23 槽事件。

两种方案都不与原版兵种号冲突：新兵种号由插件新增，不顶替 0..3。

## 2. 玩家近战：谁驱动、伤害怎么判

### 2.1 0x6C7CF0 这套近战 helper 只给敌人用 [确认]

0x6C7CF0 的调用点：

| 调用点 | 所在函数 | 类 |
|---|---|---|
| 0x39C67B | 0x39C3F0 | EDF6_Merman |
| 0x3ABA10 | 0x3AB910 | EDF6_FlyingDyno |
| 0x4411E1 | 0x441030 | Humanoid_BigGreyBoss |
| 0x4B54F9 | 0x4B5230 | MonsterBase |
| 0x60174D | 0x601420 | Vehicle501_FortressRobo |

- 0x6C8140 / 0x6C8840 的调用点也只在这几个类里。
- 0x6C8A50 的内部逻辑：
  - 用 std::map<wstring, region> 管理判定区域；
  - 按骨骼取世界坐标（0x6BB420），对本帧位移做扫掠；
  - 调 0x542490 判定，带命中去重表；
  - 读 GS+0x40（推测是联机标志）。
- 玩家武器类不调用这套 helper。要给玩家用，只能由插件自己喂区域数据 [推断：可行，但没有先例]。

### 2.2 原版玩家近战链 [确认]

1. **CAS 动画触发事件**。动画里的数据命令触发 SoldierBase 用 0x116BE30 注册的三个事件，处理函数都是 0x590880：

   | 事件名 | 字符串地址 | 注册点 |
   |---|---|---|
   | 「武器」 | 0x17D28D0 | 0x58D86C |
   | 「補助装備使用」 | 0x17D28D8 | 0x58D9B8 |
   | 「アクションキャンセル」 | 0x17D28E8 | 0x58DB3E |

2. **0x590880 找到对应武器**：
   - 若 [+0x1970]+0x140 非空，置 +0x139=1；
   - 否则按事件所属播放器（[+0x1420]，或某武器集的播放器）和参数 "weapon_set"（0x17D2B60）找到武器；
   - 武器还要满足 +0x13E、+0xEC4 非 0，且 +0xED4 等于 param_3[1]；
   - 找到后调武器 vtable+0xB8（第 23 槽）。
3. **第 23 槽按参数 "name"（0x17B66B8）分发**。ImpactHammer 的实现是 0x69FF00，Swing 是 0x6AFA00：

   | "name" 取值 | ImpactHammer（0x69FF00） | Swing（0x6AFA00） |
   |---|---|---|
   | 「発射」 | 调 0x690BB0（共用 TryFire），按 AmmoClass 生成子弹 | 同左 |
   | 「キャンセル」 | 调 0x68FDE0(this,1)，置 +0x15A0=1 | 调 0x68FDE0，置 +0x165C=1、+0x1654=0x5A（90 帧窗口） |
   | 「旋回不可」 | 置 +0x15A0=0 | 无 |
   | 其他 | 打印 unknown event | 打印 unknown event |

   0x68FDE0 经 [this+0x120] 状态机的 vtable +0x50 / +0x48 / +0x58 发消息 0x2000。
4. **伤害全部交给子弹**：
   - Force Blade 用 SlashWaveBullet01Exp；
   - Vulcan Hammer 用 ShockWaveBullet01 / 02Exp；
   - Power Blade 用 EfsBullet；
   - Flashing Spear 走 PileBanker。

   出刀帧和连段取消帧都写在士兵 CAS 动画里，dll 里没有。

### 2.3 武器类与参数 [确认，数据]

**Weapon_ImpactHammer**（vtable 0x17E42A0，ctor 0x69ECA0）
- custom_parameter 结构：["hammer_charge", ?, ?, [[蓄力帧阈值, 动画名, …, 爆炸弹类], …], 音效]。
- 按蓄力分级对应不同动画：hammer_swing / hammer_turnImpact / hammer_knockH / hammer_jumpKnock / hammer_KnockRoller。
- 音效：フェンサーハンマー発動、チャージLv1–3。

**Weapon_Swing**（vtable 0x17E5950，ctor 0x6AEFF0）
- 支持多段条目：每条 0xC0 字节，存放在 +0x2C6 的 vector 里；另读 "is_semiauto"。
- 原版用法：

  | 武器 | 条目 |
  |---|---|
  | Power Blade 1（hWeapon027） | 原生三段连击 sword_thrush / sword_thrush2 / sword_thrush3 |
  | Blasthole Spear | pile_swing1… 和 pile_swingH1… |
  | Blast Twin Spears | pile_dashH1 ×2（冲刺突刺） |
  | Spine Driver | pile_swing2 |

**武器 InitParam 由 0x58F360 构建**：

| InitParam 字段 | 内容 |
|---|---|
| +0x30 | 持有者 |
| +0x38 | 士兵+0x900 |
| +0x48 | 士兵+0xB90（CAS 控制器） |
| +0x50 | [士兵+0x1420]（主播放器） |
| +0x40 / +0x54 | 来自槽表 |

构建后用 0x11945E0 生成武器，cast 到 WeaponBase，写 +0x214 [确认 0x58F360 主体；字段语义为推断]。

## 3. 跳跃与飞行参数

### 3.1 取值机制 [确认]

- 0x592940(soldier, id, 默认值, 合并模式)，合并模式 0 取首个、1 取大、2 取小、3 求和、4 求积。
- 0x592940 的查找过程：
  1. 遍历槽表 +0x1A00 / +0x1A10；
  2. 经 +0x1950 找到武器；
  3. dynamic_cast 到 Weapon_Accessory；
  4. 用 0x699380(acc, &out, id) 在 custom_parameter 的 [[id, 值]] 里查值。
- 0x592A70 是整型版，0x592B80 是向量版。
- 通用刷新函数是 SoldierBase 0x599A30：属性 3 写 +0x1A98，后面还有一串 +0x1A9C..+0x1AA8 的连乘合并。各兵种的刷新函数都先调它。

### 3.2 地面跳跃 [确认]

- HumanBase 在 0x5669A8..0x5669C4 写 walk+0x30 = (0, 10, 0)，再调 0x11B9880 Detach。这个值是常量，不来自 SGO 或装备（引自 gravity-re.md §3.2.3，已复核）。
- 着地与腾空：+0x711 == 2 表示着地；+0x1264 是腾空帧数，在 0x573F85 计数（引自 fp-soldier-re.md §3.1）。

### 3.3 剑兵冲刺与推进跳 [确认，引自 npc-mobility-re.md §4，地址已复核]

**刷新函数 0x567690**（先调 0x599A30）读取的属性：

| 属性 | 字段 | 含义 |
|---|---|---|
| 302 | +0x2064 / +0x2068 | 推进跳次数 |
| 0x131 | +0x206C | 推进跳水平倍率 |
| 300 | +0x2078 / +0x207C | 冲刺次数，默认 2 |
| 301 | +0x2074 | 连冲间隔，默认 0.5 s |
| 305 | — | 倍率 |

**输入链**：
1. 0x59AE42 处理武器组按键；
2. 0x698570 按 SecondaryFire_Type 发 0x2003；
3. 第 9 槽 0x567250 处理该消息：kind 0 = 推进跳，kind 1 = 冲刺。

**冲刺**：状态 0x569D50，+0x5D0 = 0xA38；初速 60，每帧 ×0.95，计 90 帧。

**推进跳**：0x566BE0 → 起跳 0x569810 → 飞行 0x569A40。
- 推力：推了摇杆时水平 9×+0x206C、竖直 12；没推摇杆时为 (0, 25.2, 0)。
- 进入空中时速度压到 42 以内，之后每帧 ×0.98。

**「ブースター」CAS 事件**（注册点 0x5655A6，事件名 0x17CFB90，处理函数 0x566890）：读参数 "time"（0x179E8C0，默认 0x3C），写各推进器的 +0x3EC / +0x3F0 / +0x3F4（喷口特效时长）。

### 3.4 飞翼飞行与能量 [确认 地址与属性号；字段含义为推断]

刷新函数 0x5806B0（先调 0x599A30）读取的属性：

| 属性 | 字段 | 说明 |
|---|---|---|
| 0xCA | +0x2398 | ×DAT_176A3B8 |
| 0xCB | +0x23A0 | |
| 0xCC | +0x239C | |
| 200（向量） | +0x23B0..+0x23BC | 经 0x592B80 读取 |
| 0xD2 | +0x2370 | |
| 0xCE | +0x2384 | |
| — | +0x304 / +0x308 | 能量上限，经 0x5927C0 |
| 0xD0 | +0x2374 | 回充 |
| 0xD1 | +0x2378 | 紧急回充 |
| 0xC9 | +0x2380 | |
| 0xCD | +0x237C | |

- 原版 Turbo Core 的写法：[200, [1, 1.25, 1]]、[201, 1.05]。
- 状态函数挂在 Agent +0x2448 下：0x5834B0（+0x5D0 = 0x25），以及 0x583820 / 0x583D60（+0x5D0 = 0xE39，分空中和地面）。哪个是推进、哪个是冲刺还没逐一确认（L）。

### 3.5 游侠冲刺 [确认 代码；语义为推断]

- 0x5508E0（第 46 槽）：处于冲刺状态 0x552040 / 0x5528C0 时，返回 1 + (速度倍率 − 1) × 属性 0x68。
- 0x552040 读属性 100 / 0x65 / 0x66。

## 4. 刻印剑士五个动作怎么借原版底层

| 动作 | 借用的原版底层（只当实现手段） | 需要新做的 | 置信 |
|---|---|---|---|
| 连斩 | Weapon_Swing 多段条目（参考 sword_thrush 1/2/3）；用 CAS「武器 / 発射」定出刀帧，「キャンセル」开 90 帧连段窗口 | 新 CAS 动画及其数据事件、新斩击弹 | 机制 [确认] / 套用 [推断] |
| 突刺 | Weapon_Swing 冲刺条目（参考 pile_dashH1），或剑兵冲刺状态 0x569D50 加一次「発射」 | 新动画、新弹 | [推断] |
| 下砸 | ImpactHammer 的空中档（参考 hammer_jumpKnock），配合推进跳飞行状态 0x569A40 | 新冲击波弹 | [推断] |
| 居合 | ImpactHammer 蓄力分级：帧阈值 → 动画 → 爆炸弹（参考 Dynamo Blade） | 新动画、新弹 | 机制 [确认] |
| 踏阵 | 剑兵推进跳链（属性 302 次数、0x131 倍率、0x566BE0），按 kind 0 发起；以太槽由插件维护，起跳前扣以太，并由插件回补 +0x2068 | 插件以太槽、联机同步 | [推断] |
| 光痕持续伤害 | 子弹的 AmmoAlive 和持续判定 | 弹种参数 | [推断] |
| 回旋刃（M3） | 没有现成的回旋子弹 | 新弹种，或由插件驱动子弹 | [猜测] |

载具手用 AssultSoldier / Engineer 为底：只有 ArmySoldier / Engineer 的 CAS 带摩托驾驶座和翻滚状态（docs/all-classes-ride-re.md）[确认，引自]。

## 5. 风险与阻碍

1. **兵种数组只有 4 个兵种的空间**：SoldierInit、颜色块、GS 各兵种数组、TypeSelect 都是如此，而且读取点不查边界 [确认]。新兵种号有两种处理方式，这是工作量最大的一块：
   - 插件把所有读取点重定向：0xE34E0 / 0xD8730 / 0xE3420 / 0xE3680 / 0xE34D0 / 颜色块 / GS 数组 / 联机打包 0x78E9A0；
   - 扩表，并搬迁 GS 数组。
2. **CAS 名字缺失没有任何校验**：缺播放器会崩，缺变量会越界写，缺动画会拿到 -1。新 CAS 必须做离线完整性检查，对照基类和武器要求的名字清单 [确认]。
3. **新动作需要编辑 CAS 数据事件**：近战连段的出刀帧、取消帧全在 CAS 数据事件里，做新动作必须能编辑 CANM 0x300 的数据命令。仓库目前只有 graft / legacy 转换工具，没有新建事件的工具 [推断]。
4. **联机**：建人走 CreateOnlinePlayerObject@SoldierBase（lambda vtable 0x17D3020）和联机预载 0x59DC90，这两处也要同样换路径。兵种号和外观经成员属性同步，要求全员同版本 [推断，联机建人未逐行读]。
5. **剑兵状态语义未定**：Fencer 4 个状态函数的语义只有 L 级置信，需要实机记录一次 [确认待定]。
