# Tempest 巡航导弹电视制导（`src/tvguide.cpp`）：逆向笔记（EDF.dll 0x678CCB46，RVA）

置信度：H = 反汇编 / 数据交叉确认；M = 反汇编推断；L = 推测。全部静态，未进游戏。
工具：`tools/edfre.py`，SGO 用 `pylib/rootcpk.py + sgo.py` 读 `WEAPON/eWeapon0xx.SGO`。

## 0. 一句话结论

Tempest 不是「生成后飞向一个固定点」：发射出的 MissileBullet01 的锁定条目 `B+0xB10` 就是**激光指示器自己在锁定登记表里的条目**
（`weapon+0x1678`），激光每帧把命中点写进条目 `+0x10`。导弹要先**直飞 720 帧（12 秒）**才开始追（CP[8]），追的是激光**当前**命中点。
TV 制导最省事的做法：在已有的 MissileBullet01 第 5 槽钩子（`src/missile.cpp UpdateHook`）里，对认出来的那一枚**直接改自身速度方向**
`B+0x13D0`（模型朝向跟它走），把 `B+0x13B8` 写大让原版永不转向；镜头 / 冻结人物 / 输入全部复用 `src/map.cpp` 现成的三件套。

## 1. LaserMarkerCallFire 流程

### 1.1 对象布局（weapon = Weapon_LaserMarkerCallFire，vtable 0x17E4750，ctor 0x6A1090）

| 偏移 | 内容 | 依据 | 置信 |
|---|---|---|---|
| +0x120 | 持有者（人物）。`0x691240` 用它的 `+0x120` 网络对象判远端，与 `common/edf/layout.h kRiderNet` 同一套 | 0x69124C | H |
| +0x1570 | 激光指示子对象 marker（ctor 0x6A0C50，init 0x6A28B0(marker, owner, weapon, …) @0x6A1249） | 0x6A10E5 | H |
| +0x1678 / +0x1680 | = marker+0x108/+0x110：激光的**锁定登记条目** shared_ptr，`0x22E210(*(img+0x20B2AB0), out, owner, 2)` 新建（与 AddLockTarget 同一登记表，条目 +8 = owner） | 0x6A2981..0x6A29A2 | H |
| +0x1780 | = marker+0x210：激光本帧命中点 | 0x6A67ED | H |
| +0x18A0 | 共用发弹单元 IndirectFireUnit（ctor 0x2B3940 @0x6A10FF；见 docs/airstrike-re.md §3） | | H |
| +0x18C0 | = 发弹单元 +0x20：发弹单元的瞄准点 | 0x6A407C/0x6A408C | H |
| +0x1940/+0x1948 | = 单元 +0xA0/+0xA8：子弹锁定条目的弱引用（ctor 在 0x6A19AD 写成 marker+0x108/+0x110，即激光条目） | 0x6A19AD..0x6A19BB | H |
| +0x1D00 | = 单元 +0x460：**本单元已生成子弹的 std::list**（节点 +0x10 对象，+0x18 控制块；list size 在单元 +0x468） | 0x2BA1F4..0x2BA24D | H |
| +0x1D60 / +0x1D70 | 平滑后的瞄准点 / 其速度（每帧向 +0x1780 逼近，步长上限 +0x1D88=CP[2][0]，阻尼 +0x1D8C=CP[2][1]） | 0x6A3E59..0x6A4071 | H |
| +0x1D90 / +0x1D91 | 呼叫进行中 / 激光强制常亮 | 0x6A5CF6, 0x6A5DE5, 0x6A6188 | H |
| +0x1DE0 / +0x1DE4 | CP[0] 呼叫倒计时初值 / 计数器（Tempest 180 帧） | 0x6A13AC, 0x6A5DEB | H |
| +0x1DE8 | CP[3]：发射态选择（0→0x6A59F0，1→0x6A5850，2→0x6A4F10）。Tempest = 1 | 0x6A5EEB | H |
| +0x1DEC | CP[1]：1 = 呼叫后激光常亮 + 单元瞄准点跟平滑点走。全部 Air Raider 导弹都是 1 | 0x6A147C, 0x6A3E60, 0x6A5DDC | H |
| +0x1DF0 | CP[4] 发弹规格（ShotSpec），ctor 交给 `0x2B5F40` 解析 | 0x6A1978 | H |
| +0x1EE0 | 状态机（`0x6A4B70` 切态；状态内计时器在 `[+0x1EE8]+4`） | | H |

### 1.2 激光怎么取点（H）

