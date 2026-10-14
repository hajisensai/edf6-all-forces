# EDF4.1 / EDF5 武器、载具、敌人移植进 EDF6 —— 计划与 P1 记录

用户需求（2026-10-10）：把 EDF4.1、EDF5 的武器、载具、怪物全部弄进 EDF6，包括 AI 逻辑；不要求 100%，尽量像；
可以做现代化修复（例：4.1 双足机器人被打后仰幅度过大）；做到含 4.1 战役。

全部是静态分析与离线生成，**没有在游戏里跑过**。可信度：H = 直接读到实物 / 全量数据核对，M = 有证据的推断，L = 推测。

## 0. 分阶段

| 阶段 | 内容 | 状态 |
|---|---|---|
| P0 | `pylib/cpk.py` 文件偏移改为从 0x800 头扇区算（原来误用 TocOffset，读 EDF5 / 4.1 / EDF6 `DX11.cpk` 全部失败） | PR #107 |
| P1 | EDF5 武器：EDF6 缺的 61 把（载具呼叫在内）进 EDF6 武器表 | PR #108 |
| P2 | EDF4.1 武器：EDF5、EDF6 都没有的 284 把（见 §6） | 本 PR |
| P3 | 资产转换器（MDB、CAS）+ EDF5 的 3 辆换皮载具（见 §7）；诱饵 17 把待 SHKT 实机确认 | 本 PR |
| P4 | EDF4.1 敌人：第一批 EDF6 还有原类的 15 种已转换（见 §8）；其余借 EDF6 相近类 + 行为补丁；含现代化修复 | 进行中 |
| P5 | EDF4.1 载具呼叫 65 把、空袭 19 把，做成 EDF6 的呼叫（见 §6「呼叫」） | 本 PR |
| P6 | EDF4.1 战役任务包（先验证 4.1 BVM 能否在 EDF6 执行器里跑） | 待做 |

EDF5 的敌人：正式任务用到的 47 种敌方类 EDF6 全有原生类（H），只缺 DM015 移动要塞的 11 个 SGO，不需要补 AI 代码。

## 1. P1 选了哪些（`tools/make_edf5_weapons.py` → `edf5port/weapons.json`）

EDF5 武器表 1187 行里，EDF6 表的日文名或英文名都没有的，81 把（H）。名字比较：NFKC、去空白（EDF5 写 `ニクス  レッドガード`），英文名再按单词集合比（EDF5 的 `Robot Bomb Type D` 就是 EDF6 的 `Type D Robot Bomb`，日文 `ロボットボムＤ型` / `Ｄ型ロボットボム` 词序同样变了）：

- **61 把收录**：
  - 15 把 `edf6`：EDF6 包里有这把武器的 SGO，但 EDF6 的表从不引用（EDF5 DLC 武器、DLC 载具涂装、3 种炮艇请求、轻迫击炮等）。直接用 EDF6 那份文件（开发组已经转好格式、调过平衡、给了新译名），安装时复制成我们的 id。判定：同文件名、同模型，且文件里的名字是这把武器的之一或文件名是 `DLC_`（编号文件名两代之间会复用给别的武器：EDF6 未入表的 `eWeapon182` 是 Power Assist Gun，EDF5 的是 Life Spout Gun，同一个支援装置模型）。
  - 46 把 `edf5`：开发工具从 EDF5 原版包（不读 EDF5 的 Mods）转换，转换结果存进登记表（`weapon`，`dsgo.dump`），安装时直接用，玩家不需要装 EDF5（2026-10-10 用户：「让用户不用装 EDF5 也能正常使用」）。安装时只做依赖 EDF6 的两步：转 `Weapon_Sub`（取 EDF6 模板的 custom_parameter）和按家族补 `AmmoDamageAttribute`。
- **20 把跳过**（资源 EDF6 没有，留到 P3）：纯净诱饵发射器 16 把 + 佩尔·温诱饵 1 把（`e_throw_decoyscreen2_*`），欧米茄·自由骑士、黑掠者 4.1、黑掠者 5（车模 `.mrab` 缺）。资源检查顺着 SGO 引用递归：载具呼叫引用的载具 SGO 在 EDF6 里，但换皮车的那份再引用的车模不在。`app:/sound/adx/<bank>.acb` 是游戏目录 `SOUND/PC/<BANK>.ACB` 的散文件；`app:/weapon/icon/none` 不是文件，是 EDF6 支援装备都在用的「无图标」标记。

