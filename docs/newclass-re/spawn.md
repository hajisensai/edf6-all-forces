# 新兵种逆向：玩家对象构造（spawn）

EDF.dll Steam 版 0x678CCB46，地址均为 RVA。只读静态分析（vanilla-code 索引 + pylib 解 SGO），未开游戏。
置信度：[确认] 有汇编 / 数据直接证据；[推断] 有间接证据；[猜测] 未验证。

## 1. 结论先行

- 兵种 → C++ 类的选择**不在代码里**，在数据里：`SoldierInit[class][2][outfit]` 给出战斗 SGO 路径，SGO 的 `xgs_scene_object_class` 键决定实例化哪个工厂（AssultSoldier / PaleWing / Engineer / HeavyArmor）。[确认]
- 所以新兵种 4/5 的对象构造路径是：CONFIG.SGO 的 SoldierInit 加第 5、6 项 → 指向新 SGO（如 `app:/object/zw_vehicler.sgo`、`zw_runeblade.sgo`）→ SGO 里 `xgs_scene_object_class` 写插件注册的新工厂名 → 插件工厂分配内存、调原版 ctor 作底、换克隆 vtable（edf41classes 已有完全相同的先例）。[确认 机制可行；推断 新类整体可用]
- 模型、CAS 动画、布娃娃、HUD 能量槽 / 双持槽、武器槽数、可乘载具掩码都由**玩家 SGO** 决定；类代码只决定行为（移动、飞行、状态机、GetSoldierType）。[确认]
- 主要阻塞不在构造本身，而在「兵种号 ≥4」穿过大量按 4 项定长的表：GS 存档 per-class 数组、约 40 处读 GS+0x6E90、HUD 表 0x81F6F0、slot 83 GetSoldierType 的 18 个调用者、任务脚本 GetPlayerSoldierType。这些属于其它主题（count / equip / online），本文列出与 spawn 直接相关的部分。

## 2. 离线出生调用链 [确认]

`CreatePlayer → 0x1DC450 → 0x591410 → 0x595C80（组 PlayerSpec）→ 0x5A3F90（建对象）`

- 0x591410 只做两件事：调 0x595C80 填 PlayerSpec（+0 slot、+8 class、+0xC outfit、+0x10 armor、+0x18 weapons），再调 0x5A3F90。[确认]
- 联机远端：0x591130 → 0x595960 → 0x591234 → 同一个 0x5A3F90。[确认，见 docs/edf41-outfits.md]
- 预载：0x59DE50 调 0xD8730（包 0xE34E0）→ 0x7A3780 预载模型 SGO；0xE3680 / 0xE1CF0 → 0x7A3780 预载武器。联机预载 0x59DD27。[确认]

### 2.1 0x5A3F90 逐步 [确认]

1. `r12d = [desc+8]` 兵种号。
2. 0x5A3FFC `call 0xE34E0(GS+0x130, class, [desc+0xC] outfit)` → SGO 路径（全 DLL 唯一取战斗模型路径处）。
3. 栈上构造 InitParam，vtable 0x17D2498（RTTI `InitParam@SoldierBase`），desc+0 / +4 → param+0x30 / +0x34。
4. 0x5A4040 `call 0x11945E0(mgr=[0x20B2958], transform, path, &param)`：通用「按 SGO 建场景对象」，读 SGO 的 `xgs_scene_object_class` 找工厂。
5. 0x5A4065 `__RTDynamicCast 0x12DA7AA`，GameObjectBase(TD 0x2006450) → SoldierBase(TD 0x2006428)；失败则中止。**新类必须真继承 SoldierBase 且保留 RTTI**。
6. 设 +0x128 bit0/bit1（本地 / 远端）。
7. 0x5A40CB `0xDE250(class, armor)` 算 HP → +0x2F4、+0x2F8；+0x484 = HP × +0x480 / +0x2EC。
8. `0xE3680(class)` 取武器槽数；模式 2 用 `0xE2450(class,i)` 默认武器 + 4 星。
9. 武器经 0xE1CF0 → 0x58F360 挂上。
10. 末尾 `0x59F410(obj,0,1)`，写武器 +0x8B6 = 1。

### 2.2 CONFIG 访问器 [确认]

| 函数 | 含义 | 备注 |
|---|---|---|
| 0xE34E0 | `SoldierInit[class][2][outfit]` | tables+0x20 是 SoldierInit 节点，项 [2] 在 +0x18；**无越界检查** |
| 0xE3680 | `count(SoldierInit[class][4])`（+0x34） | 武器槽数 |
| 0xE34D0 | `[tables+0x20]+4`，SoldierInit 数组长度 | 20+ 个调用者，菜单循环靠它 |
| 0xDE250 | `0xE3470() + 0xE34A0(class) × armor` | HP |