- 武器第 5 槽 `0x6A3D20` 每帧：起点 = 枪口（`[+0x1D0]+0x80`），终点 = 起点 + 瞄准方向 `+0x170` × (`+0x894` AmmoSpeed × `+0x898` AmmoAlive)
  （0x6A3D40..0x6A3DB3；Tempest A1 = 1×750 m，A2–ATS = 1200 m）；激光开 = `+0x13A && (+0x1D91 || +0xBE8 > 0)`（0x6A3DBE..0x6A3DDB，
  +0x13A / +0xBE8 的确切语义 M：持枪 / 扳机计数），然后 `0x6A64A0(marker, dt, &from, &to, on, mode)`（调用点 **0x6A3DFB**）。
- `0x6A64A0`：先把条目 `+0x29`（有效）清 0（0x6A6640）；持有者是远端则直接返回（0x6A6504 → `0x691240`）；激光开时做地图 / 物体射线
  `0x11A7480`（0x6A676F），命中则条目 `+0x29 = 1`、条目 `+0x10` = 命中点（**0x6A67C0 / 0x6A67D0**），`marker+0x210` = 命中点，
  联机时 `0x6A4620` 把命中点作为消息 10 发出去（0x6A67E3）。**没打中（朝天）= 条目无效**。

### 1.3 呼叫状态机（H，时间为 M）

1. 开火 → 状态 `0x6A5BA0` phase0：联机发 `OnlineSys_IndirectRequest`（0x6A5BF3），语音，`+0x1DE4 = CP[0]`（180 帧），`+0x1D90/+0x1D91 = 1`，
   平滑点初始化为激光点（0x6A5CEA..0x6A5D14）。
2. phase1 每帧：倒计时；状态第 120 帧把 `+0x1D91` 设为 `CP[1]==1`（Tempest 继续常亮）；倒计时到 120 时播语音；≤0 时按 CP[3] 切到
   发射态 `0x6A5850`（0x6A5EE7..0x6A5F22）。
3. `0x6A5850` phase0：`0x2B8090(unit)` 装填（发数 = 规格[2] → 单元 +0x2F0）、`0x2B4340`（取随机方位角存单元 +0x2C8）；之后每帧等
   `0x2B7B90(unit)`（**剩余发数 0 且列表里的子弹全死**，死 = 对象 `+0xC34` bit0）为真才回到待机 `0x6A6170`（清 `+0x1D90/+0x1D91`）。
   所以**导弹活着期间激光一直强制开着**（只要 +0x13A 还为真）。
4. 发弹：武器第 5 槽末尾 `0x2B95A0(weapon+0x18A0, dt)`（0x6A409D）。每发：瞄准点 = 单元 +0x20（+ 随机散布，半径单元 +0x224），
   出生点 `0x2B43A0`（用 +0x2C8 随机方位角的 sin/cos + 规格 [[0,0.5],[1000,300]]：推断是目标水平 1000 m 外、高 300 m，M），
   矩阵 = look-to(瞄准点 − 出生点)（0x2B9D29），`CreateObject 0x1194280`（0x2B9F68），子弹弱引用挂进 +0x460 列表（0x2BA1F4..）。
   **一次呼叫只有 1 发**（Tempest 规格[2] = 1；其它 AH/Lionic/N5/N6 是 4–45 发，H 数据）。从按下到出弹 ≈ 180 帧 + 单元首发延迟（M，约 3–4 秒）。

### 1.4 导弹拿到的锁定条目（H）

- 子弹基类 ctor `0x22E9C0` → `0x231CC0(core, B+0x60, param+0x30)` → `0x2307F0` 把参数块拷到 core+0x9A0：块 +0x30/+0x38 → core+0x9D0/0x9D8
  = **B+0xB10/B+0xB18**。参数块 = 单元 +0x70，所以 B+0xB10 = 单元 +0xA0 = 武器 ctor 写入的 **marker+0x108（激光条目）**。
  （单元 +0x450 那一对 → 块 +0x48 → B+0xB28，LaserMarker 从不写，空。）
- MissileBullet01 转向 `0x269AF0`：年龄 < CP[8]（`+0x13B8`）就不转（0x269B7C..0x269B84）；否则默认瞄准 = 位置 + 自身前向
  （0x269B8A..0x269BA0：没锁 = 直飞），条目强引用还在且 `+0x29` 有效则用条目 `+0x10`（0x269C35..0x269C5F）→ 纯追踪 `0x26A380`。
- 因此原版 Tempest：先沿出生方向直飞 720 帧，之后追激光**当前**点（每帧转 CP[5] 弧度）；激光朝天 / 收枪 → 直飞。

### 1.5 五把 Tempest 的数据（SGO，H）

