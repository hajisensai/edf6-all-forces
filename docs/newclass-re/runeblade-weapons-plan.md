# 刻印剑士（兵种 5，`ZwRuneBlade`）武器：数据已就位，运行时待做

范围：`docs/plan-2026-11-28-zero-worldline-gear.md` §2.2（M1–M5）、§2.3（R1–R4）的武器行与 SGO 数据。接口约定见 `docs/plan-2026-11-28-new-classes-impl.md`（类别 500 / 501 / 502，剑士槽序 slot0 近战 / slot1 远程 / slot2 支援）。

- 数据在 `tools/zw_weapons.py`：`WEAPONS` 末尾追加 8 行（`kind='tuned'`，各带一个 `Tune`），`RELEASED` 记了 16 行这一版。R1 `ZW_RuneCarbine01` 沿用原来的 `carbine` 行。
- 标记与穿透计数的纯逻辑在 `src/runeblade_weapons_logic.h`，单测 `tests/runeblade_weapons_test.cpp`；Python 侧 `tests/runeblade_weapons_test.py` 对表，有游戏时用原版模板构建全部 SGO 和 5 种语言的文本行。
- 置信度：[确认] 已在原版 SGO 或汇编上核过；[推断] 从调用关系或现有代码推出来的；[猜测] 只是方向。
- 本轮没开游戏，也没写钩子。

## 0. 结论

1. **9 件武器都有了行和 SGO**，全部按原版模板改参数（标量，第 4 阶段之前不随星级成长），光痕统一用蓝紫 `RUNE_COLOR (0.9, 0.6, 2.8, 1)`。名字和说明有 5 种语言；模型都是模板的占位，说明里写了「模型暂用 X 代替」。
2. **运行时没做的行设为 acquire 0（`LOCKED`）**，即任何存档都不拥有、装备不上。涉及 M1–M5、R3、R4。原因：
   - 近战模板是剑兵的 `Weapon_ImpactHammer` / `Weapon_Swing`，靠士兵 CAS 里的「武器 / 発射」事件出刀（`motion.md` §2.2）。刻印剑士的身体底层是 AssultSoldier 的构造函数加 ArmySoldier CAS（`plan-…-new-classes-impl.md` §0），CAS 里没有 `hammer_*` / `pile_*` 动画。CAS 缺名字没有任何校验（`motion.md` §5 第 2 条），装上后会拿到 -1 甚至崩。
   - R3 的模板是飞翼的 `Weapon_HomingShoot`，原版靠能量槽充能。`EnergyChargeRequire` 已改成 `[-1, -1]`（不充能），但非 PaleWing 士兵走 slot 31 时会不会出问题没有核过（`gauge.md` §2 [推断]）。
   - R4 的模板是空袭兵的 `Weapon_Throw` 炮击（`ReloadType 2`，按造成的伤害积点装填）。非 Engineer 的士兵能不能正常积点、能不能叫来炮兵，没有核过 [推断]。
   - 运行时做好、实机验过的那件武器，把它的 `acquire` 改回 1 即可；不需要改行序。
3. **R1 晨钟定稿**：去掉「试作型」，说明改成「最多贯穿 2 个敌人；飞行中可射，不耗以太槽」。SGO 仍是 `AmmoIsPenetration 1`，另加穿透标记（`AmmoHitSizeAdjust` = 标记 1）。「2 敌」的计数在 `runeblade_weapons_logic.h` `PierceTable`，**钩子还没接**（见 §2 R1）。在接上之前，晨钟实际会贯穿全部敌人。
4. **R2 萤火可以直接用**（acquire 1）：模板与 R1 同为 Blazer（`LaserBullet02`，Ranger 的 `Weapon_BasicShoot`），AssultSoldier 身体原生就能拿。插射不打断连段要等近战连段做出来。

## 1. 子弹标记

