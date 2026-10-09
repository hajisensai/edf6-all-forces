# EDF4.1 / EDF5 武器、载具、敌人移植进 EDF6 —— 计划与 P1 记录

用户需求（2026-10-10）：把 EDF4.1、EDF5 的武器、载具、怪物全部弄进 EDF6，包括 AI 逻辑；不要求 100%，尽量像；
可以做现代化修复（例：4.1 双足机器人被打后仰幅度过大）；做到含 4.1 战役。

全部是静态分析与离线生成，**没有在游戏里跑过**。可信度：H = 直接读到实物 / 全量数据核对，M = 有证据的推断，L = 推测。

## 0. 分阶段

| 阶段 | 内容 | 状态 |
|---|---|---|
| P0 | `pylib/cpk.py` 文件偏移改为从 0x800 头扇区算（原来误用 TocOffset，读 EDF5 / 4.1 / EDF6 `DX11.cpk` 全部失败） | PR #107 |
| P1 | EDF5 武器：EDF6 缺的 61 把（载具呼叫在内）进 EDF6 武器表 | 本 PR |
| P2 | EDF4.1 武器（827 把里 EDF6 没有的） | 待做 |
| P3 | 资产转换：MDB 0x14→0x20、CAS 动画 0x203/0x200→0x204、物理 SHKT 借壳；解锁 EDF5 诱饵 17 把、3 辆换皮载具、4.1 独有模型 | 待做 |
| P4 | EDF4.1 敌人：插件「别名工厂」借 EDF6 相近类 + 参数转换 + 标志性行为补丁；含现代化修复 | 待做 |
| P5 | EDF4.1 载具 | 待做 |
| P6 | EDF4.1 战役任务包（先验证 4.1 BVM 能否在 EDF6 执行器里跑） | 待做 |

EDF5 的敌人：正式任务用到的 47 种敌方类 EDF6 全有原生类（H），只缺 DM015 移动要塞的 11 个 SGO，不需要补 AI 代码。

## 1. P1 选了哪些（`tools/make_edf5_weapons.py` → `edf5port/weapons.json`）

EDF5 武器表 1187 行里，EDF6 表的日文名或英文名都没有的，81 把（H）。名字比较：NFKC、去空白（EDF5 写 `ニクス  レッドガード`），英文名再按单词集合比（EDF5 的 `Robot Bomb Type D` 就是 EDF6 的 `Type D Robot Bomb`，日文 `ロボットボムＤ型` / `Ｄ型ロボットボム` 词序同样变了）：

- **61 把收录**：
  - 15 把 `edf6`：EDF6 包里有这把武器的 SGO，但 EDF6 的表从不引用（EDF5 DLC 武器、DLC 载具涂装、3 种炮艇请求、轻迫击炮等）。直接用 EDF6 那份文件（开发组已经转好格式、调过平衡、给了新译名），安装时复制成我们的 id。判定：同文件名、同模型，且文件里的名字是这把武器的之一或文件名是 `DLC_`（编号文件名两代之间会复用给别的武器：EDF6 未入表的 `eWeapon182` 是 Power Assist Gun，EDF5 的是 Life Spout Gun，同一个支援装置模型）。
  - 46 把 `edf5`：安装时从玩家本机 EDF5 的 `Root.cpk` 转换（只读原版包，不读 EDF5 的 Mods）。
- **20 把跳过**（资源 EDF6 没有，留到 P3）：纯净诱饵发射器 16 把 + 佩尔·温诱饵 1 把（`e_throw_decoyscreen2_*`），欧米茄·自由骑士、黑掠者 4.1、黑掠者 5（车模 `.mrab` 缺）。资源检查顺着 SGO 引用递归：载具呼叫引用的载具 SGO 在 EDF6 里，但换皮车的那份再引用的车模不在。`app:/sound/adx/<bank>.acb` 是游戏目录 `SOUND/PC/<BANK>.ACB` 的散文件；`app:/weapon/icon/none` 不是文件，是 EDF6 支援装备都在用的「无图标」标记。

## 2. 格式转换（`pylib/edf5port.py`、`pylib/mab_legacy.py`）

EDF5 武器 SGO 是 v0x102、类型化数值；EDF6 是 DSGO（数值全为 double）。对两边同名同参数的武器逐字段比对得到的规则：