## 2. 格式转换（`pylib/edf5port.py`、`pylib/mab_legacy.py`）

EDF5 武器 SGO 是 v0x102、类型化数值；EDF6 是 DSGO（数值全为 double）。对两边同名同参数的武器逐字段比对得到的规则：

1. **星级曲线**（`AmmoCount`、`AmmoDamage`、`AmmoExplosion`、`AmmoSpeed`、`FireAccuracy`、`FireBurstInterval`、`FireCount`、`FireInterval`、`LockonRange`、`LockonTime`、`ReloadTime`，以及 `EnergyChargeRequire[0]`、`ExtPrams[0]`）从 6 个元素变成 7 个：第 7 个 = EDF5 存的基值是浮点则 1.0、整数则 0.0。SGO 曲线 765/765、菜单文本曲线 847/847 符合（H）。DSGO 全存 double，EDF6 用这一位记住原值是不是整数。其它 6 元素数组（`ShellCase` 参数等）在 EDF6 里仍是 6 个，所以只按字段名转，不按长度猜。
2. **空的 `SecondaryFire_Parameter []` → `[0.0]`**：EDF6 武器文件 1833 个没有一个是空列表，EDF5 有 929 个（H）。
3. **MAB 块**（`animation_model[2]`）：头 0x08 从 0x03 变 0x83，块内偏移从「相对块开头」改成「相对持有它的 8/16 字节结构」，字符串区空隙清零。EDF6 包里 2487 个 MAB 全是 0x83、EDF5 1652 个全是 0x03（H）。转换器对 1318 对两边同名块：592 对逐字节一致，其余 726 对逐个解码比对，全部是 EDF6 改了内容（定位点坐标 416、名字 191、记录 / 轨道数 114、键值 5），0 对编码差异、0 报错（H）。4.1 的 1153 个块同样是 0x03，能全部转换（H）。
4. **定位点挂到 EDF6 模型真有的骨骼上**（`edf5port.fit_locators`，安装时按 EDF6 的模型做）：EDF6 重新导出了很多与 4.1 / EDF5 共用文件名的模型，根骨骼 `mdl` 改名成模型名（p_lazer_LAZR01：4.1 `[mdl, p_lazer_LAZR01]`，EDF6 `[p_lazer_LAZR01, polymesh]`）。4.1 的 MAB 还把枪口、握把挂在 `mdl` 上，EDF.dll 找不到这根骨骼就跳过这个枪口，而每次开火都按「已射发数 % 找到的枪口数」轮换枪口（0x69AA20，Weapon_BasicShoot 一族）：一个都没找到时第一枪就整数除零，游戏崩溃（2026-10-10 用户实测：用 4.1 的飞翔兵武器开火即崩，崩溃转储 EDF+0x69AA20 `div [rcx+1E0h]`）。受影响的是 67 把 4.1 武器（飞翔兵的激光枪、长枪、狙击枪、发射器等，加 1 把投掷武器）和 1 把 EDF5 手雷。修法和开发组自己移植这些武器时一样：缺的节点改挂模型唯一的根骨骼，块里别的东西不动（`mab_legacy.mab_set_strings` 按游戏的排法重排字符串区）。核对：EDF6 每把武器的每个定位点都挂在自己模型的骨骼上，每个模型只有一根根骨骼；被改的 68 把里，有 62 把 EDF6 自带同模型、同结构的武器，字符串与开发组的块逐项相同，只差开发组重调的坐标（`tools/selftest.py` `ported_weapon_locators_fit_real`）。没有枪口、或缺节点而模型没有唯一根骨骼的武器不装（`Unsupported`，退回占位行）。被拒绝的武器也不再沿用以前安装留下的 SGO（那是旧转换做出来的），只有缺游戏时才沿用（`call_weapons.stack`）。
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
- **每台机器的行一样**：不管能不能生成，每把都占一行（`plan_rows` 总是规划全部），所以同一版本的安装在任何机器上行号完全一致（联机按行号交换装备）。EDF5 的 61 把都不需要 EDF5；以后需要别的游戏才能生成的武器（4.1 武器、要转换模型的武器），没装那个游戏时行是占位行（模板武器的原版行，名字标「需要 …」），装上后再安装就变成真武器；之前装好的真行（Mods 里有它的文件）原样保留。
- 卸载：和载具呼叫一样变成占位行（模板武器的原版行，id `EDF6VC_RETIRED_E5_*`），保住行号；SGO 删除。`--delete-rows` 只删表尾连续的本方行。