SoldierInit 每项：`[0] id, [1] 名称键, [2] 各外观战斗 SGO, [3] [HP 底, 每护甲增量], [4] 武器槽, [5] [CustomColor SGO, MENUOBJECT SGO 列表, TypeSelectImage.rab, dds 列表]`。**这就是 per-class 资源表**，在 CONFIG.SGO 里。[确认]

## 3. 四个原版兵种

### 3.1 SGO（pylib 读取）[确认]

| 兵种 | SGO | xgs_scene_object_class | CAS | game_object_vehicle_mask | gauge_setting |
|---|---|---|---|---|---|
| 游击 | p505_ranger.sgo | AssultSoldier | EDF6ArmySoldier.cas | 1 | [0,1] |
| 飞行 | p506_wingdiver.sgo | PaleWing | EDF6_wingDiver.cas | 2 | [1,1] |
| 空降 | p508_airRader.sgo | Engineer | EDF6Engineer.cas | 8 | [0,1] |
| 重装 | p507_fencer.sgo | HeavyArmor | EDF6HeavyArmor.cas | 4 | [0,1] |

SGO 里还有：`animation_model`（mrab/mdb、CAS、blob、efarc）、布娃娃 `.shkt`、`soldier_weapon_slot`、`soldier_sub_weapon_slot`、`soldier_config`、`human_type`（重装为 1）、`pale_wing_*`、`heavy_booster_setting`。
菜单模型来自 `SoldierInit[class][5][1][outfit]`（APP:/MENUOBJECT/...），不是战斗 SGO。

### 3.2 工厂 / 构造 / 大小 [确认]

每个工厂 vtable 的 slot 2 是 create：`operator new(size)`（0x12D85B0）后调 ctor。

| 类 | 工厂 vtable | create | 大小 | ctor | 工厂 maker |
|---|---|---|---|---|---|
| AssultSoldier | 0x17CDED0 | 0x54FE50 | 0x2090 | 0x54FF10 | 0x552FA0 |
| PaleWing | 0x17D0F80 | 0x57C530 | 0x2490 | 0x57CAF0 | 0x584560 |
| Engineer | 0x17CF0B0 | 0x563550 | 0x1F60 | 0x5635E0 | 0x564E90 |
| HeavyArmor | 0x17CF568 | 0x564FB0 | 0x20F0 | 0x565130 | 0x56A290 |

maker 指针列在 .data 0x1F6CBD0..0x1F6CD70 一带 [推断：静态注册表]。插件注册入口 RegisterSceneObjectFactory 0x119AF00（edf41classes 在用）[确认]。

继承链：GameObjectBase(0x545670) → Human(0x56A5C0) → SoldierBase(0x58D020) → 具体类。游击 ctor 链 54FE50 → 54FF10 → 58D020 → 56A5C0 → 545670 → 1189C00。[确认]

### 3.3 vtable [确认]

ctor 先调 0x58D020，再在 +0 / +0x120 / +0x1940 写自己的三张 vtable：

| 类 | +0（94 槽） | +0x120（22 槽） | +0x1940（2 槽） | StateMachine Agent |
|---|---|---|---|---|
| SoldierBase | 0x17D24D8 | 0x17D27D0 | 0x17D2888 | 0x17D2480 |
| AssultSoldier | 0x17CDF28 | 0x17CE220 | 0x17CE2D8 | 0x17CDF10 |
| PaleWing | 0x17D0FF8 | 0x17D12F0 | 0x17D13A8 | 0x17D0FE0 |
| Engineer | 0x17CF100 | 0x17CF3F8 | 0x17CF4B0 | 0x17CF0E8 |
| HeavyArmor | 0x17CF5B8 | 0x17CF8B0 | 0x17CF968 | 0x17CF5A0 |

次 vtable 的 slot 0 是 adjustor thunk（如 0x550414 `sub rcx,0x120; jmp 0x5505B0`，0x550420 对 0x1940 同理），**直接 jmp 到原版删除析构，绕过克隆主表 slot 1**。若插件给对象追加状态需要析构，次表 slot 0 也要换。[确认]

### 3.4 相对 SoldierBase 的覆写（slot: SB / AS / PW / EN / HA）[确认]

