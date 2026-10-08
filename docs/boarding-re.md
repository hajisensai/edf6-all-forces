# 登车狙击枪：静态逆向笔记

EDF.dll TimeDateStamp `0x678CCB46`，地址均为 RVA。全部来自静态分析（2026-10-05），**未在游戏内验证**。
H = 直接读代码得出；M = 强推断；L = 猜测。实现见 `src/boarding.cpp`，武器数据见 `tools/calls.py` / `tools/call_weapons.py` 的 `gun`。

## 1. 激光和缩放来自哪里（H，数据）

- ＫＦＦ５０ 与 ＫＦＦ５０ＬＳ 的 SGO 只差 `custom_parameter[4] = [1.0]`（激光瞄准器）和数值；缩放是 `SecondaryFire_Type 1`、`SecondaryFire_Parameter 5.5`。
- 所以登车枪直接以 `aWeapon081`（ＫＦＦ５０ＬＳ，Weapon_BasicShoot，SolidBullet01）为模板，激光和缩放都是原版的。

## 2. 在子弹上认出这把枪

子弹参数：开火 `0x696FD0` 把武器 `+0x830` 的模板拷到 `+0xA10` 再发射（`0x69712F`），参数拷贝 `0x2307F0` 放到 core `+0x9A0` 起。
所以 **core 偏移 = 0x9A0 + param 偏移，weapon 偏移 = 0x830 + param 偏移**（`docs/bullet-pass-re.md` §3.3）。

| SGO 键 | 读取处 | 转换 | weapon | param | core | 可信度 |
|---|---|---|---|---|---|---|
| **AmmoColor** | `0x68D952` → `0x54F10` | 最多 4 个 double → float，`0x68D97C` 一次 16 字节写入 | `+0x8D0..+0x8DC` | `+0xA0..+0xAC` | **`+0xA40..+0xA4C`** | H |
| AmmoGravityFactor | `0x68D62F` | double → float | `+0x8E0` | `+0xB0` | `+0xA50` | H |
| AmmoAlive | `0x68D66D` | 取整 | `+0x898` | `+0x68` | `+0xA08` | H |
| AmmoSize | `0x68D898` | float | `+0x8C0` | `+0x90` | `+0xA30` | H |
| AmmoHitSizeAdjust | `0x68D8D6` | float | `+0x8C4` | `+0x94` | `+0xA34` | H |
| AmmoHitImpulseAdjust | `0x68D914` | float | `+0x8C8` | `+0x98` | `+0xA38` | H |

- 选 **AmmoColor 的 alpha** 当标记：子弹代码（`0x22E000–0x2A6000`）里没有按位移读写 core `+0xA40..+0xA4C` 的地方，只有绘制用它（M/H）；
  不走星级曲线，开火修正（`0x696FD0` 的 r8）只缩放伤害、AmmoAlive、AmmoSize，不碰颜色（H）。
- AmmoSpeed（`0x68D611` → `+0x894`）、AmmoDamage（`0x68D6E3` → `+0x89C`）、AmmoExplosion 走星级曲线 `0xD7CE0`，不能当精确标记（H）。
  `0x68DB1B` 把 `AmmoAlive × AmmoSpeed` 存为射程，AmmoSpeed 单位是米/帧（M/H）。
- 标记值：alpha = 1 + 7301 个 ulp（位模式 `0x3F801C85`）。SGO 里存 double，读入转 float 是精确的。原版 1564 把武器的 alpha 只有 0.05、0.1、0.15、0.25、0.5、1.0 六种。
- **子弹 owner（core `+0x9A8`）是开枪的士兵 Human**（H）：建武器 `0x58F360`（装备循环 `0x5A4484` 调用）的 create-info `+0x30` 是 Human（`0x58F3B2`），
  武器 init 在 `0x68D584–0x68D5B8` 把它的 weak 写到 weapon `+0x838`（= param `+0x08`）；SetOwner `0x2355F0` 唯一调用者 `0x2A5552` 不在手持武器的开火路径上。
- 备用（未采用）：core `+0x9E0`（param `+0x40`）指向开火武器的枪口数组 `*(weapon+0x1D0) + idx*0xF0 + 0x50`（`0x6972BD`、`0x6B13BB`）。武器对象上没找到武器表行号（M）。

插件启动时校验这几处的 16 字节：`0x2307F0`（参数拷贝）、`0x2309D6`（颜色拷贝）、`0x68D978`（AmmoColor → weapon+0x8D0）、`0x68D5AD`（owner）、`0x69712F`（开火拷贝）。

## 3. 子弹一帧 750 米

- 移动 `0x2349D0`：`vel(+0xB90) += acc(+0xBA0) × 1/60`，终点 = 起点 `+0xB80` + `vel × 1/60`（常量 `0x176B040`），所以 `+0xB90` 是米/秒（H 读码）。
- 命中由原版沿这段扫掠做 shape cast 决定（插件只截命中后的伤害调用，见 `src/boarding.cpp`），所以子弹再快也只会打中线上真正挡着的东西。

## 4. 远距离上车

### 3.1 先确认真实命中（2026-10-06 审查修复）

旧实现把 `addBody` 的 broadphase 候选当命中，候选此时还没经过 layer 过滤、地图最近命中截断和 object shape cast
（`docs/bullet-pass-re.md` §2）。这会让擦过载具包围盒、或同一帧扫掠中墙后的载具触发登车。现在不改候选列表，
只重定向真实命中处理 `0x230CA0` 内的直击伤害 call `0x230EA6 → 0x541FF0`。原生碰撞、命中事件与子弹停止/贯通仍正常执行。

本机 `EDF.dll`（TimeDateStamp `0x678CCB46`）反汇编确认（H）：