| | 出弹规格 | 导弹 CP（类型, …, 加速 CP4, 转角 CP5, 极速 CP6, 导引延迟 CP8） | 伤害 | AmmoAlive（激光长） |
|---|---|---|---|---|
| A1 eWeapon087 | 1 发，弹速 1.0 | 2, 0.03, 0.03, 1.0 m/帧, 720 | 20000 | 750 |
| A2 eWeapon089 | 1 发 | 2, 0.03, 0.03, 1.0, 720 | 30000 | 1200 |
| A3 eWeapon094 | 1 发 | 1, 0.03, 0.03, 1.0, 720 | 80000 | 1200 |
| AT eWeapon095 | 1 发 | 1, 0.03, 0.03, 1.0, 720（大号音效） | 160000 | 1200 |
| ATS eWeapon098 | 1 发，弹速 1.5 | 1, 0.045, 0.03, 1.5, 720 | 320000 | 1200 |

- 「AT/ATS 可制导」在数据里**不存在**：五把的 CP[1] 都是 1，其它导弹武器也都是 1。区别只在伤害 / 速度 / 导引类型。
- **区分 Tempest 与其它激光呼叫导弹**：CP[8] = 720 只有这五把（其它 120–360）、且一次只出 1 发（H 数据）。

## 2. 运行时认出这一枚并绑到玩家

在 MissileBullet01 第 5 槽钩子里（H 字段，组合判据 M）：
1. `At<void*>(b,0)==image+0x17A1C10`；
2. `At<int32>(b,0x13B8)==720`（Tempest 的 CP[8]；插件写过之后用自己的表记录，不再靠它）；
3. 条目 `e=At<u8*>(b,0xB10)`、控制块 `At<u8*>(b,0xB18)` 的强计数 `+8 > 0`，`At<void*>(e,8)` == 本地玩家人物（条目对象 = 激光持有者，0x6A123F→0x6A2981）。
   等价的更硬绑定：`e == At<void*>(weapon,0x1678)`。
- 反方向（从武器找子弹）：遍历 `weapon+0x1D00` 的 std::list（头指针，节点 {next, prev, obj +0x10, ctrl +0x18}，同 `0x2B7B90` 的走法）；
  obj 是 dynamic_cast 到 SceneObject 的指针（0x2BA020），对单继承首基类应等于 B（M：运行时核 vtable 0x17A1C10）。
- 死亡：`B+0xC34` bit0（= missile.cpp kFlags & kDead）；年龄 / 寿命见 missile.cpp。

## 3. 转向钩子怎么做

- (a) 写条目 `+0x10`：条目是**激光的**，`0x6A64A0` 每帧先清有效再按激光射线重写；要在武器第 5 槽之后覆盖，还要先把 `B+0x13B8` 清 0 才会转；
  转向受原版纯追踪 0.03 rad/帧限制。好处只有一个：见 §6，远端副本也追同一个点。
- (b) **推荐**：在 `UpdateHook` 里、调原版之前写 `B+0x13D0`（自身速度）方向，保持模长；同时把 `B+0x13B8` 写成 1000000，原版永不转向
  （missile.cpp 的 kNoStockHoming 同一招）。原版之后照常加速 CP[4] 并钳到 CP[6]（0x269B59..0x269B77、0x26AAD7..0x26AAF2），
  再用 **look-to 0x4E220 沿 +0x13D0 重建 B+0x60..0x9F 矩阵**（0x26AAF7..0x26AB25，H）：模型机头跟速度。继承速度 +0x13E0 不参与朝向。
- 插件转向：每帧方向 = 旋转(当前方向, 偏航 Δ, 俯仰 Δ)，Δ 来自鼠标 / 右摇杆，限一个最大角速度（原版 0.03 rad/帧 = 103°/s 可作上限）。
- 注意 `src/missile.cpp Guide()` 只处理 CP[9]==4242 的插件导弹，Tempest 会被它跳过；TV 分支要放在它前面。

## 4. 镜头（复用 map.cpp）

- 现成机制（H，`src/map.cpp`）：串接 `CharacterGhostCamera`（vtable 0x1768C10）第 4 槽 `0xF86A0`（`InstallCamera`, map.cpp:759）；
  `CamStepHook`（map.cpp:710）先把上一帧原版矩阵放回 `cam+0x220`，调原函数，存下这一帧原版矩阵，再由 `Camera()` 用
  `PutView(cam, eye, look)`（map.cpp:641：游戏自己的 look-to `0x4E220`，行 `+0x220` 右 / `+0x230` 上 / `+0x240` 前 / `+0x250` 眼）写入；
  进出 `Ease` 缓动（kEaseIn 24 / kEaseOut 15 帧）；`cameraSession` / `camSide` 管接管代次；`StockHud` 可藏原版 HUD。目标判定用
  `cam+0x350 / +0x360` == 玩家。