需要在运行时找到某件武器子弹的，把 `AmmoHitSizeAdjust` 写成位模式为 `0x3F802000 | n` 的 float（约 1.0005，命中半径基本不变）。做法和 `barrage.cpp` 的 `kMarkBase` 相同：子弹核心出生时把它抄到 core `+0xA34`（`0x231EEF`，引 `barrage.cpp`）[确认 引]。

| n | 武器 | 运行时靠它做什么 |
|---|---|---|
| 1 | R1 晨钟 | 2 敌穿透上限 |
| 2 | M1 破晓 | 第三段光痕停留 0.5 s |
| 3 | M2 天穹 | 刺中时光阵炸开 |
| 4 | M3 群星 | 切过的敌人留刻印，光环飞回时引爆 |
| 5 | M4 坠星 | 按下砸高度加伤害 |
| 6 | M5 残光 | 表盘刻印 1 s 后一齐爆开 |
| 7 | R4 信标 | 信标落点 |

已占用的标记：thrown drone / 重力枪 / 登车枪都在 `0x3F801000` 以下，barrage 是 `0x3F801000..0x3F801FFF`，Andryusha 是 `0x3F8F4A36/7`。原版所有接近 1.0 的值都恰好是 1.0（`tools/calls.py` `THROW_MARK_BASE` 的扫描结果）。`zw_weapons.build_sgo` 只在模板值恰好为 1.0 时写标记，否则直接报错。R3 的模板是 0.5，所以 R3 不打标记（它的运行时按武器判断，用不着找子弹）。

## 2. 每件武器：数据，以及运行时还要做什么

帧数按 60/s。「伤害」指 SGO 的 `AmmoDamage`；近战的蓄力档位倍率沿用模板 `custom_parameter` 的第 [3] 项 [确认 数据：Force Blade 1/2、Vulcan Hammer 1/5/12、Dynamo Blade 1/2/5]。

### M1 刻印军刀「破晓」 `ZW_RuneSaber01`（500，模板 `hWeapon002` Force Blade）
- 数据：弹 30 / 装填 90 帧 / 伤害 90（满蓄 180）/ 弹速 6 × 寿命 8 = 48 m / 尺寸 2.0 / 蓝紫 / 标记 2。
- 运行时：
  1. 三段连斩：横斩、回斩、上挑。参考 `Weapon_Swing` 的多段条目（Power Blade 的 `sword_thrush` 1/2/3），模板要改成 Swing 类，或者由插件按输入依次发三刀。
  2. 空中急坠下劈。
  3. 第三刀的光痕停 0.5 s：按标记找到第三刀的弹，把它的寿命延到 30 帧，原地持续判定。
  4. 刻印剑士 CAS 里要有这些动画及其「発射 / キャンセル」数据事件。阻塞点是 CANM 0x300 事件编辑工具（`motion.md` §5 第 3 条）。

### M2 以太骑枪「天穹」 `ZW_RuneLance01`（500，模板 `hPileBanker01` Blasthole Spear）
- 数据：弹 12 / 装填 120 / 伤害 700（第二段 875）/ 弹速 10 × 寿命 6 = 60 m / 贯穿 / 蓝紫 / 标记 3。
- 运行时：
  1. 蓄力后沿视线冲刺突刺，地面和空中都能用。参考 Blast Twin Spears 的 `pile_dashH1` 条目，或剑兵冲刺状态 `0x569D50` 加一次「発射」（`motion.md` §4 [推断]）。
  2. 空中刺完接踏阵。
  3. 刺中时炸开光阵（新特效）。

### M3 刻印回旋刃「群星」 `ZW_RuneRing01`（500，模板 `hWeapon002`）
- 数据（占位）：弹 20 / 装填 120 / 伤害 60（满蓄 120）/ 弹速 3 × 寿命 40 = 120 m 的慢速宽刃 / 尺寸 3.0 / 标记 4。
- 运行时：原版没有回旋弹种（`motion.md` §4 [猜测]），这是缺口最大的一件。
  1. 近身双刃乱舞。
  2. 掷出光环：飞出去绕一圈再飞回。由插件逐帧改弹的速度，和 `barrage.cpp` `PathAt` 的写法一样。
  3. 被切过的敌人留刻印，光环回来时依次引爆。
  - 计划在 §3 的第 6 阶段。