| 位置 | 契约 |
|---|---|
| `0x232359–0x23238F` | 对 shape-cast 的 hit fraction 做限制后，传 `hit+0x48` body ID、hit position 和 normal 到 `0x230CA0` |
| `0x230CD3–0x230CDB` | `*r9` body ID 经 `0x108260` 得到命中对象，放入 `r14` |
| `0x230E34–0x230E6C` | `core+0x7A8` 指向命中记录；记录 `+0` 写真实接触点，`+0x20` 写 body ID |
| `0x230E70` | 第一个参数 `rcx = core+0x6E0`，因此 hook 能恢复 core |
| `0x230E80–0x230E96` | 从命中对象的 self weak `+0x28/+0x30` 构造目标弱引用，并增加弱引用计数 |
| `0x230E9A–0x230EA6` | `r8 = core+0x730`、`rdx = &目标弱引用`，调 `0x541FF0`；没有第 4 个参数，返回值未使用 |

hook 只在本机登车枪实际命中己方载具时不调用该伤害函数，并按**真实接触点**到扫掠起点的距离排队。其他命中原样转发
全部三个参数。后续原生代码仍释放目标弱引用、生成命中事件、更新子弹。安装前核验上表的数据写入和调用签名；未匹配时保持原版。
锁内只操作已经读出的普通数据，不再从游戏内存解引用控制块，避免访问异常后遗留锁。

`tests/boarding_hit_test.cpp` 编译并执行生产 hook，覆盖己方/敌方/远端射手/普通子弹、真实接触点排序、关闭配置、安装失败和缺失命中记录；
`tools/selftest.py` 守卫 broadphase 不再调用登车逻辑。仍需游戏内确认墙前/擦边不会登车、真正命中无伤害，以及上下车位置。

### 3.2 按原生上车键

- 上车键 `0x56D700(human)` → 访问器（第 5 队，然后人物所在队）→ 各载具第 49 槽 FindSeat → CanRideSeat `0x6346D0`（`docs/re-notes.md`、`docs/rescue-re.md`）。
- CanRideSeat 的距离判定读 **`human+0x90`**（`0x634748 subps xmm4,[rdi+0x90]`）与座位上车点（`0x6BB420(seat+0x1E0)`）的距离，阈值 = 上车点半径 + 0.5。
- 插件做法：只在按键这一次调用里把 `human+0x90..+0x98` 写成该座位的上车点，并让 FindSeat hook 只对目标载具给座位；坐上了就不写回（原版上车后由载具决定人物位置，下车时原版把人放到下车点），没坐上才写回原位置。
- 友军（2 队）载具不在访问器走的集合里，先用原版 SetTeam（`SetObjectTeam`，在 FrameTick 里、不在任何队伍遍历中）放进 5 队，同海上救援。

## 5. 待实机确认

1. 手持狙击枪（Weapon_BasicShoot）确实经 `0x696FD0` 开火（它的 5 个调用者之一，M）。
2. 子弹打到载具时 `0x108260(bodyId)` 返回的就是载具基址（喷气机僚机穿弹已按此实现）。
3. 开枪后的拉栓动作期间 `0x56D700` 的三道门（`+0x128` bit0、`+0x5D0` bit2、`+0x39C`）是否挡住按键；插件在 1.5 秒内每帧重按，超时把三道门的值写进日志。
4. 被写过 `human+0x90` 的玩家坐上后、下车时位置是否正常（预期由原版下车点决定）。
5. AmmoSpeed 750 米/帧、AmmoAlive 2 帧时子弹是否照常生成、照常走碰撞（原版没有这么快的子弹；有上限的话日志 `BOARDING hit` 一行都不会出现）。

## 2026-10-08：大飞机命中后被旧禁登门拒绝；四职业登记

本机已有游戏日志 `10:59:44.808` 记录 `BOARDING hit ... team 5: damage suppressed`，
下一帧 `10:59:44.837` 却记录 `an NPC jet or a submarine carrier (its pilot is never bumped)`；
同一目标反复出现此顺序。因此这条反馈的已确认原因是 `StartBoarding` 的旧 `IsJet` 总禁令，
不是模型太大导致射线漏命中。现在只排除没有玩家飞控的机种，保留真实碰撞和友军检查；
当前原生登机按键调用的精确 `BoardingOnly` 目标可以在空中登机，普通步行登机仍受低速/高度门限限制。
未新增 broadphase 命中、按中心距离猜目标或无依据的子对象父指针追踪。

四职业新增项追加至 `CALLS` 尾部，原游击兵条目与其它已发布索引保持不变：

| 职业 | 原生模板 | 装備类别 | 武器类 | 子弹 alpha 标签 |
|---|---|---|---|---|
| 游击兵 | aWeapon081 | 2 | Weapon_BasicShoot | 0x3F801C85 |
| 飞翼 | pWeapon127 | 104 | Weapon_PreChargeShoot | 0x3F801C86 |
| 空袭兵 | eWeapon120 | 303 | Weapon_BasicShoot | 0x3F801C87 |
| 剑兵 | hCannon01 | 204 | Weapon_HeavyShoot | 0x3F801C88 |

保留本职业原生模型、动画约束和瞄具。四者使用 SolidBullet01 无爆炸、无重力、不贯通的直击弹；
空袭兵副操作从利姆佩特引爆改为狙击镜，飞翼仍使用本职业蓄力射击操作。读取真实 Root.cpk
验证表行类别、武器类、模型和瞄具保持、直击参数及五语言菜单生成；生产 C++ hook 回归覆盖四标签、
相邻未分配标签、已支持/未支持/敌方飞机。未运行游戏或写入本机 Mods，四职业游戏内操作仍待实机验收。