1. **星级曲线**（`AmmoCount`、`AmmoDamage`、`AmmoExplosion`、`AmmoSpeed`、`FireAccuracy`、`FireBurstInterval`、`FireCount`、`FireInterval`、`LockonRange`、`LockonTime`、`ReloadTime`，以及 `EnergyChargeRequire[0]`、`ExtPrams[0]`）从 6 个元素变成 7 个：第 7 个 = EDF5 存的基值是浮点则 1.0、整数则 0.0。SGO 曲线 765/765、菜单文本曲线 847/847 符合（H）。DSGO 全存 double，EDF6 用这一位记住原值是不是整数。其它 6 元素数组（`ShellCase` 参数等）在 EDF6 里仍是 6 个，所以只按字段名转，不按长度猜。
2. **空的 `SecondaryFire_Parameter []` → `[0.0]`**：EDF6 武器文件 1833 个没有一个是空列表，EDF5 有 929 个（H）。
3. **MAB 块**（`animation_model[2]`）：头 0x08 从 0x03 变 0x83，块内偏移从「相对块开头」改成「相对持有它的 8/16 字节结构」，字符串区空隙清零。EDF6 包里 2487 个 MAB 全是 0x83、EDF5 1652 个全是 0x03（H）。转换器对 1318 对两边同名块：592 对逐字节一致，其余 726 对逐个解码比对，全部是 EDF6 改了内容（定位点坐标 416、名字 191、记录 / 轨道数 114、键值 5），0 对编码差异、0 报错（H）。4.1 的 1153 个块同样是 0x03，能全部转换（H）。
4. **`Weapon_Sub`**：EDF6 支援槽、载具槽的武器全是 `Weapon_Sub`（EDF5 是 `Weapon_BasicShoot`）。转换 = 改类名 + `custom_parameter` 从列表变成命名字典（取同分类 EDF6 武器的字典，动画用 EDF5 的 `[0]`，除非该分类统一播 `vehicle_call`；速度用 EDF5 的 `[3]`）。投掷类转 `Weapon_Sub` 要改弹道物理（弹速、体积、投掷向量都变了），不做：投掷武器保留原类放主武器槽（EDF5 本来就在主武器槽）。
5. **`name.sc`**：补简体中文名（繁中经 OpenCC t2s）。
6. **`AmmoDamageAttribute`**（EDF6 独有的对护盾伤害倍率）：按「分类 + 弹种」家族的众数补（实弹狙击枪 31/35 是 1.5、飞翔兵激光全部 2.0），收录的里只有 3 把实弹狙击枪得到 1.5。

**金标准验证（H）**：那 15 把 `edf6` 武器就是开发组亲手把 EDF5 版转成 EDF6 的产物。把 EDF5 原版过一遍上述转换，与 EDF6 文件逐字段（含 MAB 字节）比对：轻型卡车整套 `Weapon_Sub` 转换 70 个字段全同；其余只差开发组的平衡改动（3 种炮艇请求的装填时间与 MAB、尼克斯两款涂装的 `ReloadInit`、外骨骼的一个数值）。两把飞翔兵投掷武器开发组改成了 `Weapon_Sub`，我们的转换按设计拒绝（`tools/selftest.py` `edf5_weapons_match_developers`）。

## 3. 分类与表行

- EDF6 没有槽位收的分类：304（AR 哨戒炮 / 甲虫 / D 型机器人炸弹，EDF5 在武器槽 1–3）→ 305 `Weapon_Engineer_Special`（同样在武器槽 1–3）；107（飞翔兵投掷）→ 105（主武器槽）；108（飞翔兵核心）→ 120。
- `Weapon_Sub` 不在支援 / 载具分类时，归到同兵种里装同弹种 `Weapon_Sub` 最多的支援分类（EDF6 把 Volcanic Cracker、Star Burst 做成 `Weapon_Sub`，于是进 114 `Weapon_Pale_SupportSpecial`）。
- 表行 9 列：`[id, app:/weapon/<id>.sgo, 分类, 1.0, 等级, 0（掉落获得）, EDF5 的星级上限, 模板的第 7 列, 0（不挂 EDF6 DLC 包）]`。EDF5 第 7 列（0/1/2 = EX 武器包）对应 EDF6 第 8 列，EDF6 用第 3 列 = 3 标记武器包；移植武器一律不挂 EDF6 的包（否则没买 EDF6 DLC 的玩家看不到）。EDF6 第 7 列含义未明（载具全 0、AR 呼叫一半一半），照抄模板（同分类、同类、同弹种优先、等级最近的 EDF6 武器）。
- 获得方式：和原版武器一样按等级随任务掉落。插件的 `GrantCalls` 只发 `EDF6VC_CALL_*`，不发这些。

## 4. 安装（`tools/edf5_weapons.py` + `tools/call_weapons.py`）

移植武器是共享武器表里的第二组「本方行」，和载具呼叫同一个事务、同一份清单（`Mods/.edf6vc_calls.json`）：

- 已有的行按 id 原位更新；新行接在表尾，先载具呼叫后移植武器，按登记表顺序。登记表顺序冻结（`edf5_weapons.RELEASED`，selftest 校验已发布的 id 仍是前缀）。
- **每台机器的行一样**：不管能不能生成，61 把每把都占一行（`plan_rows` 总是规划全部），所以同一版本的安装在有没有 EDF5 的机器上行号完全一致（联机按行号交换装备）。没装 EDF5 时：`edf6` 来源的 15 把照装；`edf5` 来源的行是占位行（模板武器的原版行，名字标「需要 EDF5」），以后装上 EDF5 再安装就变成真武器；之前装好的真行（Mods 里有它的 SGO）原样保留。
- 卸载：和载具呼叫一样变成占位行（模板武器的原版行，id `EDF6VC_RETIRED_E5_*`），保住行号；SGO 删除。`--delete-rows` 只删表尾连续的本方行。

## 5. 未验证 / 风险

- 没在游戏里跑过：菜单显示、掉落、装备、开火、召唤载具都待实机。重点看：转成 `Weapon_Sub` 的 9 个 AR 支援装置和 6 个载具呼叫、305 分类里的哨戒炮和甲虫（EDF6 里 305 原本只有激光引导类）、游骑兵主武器槽里的投掷手雷 MG13J（EDF6 的游骑兵手雷都在辅助槽、都是 `Weapon_Sub`；AR 的 310 迫击炮在 EDF6 里本来就是投掷类，不算风险）。
- EDF6 表第 7 列含义未明，照抄模板。
- EDF6 第 6 列（星级上限）沿用 EDF5 的值，EDF5 DLC 武器是空表，与 EDF6 的 DLC 武器一致。
