# 2026-10-10 尾焰 / 入场尾烟改为从模型取（exhaust-from-model）

## 用户原话

> 尾烟和实际不对。尾烟应该跟着模型生成，而不是用两段可能不同步的代码维护。

## 根因

喷口位置在模型之外被手抄了几份，彼此只靠正则自检对齐，模型里没有任何喷口信息：

| 位置（修改前，基于 origin/main 90e8e85） | 内容 |
|---|---|
| `src/booster.cpp:79-92` `kJetNozzles` | 喷气机按 SGO mark 查的喷口坐标表（模型系） |
| `src/booster.cpp:96-99` `kBomberNozzles` | 接管的原版轰炸机（bomber401 / bomber501_2）喷口 |
| `src/booster.cpp:194` `kNozzleAt` | 空母四个推力舱的喷口：从 V508 MAB「ブースト0..3」手抄后 ×1.6 |
| `src/booster.cpp:351`、`:601` | 喷射方向写死为机身骨骼 −z，入场尾烟（`SmokeFrame`）与尾焰（`JetFrame`）各算一遍 |
| `pylib/jet_models.py:541-548` `NOZZLES` | Python 侧又一份常量；`check_nozzles` / `tools/selftest.py:131-173` 用正则把 C++ 表和它对齐 |
| `src/sazabi_pose.h:58-77` `kNozzles` | 沙扎比手量的喷口（本次未动，见「后续」） |

由此带来的实际问题：

1. **空母有尾焰没有尾烟**：`NozzlesOf` 里没有空母的行，`SmokeFrame`（`src/booster.cpp:592`）直接返回；而且空母走 `Rotor` → `Thrusters`，
   从不调用 `JetSmoke`。日志佐证：`EDF6VehicleCrew.log.1` 里全部 16 行 `SMOKE v=... exhaust N: trail ... laid` 都只有 exhaust 0/1，从没有
   四喷口的机体铺过烟。
2. **尾焰和尾烟是两条路径**：同一喷口的矩阵在 `JetFrame` 和 `SmokeFrame` 里各自从 `kJetNozzles` + 机身矩阵算，任何一边改动都可能让烟和火对不上。
3. **原版 bomber401 / bomber501_2 的喷口在模型空间量、运行时挂骨骼世界矩阵**，只在该骨骼绑定姿态为原点 + 单位阵时才对，没有校验。
4. **空母倾转时尾焰慢一帧**：推力舱的局部矩阵在输入步（`Thrusters`）里写，骨骼记录的世界矩阵还是上一帧的倾角（`exhaust_pose.h` 的 `Carry` 只补了机体位移，没补舱的倾转）。

## 修法

模型成为唯一数据源，尾焰和尾烟走同一条路径。

### 构建期（Python）

- `pylib/jet_models.py`：
  - `with_nozzles` 在模型里插入喷口骨骼 `nozzle_0..n`（`NOZZLE_BONE`）：kind 0（只有变换）、unbounded（不进实例包围盒）、挂在喷口随之运动的骨骼下；
    绑定原点 = 出口中心，+z = 喷射方向，`half`（+0xA0）= (火焰长度, 宽度, 0, 1)。引擎把模型骨骼的 +0xA0 原样拷进实例骨骼记录的 +0xF0
    （`EDF.dll 0x1111042`，本次静态反汇编 `0x1110FC0` 核实，H），运行时就从那里读火焰大小。
  - 喷气机（`exit_nozzles`）：仍用 `NOZZLE_EXITS` 选出口边缘顶点、`exit_of` 量中心与直径（量取逻辑保留，手抄的 `NOZZLES` 常量删除），挂网格骨骼，
    方向 = 网格骨骼绑定的 −z（推力轴）。没有用出口几何法线：截击机的方形喷口是斜切面，法线偏推力轴约 30°，火焰应沿发动机轴。
  - 空母（`carrier_nozzles`）：构建时从 `V508_TRANSPORT.SGO` 读 `boosts`（定位点名、长度、宽度、点亮标志）和 `animation_model[2]` 内嵌 MAB 的定位点
    （节点 = 推力舱骨骼、位置、欧拉角），点亮的四个各挂到自己的推力舱下，偏移和火焰尺寸乘模型缩放 1.6。不再手抄 `kNozzleAt`。
    只接受原版的 (0, π, 0) 欧拉角（引擎的欧拉顺序没有逆向过，别的角度直接报错）。
  - `jet_gear.insert_bones` 增加 `kind` / `bounded` / `half` / `renumber_skin` 参数（默认值不变，起落架行为不变）；空母的推力舱后面还有被蒙皮的骨骼，
    插入时按新序号重写顶点的 BLENDINDICES。
  - 构建自检 `check_nozzle_bones`（`build` / `elevon_archive` 里对每个产物执行）：喷口骨骼存在、kind/bounded 正确、父骨骼与绑定矩阵等于量出值
    （1e-4）、火焰尺寸一致；去掉喷口骨骼后每个蒙皮顶点、每个 object 仍挂在同名骨骼上；`strip_nozzles` 与 `with_nozzles` 互逆（字节一致）。