## 5. 未验证 / 风险

- 没在游戏里跑过：菜单显示、掉落、装备、开火、召唤载具都待实机。重点看：转成 `Weapon_Sub` 的 9 个 AR 支援装置和 6 个载具呼叫、305 分类里的哨戒炮和甲虫（EDF6 里 305 原本只有激光引导类）、游骑兵主武器槽里的投掷手雷 MG13J（EDF6 的游骑兵手雷都在辅助槽、都是 `Weapon_Sub`；AR 的 310 迫击炮在 EDF6 里本来就是投掷类，不算风险）。
- EDF6 表第 7 列含义未明，照抄模板。
- EDF6 第 6 列（星级上限）沿用 EDF5 的值，EDF5 DLC 武器是空表，与 EDF6 的 DLC 武器一致。

## 6. P2：EDF4.1 武器（`tools/make_edf41_weapons.py` → `edf41port/weapons.json`）

4.1 武器表 827 行里，EDF5、EDF6 两张表都没有其日文名 / 英文名的 396 把（名字比较时去掉 4.1 的部队后缀：4.1 的「榴弾砲〔砲兵隊〕」就是 EDF6 的「榴弾砲」；EDF5 有、EDF6 没有的归 P1）。收录 284 把，跳过 112 把（收录的和 EDF5 武器一样在开发时转换好存进登记表 `weapon`，玩家不需要装 EDF4.1，2026-10-10 用户：「预生成吧」；`tools/selftest.py` `ported_weapons_registry_current` 守住登记表与当前转换器一致）：

- （P5 起收录）65 把载具呼叫与 19 把攻击机 / 轰炸机呼叫，见下文「呼叫」。

**呼叫（P5，`pylib/edf41calls.py`）**：4.1 的载具呼叫是 `Weapon_Throw`，`Ammo_CustomParameter` 外层与 EDF6 自己的载具呼叫（`Weapon_Sub`）逐字段相同：`[0.05, 180, 120, 1, [运输机, 集装箱, 载具, 设定块], 语音]`；4.1 用 `Transporter401` 投送（EDF6 已没有这个类），EDF6 和 EDF5 用 `v508_transport` / `v509_transportbox`。所以呼叫取 EDF6 模板（呼叫同一 EDF6 载具、空爆兵分类优先、等级最近的那把：类、`custom_parameter`、运输机、语音），换上 4.1 自己的：载具（`VEHICLES`：301 坦克含联动涂装 → v505_tank，406 贝加尔塔 → v504_begaruta / _red / _gold，302 直升机 → v506_heli，408 摩托 → v503_bike，501 巴尔姆 → 移植过来的 4.1 巴尔姆本体 vehicle501_fortressrobo（P4 已整套搬进 Mods，模板取巴尔加呼叫 `FAMILY`；2026-10-12 前误指 EDF6 的 v515_retrobalam，那是巴尔加），403 / 402 / 404 / 502 金色 EDF6 同名），设定块（装甲和车载武器，武器按 `WEAPONS` 改名，EDF6 拆成左右的写两行；与模板排法不同时用模板的：贝加尔塔 4.1 6 个武器槽、EDF6 8 个，摩托另一种排法），用量（`AmmoCount`、`ReloadTime`、`ReloadType`、`EnergyChargeRequire`）。分类跟模板走：306–309 / 320，403 Epsilon 与摩托在 EDF6 只出现在游骑兵分类 8 / 10。ホエール攻击机 8 把（`Weapon_BasicShoot` 烟雾弹、模式 0）原样进 EDF6 的炮舰分类 311；轰炸机 11 把（`Weapon_Throw` 模式 2）做成 EDF6 的 `Weapon_RadioContact`（312），4.1 `ACP[4]` 的轰炸参数与 EDF6 `ACP[2]` 同一排法，取弹药参数排法相同的模板（`Bomber401` 优先）换上 4.1 的。4.1 按载具区分的语音（「輸送部隊発射ギガンテス」等）EDF6 没有，用模板的通用语音。登记表条目记 `call`（种类、模板、载具），`ported_weapons_registry_current` 据此重算。这 84 把放在单独的登记表 `edf41port/calls.json`（`ported_weapons.REGISTRIES` 排在 `edf5port/models.json` 之后），使已发布的 EDF5 换皮载具 3 行（346–348）位置不变，呼叫从第 349 行起追加；`edf41port/weapons.json` 不含呼叫。研究：jobs/4bf89026/tmp/vehicles41/REPORT.md。未实机：呼叫能否召出（尤其 Epsilon / 摩托在空爆兵分类）、轰炸效果。
- 22 把资源缺失（主要是诱饵模型，P3），2 把诱饵内嵌大端 SGO 块（转换器拒绝，P3），4 把开火音效 EDF6 没有且模板也没有可替换的。