- TV 镜头需要的只是另一组 (eye, look)：eye = `B+0x90` + 前向 `B+0x80` × 2 m（机头前，避免看到弹体）或后方 8 m 上方 2 m（追尾视角），
  look = eye + 前向 × 200。远裁剪：出生点约 1000 m 外 / 300 m 高，`view.cpp ViewMapClip` 那套可以抬（M）。
- **不要另挂第二个第 4 槽钩子**：map 的「放回原版矩阵」只对它自己的 camSide；两个钩子各自 restore/save 会互相把对方的写入当成原版。
  做法：把 map.cpp 的 `pose` 来源泛化成「谁拥有视图：map | tv」（`Pose{eye,look,open,human}` 加一个 TV 来源），沿用同一个 CamStepHook。
- `hud.cpp` 在地图接管时不更新 `LastViewProj`（camera-re.md §8.7）；TV 期间同理，否则 `CameraRay` 的使用者会跟着导弹视线（M）。

## 5. 输入与冻结人物（复用 map.cpp）

- 冻结：`InstallHold`（map.cpp:735）把士兵预更新 `0x572DF0` 里 `0x572F0C` 的手柄测试改道到 shim，调 `MapHumanFrame(human)`
  （map.cpp:632），返回真就走原版「无手柄」路径 `0x573A4D`：人物输入块 `+0xD50` 清空（`0x56D300`），骑乘时座位输入清空（`0x62C120`）。
  人物不走、不转、不开火（H）。**只能有一个 shim**：把它改成 `return Frame(human) || TvFrame(human);`，TV 的每帧逻辑也在这里跑（游戏线程，
  在导弹更新之前或之后都行，导弹钩子读 TV 算好的期望方向）。保持期间插件自己的键由 `MapHoldsKeys()` 屏蔽，EDF6AutoTurret 经
  `EDF6VehicleCrew_InputHeldV1` 同样停手 — TV 要让 `holds` 也为真。
- 鼠标：`MouseDelta(human,&dx,&dy)`（map.cpp:232）：游戏自己的每帧位移 = `[human+0x340]` 手柄对象第 `[human+0xD40]` 条记录（步长 0xA80）
  的 `+0x66C/+0x684`（0x56DCED 读的那一处，除 24 前），保持期间仍更新（地图正是这样用的，H 机制 / 正负号 L）。
- 手柄：`PadState()`（map.cpp:210，XInput，因为保持清掉了游戏自己的手柄输入）+ `Stick()`（map.cpp:226，死区）。
- 开火 / 结束键：`InFront() && Down(VK_LBUTTON)`（map.cpp:176/181）、XInput 右扳机 / B。结束 = 引爆：missile.cpp `Detonate()` 的做法
  （core 标志 `|= 0x20`，年龄 = 寿命，由原版到期流程原地爆炸）。
- 这些函数现在都在 map.cpp 匿名命名空间里，要挪进共享头（例如 `crew.h` 声明）才能复用。
- 释放按键残留：沿用 map 的 `draining`（关闭那一帧键还按着，保持到松开），否则结束时那一下左键会被士兵当开火。

## 6. 联机

- 每台机器各自生成导弹（M）：开火在本机发 `OnlineSys_IndirectRequest`（0x6A5BF3）；远端第 30 槽 `0x6A3190` 收消息 12 重启同一呼叫状态
  （0x6A3227 指向 0x6A5BA0）并跑自己的发弹单元 → 本地 CreateObject。激光点由本机经消息 10（发送 `0x6A4620`，接收 marker 第 2 槽
  `0x6A4102`：写 marker+0x210 与条目 +0x10）流到远端，远端导弹追的是**流过去的激光点**。
- 方案 (b) 只改本机那一枚：远端副本 12 秒直飞后去追原来的激光点 → 远端看到的命中位置不同。伤害在哪台机器结算未知（docs/online-re.md 表末同一未决项，L）。
- 降低分歧（M）：TV 期间同时让激光指向导弹的瞄准点——在调用点 **0x6A3DFB** 把 `from/to` 换成 (TV 眼睛, 眼睛 + 视线 × 激光长) 并强制 `on=1`：
  原版射线、条目、`marker+0x210`、消息 10 全照常，远端导弹追同一点（受它们自己的 720 帧延迟和 0.03 rad/帧限制）。副作用：激光束画在导弹机头上（marker+0x1B0/+0x1C0 是束的两端，0x6A666C）。