- 1 dtor：58F2C0 / 5505B0 / 57E680 / 563B00 / 5661B0
- 4：572DF0 / 550A30 / 580520 / 563DA0 / 572DF0
- 5（每帧 update / 输入）：59ABB0 / 550C30 / 5808B0 / 563DC0 / 567780
- 6：PW 5803D0；7（Think）：PW 580460、HA 567080（AS / EN 用基类 596B50）
- 9（消息）：HA 567250；18：HA 5671B0；31（扣能量）：PW 5804B0（基类 54A2E0）
- 45：AS 550A40、PW 580530；46：AS 5508E0
- 52–55：PW 582E40 / 57FCC0 / 5802A0 / 5803C0
- 60、61：HA 567680 / 567640
- 62：SB 59CC80 / AS 551020 / PW 5817A0 / EN 564170 / HA 567DB0
- 68：AS 552E20、EN 564DF0；69、70、73：PW 584310 / 584290 / 584390
- 74：PW 583240、HA 5692A0；76：AS 552EA0；82：PW 5806B0、HA 567690
- **83（+0x298）GetSoldierType**：SB 0x12DA74A（纯虚）/ AS 550A20 返 0 / PW 580510 返 1 / EN 563D90 返 2 / HA 567240 返 3
- 84：EN 563DB0（jmp 0x59F220）；85：PW 584400；88：HA 566F90
- 89–92：PW 57EE70 / 57EE20 / 57EDD0 / 582DF0；HA 91 = 566290
- +0x120 slot 15：SB 59E210 / AS 5510E0 / PW 581940 / EN 564230 / HA 5682E0

AssultSoldier 与 SoldierBase 差异最小，是最干净的底。

### 3.5 ctor 里按名字查 CAS 动画 [确认 查找；推断 后果]

各 ctor 用 0x11664F0 查 CAS 动画名，找不到返回 -1：
- AS / EN："noWeapon"、"dodge"
- PW：flying_unit、down_recovery、fly_boost_*、ground_boost_*、v_fly_*
- HA：base、base_dash_*、base_high_jump_*、base_down_dash_*
- SoldierBase 读 `soldier_config`，Human 读 `human_type`。

所以底 ctor 决定新 SGO 的 CAS 至少要带哪些动画名；-1 动画播放时是否安全 [推断：多数调用点判 -1，未逐一核]。

## 4. 能力组件挂在哪

- **飞行能量（飞行兵）**：PaleWing 对象内，slot 5 0x5808B0 每帧回能（+0x308 当前、+0x304 上限，+0x2FC==1 紧急充能），slot 31 0x5804B0 扣能。是类代码，不是组件。[确认]
- **HUD 能量槽 / 双持槽**：0x1DC450 在 0x1DC7B4 调 0x7FE5D0(hud, player)；它 dynamic_cast 到 GameObjectBase，从 obj+0x100/+0x110 的 SGO 读 `gauge_setting`（串 0x17F6770），[0] 作为 bool 传 0x7FDA40 → 0x82E170。0x82E170 遍历武器组（+0x1970，步长 0x150，数量 +0x1980）与副武器（+0x1A10）：组 0 有标志 → `app:/ui/lyt_HudEnergyGuage_R.sgo`（0x17FD660），无 → `lyt_HudWeaponGuageR`（0x17FD6B0），组 1 → `lyt_HudWeaponGuageL`（0x17FD6F0）。**HUD 由 SGO 数据驱动，不看兵种号**。[确认 逻辑；推断 语义]
- **双持（重装）**：`soldier_weapon_slot` 在 0x593011、`soldier_sub_weapon_slot` 在 0x593B7B 由 SoldierBase 读；重装的 weapon_r / weapon_l 两组来自 SGO。双持是数据驱动的。[确认]
- **呼叫点数（空降）**：在 RadioContact 类武器里，靠造成伤害回充，不在 Engineer 对象上。[推断]
- 0x1DC450 建完对象后只做 dynamic_cast、0x54ED40 / 0x54EDB0（本地 / 远端）、0x54EE70（队伍），无兵种分支。[确认]

## 5. GetSoldierType（slot 83）调用者与兵种号 ≥4 的风险