**分类**：4.1 自己编号（0–39），按兵种和种类对到 EDF6：游骑兵 0–6 → 0–6；飞翔兵 10–16 → 100–106，17（投掷集束）→ 105（主武器槽，同 EDF5 的 107）；剑兵 20–25 → 200–205；空降兵 30 → 305、32 → 302（转 `Weapon_Sub`）、33 → 303、34（炸弹、哨戒炮）→ 305、35 → 302/305；31 的投掷炮击（模式 0）→ 310，激光引导空袭按 EDF6 同系列名归 313（ライオニック、テンペスト）或 314（バルジレーザー、スプライトフォール），EDF6 没有的系列看名字里有没有「ミサイル」。

**格式**（`pylib/edf5port.py` `weapon41`）：
- 4.1 没有星级系统，伤害、弹数等都是普通数值；EDF6 同样接受（它自己 1833 个 `AmmoCount` 里 1066 个是标量，H），原样保留。
- `name`（`[日, 英, 日]` 一个列表）拆成 `name.<lang>`；`Range` 去掉：827 把全部等于 `AmmoSpeed × AmmoAlive`（H），EDF5 起就由这两个字段算射程。
- 补 EDF6 每把武器都有的 `AmmoDamageReduce [1, 1]`（无距离衰减，4.1 本来就没有；EDF6 1564 把里 1350 把是这个值）和 `ExtPrams [1]`（1486 把）。
- `EnergyChargeRequire`：4.1 是一个数，EDF6 每把武器都是 `[曲线, 数值]`。写成 `[v, v]`：EDF6 包里有 5 把开发商自己转换的 4.1 武器（`HHELLSTORM01`、`HWEAPON112/118/123/127`），字段集合与 `weapon41` 的输出完全一致，`-1` 写成 `[-1, -1]`（H，`edf41_weapons_match_developers`）；飞翔兵武器 4.1 的数等于 EDF5 同名武器的曲线基值和数值（レイピア 25 / `[[25, …], 25]`）；曲线位置放普通数值即不随星级变化，和 EDF6 自己大量的标量曲线字段一样（M：飞翔兵能量的实际效果待实机）。
- 那 5 把不用 EDF6 自带的文件：开发商给它们加了星级、改了数值，与 4.1 说明里的属性对不上；从 4.1 转换保证菜单数字与武器一致。
- MAB 块、空副射击参数、`Weapon_Sub`、`AmmoDamageAttribute` 同 P1。

**音效**：EDF6 的音效库（`SOUND/PC/*.ACB`，`pylib/acb.py` 读出 4259 个 cue，与独立工具的结果一致）里没有的 4.1 cue 会无声，换成模板武器同一位置的 cue（登记表 `cues`）；连模板也没有的武器不收录。`pylib/cpk.py` 的 @UTF 表头顺带修正：前 4 字节是 u16 版本 + u16 行偏移（CPK 的版本恰为 0，读 ACB 时版本为 1 才暴露）。

**文本**：
- 名字、说明：日文、英文用 4.1 官方的；4.1 没有中文和韩文，繁中 / 简中 / 韩文由翻译补齐（`edf41port/translations.json`：系列名沿用 EDF5/6 官方译法，兵种名用 EDF6 官方的 `ETC/TEXTTABLE_STEAM` `SoldierType_*`——突擊兵 / 飛翼潛行兵 / 空襲兵 / 劍兵；4.1 独有的系列名与载具名是自定译名）。
- 属性：4.1 把属性写在说明文字开头（`label：value`）。每行按 EDF6 官方文本的同名属性格式转成 EDF6 的属性行 `[标签, 数值]`，五种语言的标签和单位都取 EDF6 官方写法：EDF6 格式里写死的数字（如「90.0秒」）在各语言数字一致时泛化成格式；一行两项（「弾数：30　ズーム:2倍」）拆开；等级、横线等与语言无关的值原样；其余由 EDF6 格式中的片段拼出（「60.0発／秒×900」）；EDF6 没有的标签（「供給量」）手写对照。303 把的属性行全部转换成功，没有留在说明里的。

