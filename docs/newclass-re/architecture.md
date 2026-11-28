# 新兵种 4/5 架构：独立反驳与实现建议

只读静态核对（`vanilla-code/vanilla.py show/refs`，EDF.dll 0x678CCB46，RVA）。没开游戏，没改代码。
置信度：[确认] = 本轮亲自看过汇编；[推断] = 有证据但没逐点核完；[猜测]。

## 1. 逐条核实（本轮亲手看的 8 处）

| # | 命题（来源） | 结论 | 证据 |
|---|---|---|---|
| 1 | GS 每名本机玩家块按 10 个兵种留位（count/equip） vs「GS 各兵种数组只按 4 个兵种开、4/5 越界」（spawn/motion/online 的建议） | **count/equip 对，spawn/motion 错** [确认] | `0xDEDA0`：外层 `r11=2`（两名本机玩家，步长 `0x3E60` @`0xDEF20`）；`0xDEDD5` 写兵种=0、`0xDEDD8` 外观=-1；`0xDEDE4 mov ecx,0xA` 循环 10 次每次 0x18（6 槽 int）；`0xDEE20 lea r9d,[rcx+0xA]` 10 个护甲 int（`r10+0xF8`）；其内 `edx=4`（外观）×`ecx=2`（主/副色）×`0xC4`，起点 `r10+0x120`。每兵种颜色 = 4×2×0xC4 = 0x620；0x120+10×0x620 = 0x3E60，正好排满块。|
| 2 | 「颜色块按 outfit+class*4 索引，所以只够 4 个兵种」（motion） | **错**：`(outfit+class*4)*0x188` = `class*0x620 + outfit*0x188`，「4」是**每兵种 4 个外观**，不是 4 个兵种 [确认，同 #1] | 同上；online 记的 `+class*0x620`（0x78EC53）与此一致 |
| 3 | 打包 0x78E9A0 要「从插件自己的新兵种存档填装备」（online） | **不需要**，class 4/5 的装备 / 颜色 / 护甲就在 GS 预留格里，原版打包按 `class*stride` 直接读到正确数据 [确认（布局）/推断（打包不另做钳制）] | #1 |
| 4 | 兵种数 = SoldierInit 元素数（count） | [确认] | `0xE34D0: mov rax,[rcx+0x20]; mov eax,[rax+4]; ret` |
| 5 | 造人按数据选类，没有按兵种 switch（count/spawn/motion） | [确认] | `0x5A3FF5..0x5A3FFC`：`edx=r12d`（class）、`r8d=[rsi+0xC]`（外观）→ `call 0xE34E0`；`0x5A4040 call 0x11945E0`；`0x5A4048 je 0x5A4571`（建不出就放弃）；`0x5A4065 call 0x12DA7AA`（dynamic_cast，TD `0x2006450`/`0x2006428`）|
| 6 | 名牌表 0x806FE0 只 4 项，class 4/5 堆越界读（online） | **确认，且比原文更硬** [确认] | `0x8047E7 lea rbx,[rdi+0xB0]` → `0x804875 call 0x806FE0(rbx)`，所以 806FE0 的 `[this+8]` = HUD 对象 `+0xB8`。`0x8071EC cmp [rdi+0x10],4` / `0x8071F7 mov ecx,0xC0`（4×0x30）/ `0x8072AC mov [rdi+0x10],4`；`0x807310..0x8073AD` 按下标 0..3 switch 填名字串与 RGB（0 红 / 1 蓝 / 2 绿 / 3 黄，default 白），`cmp esi,4` 结束。读取 `0x8056E2..0x8056EF`：`[rsp+0x60]*0x30 + [rdi+0xB8]`；`[rsp+0x60]` 是 `0x7FFBD0` 的 r8 输出 = `GS+0x14C78+i*0xD4`（`0x7FFDAA`）或 `GS+0x6E90`（`0x7FFE66`）= 兵种。只有 `0x804ECF cmp [rsp+0x60],0; jl` 下界检查。第二张表 `+0xD8`（`[rsp+0x64]`，玩家序号）也是 4 项，与兵种无关。|
| 7 | 0x81F6F0 HUD「param+0x920+type*0x30 的 4 项表」（spawn） | 形状 [确认]，「4 项」与后果 [推断] | `0x81F83E call [rax+0x298]`（GetSoldierType）→ `0x81F844..0x81F855` 算出 `r15 + 0x920 + type*0x30 + rbx*0x10`，`r15 = this`（`0x81F711`）。这是**对象内成员数组**，type 4/5 读到 `+0x9E0/+0xA10` 的相邻成员：不崩，显示错值。表长和谁写 `+0x920` 本轮没找 |
| 8 | 0x5DF770 载具座位掩码：type 4/5 通过所有判断（spawn） | [确认] | `0x5DF7B2..0x5DF806`：bit0/1/2/3 未置时分别拒绝 type 0/1/2/3，其他 type 不受拒。载具手「能上所有载具」正好是想要的；剑士要限制则需插件在此加显式规则 |
| 9 | GameObject 上所有士兵都有能量字段（gauge） | [确认] | `0x546272..0x54628D`：`+0x2FC=0`、`+0x300/+0x304/+0x308=100.0f`（0x42C80000），在 GameObject ctor `0x545670` 内 |
| 10 | 0x70FBD0 PlayCount 按 class 下标 0..3 switch（count） | [确认] | `0x70FC36..0x70FC6E` 四路取前缀，`0x70FC45 jne 0x70FC85` 走无前缀 → 键只剩 "PlayCount"（`0x70FC8B`），4/5 共用一个计数 |

## 2. 证据不足、互相矛盾、会把实现带偏的结论

1. **spawn/motion 的「GS per-class 数组按 4 定长，要全量重定向或搬迁」被 #1 证伪。** 照它做，会白写一整套读取点重定向表（motion 的落地顺序第 1 步就是这个），还会把本来原样可用的原版打包、存档、HQ 快照全部改坏。删掉这一步。
2. **online 建议的「打包从插件新兵种存档填装备」以及任何「插件侧独立整套存档」都不需要**（#3）。和 count 一致：装备 / 护甲 / 颜色 / 通关位直接用 GS / MAIN.GST / DEFP_*.MST 预留格。
3. **「每个模式的 CONFIG.SGO 追加 SoldierInit」（count/equip/spawn）前提没核实。** `mode+0x20` 由 `0xE2AD0` 从「模式包 CONFIG」填（`docs/loadout-re.md` §8.1），但 EDF5 / EDF4.1 / DLC 各模式的 SoldierInit 分别在哪个文件、是不是在加密的 `addon.sgo` 里，没有人查过。`docs/edf5-campaign-plan.md:27` 还记着「护甲倍率等常见 mod 都会整份覆盖 CONFIG.SGO」。文件追加的路线可能在 DLC 模式上做不到，也会被别的 mod 静默盖掉。见 §3.1 我的建议。
4. **新兵种的槽类别不能复用原版类别号**（我从 equip 的发现推出的新风险，[推断]）。`0xE2D51` 把 `map[cat]+0x20 = SoldierInit[class][0]` 写进类别表，后写的覆盖先写的。新兵种的槽要是列出原版类别（比如突击步枪 0xx），这个类别就被改判成 type 4，原版游侠的这类武器会出错。所以新兵种只能用新类别号（4xx/5xx），默认武器也只能是新武器行。空壳兵种不能直接借一把原版游侠的枪。
5. **兵种存档字段 +0x6E90（count）**：count 说「卸载插件后读档会越界」是对的，但漏掉了更常见的触发方式：**Steam 更新 EDF.dll 后插件自己 REFUSED 退出**，原版读到 class 4 一样越界。仓库有先例（`docs/edf41-outfits.md:28`「存档始终只存原版 -1..3」）。另外 +0x6E90 是两名本机玩家各一份（`+p*0x3E60`），两份都要处理。处理方式见 §3.3。不建议另开 sidecar：武器记录 sidecar 已经踩过两份数据不同步的坑。
6. **底层基类选哪个，spawn（两个都用 AssultSoldier）和 motion（剑士用 HeavyArmor）互相矛盾。** HeavyArmor ctor 会把重装的冲刺、推进跳、双持状态整套带进来，剑士最后很可能就是「换了把剑的重装」，正是用户 2026-11-28 否掉的那种。motion 的理由是能复用 302/300 参数、`0x566BE0` 推进跳、`0x569D50` 冲刺，这些状态依赖 HeavyArmor 的字段布局，所以选 AssultSoldier 就复用不了，要自己在 slot 5 写位移。我的建议：两个兵种都以 AssultSoldier（`0x54FF10`，0x2090）为底，位移和资源逻辑写在插件的克隆 vtable 里。这个取舍要在计划里明确写下来。
7. **0x81F6F0（spawn）**：spawn 记成「4/5 会越界读」，实际是对象内越界，不会崩，只会显示错（#7）。它和 0x806FE0（真正的堆越界，#6）严重程度不同，要分开排优先级。
8. **gauge 的「标签仍是翼装能量外观」**：方向可以，但没核实 `lyt_HudEnergyGuage_R` 能不能换字样。如果换不了，就只能接受翼装外观或者插件自己画。这一点影响「不像换皮」，要列为待定，不能当成「可以直接用」。
9. **motion 的「回旋刃没有原版弹种」标了 [猜测]，「CANM 0x300 数据事件编辑工具缺失」是真阻塞**，但只阻塞后期的近战手感，不阻塞空壳兵种。
10. **equip 的 HUiHQSoldierSelect `+0x780` 文字数组和 LYT 第 5、6 个按钮**还没查。这直接决定玩家进不进得了新兵种，属于空壳阶段必须解决的问题，不能留到最后的实机才看。

## 3. 架构建议

### 3.1 引擎层面：扩引擎数组，不另起插件存档

- 新兵种 = SoldierInit 的第 4、5 个元素（type 值 4/5，不借 0..3），GS / 存档用原版预留格（#1）。
- **SoldierInit 怎么加（推荐运行时注入，[推断]，要先核汇编）**：在 `0xE2AD0` 把 SoldierInit 节点指针写进 `mode+0x20` 之后、`0xE2CB5` 建类别表之前，插件把 `mode+0x20` 换成插件内存里的一份副本：原版 0..3 元素原样拷贝，再追加插件生成的 4/5，SGO 内部是自相对偏移（`0xE07D0` 的 `movsxd [x+8]; add` 读法），所以可以搬到插件 buffer 里。SoldierWeaponCategory 同样追加 4xx/5xx。好处：所有模式（本篇 / DLC / EDF5 / 4.1 包 / 测试场）一次覆盖，不受加密 addon.sgo 和别的 mod 整份覆盖 CONFIG.SGO 的影响，数据和插件同版本。备选是 installer 经 manifest 追加文件（`make_edf5_campaign.py` 已经这样管 CONFIG.SGO），但要先查清每个模式的 SoldierInit 在哪个文件。
- 元素 4/5 必须内容齐全（访问器都不查边界）：type、名字键、[2] 战斗 SGO × 4 个外观（4 个外观都指向本兵种自己的 SGO，不能留空）、[3] 颜色、[4] 槽列表（≤6）、[5] 菜单模型、护甲 base/step。`0xE0650/0xE23F0/0xE34A0/0xE15B0` 读哪一格还要补查（不过 E23F0、E0650 按 loadout-re / mission-list-re 是武器表行数和任务行数，大概率和 SoldierInit 无关）。

### 3.2 玩家对象

- 插件用 `0x119AF00` 注册 `ZwVehicler` / `ZwRuneBlade` 工厂：`operator new(0x2090+自有)` → `0x54FF10` → 换三张克隆 vtable（主表 94 槽、`+0x120` 22 槽、`+0x1940` 2 槽，保留 vt[-1] RTTI，以通过 `0x5A4065` 的 dynamic_cast）。主表 slot 83 返回 4/5，slot 1 和两张次表 slot0 的析构 thunk 改成先释放自有状态。
- 新战斗 SGO：`xgs_scene_object_class` = 新工厂名，`gauge_setting`、武器组、CAS 都由 SGO 给出。CAS 至少要有 AssultSoldier ctor 用到的 `noWeapon/dodge` 等名字，否则 `0x1166FB0` 会崩、`0x1167170` 返回 -1 后越界写，这一项要先做离线完整性检查工具。
- 按 type 定长的消费者逐个改：`0x806FE0` 名牌表 4→6（必须）；`0x81F6F0 +0x920` 表；`0xDCDC1` / `0x8930FD` 兵种名；`0x70FC2D` PlayCount 前缀；slot 83 的 18 个调用者逐个过一遍；任务脚本 `GetPlayerSoldierType` 的 default 分支。

### 3.3 存档

- MAIN.GST / DEFP_*.MST 格式和长度不变，新兵种数据都在预留格里。
- 只处理 +0x6E90（两名本机玩家各一份）：在存档拷块 `0x7BCD40`（插件已钩）里，**磁盘副本**的 +0x6E90 写成一个原版兵种作替身（内存里的 GS 不动，联机打包照常发 4/5），真实兵种和替身值写进**同一个文件**里不会被原版读到的预留格（例如兵种 9 的某个装备槽 int，在 count 扫过的无访问区 `0x6EF8..0x6F87` 内）。读档完成（`0x7BE970` 之后）时，只有 +0x6E90 仍等于记下的替身，才恢复成真实兵种（玩家中途用原版改过兵种，就以原版为准）。数据只在一个文件里，原子写，不会出现两份不同步。[推断：要先补查确认没有任何代码访问选定的那一格；兵种 9 以后不准再分配给新兵种]
- 插件里 `kClasses=4` 的地方（player_slots / weapon_page_logic / loadout_preset / outfit41_logic / armory / airstrike）都改成运行时读 `E34D0`/`E3680`。loadout_preset 的预设文件格式要升版本。工具 `ported_weapons.py`（`//100`）和 `gen_weapon_series.py`（SLOTS）改成按表取。

### 3.4 整备界面

- 真正在用的是 HUiHQSoldierSelect（`0x8A8CE0` / `0x8AB310`），它按 `E34D0` 建按钮，武器选择经 `E3680/E2450/E35B0` 取数据，数据都对了就会自动显示。要补的有：`+0x780` 文字数组的来源和长度、LYT 能不能摆下 6 个 ButtonSolder（摆不下就由插件补位置或改 LYT），以及先读懂 `0x8ABB6F` 外观弹窗里的 cmp 3 和 `0x8AC6E0..0x8ADC90` 改兵种写 GS 的部分。
- 兵种名用 SoldierInit 名字键，文本进文本表（5 种语言）。

### 3.5 联机

- MissionSync 直接传真实的 class 4/5（不搞替身加旁路）。原版打包、转发、解包都不检查兵种范围（online 已确认），再加上 #1，装备数据也是对的。
- 全员同版：`kAllForcesRoomProtocol` 26→27，加 SEARCH_TYPE 前缀，房主侧拒绝并在公告里写明所需版本（复用 mod_room 机制）。EDF6Coop 的 `kWeaponClasses` / `kSoldierTypeCount` 改成 6，先发新版，插件随之提高所需 Coop 版本。
- 后勤点、刻印这类 MissionSync 装不下的状态，走新的 plugin_net 通道并加能力位，在 joined 时发全量。房主权威计数，收消息时用 SenderIsPlayer 校验。
- 运行时注入 SoldierInit 时每台机器内容相同，房间指纹里带上新兵种定义的摘要。

## 4. 最小空壳兵种（一把武器）步骤

1. 补查（只读）：每个模式的 SoldierInit 来源；`0xE2AD0` 内写 `mode+0x20` 的指令和建类别表的顺序；`0xE0650/0xE23F0/0xE34A0/0xE15B0` 各读哪一格；HUiHQSoldierSelect 的 `+0x780` 和 LYT；`0x8ABB6F`、`0x8AC6E0..0x8ADC90`；预留格里选来存替身记录的那一格确实没有访问；`0x81F6F0 +0x920` 的写入方和表长。
2. 数据：一条新武器行（新类别 400，例如克隆一把突击步枪参数，起新名字），登记进 SoldierWeaponCategory。SoldierInit[4] 只有 1 个槽，type 4，4 个外观都指向新战斗 SGO（空壳阶段可以临时借游侠的 MRAB/CAS，在计划里标成占位，不当成交付）。
3. 插件：在 `0xE2AD0` 注入 SoldierInit / Category，单元测试检查生成的 SGO 节点能被原版访问器的偏移算法正确解析（离线 C++ 测试，仿 `*_check.cpp`）。
4. 插件：注册 `ZwVehicler` 工厂，用 AssultSoldier ctor 加克隆 vtable，slot 83 返回 4。native audit 测试校验 vtable 槽、ctor 地址、`0x119AF00` 签名。
5. 插件：`0x806FE0` 名牌表扩到 6 项（新增两项的名字和颜色），补兵种名 `0xDCDC1/0x8930FD` 和 PlayCount 前缀。
6. 插件：kClasses 改成运行时取，存档拷块里加 +0x6E90 替身处理和恢复，配离线单元测试（替身、恢复、玩家用原版改过兵种三种情形）。
7. 联机：协议号升到 27；EDF6Coop 的兵种数改成 6（另一个仓库），进房时拒绝旧版。
8. `python tools/check.py` 绿以后，才排一次实机（父会话用 gamequeue 一次采齐）：进兵种选择翻到第 5 个按钮 → 打开武器选择 → 出击（建人、开火、上载具）→ 存档读档 → 卸掉插件读档不崩 → 双开联机看名牌和对方模型。
9. 空壳跑通后再接剑士（同一套流程，type 5），然后做以太槽（gauge_setting 加克隆 slot 5）、后勤点（damagestats 订阅加 plugin_net）、CAS 工具和近战动作。