### M4 以太战锤「坠星」 `ZW_RuneHammer01`（500，模板 `hWeapon007` Vulcan Hammer）
- 数据：弹 3 / 装填 480 / 伤害 250、1250、3000（三档 1/5/12）/ 近距 40 m / 蓝紫 / 标记 5。
- 运行时：
  1. 地面横扫击退，沿用模板的 `hammer_swingK` 档。
  2. 空中蓄力后垂直下砸，落差越大伤害越高，并耗尽以太槽。参考 ImpactHammer 的空中档 `hammer_jumpKnock`，加上推进跳的飞行状态 `0x569A40`（`motion.md` §4 [推断]）。插件按标记找到冲击波弹，按下砸高度改它的伤害。
  3. 落地时巨大光阵扩散，地面裂痕发光几秒（新特效）。

### M5 刻时居合「残光」 `ZW_RuneIai01`（500，模板 `hWeapon008` Dynamo Blade）
- 数据：弹 3 / 装填 300 / 伤害 600、1200、3000（三档 1/2/5）/ 弹速 12 × 寿命 10 = 120 m / 标记 6。
- 运行时：
  1. 按住进入架势，表盘刻印一格格亮起，用 ImpactHammer 的蓄力分级（`motion.md` §4 机制 [确认]）。
  2. 满蓄拔刀后画面停顿一瞬。
  3. 被切中的敌人身上浮出表盘刻印，约 1 s 后一齐爆开：按标记记下被命中的敌人，60 帧后由房主结算。
  4. 最终关专属，属于第 4 阶段的解锁。

### R1 刻印卡宾枪「晨钟」 `ZW_RuneCarbine01`（501，模板 `aWeapon014` Blazer，已上线行）
- 数据（不变）：弹 5 / 射击间隔 45 / 装填 150 / 伤害 300 / 弹速 30 × 寿命 20 = 600 m / 贯穿 / 蓝紫；新加标记 1。
- 运行时：
  1. 2 敌穿透：`PierceTable::Hit(round, target)` 返回 false 时，把弹的 age 设成 life，让它当场结束（`barrage.cpp` 熄灭集束母弹也是这么做的），并放过这一次命中。
     - 还要找命中点：`LaserBullet02` 每打中一个目标时的回调，找到「这一发 / 这个目标」所在的位置。没核，下一步拿 `vanilla.py refs` 从 LaserBullet02 的 vtable 找命中分发。
  2. 枪口先打出小光阵，命中点留刻印 1 s（新特效）。
  3. 拉栓动作沿用模板，`FireInterval` 45 帧。
  - 联机：每台机器都从同一份 SGO 模拟自己那份子弹，伤害只在射手那台机器结算（和原版子弹一样）。各机器各自计数，结果一致，不需要通道 [推断]。

### R2 刻印左轮「萤火」 `ZW_RuneRevolver01`（501，模板 `aWeapon014`，**acquire 1，可用**）
- 数据：弹 6 / 射击间隔 6（10 发/秒）/ 装填 90 / 伤害 45 / 弹速 12 × 寿命 12 = 144 m / 不贯穿 / 精度 0.01 / 蓝紫。
- 运行时：近战连段中按开火插一枪，不打断连段。这要等连段系统做出来：连段的状态机在开火输入时让 R2 开一枪，不切换武器。另外还需要新特效：拖光尾的萤火弹，命中时爆出小光点。