## 7. P3：模型与动画转换（`pylib/mdb_legacy.py`、`pylib/cas_legacy.py`、`pylib/legacy_assets.py`）

**MDB 模型 0x14 → 0x20**（4.1、EDF5 → EDF6）。文件结构三代相同（`pylib/mdb.py` 能逐字节读写三代全部 MDB：4.1 4385 个、EDF5 6477 个、EDF6 18826 个），要改的只有：
- 版本 0x14 → 0x20（EDF.dll `0x11126C0` 按相等比较）；
- object +0 从自己的序号改成名字表里挂接骨骼名的下标（EDF6 18669/18669）；
- binormal 的 x/y/z 取反：4.1、EDF5 是右手系，EDF6 是左手系（配对里 1957 个网格正好取反）；
- 蒙皮网格的 BLENDWEIGHT / BLENDINDICES 挪到顶点末尾；没有权重的蒙皮骨骼 bounded 置 0；材质参数里超出分量数的垃圾值清零；
- 4.1 的参数 type 高字节是「分量数 − 1」，统一加 0x100 即 EDF5 的编码；4.1 地图的刚体网格要改成单骨骼蒙皮（`map_part`，由调用方决定：同样用地图着色器的石头三代都是刚体）。
「EDF6 改了材质参数集」不成立：没换着色器的 5640 个材质里 5310 个参数完全一致，差异是 EDF6 自己对约 900 个材质的重制（换成 snd_BRDF_* 着色器）。EDF5 的 89 个、4.1 的 70 个着色器 EDF6 都保留。验证：EDF5→EDF6 3203 对中，能逐字节对照的全部一致，其余为 EDF6 的重制；EDF5 6477 个、4.1 4377 个（另 8 个用了 EDF5/6 从没出现过的顶点格式 24，转换器拒绝）全部能转换。

**CAS 动画 0x203 / 0x200 → 0x204**：头和结构三版布局相同（版本字、4.1 data 结构 0x24 → 0x34、命令号整体后移、EDF6 清掉的旧工具脏数据）；内嵌 CANM 0x200 → 0x300：通道 0x20 → 0x30 字节、通道重新编号，旋转从量化欧拉角改成四元数（静态 kind 2、动画 kind 3，16 字节对齐）。欧拉 → 四元数按 EDF.dll `0x649A0` 精确还原（X、Y、Z 依次，float32，UCRT sinf/cosf），静态四元数 1487/1487 逐位一致。EDF5→EDF6 230 对：66 对逐字节一致，143 对只差动画四元数的低位（EDF6 用的是量化前的源角度，EDF5 文件里已丢失，误差不超过原量化步长），21 对是 EDF6 改了内容；4.1 的 205 个转换 204 个（GIANTANT_HOKAO 用了对不上的 4.1 命令，拒绝），其中 141 个与 EDF5 同名文件的转换结果逐字节相同。

**接进移植武器**：`legacy_assets.convert` 把整个 RAB / MRAB 容器的模型和动画转换、贴图原样保留（成员顺序是加载器的约定，`mdb.insert_member`）。登记表条目可以带 `assets`（Mods 路径 → 源游戏文件），安装时随武器生成、卸载时删除；开发工具对每个资源实际转换一次，转换器拒绝的不收录。这类武器在单独的登记表 `edf5port/models.json`，排在已冻结的两份之后。

**EDF5 的 3 辆换皮载具**（ブラッカー ナンバー４．１ / ５、オメガ·フリージャー）：EDF6 自带它们的呼叫和载具 SGO（`V505_TANK_EDF4.SGO` 等，引用 `app:/Object/v505_tankedf4.mrab`、EDF6 原车的 `Ragdoll_v505_tank.shkt` 与 `v505_tank.cas`），只缺车模；EDF5 的车模是同一种容器（`.rab`），转换后装成 `Mods/OBJECT/<名字>.MRAB`。转换好的车模由开发工具写到 `edf5port/assets/<Mods 路径>`（3 个共约 37MB），随工具与安装包发布，安装时直接复制，玩家不需要 EDF5（2026-10-10 用户：「不能我们预先生成吗」；这是 EDF5 的模型与贴图本身的再分发，由仓库所有者决定）。只有一把武器的全部资源都能转换时才写文件，被跳过的武器不留文件（`ported_assets_bundled` 守住两个方向：每个资源都有文件、每个文件都有武器）。没有预生成文件的资源仍可从玩家本机的游戏转换（`ported_weapons.build_assets`）。骨骼按名字与原车动画对得上，唯一不同的是模型自己那根物体骨骼（`v505_tankedf4` / `v503_bike_omegaz` 对原车的 `v505_tank`）：动画里 `v505_tank` 那条轨道在换皮车上找不到，EDF5 自己的换皮也一样。EDF6 的原车重做过（物体骨骼挪到最后、材质换成 snd_BRDF_*），换皮车保持 EDF5 原车的骨骼顺序和着色器。已实现、未实机验证。

