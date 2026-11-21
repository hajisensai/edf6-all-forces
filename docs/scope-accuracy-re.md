# 开镜 / 不开镜精度差分：逆向笔记

EDF.dll（Steam，TimeDateStamp `0x678CCB46`），地址都是 RVA。只做了静态逆向（capstone + pefile，`tools/edfre.py`），**没有开游戏实测**。
置信度：H = 读代码确认，M = 推断 / 旁证，L = 猜测。实现见 `src/scope_spread*.{h,cpp}`，需求见
`docs/feedback-2026-11-21-scope-accuracy.md`。下面的 H 级地址全部由 `tests/scope_spread_native_audit.py` 钉住。

## 1. 开镜状态与触发

- 武器字段（H）：`+0x690` SecondaryFire_Type，`1` = 有镜；`+0x780` 开镜标志（byte）；`+0x784` 当前倍率。
  `.text` 里读 `+0x780` 的只有士兵每帧 `0x59ABB0`（`0x59AE81`、`0x59AEB3`）和开关 `0x696310`（`0x696322`）；写它的是加载器
  `0x68EBB9`、复位 `0x69009C`、`0x69633A`。人物汇总倍率 `human+0x1D0`（`0x59B209` 每帧取各武器 `+0x784` 的最大值），
  镜头 FOV = π/4 ÷ 它（`docs/zoom-re.md`）。
- 触发输入（H）：意图块的 `d72+k` 是第 k 组 WeaponSet 的副开火（开镜）——`0x59AE42 cmp byte [r14+rbx+0xD72],0` 与扳机
  `0x59ADE9 [r14+rbx+0xD70]` 用同一个 r14。按下时还要：不在座位上（`+0x1550`）、`+0x2E8==0`、`edx=+0x690==1`
  （`0x59AE7C`）。这一帧先对另一把已开镜的武器调强制关镜 `0x695F10`，再置 `weapon+0x13B=1`（`0x59AEF1`）；武器每帧
  `0x6934F0 → 0x698570 → 0x696310` 翻转 `+0x780`，开镜 `+0x784 = SecondaryFire_Parameter`，关镜回 1.0。
  写 d72 的是输入映射 `0x570757`（`0x5707FA/0x5708E8/0x5709FB/0x570AB7`，d73 在 `0x570A09/0x570AC5`），坐车时
  `0x572DF0` 在 `0x573B4C/0x573B59` 清零。`docs/npc-ai-design.md §3.1` 里原标「未知」的 d72/d73 就是它。
- 插件的 PiP 镜（`src/infantryscope.cpp`、`docs/scope-pip-re.md`）只读 `+0x690/+0x780/+0x784` 与 `human+0x1D0`，只在镜头步把 FOV
  写回 π/4，不动武器状态（H）。所以 `InfantryScope` 三种模式下 `weapon+0x780` 都可靠表示「开着镜」，本功能直接读它，
  不依赖 infantryscope.cpp。

## 2. FireAccuracy 怎样变成散布

- 每发路径（H）：本机开火 `0x690CC0 → 0x696FD0`（各武器类共用的单发入口，heli.cpp 的 `MedicShotHook` 已挂在这里）
  `→ 0x6976CA call 0x691560`（每发射击矩阵，唯一调用点）`→ 0x691AFA movss xmm3,[rdi+0xE14]` / `0x691B02 mulss xmm3,[rdi+0x378]`
  `→ 0x691B10 call 0x4E820`。`0x4E820` 的 xmm2 是下界 0（`0x691ACC xorps`），xmm3 是上界；用武器种子 `+0xBC8` 的 LCG 在
  `[0, E14 × FireAccuracy]` 里均匀取极角，即散布锥半角（弧度）。`0x691560` 不读镜头、准星或开镜状态。