- 远端玩家的 Tempest：`0x691240(weapon,1)` 为真的武器 / 条目对象不是本地玩家 → 不接管。

## 7. 实现配方

1. **识别**（missile.cpp UpdateHook，前置）：满足 §2 的 1–3 且 TV 开着（ini 开关）且当前没有别的 TV 会话 → 记下 `tv.b`、`tv.ctrl`、玩家；
   首帧把 `B+0x13B8 = 1000000`。会话以「B 地址 + 年龄单调」认同一枚（同 missile.cpp `RoundOf`，地址会被复用）。
2. **期间每帧**（`TvFrame(human)`，挂在 map shim 里，返回 true = 冻结）：读 `MouseDelta` / `PadState` → 期望偏航 / 俯仰速度；
   左键或 RT → 立即引爆；B / Esc / 地图键 → 结束 TV（导弹继续直飞，回原版镜头）。
3. **导弹钩子**：对 `tv.b` 在原版更新前改 `+0x13D0` 方向（保模长，限角速度），`+0x13E0` 清零（可选）。
4. **镜头**：map.cpp 的 CamStepHook 视图来源加 TV：eye/look 由导弹矩阵算，接管 / 交还用现有缓动；藏原版 HUD 可选。
5. **结束条件**：`B+0xC34` bit0、控制块强计数 0、导弹钩子 2 帧没见到它（missile.cpp kStaleFrames 套路）、玩家死亡 / 换任务（`ResetMissiles` / `ResetMap`）、
   地图被打开（地图优先）。结束后沿用 map 的 draining 直到开火键松开。
6. **签名**：武器 vtable 0x17E4750 第 5 槽 = 0x6A3D20；若做 §6 的激光改道，0x6A3DFB 处 `E8 A0 26 00 00`（call 0x6A64A0）前后字节核对；
   导弹侧沿用 missile.cpp 现有 0x26A880 签名。
7. **日志**：`TV take b=… weapon=… entry=…`、每秒一行位置 / 速度 / 角速度、`TV end (dead|key|stale)`；联机时记 `0x691240` 结果。

## 8. 未确认（需要进游戏看）

- 出生点几何（1000 m / 300 m / 随机方位）与首发延迟（M）；`+0x13A` 是否在冻结（无手柄路径）期间保持为真（影响 §6 的激光改道，M）。
- 列表节点 obj == B（M）；鼠标位移正负（L）；远端是否真的各自生成导弹、伤害结算方（L）。

## 9. 插件的实现（2026-10-06）

- 认法（`TvSteer`，在 `src/missile.cpp` 的导弹更新钩子里、插件导引之前）：vtable 0x17A1C10、`B+0x13B8` = 720、起飞不到 60 帧、锁定条目 `+8` 是本地玩家（`PlayerHuman()`）、
  没有交还过（交还的那一枚按「地址 + 年龄单调」记下，不再接管）。ini `TempestTv=1`。
- 接管：`B+0x13B8` 写成 1000000（原版和 `src/guidance.cpp` 都不再转它）；每帧把自身速度 `+0x13D0` 朝玩家操纵的航向转，最多原版转角 CP5，保持速度大小；
  同时用 look-to 写机头前三行（type 2 的 A1 / A2 沿机头推力，而 update 只对 type 0 按速度重建姿态：`0x26AA8F..0x26AB25`）。
- 输入（`src/map.cpp TvRead`）：游戏自己的鼠标位移（`MouseDelta`）× 0.003 rad × `TempestTvMouseSpeed`，右摇杆满偏 = 每帧原版转角；左键 / RT 加速
  （一次、不可取消：极速 CP6 `+0x13A8` × `TempestTvBoost`，加速度 CP4 `+0x13A0` 至少够 30 帧加满；原版 update 照常加速、钳到新极速）；Esc / B 交还（`B+0x13B8` 恢复 720，已过延迟，立即追激光点）；地图打开也交还。叫出导弹时按着的左键要先松开一次才算加速。
- 冻结人物：地图的同一个 shim（`MapHumanFrame` 里 `Frame` 之后调 `TvFrame`）；结束时按着的键沿用「松开前一直保持」；`MapHoldsKeys` 同时看电视制导。
- 镜头：地图的同一个第 4 槽钩子（`Camera()`：地图没开且电视制导有画面时用它的眼睛 = 导弹位置 + 机头 × 4 m、看向机头前 300 m），缓动进出与隐藏原版 HUD 同地图。
- 没做：§6 的激光改道（让别人机器上的副本也追同一点）。