**SHKT（物理）与诱饵发射器**：EDF6 的 EDF.dll 还带着旧 Havok 文件读取器（2014 二进制 tagfile、2015 TAG0）、全套版本补丁和 Havok 的 hkp → hknp 迁移工具（ragdoll 加载 `0x11A5270` 发现 `hkpPhysicsData` 就调 `0x160B950` 转换），诱饵用到的碰撞形状都在支持范围内，所以旧 SHKT 很可能可以直接用（M，静态）。但原版资源从没走过这条路，且加载后的查找不判空（`0x6E85D6`），任一步失败就崩溃。诱饵发射器因此另做，先请测试者实机确认不崩溃再合入。

## 8. P4：EDF4.1 敌人（`tools/make_edf41_objects.py` → `edf41port/objects.json` + `edf41port/objects/`）

**要什么**：扫 4.1 全部 98 关的 BVM 脚本，脚本里点名、EDF6 没有的 OBJECT 共 60 个，77 关至少用到一个（2026-10-10，`jobs/4bf89026/tmp/bvm41/assets41.json`）。按 EDF6 还有没有那个类分三层：

| 层 | 对象（关数） | 怎么来 |
|---|---|---|
| 一：EDF6 还有原类 | 蚁巢 AntHill301（14）、迪洛伊 Deiroi401 系列 5 个（25）、外星运输车 AlienTrailer401 系列 5 个（17）、欧米伽队士兵 AiArmySoldier_OMG（+队长，各 5）、巴尔姆 Vehicle501_FortressRobo（+_AI，各 1） | 4.1 的文件原样搬过来，只做格式转换：**本节，已完成转换** |
| 二：类没了、有可借的壳 | 赫克托 Hector 全系（几十关）、UFO301/401（33）、运输船 Carrier301/401（30）、4.1 坦克 Vehicle301_tank_ai（13）、四足要塞（4）、巨龙（2） | 借 EDF6 相近类（UfoSmall507、UfoCarrier508、EDF6_Berserker_Large …）+ 模型补骨骼 / CAS 补契约 / 插件补行为：待做 |
| 三：没有可借的壳 | 母舰 UfoMother301、地球吞噬者 UfoMother401 系列、UfoRobo | 插件原生类（`src/edf41classes` 的工厂 + 克隆虚表，`docs/edf41-classes.md`），2026-10-15 都已做：UfoMother301（`src/edf41mother.cpp`，`docs/edf41-ufomother301.md`，M016 M110 M123 M130 M210 M603 + 任务包１ M993）；UfoMother401 船体 + 11 个部件类（`src/edf41mother401.cpp`，`docs/edf41-ufomother401.md`，15 个故事变体 + 任务包２ M809 / MP503，17 关）；UfoRobo（`src/edf41robo.cpp`，`docs/edf41-uforobo.md`，M007 M411 + M415 M416 M010） |

**第一层的转换**：每个对象沿 SGO 里的 `app:/` 路径（含它引用的子 SGO）收集 EDF6 没有的文件；EDF6 有同路径文件的用 EDF6 的（所有档案都算：Root.cpk、ChunkNN、DX11）；4.1 自己也没有的不需要（4.1 引用的 `*_LIGHT.MRAB` 在它的任何档案里都没有，游戏照样跑）；没有扩展名的是目录前缀。各类文件：

