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

## 3. 远距离上车

- 上车键 `0x56D700(human)` → 访问器（第 5 队，然后人物所在队）→ 各载具第 49 槽 FindSeat → CanRideSeat `0x6346D0`（`docs/re-notes.md`、`docs/rescue-re.md`）。
- CanRideSeat 的距离判定读 **`human+0x90`**（`0x634748 subps xmm4,[rdi+0x90]`）与座位上车点（`0x6BB420(seat+0x1E0)`）的距离，阈值 = 上车点半径 + 0.5。
- 插件做法：只在按键这一次调用里把 `human+0x90..+0x98` 写成该座位的上车点，并让 FindSeat hook 只对目标载具给座位；坐上了就不写回（原版上车后由载具决定人物位置，下车时原版把人放到下车点），没坐上才写回原位置。
- 友军（2 队）载具不在访问器走的集合里，先用原版 SetTeam（`SetObjectTeam`，在 FrameTick 里、不在任何队伍遍历中）放进 5 队，同海上救援。

## 4. 待实机确认

1. 手持狙击枪（Weapon_BasicShoot）确实经 `0x696FD0` 开火（它的 5 个调用者之一，M）。
2. 子弹打到载具时 `0x108260(bodyId)` 返回的就是载具基址（喷气机僚机穿弹已按此实现）。
3. 开枪后的拉栓动作期间 `0x56D700` 的三道门（`+0x128` bit0、`+0x5D0` bit2、`+0x39C`）是否挡住按键；插件在 1.5 秒内每帧重按，超时把三道门的值写进日志。
4. 被写过 `human+0x90` 的玩家坐上后、下车时位置是否正常（预期由原版下车点决定）。