- **原版没有开镜精度修正**（H）：读 `+0x780` 的只有 §1 那几处，散布路径不读它。
- 其它读写 `+0x378` 的地方：
  - 加载器 `0x68CF1F`（H）。
  - `0x6967C0` 按标志位重写 `+0x378`、`+0x368`、`+0x89C` 等（`0x696938/0x69697F`，H 写入 / M 语义：星级或强化修正）。它的调用者
    `0x69C1A3…0x6A7A06` 都**不在** `0x696FD0` 的直接调用树里（从 `0x696FD0` 起 6 层直接调用、138 个函数里没有它；
    其中 `0x6A7830/0x6A78A0/0x6A7920` 是 `Weapon_PreChargeShoot` 的 vtable 第 31/5/35 槽，不是单发路径）（H 直接调用 / M 间接调用）。
  - `0x767150` 在 `0x7672CD` 拿 `+0x378` 与 0.01 比较，决定联机用计数模式还是全量模式。它唯一的调用者 `0x691CEB` 在 `0x691BC0` 里，
    `0x691BC0` 唯一的调用者是武器初始化 `0x68EBDE`（H）。所以**每发临时改 `+0x378` 不会改变联机同步模式**；但不能常驻改。
  - `0x2CDC60` 菜单数值显示（M）；`0x5584EF` 格式化字符串（L）。
- `weapon+0xE14`（H）：全镜像唯一写入点 `0x69DB11`（蓄力类每帧 `E14 = base(+0x15AC) + (1−base)·pow(蓄力比, +0x15B0)`），
  唯一读取点 `0x691AFA`。静态没找到初始化（`0x680000..0x6B0000` 里没有覆盖 `+0xE14` 的宽写入）；launcher 日志里 VehicleShoot 是 1.0（M）。
  实现按相对倍率做（只在 E14、FireAccuracy 都为正时改 E14 = 目标锥 / FireAccuracy），不依赖它的初值。

## 3. 「有镜」与「狙击枪」的判定

- 有镜：`weapon+0x690 == 1`（H）。
- 狙击枪：武器对象上没有武器表行号（`docs/boarding-re.md` 同结论，M），SGO 参数也分不出来：狙击枪与步枪同为 `Weapon_BasicShoot`；
  FireAccuracy 为 0 的狙击枪 19 把（Ranger MR Fang ×4，WD Closed Laser / Monster / Cosmic ×15）；插件现有的 LookFor 判据会把 WD 的
  Pale_Long 21 把判成 combat、Fencer 的炮 17 把判成 marksman。所以用武器表的 category。Root.cpk 1564 行里有镜（type 1）的分布（数据 H）：
  - Ranger：SniperRifle（cat 2）36、AssaultRifle（0）18、RocketLauncher 21、MissleLauncher 4、Grenade 3、Shotgun 3、Special 7；
  - WingDiver：Pale_Long（cat 104）27；
  - Fencer：Heavy_Cannon（204）23；
  - AirRaider：Call_Gunship / Call_Missile / Call_Satellite / Special 共 112，其中 97 把 FireAccuracy=0。

  因此「狙击枪」= category ∈ {2, 104}；Fencer 与空袭兵的有镜武器按「其它有镜武器」处理。移植武器：EDF5 狙击枪 category 2，
  EDF4.1 狙击枪 2、WD 长射程（4.1 的 category 14）登记成 104（`edf5port/weapons.json`、`edf41port/weapons.json`，`tests/scope_spread_guard.py` 核对）。
- 拿 category 的点（H）：玩家建人 `0x5A3F90` 的装备循环里，`0x5A4330 mov esi,[r13+rbx*4]` 取武器表行号，`0x5A4335` 取
  `*(EDF+0x20B2890)`（GameStatus）`+0x130` 作表，`0x5A434A` 调 GetRow `0xE1CF0`；`0x5A4484 call 0x58F360`（rcx=human r14，
  rdx=路径 wstring，r8=星级），调用前的字节 `4c8d45f8 488d5500 498bce e8d7aefeff`。从 `0x5A4335` 到调用，esi / rsi 没有被写
  （审计逐条指令核对），所以调用时 esi 还是行号。`0x58F360` 先在 `0x58F3CA` 读旧个数 `+0x1960`，把武器追加进 `human+0x1950`
  （H 追加 / M 下标：实现只在调用后个数正好多 1 时取 `[旧个数]`）。NPC 走另一个调用点 `0x5944A9`（support_soldier.cpp 已挂），
  不经过这里；`0x58F360` 只有这两个调用者（H）。GetRow 返回行的 `+0x10` 是 category（`docs/loadout-re.md §8.1`；GetRow 在
  `0xE1D4F` 给它填初值，H）。RowCount `0xE23F0`。

## 4. 联机与 NPC