- `.SGO`：EDF6 仍读 4.1 的 SGO 格式（EDF6 自己抽样 300 个 OBJECT SGO 里有 143 个是这种），只把里面的 MAB 块从 0x03 改成 0x83（`pylib/legacy_sgo.py`），原地替换、长度不变，其它字节保持 4.1 的；不重新编码（`sgo.write_depth_first` 有 174 / 233 个 4.1 OBJECT SGO 写不回原样）。
- `.RAB` / `.MRAB` / `.CAS`：`legacy_assets.convert`。4.1 有 30 个档案的成员不按文件夹分组（迪洛伊、外星运输车、欧米伽队士兵的模型），EDF6 自己也有 22 个这样的档案照常加载，所以转换时保持原顺序（P3 的修复）。
- `.SHKT`：4.1 的 Havok 2014 二进制 packfile 原样带过来（EDF5 / EDF6 都是 TAG0）。**实机（2026-10-10，测试场，2 个 AntHill301 + 2 名 AI 飞翔兵）**：4.1 的蚁巢刷出、模型正常显示、雷达上是敌人，带着 4.1 的 SHKT 加载不崩溃，整局正常退出；蚁巢被打垮（物理坍塌真正启用的那一刻）这次没等到，仍待确认。
- `.ACB`：4.1 的音效库（TIKYUU4_*）EDF6 作为散文件放在 `SOUND/PC`，只核对存在，不复制。

15 个对象、45 个文件、约 62 MB，转换好放进 `edf41port/objects/`，随工具发布，玩家不需要装 4.1。安装（`tools/edf41_objects.py`，账本 owner `edf41`）按 4.1 原来的文件名写进 `Mods/OBJECT`，4.1 任务脚本可直接引用；Mods 里已有别的 mod 放的同名文件时不覆盖，用到它的对象整个跳过并说明。测试：`edf41_objects_shipped`（登记表与文件双向一致、哈希、MAB 0x83、MDB 0x20）、`edf41_objects_install`（替身游戏上安装 / 卸载、不覆盖别人的文件）。

### 8.1 第三层：母舰 UfoMother301 / UfoMother401 系列（2026-10-13 调查；结论：数据层做不了，需要原生类）

**要什么**：301 母舰（M016 M110 M123 M130 M210 M603）和地球吞噬者 401 系列（`UfoMother401` 与 M009 / M012 / M019 / M132 / M142 / M145 / M149 / M201 / M308 / M312 / M412 / M516 / M518 / M602 的专用变体，另有 M013 / M519 / FLATLAYOUT），共 21 关要它们。`plan41` 现在把这些关按「缺少 Object/UfoMother…sgo」跳过。

**为什么不能像第二层那样「借壳」**（EDF6 `EDF.dll` 与 `EDF41.exe` 的 RTTI、反汇编核对）：

| | 4.1 | EDF6 |
|---|---|---|
| 母舰类 | `UfoMother301`、`UfoMother401`（+ 部件类 `_Hatch` `_Panel` `_BigCannon` `_Cannon` `_PartsBase` / `_Core` `_Generator` `_FinalCannon01` `_FinalGenerator` `_GunBattery_{PlasmaCannon,RapidCannon,SmallLaser,Sniper}` `_Panel01` `_PanelBase` `_WeakPanel`） | 只有 `UfoMother511` 一族 12 个类（`…Barrier` `…BulletModule` `…CoreBigCannon` `…CoreSmallCannon` `…GeneratorModule` `…HatchModule` `…PartsBase` `…RingUnitParts` `…RingUnitParts_DummyRing`）；RTTI 里没有 Panel、没有读 `mp_*` / `panel_definition` / `action_list` 的类 |
| 数据形状 | 301：`mp_Weakpoint` `mp_HatchLow` `mp_HatchMiddle` `mp_HatchEmission` `mp_Panel`×4 组（数百块，各自位置 / 朝向）`mp_Cannon` `mp_BigCannon` `mp_PanelParameter`，部件是独立子对象，按 SGO 里写的坐标摆。构造函数 `EDF41.exe` 0x24ece0。401：`panel_definition`（板种类表）+ `panel_layout_map*`（19 列的板布局网格，多张）+ `action_list`（见下）。 | 511：核心 + 环（`ring_info` / `ring_layout_list` 按环公式排 5 种 `RingUnitParts`，环单元自带炮与屏障）+ `core_parts`（4 门歼灭炮）+ `hatch_auto_open_close` 自动循环。部件位置由环公式决定，SGO 不能逐个指定。 |
| 脚本动作 | 301：`ObjectAction` 消息处理在虚表槽 6（`EDF41.exe` 0x250870，消息 0x80000009，跳转表 0x250f6c，18 个动作）。0 / 1 / 2 往 +0x868 队列里排三段序列（函数对象 0x251e50 / 0x251e90 / 0x251ef0）；3 / 4 调 0x251bc0(this, 0, 1/0)；5 / 6 / 7 对 +0xa88 那组部件置 / 清标志 +0x901 / +0x902；8–12 对 +0xaa8 那组置 / 清 +0x900（仅耐久 > 0 的）/ +0x901 / +0x902；13–17 对 +0xac8 那组同理。也就是「分组启停部件」。401：动作号 n 就是 `action_list[n]`，条目只有 8 种形：`[0, 布局名]` 换板布局（44 条）、`[1, 位移, 速度…]` 平移（34 条）、`[2]` `[3]` `[4]` `[5]` `[6]` `[7]` 无参（26 / 9 / 10 / 18 / 16 / 1 条）。 | 511 的动作枚举 `UfoMother511_Action` 是另一套（舱门 / 环 / 屏障），没有「某组炮启停」，也没有「换板布局」。 |

