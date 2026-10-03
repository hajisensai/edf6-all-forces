# 上车门槛与座位：逆向笔记

EDF.dll TimeDateStamp `0x678CCB46`，下文地址均为 RVA。全部来自静态分析，尚未实测。直升机输入块见 `heli-input-re.md`。

## 对象布局

- GameObject：`+0x28` 是 weak-this 指向的对象，`+0x30` 是控制块，控制块 `+8` 是 use count。
- Vehicle：
  - `+0x60` 世界矩阵（行：右 / 上 / 前，`+0x90` 是位置）
  - `+0x2E8` 死亡标志，`+0x314` 队伍
  - 座位数组 `+0x608`，座位数 `+0x618`，每个座位 `0x340` 字节
  - `+0x628` 座位占用位
  - `+0xE30` 生成方式：1 = RideAi，2 = 空投（计时器在 `+0xE34`）。只在快照 `0x62FBF0` 里用到，**不决定由谁驾驶**。
- Seat：
  - `+0x260` 乘员对象，`+0x268` 乘员的 weak 控制块。use count 不为 0 就算有人。
  - `+0x2C0..` 摇杆块，由人类乘员每帧写入。
  - `+0x2F8/+0x2FC` 下车请求（`0x6313C0` 写入）。
- Human：
  - `+0x31C` 乘坐掩码
  - `+0x340` 手柄对象，`+0x354` 玩家标志。两者都不为 0 才是玩家。
  - `+0x1540` 所在座位，`+0x1548/+0x1550` 所在载具（weak）
  - `+0x1185` 上车提示结果
- DummyVehicleRider（vtable `0x17D7320`）：由 RideAi 让它坐进去。收到消息 `0x10000015/16`（第 9 槽，`0x5E3B50`）时把 `+0x5C0` 置 1，也就是死亡，所以“踢下车”等于让它消失。

## 上车提示

1. 人物每帧更新 `0x572DF0`。只有步行时（`human+0x1550` 为空）才会对周围对象跑一个访问器。
2. 访问器的 functor vtable 是 `0x17D09B8`，结构为 `{vtable, human, bool result}`。它的第 1 槽 `0x5725A0` 对每个对象调用 `CanRide 0x62DCB0(vehicle, human)`。
3. 结果写入 `functor+0x10`，再存到 `human+0x1185`，最后由 HUD `0x808410` 显示 `HUD_BOARD_VEHICLE_RIDE`。

## 上车按键

1. `0x59B423` 检查按键状态 `human+0xD78`，然后调用 `0x56D700`。
2. `0x56D700` 跑一个访问器（functor vtable `0x17CFCF0`），它的第 1 槽 `0x572610` 调用载具第 49 槽（`+0x188`），即 `FindSeat 0x633B80(vehicle, human)`。
3. FindSeat 对每个座位调用 `CanRideSeat 0x6346D0(vehicle, human, seat)`。第一次命中时立刻用 `0x633FE0` 预约这个座位，并返回座位指针。
4. 返回值写入 `human+0x1540`，载具写入 `+0x1548/+0x1550`，然后由 `RideVehicle 0x5765E0` 完成上车。

CanRide 与 CanRideSeat 的判定条件相同：

- **队伍**：双方队伍不同，且载具队伍不为 5 时，乘坐掩码需要第 7 位。
- **职业掩码**：`(seat+0x34 & human+0x31C) & seat+0x30` 不为 0。
- **距离**：在可上车范围内。
- **空位**：`seat+0x268` 的 use count 必须为 0。**这就是“有人的座位不能上”的门槛。**

RideVehicle 碰到有人的座位时，会先调 `0x6313C0` 请对方下车，然后 `0x633C10` 以 force=0 返回 null，上车失败。

## 座位函数

| 函数 | 作用 |
|---|---|
| `0x633C10(veh, rider, idx, force)` SeatRide | 让乘员坐进指定座位。座位有人且 force=0 时返回 0 |
| `0x633FE0` | 写座位的 weak 指针和占用位，不通知乘员 |
| `0x634940(veh, seat)` SeatClear | 清空座位，不通知乘员 |
| `0x62E1A0(veh, seat)` SeatKick | 给乘员发消息 `0x10000015`，再清空座位 |
| `0x62D350` | 下车，消息为 `0x10000016` |
| `0x633030` RideAi（第 50 槽） | 依次执行 `0x835C0` 生成乘员 → `0x54EE70` / `0x54E740`（不接手柄） → `0x633C10(veh, r, 0, 1)` → `0x118AF20` 挂父子关系 → `veh+0xE30 = 1` |
| `0x1CC170` | 脚本 `Vehicle_RideAi` 的包装，以 dl=0 调用第 50 槽 |

## 插件的做法

- **提示**：hook 访问器第 1 槽。原版判定为否、且载具上有 NPC 乘员时，先临时把这些 NPC 座位的 `+0x268` 置空，再调一次原版 CanRide，判定通过就把结果置 1，最后恢复 `+0x268`。
- **按键**：hook 全部 23 个载具 vtable 的第 49 槽。原版没找到座位、且上车的是玩家时，对每个 NPC 座位用同样的“临时隐藏乘员”法跑一次 CanRideSeat。命中后：
  - 把 NPC 用 SeatRide(force=0) 挪到空的副座，再 SeatClear 原座位；
  - 没有空副座时用 SeatKick 把它踢下车。
  然后再调一次原版 FindSeat，原版会正常预约这个座位并走完上车流程。
- **每帧入口**：在第一个任务帧（由提示访问器触发，此时所有插件都已加载）串接各具体类的第 55 槽（输入）。如果某槽已被别的插件（EDF6AutoTurret）改过，就串在它后面。