- 原版模型（炮艇机与空袭接管用的 bomber401、bomber501_2）不能改：`tools/gen_nozzles.py` 用同一套量取逻辑在 Root.cpk 的原版模型上量，
  生成 `src/nozzles_gen.h`（按「模型自有骨骼名 + 骨骼数」认模型，喷口的局部矩阵按父骨骼绑定求出，不再假设父骨骼在原点）。
  头文件入库；头里带一行 `// data:` JSON 记录生成输入（生成器版本、`FLAME_LENGTH_PER_DIAMETER`、出口框）、量取结果和原版 .mdb 的 sha256。
  `--check`：有游戏时重新量取比对；没有游戏（CI）时核对记录的输入与当前一致、且由记录重新渲染的文本等于文件。已加入 CI 的
  「Generated files are current」步骤。
- 量出的值与旧手抄表一致（截击机 (±2.327, 1.511, −7.804) 1.195 m、攻击机 (±3.58, 2.325, −12.006) 1.839 m、多用途机、无人机、bomber401、
  bomber501_2，空母 F 56/16、B 40/12），所以位置本身没有变化；变化在于数据来源只剩模型。

### 运行时（C++）

- 新增 `src/exhaust_nozzles.h`（纯函数，可离线测）：`FindNozzles` 在骨骼记录里按名字找 `nozzle_0..`（与 `BoneRecord506` 同样比前 16 字符），
  读父骨骼序号（+0x10）、局部矩阵（+0x70）、火焰尺寸（+0xF0）；找不到时查 `kStockNozzles`（骨骼名 + 骨骼数都对上才算原版模型，
  旧安装的多用途机模型有 `bomber401` 骨骼但骨骼数不同，不会误用原版表）。
  `NozzleWorld` = 喷口局部 × 父骨骼当前局部 × 父骨骼上一帧局部⁻¹ × Carry(父骨骼记录)：父骨骼记录是上一帧的姿态，先按机体位移搬到本帧，
  再把上一帧的父局部换成本帧的（空母推力舱在输入步里刚倾转过）——倾转零延迟。不依赖喷口骨骼自己的世界矩阵是否被游戏更新
  （`docs/drill-re.md`：update 对 flag==1 的所有记录算 world，无网格骨骼也算；但这里不需要它）。
- `src/booster.cpp`：删掉 `kJetNozzles` / `kBomberNozzles` / `kNozzleAt` / `NozzlesOf` / `NozzleMatrix` / `ModelBone` / `ExhaustBasis` /
  `CarrierFlames`。每架机每帧由 `ExhaustOf` 算一次喷口世界矩阵（按骨骼数组指针缓存喷口集合），`JetFrame`（尾焰）与 `SmokeFrame`（入场尾烟）
  都只读它。入场烟的条数从 2 改为 `kMaxNozzles`（4）。
