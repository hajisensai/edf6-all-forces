# 2026-10-12 反馈：登车 / 登机 / 换座

## 用户原话

1. 「有些npc派他上车的时候有几率被卡住导致上不了车」
2. 「上npc飞机的时候，速度会清零」
3. 「用登车狙击枪上飞机以后从座位1切换到座位2以后无法开火，并且视野无法动。并且无法切回座位1，显示有人」

能有两个座位的插件飞机只有炮艇机（座位 0 驾驶、座位 1 侧炮手，`docs/player-jet-re.md` §11），第 3 条按炮艇机处理。

## 1. NPC 登车「有几率」卡住

- 路径：`src/npcai.cpp Board`。士兵走到上车点（座位 MAB 定位点）正下方的地面点 `walkAt`，导航到达判定是水平距离 ≤ 停止半径；
  下一帧再要求三维距离 `Dist(pos, at) < reach`（原生触发球，`reach` = 定位点半径 + 0.5）才调 `RideVehicle`。
- 根因：停止半径固定 0.5 m，地面点只验了「正下方那一点在球内」（`Dist(walkAt, at) < reach`），没给停止圈留余量。
  上车点离地越高（落差 > √(reach² − 0.25)），到达时残余的 0–0.5 m 水平偏差就越可能把士兵留在球外；导航此时已报「到达」→ `Stand`，
  一直站到 `kBoardMs`（20 s）命令作废。「有几率」= 到达点落在停止圈的哪里。
- 证据：测试站 report-10 日志 00:16:30 下达登机命令后，00:16:36–37 同一小队被重复下令 8 次（用户在反复点），两名士兵在最后一次命令后
  13 s、19.6 s 才坐上（差 0.4 s 就超时）。
- 修复：`src/npc_logic.h BoardStop(drop, reach)`：按该高度下触发球真正留出的水平空间算停止半径（脚底再算 5 cm 余量、取 90%，最多 0.5 m），
  停止圈内任意一点都在球内；空间小于 0.1 m（地面在球外或球的边缘）才不走过去。地面高度的上车口仍是 0.5 m，行为不变。
- 测试：`tools/npc_ai_check.cpp Boards`：4 种半径 × 21 种落差，「停止圈内任意到达点都在触发球内」；负对照：reach 1.0、落差 0.9 时旧的 0.5 m
  停止圈最远点在球外，新停止半径 ≈0.28 m 全部在球内。

## 2. 登上 NPC 飞机后速度清零

- 路径：`src/playerjet.cpp PlayerJetFrame → Fly → Board`。NPC 驾驶的插件飞机在玩家坐上驾驶座之前没有 `PJet` 记录（`Held` 为假时每帧 `Forget`），
  坐上的那一帧 `Make` 新建记录，`Measure` 需要上一帧位置，`fresh=false`、`measured` 全 0。
- 根因：`Board` 用 `measured` 作为接管时的速度（`j.vel = j.measured`），于是空中接管（登车狙击枪命中、炮艇换回驾驶座）一律从 0 m/s 起飞——失速。
  测试站日志里每条 `PJET ... one of ours boarded` 后面都是 `0 m/s`。
- 修复：`SeedMeasured`：没有自己一帧测量的记录，用 `jet.cpp` 里这架飞机 NPC 飞行写的速度（`jet::FindJet(v)->m.vel`，m/s）；没有条目或非有限值就不编。
- 测试：`tests/playerjet_recovery_test.cpp`（include 生产 playerjet.cpp）新增 4 项：空中接管继承 NPC 速度、已有测量的记录不被覆盖、没有飞行条目不编造、非有限值不采用。

## 3. 炮艇机换到炮手座后不能开火、视角不动、换不回驾驶座

- 换不回（「显示有人」= 换座拒绝提示）：`src/seatswitch.cpp MayTake` 对插件飞机的驾驶座要求 `PlayerJetBoardable`（在地面或低空低速），
  这是步行登机的规则；当初限制空中接管，实际挡的是上面第 2 条的速度清零。第 2 条修好后，机上换座在空中也允许（职业掩码、座位占用照旧检查；
  座位上的 NPC 照旧与玩家对调）。
- 视角不动 / 不能开火：炮手座共用驾驶座的三个 MAB 定位点（§11.1），原版 Type-1 座位镜头只跟着机身，骑手的鼠标 / 右摇杆不转任何东西；
  炮手的瞄准点是屏幕中心视线与地面的交点（`GroundSight`，3000 m 内），镜头不动就常常对不到地面，扳机什么都不打。
  修复：`playerjet_crew.inc GunnerLook` 每个游戏帧用骑手自己的瞄准输入（座位 `+0x2D0`，鼠标同样写这里）转一个世界坐标下的航向 / 仰角
  （`turretcam.h` 的约定与 `TurretCamRate` 速率，放大时按倍率减慢）；`turretcam.cpp LookHook` 在 Sazabi、直升机鼠标镜头之后加一环
  `GunshipGunnerCamera`：眼睛沿视线方向放到机身外（原版镜头到注视点的距离），屏幕中心就是骑手指的方向，炮艇不挡十字。

## 验证

- `build.cmd`（/W4 /WX）通过；`offline_checks` 全部构建；`ctest` 225/225 通过（Skipped 的是需要本机游戏文件的原生审计）；
  `tools/selftest.py` 172/172；`npc_ai_check` 1553 项 0 失败；`seat_switch_check` 12 项全过；`playerjet_recovery_check.py` 60 项通过，`--negative-controls` 各变异均被抓到。
- **未真机验证**：炮手镜头的手感与方向符号（沿用 turretcam 已实机使用的约定，但炮手座没在游戏里看过）、炮手座主射击在键鼠上是哪个键
  （按键行 6，`docs/player-jet-re.md` §11.3 第 3 条仍待确认）、空中换回驾驶座后的飞行、NPC 登车在各车型上的到达。