共 18 个调用者，与 spawn / HUD 直接相关的：
- 0x5DF770 载具座位兵种掩码：0..3 → bit 1/2/4/8；4/5 所有判断都通过、返回 1 → **能上所有载具**（对载具手正好是想要的，但要显式确认，不能靠偶然）。[确认]
- 0x81F6F0 HUD：按 `param+0x920 + type*0x30` 索引 4 项表；type 4 落在 0x9E0，与载具分支的字段相邻（0x825F2D 写 [rdi+0x9e0]），4/5 会**越界读**。[推断]
- 0x830E40：type==3 → "CenterReloadIcon_Fencer"（0x17FD560）。4/5 走默认。[确认]
- 0x95F4B0、0x599462（SoldierBase 消息处理）→ 0x772950(type)，只特判 1。[推断]
- 脚本 GetPlayerSoldierType 0x1B5030（0x1E91B6 注册），6 个任务脚本在用（如 M000B 915、4148 行）。[确认]
- native 0x3ED 0x222740 返回 vf 0x298。[确认]
- 全 DLL 只有一处 dynamic_cast 到 PaleWing（0x2ACA3F，在 0x2AC7E0 里）；没找到 dynamic_cast 到 AssultSoldier / Engineer / HeavyArmor。即其它地方都按 slot 83 返回值区分兵种。[确认]

另：约 40 条指令读 GS+0x6E90（菜单、MissionSync 等）；GS+0x6E98+class*0x18 是按 4 个兵种定长的装备格。这是存档布局阻塞，归 count / equip 主题。[确认 数量级]

## 6. 推荐方案

### 6.1 对象构造

1. CONFIG.SGO 的 SoldierInit 追加第 5、6 项（不改原版 0..3 项）：战斗 SGO、HP、武器槽、菜单资源都写新文件。
2. 新战斗 SGO `xgs_scene_object_class` 写插件工厂名（如 `ZwVehicler`、`ZwRuneBlade`）。模型、CAS、布娃娃、`gauge_setting`、`soldier_weapon_slot`、`game_object_vehicle_mask` 在 SGO 里定。
3. 插件启动时用 0x119AF00 注册工厂；create 分配 `0x2090 + 自有状态`，调 **AssultSoldier ctor 0x54FF10**，再写三张克隆 vtable（保留 vt[-1] 完整对象定位符，RTTI / dynamic_cast 到 SoldierBase 仍成立，0x5A4065 能过）。
4. 主表需换：1（删除析构，先析构自有状态再调原版）、5（每帧 / 输入，载具手召唤 / 刻印剑士踏阵）、7（Think，NPC 化时）、9（消息）、62、83（GetSoldierType）、按需 4、45/46。次表 +0x120 / +0x1940 的 slot 0 thunk 绕过主表 slot 1，若有自有状态也要换。
5. 刻印剑士的飞行两条路：
   - 底用 PaleWing ctor 0x57CAF0（0x2490）：现成飞行 / 能量，但新 SGO 的 CAS 要带 flying_unit / fly_boost_* / v_fly_* 等动画名、SGO 要带 pale_wing_* 键；`gauge_setting [1,1]` 出能量槽。代价是行为仍然很像飞行兵，与「全新兵种不借原版兵种」有张力。
   - 底用 AssultSoldier，飞行（踏阵）由插件在 slot 5 自己写。更干净，工作量大。
   - 建议：两个新兵种都以 AssultSoldier 为底，飞行自写；PaleWing 只作参考实现。[推断]

### 6.2 slot 83 返回值（需要决策）

- 返回 4/5：语义正确，但 0x81F6F0 HUD 4 项表越界、任务脚本 GetPlayerSoldierType 的分支掉到默认；需要在这些调用点补 4/5 的表项或改成按数据取。
- 返回 0..3：表都不越界，但游戏把它当原版兵种（座位掩码、HUD、脚本、0x772950），违背「全新兵种」。
- 建议返回 4/5，并逐一修 18 个调用者（尤其 0x81F6F0 表扩到 6 项）；0x5DF770 对 4/5 全放行要改成显式掩码。[推断]

### 6.3 联机

0x5A3F90 本地 / 远端共用，远端也按 PlayerSpec.class 查 SoldierInit，所以两端都要装同一份 CONFIG.SGO 与插件工厂；按项目规则拒绝未装插件的成员进房。[确认 共用路径；推断 其余]

## 7. 阻塞

- GS per-class 数组（+0x6E98 起 4×0x18）与存档布局按 4 个兵种定长，兵种号 4/5 会写出界（其它主题）。[确认 布局；推断 后果]
- 0xE34E0 等访问器无越界检查，SoldierInit 没扩项就选 4/5 会读野指针。[确认]
- 0x81F6F0 HUD 表 4 项。[推断]
- 约 40 处读 GS+0x6E90、0xE34D0 的 20+ 调用者（菜单循环）。[确认]
- 底 ctor 的 CAS 动画名依赖：新模型要有兼容的 CAS。[确认]
- 次表 thunk 绕过克隆析构。[确认]