- `src/jet_flight.cpp Thrusters`：空母改为 `JetFlames(...)` + `JetSmoke(v,Entering(j,ms),ms)`，在推力舱姿态写完之后调用。空母因此也有入场尾烟。
- 不再按 mark 查表：玩家驾驶的空母（以前 `NozzlesOf` 查不到空母 mark，没有尾焰）现在也有四个尾焰。

## 测试

- 离线 C++：`tools/exhaust_nozzles_check.cpp`（已加入 `EDF6_OFFLINE_CHECKS` / ctest）：替身骨骼数组上找喷口骨骼（顺序、父骨骼、局部、尺寸、
  断号与无效尺寸截断、名字精确匹配）；原版表按骨骼名 + 骨骼数匹配（bomber401、bomber501_2、多骨骼的 bomber401 不匹配）；空母逐帧飞行仿真
  （悬停 / 120 m/s / 转弯爬升 / 245 m/s，推力舱倾转）中火焰与本帧画出的喷口误差 < 1 mm，且不跟父局部时误差 0.317 m（测试自证能看见它守的东西）。
- selftest：`exhausts_come_from_the_models`（booster.cpp 不再有自己的喷口表 / 路径，尾焰和尾烟都读 `ExhaustOf`；C++ 与 Python 的骨骼名前缀、
  上限一致；`nozzles_gen.h` 是最新；每个有排气的模型都生成喷口骨骼，有游戏时逐模型 `check_nozzle_bones` 且数量为 2 / 1 / 4）；
  `nozzle_bones_in_a_made_model`（无游戏的替身模型：出口量取、插入位置、蒙皮重编号、写出读回、`strip_nozzles` 字节还原）。
- 结果：`build.cmd` 退出 0；`offline_checks` + ctest 207/207 通过（Skipped 为需游戏 DLL 的 native 测试）；`python tools/selftest.py` 148/148；
  `python tools/gen_nozzles.py --check` 在有游戏 / 无游戏两种模式都通过；`make_jets.build` 全量在内存里构建 81 个文件通过（不写游戏目录）。
- 变异实测见提交说明（去掉父局部修正、去掉蒙皮重编号、去掉骨骼数匹配，对应测试均变红）。

## 未实测（没有进游戏）

- 游戏是否接受 kind 0、unbounded、不带网格的新骨骼（加载、CAS 绑定、Havok ragdoll / `EDF6VC_*_AIRFRAME.SHKT` 是否只按名字对骨骼）：
  静态上骨骼记录逐条从模型拷贝（`0x1110FC0`），CAS 与 SGO 映射按名字；没有在游戏里跑过。
- 空母插骨骼后推力舱之后的骨骼序号变了（v508_transport 的 object 骨骼 7 → 11）：蒙皮顶点已按名字核对，但游戏内画面未看。
- 实例记录 +0xF0 的火焰尺寸在游戏里读出的值（静态 H，未在日志里看过；Debug 时 `FLAME v=... nozzle 0 at ... W x L m` 会打出来）。
- 空母倾转零延迟、空母入场尾烟的观感。
- 需要重新运行安装（`make_jets`）生成带喷口骨骼的模型；旧安装的模型没有喷口骨骼时，自家机型没有尾焰和尾烟（日志 Debug：`FLAME v=... 0 nozzles (none in its model)`）。

## 后续（未做）

- 沙扎比（`src/sazabi_pose.h kNozzles`）：喷口是对着用户提供的模型文件夹（OBJ，不在仓库里，CI 没有）在渲染图上手挑的，没有可自动化的量取逻辑；
  它的火焰由插件自己的骨架姿态代码（`sazabi_arms.inc Flames`，带 burst 分组）摆放，不读骨骼记录。改成模型骨骼需要先在 `pylib/sazabi_model.py`
  里写出喷口钟形件的自动识别，再把 `Flames` 改为按骨骼取，工作量与风险都与本次不同，单独做。
- `TrailMake` 初始「方向」写的是 −z（`back={-m[8],...}`），之后每帧更新写的是 +z，两处不一致（原有行为，未改，未实测哪个对）。