- 本机开火与重放（H）：`0x696FD0` 第 5 个参数是 replay。只有本机开火 `0x690D47` 传 0；远端重放（计数模式 `0x694887`、全量模式
  `0x692A12`、`0x6904EA/0x6905F6`）都传 1。`0x690CC0` 还会查 `weapon+0x120`（持有者）的 NetworkObject，远端副本不走本机开火（`0x690CFD`）。
- 散布在射手本机算（H）：全量模式发的是锥前矩阵 `muzzle+0x50` 与种子位，接收端用自己的 `E14 × 378` 重新算锥；计数模式由接收端自己补发。
  打敌人的命中只看开火机器的弹道（`docs/fp-soldier-re.md §2.3` / damage.md，M）。所以只在射手本机改：
  - 射手本机：自己的子弹按新锥飞、按新锥判命中；
  - 远端：看到的是原版散布的重放（画面）；友伤由被打方按重放判，会有一点不一致（M）。
- NPC 走同一条 `0x696FD0 / 0x691560`，replay 也是 0，区分只能靠持有者：`weapon+0x120` 是士兵（H/M）；本机玩家用
  `edf::IsPlayer`（`human[0x354]` 非零、有手柄对象 `+0x340`、`+0x128 bit0` 为 0 即不是远端玩家）。原版 AI 不写 d72 开镜（M）。
- 设置走房主：`host_settings_logic.h` 的房间字升到版本 2，加 `s=<开关>,<狙击不开镜倍率>,<狙击下限>,<其它不开镜>,<其它开镜>`
  （万分之一为单位的整数，避开小数点的区域设置）与特性位 `kFeatScopeSpread`。客人用房主的值（房主没发字时用本机 ini），
  房间菜单显示房主的设置与版本不同的成员。

## 5. 实现（`src/scope_spread.cpp`）

1. 安装：核对 `0x5A4479` 的 16 字节与 `0x691AFA` 的 16 字节，装射击入口钩子（`InstallShotEntry`），在 `0x5A4484` 附近放 stub
   `mov r9d,esi; mov rax,MakeHook; jmp rax` 并把调用改到 stub。任何一步不符写一行 `SCOPESPREAD REFUSED …`，功能不装，不退回别的判据。
2. `MakeHook(human, path, stars, row)`：记旧 `+0x1960`，调原 `0x58F360`，个数正好多 1 时取新武器，在表已加载且 `row < RowCount`
   时用 GetRow 取 category，记进插件表（64 项，按武器指针覆盖）；日志 `SCOPESPREAD weapon %p row %d cat %d sniper %d …`；
   查表失败 `SCOPESPREAD row %d out of table`，这把武器保持原版。表不在任务开始清：玩家创建与 `MissionStart`（预加载钩子）的先后
   不固定，改为每发核对「持有者就是记录的 human、且武器仍在它的 `+0x1950` 列表里」。
3. 每发（`MedicShotHook` 里 `medicShotNext` 前后，`FirstPersonAimShot` 同样的「改了再还原」）：replay == 0、`+0x690 == 1`、
   持有者是本机玩家且仍持有它、在表里、设置开着时，按 `scope_spread_logic.h` 算目标锥，`Plan` 决定写法（两项都正只写 E14，
   否则 E14=1、FireAccuracy=目标锥），原函数返回后原样写回。每把武器开镜状态变化时打一行 `SCOPESPREAD %p zoom %d cone a -> b`。
4. 备选（未用）：在 `0x691AFA..0x691B09` 放 shim 直接算 xmm3，不写内存；要保存 rcx、rdx、xmm0..xmm2 等活寄存器，只有发现
   `0x696FD0` 调用树里另有 `+0x378 / +0xE14` 的读者时才值得换（目前没有，见 §2）。

## 6. 未决

- `weapon+0xE14` 在步兵武器上的初值（静态只见蓄力类的 `0x69DB11` 写它），需要实机日志确认是 1.0；实现不依赖它。
- `0x6967C0` 的间接调用（vtable）是否可能在一发之内发生：直接调用树里没有，间接调用静态没穷尽（M）。
- 插件里没有任务中途重建玩家武器的功能（outfit41 只换模型，loadout 在建人前改 GS）；以后加了要另接记录点。
- HUD 准星是固定美术模型，不显示不开镜时的实际散布锥。