## 载具 vtable

除 BigBegaruta（`0x6490C0`）外，各类第 49 槽都是 `0x633B80`，第 50 槽都是 `0x633030`。

| 类 | vtable | 第 55 槽 |
|---|---|---|
| 402_Rocket | `0x17D8B50` | `0x5FD8E0` |
| 403_Tank | `0x17D8FA0` | `0x5FEBE0` |
| 404_Tank | `0x17D9458` | `0x5FFC50` |
| 501_FortressRobo | `0x17D98C8` | 不 hook |
| 502_GroundRobo | `0x17DA028` | 不 hook |
| 503_Bike | `0x17DA508` | `0x6178B0` |
| 504_begaruta | `0x17DA960` | `0x63C1C0` |
| 505_Tank | `0x17DADB0` | `0x61ACD0` |
| 506_Helicopter | `0x17DB238` | `0x61B8F0` |
| 510_Maser | `0x17DB9D8` | `0x61DDF0` |
| 511_Bike | `0x17DBDF8` | `0x61F080` |
| 601_Tank | `0x17DC250` | `0x620790` |
| 603_Flak | `0x17DC620` | `0x621460` |
| 612_nix | `0x17DD440` | `0x63C1C0` |
| Begaruta | `0x17DE0A8` | `0x63C1C0` |
| Helicopter409 | `0x17DEF98` | `0x64C020` |
| Helicopter410 | `0x17DF338` | `0x64E080` |
| HelicopterBase | `0x17DF790` | `0x6543A0` |

- 地面车的 AI 在第 72 槽：CarBase 系是 `0x661440`，机甲是 `0x773B50`。
- 直升机的 vtable 只到第 61–63 槽，后面跟着的是 `veh+0x120` 处 NetworkObject 子对象的 vtable。直升机没有 AI。

## 原版物理：载具车身质量档（physics.cpp）

- hknp 默认 body quality 表在 0xE13BA0，库对象 +0x40+0x30*i 是 requestedFlags，库指针在 world+0x930。
  DYNAMIC(3) 无焊接；VEHICLE(9) 0x180 = NEIGHBOR|MOTION 焊接；CHARACTER(10) 0x80 = 只有 NEIGHBOR。两档迭代次数相同。
- 轮式车身在 0x656E90 建体（调用点 0x64E9B6 / 0x650AA6），0x6571AD `C6 85 86 00 00 00 0A` 把 cinfo+0x86 的 quality 写成 CHARACTER。
  缺 MOTION 焊接，车身高速滑过地形三角面接缝撞上内棱（ghost contact）被弹起——这就是「开过不平的地面弹飞」。
- 修法：校验那 7 字节后把立即数 0x6571B3 改成 0x09（VEHICLE）。只影响这一个建体函数。

## 原版物理：巨型单位垂直接触冲量无上限（physics.cpp）

- 角色代理的垂直接触在 0x11DDFD0 里组约束块 [rsp+0x40] = {0 或 1.0, maxImpulse, 1.0}；
  0x11DE049 `movss xmm0,[0x18474B0]` 无条件把 maxImpulse 装成 HK_REAL_HIGH（无穷大）。
- 角色的 maxForce 在 [character+0x70]（构造 0x11DD07F/0x11DD085 从 cinfo+0x98 拷入，默认 1000），这条路径从不使用它。
  5 代 hkp 角色对动态物体的接触限制在 maxForce×dt；6 代 hknp 丢了这一步，巴尔加/巨大怪物站在（或压在）运动中的母体、
  布娃娃、碎片上时吃到无限冲量——被顶飞，或被反向压进地面。
- 判据取引擎自己的：0xD95D38 角色推物体时取 body → `test [body+0x54],5`（STATIC|KEYFRAMED 跳过）→ `test 2`（DYNAMIC）→ 用 [rdi+0x70] 推。
  hknpBody 标志 +0x54：STATIC=1 DYNAMIC=2 KEYFRAMED=4 ACTIVE=8。只有 `flags&7 == 2` 才限，静态地形与关键帧物体保持无限支撑。
- 对方 body id 在接触点 [rbx+0x20]；body 管理器 [character+0x28]，vtable image+0x18BC9C0，
  vt+0x68 = 0xDAC200（有效性：idx=id&0xFFFFFF、idx<[mgr+0x20]、[body+0x50]==id），vt+0x80 = 0xDAB170 纯 getter [mgr+0x18]+idx*0xB0。
  插件只在 vtable 恰为 0x18BC9C0 时内联计算，不调虚函数（0x11DE0E2 调 vt+0x80 后丢弃返回值，别的实现可能成对 acquire/release）。
- dt：checkSupport（槽 image+0x1AE4D50 → 0x11DE5B0，rdx=hkStepInfo，dt 在 +8）先于接触构建运行，钩它记下步长。
- 修法：校验 0x11DE042 起 16 字节，把 0x11DE049 的 8 字节 movss 换成 `E9 rel32 + 3×NOP` 跳到近页 cave；
  cave 保存 flags（0x11DE042 的 cmp 要活到 0x11DE078 的 jne）、rax（0x11DE051 要存 eax）、其余易失寄存器与 xmm1-5，
  以 rcx=rdi(角色)、rdx=rbx(接触点) 调 helper，xmm0 带回上限后跳回 0x11DE051。
  该处 rsp ≡ 0 mod 16（3 push + sub 0x170），pushfq+7 push+sub 0x80 保持调用对齐并留 0x20 影子空间。
- 开关 `GiantContactCap`（默认 1）。