**把 301 挂到 `UfoMother511` 上会怎样**：类名可以在 SGO 里改成 `UfoMother511`（`Alias.cls` 就是这么做的），但 511 在出生时按环公式摆 5 层环单元（E511 的炮、屏障、转动），4.1 的 `mp_Cannon` 逐个坐标的炮、板甲、`mp_HatchEmission` 都没有对应槽位；动作号即使按表重映射，「炮组启停」也没有对应物，出来的是一艘带 4.1 船身模型的 E511。这是「长得像」，不是 4.1 的攻击方式和行为，所以**没有做**（用户 2026-10-13：「攻击方式和行为要还原」）。401 连可借的类都没有（板网格 + 数据驱动动作）。`MovingFortress`（歩行要塞，有 `parts_setting` 逐个挂炮、`core_hatch_interval`）与 `EDF6_GrandMother`（臂 / 门 / 弱点）各只覆盖一小块，且前者是腿式地面单位，也不是 4.1 母舰的行为。

**关里用到的接口**（`EDF41` 任务脚本即 EDF5 重编的 BVM，原生函数号三代相同；`tmp/mother41` 的反汇编）：`CreateEnemy` 2000 / `CreateNeutral` 1020 创建；`ObjectAction` 3200（301：M016 用 3 4 7；M110 用 3 7 4；M130 用 0 2 13 14 15 16；M210 用 3 5 8 10 13 15；M603 共 12 次。401：M009 用 2–6、M012 用 0 3 4 5 6 8、其余关 0 / 2–7）；`GetPartsObject` 3300（M018 一次；M210 十一次，组 0 / 1 的 0–9 号部件，拿到部件对象再各自设耐久）；`Factor_ObjectDurability` 9200（部件耐久比例触发）；`SetEnemyGenerator` 2100 / `SetInstantEnemyGenerator` 2101（舱门出兵）；`SetAiRoute` 3101 / `SetAiMoveSpeed` 3100 / `Factor_AiMoveEnd` 9300（母舰沿路线飞）；`LookCameraToObject` 420。这些号码本身不用改（原生函数号 EDF6 也认），**要改的是 `ObjectAction` 的第二个实参**，但在没有同语义的 EDF6 动作可映射的前提下，没有可写的重映射表。

**数据层做不了的清单与插件钩子**（不伪造）：
1. 新增原生类 `UfoMother301`（与其 `_Hatch` / `_Panel` / `_BigCannon` / `_Cannon` 部件类）和 `UfoMother401`（与 `_Core` / `_Generator` / `_FinalCannon01` / `_FinalGenerator` / `_GunBattery_*` / `_Panel01` / `_WeakPanel`）。EDF6 的类工厂形状（`UfoMother511` 为例）：工厂对象虚表 `EDF.dll` 0x17ca200，槽 1 取类名（0x4fd900，返回宽字符串 `UfoMother511`），槽 2 创建（0x4fd910：分配 0x2110 字节，构造函数 0x4fe740），虚表在一个静态注册函数里取地址（引用点 0xfc14ba、0x50a1e7）。插件要做的是注册同形状的工厂并实现对象本体（`ObjectAction` 消息槽、SGO 加载、部件摆放、炮 / 舱门 / 板的碰撞与耐久）；这是新写一个类，不是改数据。
2. 4.1 的行为细节只部分还原（动作 0 / 1 / 2 的三段序列、0x251bc0 的两种模式、三组部件标志位的含义没有逐条读完），真要写类之前要先把 0x24ece0（构造）、0x250870（消息）、0x251bc0、0x254c20 与各部件类的更新函数读完。
3. 在原生类存在之前，涉及母舰的 21 关保持跳过；`edf41_motherships_not_faked`（selftest）钉住这一点：登记表里不得出现任何 `UfoMother*` 类，也不得出现 4.1 母舰的 SGO，否则关卡会放行却刷不出船。