### R3 光阵浮游炮「七星」 `ZW_RuneStars01`（501，模板 `pWeapon186` Mirage 15）
- 数据：一次齐射 7 发（`FireCount` 7）/ 弹 1 / 装填 90 / 伤害 80 / `EnergyChargeRequire [-1, -1]`（不走原版能量）/ 蓝紫。锁定距离和锁定时间保留模板的星级曲线。
- 运行时：
  1. 按住开火，身边依次浮起最多 7 个光阵，排成北斗七星（新特效）。松手齐射，沿用模板的锁定加追踪。
  2. 耗以太槽：插件在开火时扣以太，槽不够就拒绝开火。以太槽由房主权威维护，走以太槽模块的 plugin_net 通道。
  3. 要核：非 PaleWing 士兵拿 `Weapon_HomingShoot` 时，slot 31 扣能那条路不会出事（`gauge.md` §2）。

### R4 以太炮击信标 `ZW_EtherBeacon01`（502，模板 `eWeapon006` Large Howitzer）
- 数据：`Weapon_Throw` 加 `SmokeCandleBullet01` 信标 / 每次 1 发 / 装填 4000 pt（`ReloadType 2`，按造成的伤害积点，所以次数有限）/ 每发伤害 1000 / 原版炮击 10 发 / 信标烟蓝紫 / 标记 7。
- 第一版能跑通的做法：不接插件，就用原版空袭兵的炮击呼叫。原版呼叫联机本来就同步 [推断]。
- 运行时：走仓库现有的支援呼叫管线。
  1. 插件按标记 7 认出信标弹落地，读出落点。
  2. 把这一轮交给 `barrage.cpp` 的 `BarrageStart`（`strikes.h`），和地图支援栏 `airstrike.cpp SupportStrikeAt` 用的是同一个入口。新增一个 `barrage::Kind`：光柱一排落下；也可以先借 `heavy` 的弹道参数。
     - 联机：`BarrageStart` 已经通过 `barrage_net_protocol.h` BRGS 通知各机器，各机器按同一个种子规划自己那份，伤害只在呼叫方结算。不需要新通道。
  3. 接上之后，把模板的原版炮击关掉：`Ammo_CustomParameter` 的炮击段改成 0 发。
  4. 新特效：信标落地展开的巨大光阵，以及光柱。
  5. 信号枪外观：模板是投掷，要换成手枪式发射的话需要换模板，或者另做 `Weapon_BasicShoot` 发一枚 `SmokeCandleBullet01`。这一项没核。
  - 另外要核：非 Engineer 的士兵能否给 `ReloadType 2` 积点（找 ReloadType 2 的积点写入点）。

## 3. 文本行

统计行按模板文本行的下标写死字符串（`Tune.stats`，值依次替换 `$0` / `$1`）。给某一行写值时，要把该行所有 `$k` 都给全：行里的星级曲线会被丢掉，没给值的 `$k` 会原样显示出来。缺值时 `text_row` 直接报错。没写的行保留模板的曲线，比如 R3 的锁定距离。5 种语言的模板行结构一致，有游戏时 `tests/runeblade_weapons_test.py` 会逐语言核对。

## 4. 缺口汇总（做完一项就把对应行的 acquire 改回 1）

| 缺口 | 武器 | 阻塞 |
|---|---|---|
| 刻印剑士 CAS 的近战动画和数据事件 | M1 M2 M4 M5（M3） | CANM 0x300 事件编辑工具 |
| 回旋弹（插件驱动弹道） | M3 | 新做 |
| 子弹命中回调的钩子（穿透计数、记录命中对象） | R1 M3 M5 | 找 LaserBullet02 / ShockWave 的命中分发 |
| 以太槽扣费 | R3 M4 | 以太槽模块 |
| 信标落点 → BarrageStart，加新 Kind | R4 | barrage.cpp |
| 统一刻印光阵贴图和光痕材质 | 全部 | 新特效 |
| 实机验证：AssultSoldier 身体能拿 HomingShoot / Throw | R3 R4 | 实机 |
