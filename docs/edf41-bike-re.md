# EDF4.1 Vehicle408_motorbike：真实三轮 / 副座枪手适配

2026-10-19。源资源是本机 EDF4.1 的 `Vehicle408_motorbike`，不是将 EDF6 Freed 改名。运行模块 `src/edf41bike.cpp/.h`，数据适配 `pylib/edf41bike.py`。下列地址均为本机 Steam EDF6 `EDF.dll`（TimeDateStamp `0x678CCB46`）的 RVA；特别标出的旧地址属于 EDF4.1 `EDF41.exe`（`0x57AAB039`），绝不拿旧地址写 EDF6。

## 不能直接套现有 sidecar 的原因

现有 `src/sidecar.cpp` 的专属标记为 `edf6vc_sidecar`：503 双轮物理，第三轮仅外观，右侧固定站立点 `(-1,.30,.35)`，乘员在步行状态使用自己的武器。源 408 是左侧 `+x` 边斗、三个真实轮、八个 proxy、原生副座与挂载机枪，因此适配到 `Vehicle_Car` 的真实动态轮表和两座位，保留源骨、模型、MAB、SHKT / 双向 ragdoll binding。

源代理：`body`、`handle`、`front_sus`、`front_tire`、`rear_sus`、`rear_tire`、`side_tire`、`gun`。gun 的原约束是双轴 `hkpRagdollConstraintData`，无需拆成虚构 yaw / elevation 刚体。GenericCarBase 的 type 3 虽能解析，但 `0x5EC1C0` 只验证轴并写类型，没有建立马达；不能把它当可用双轴控制。

## 专属合同

- 新 marker：`edf41_motorbike`；SGO key：`edf41_bike`，schema 1，原 `bike_gun_ctrl=[-30,20,-15,30]` 与 operator seat 1。
- `Vehicle_Car` 使用真实的 V512 空 `car_aim_node=[]` 基底；三个原轮子分别有源位置和自己的驱动 / 转向标志。单机炮 holder 为 `['gun',1]`，驾驶座没有武器。
- gun 不列入 `car_base_rigid_body` 动态名单；必须显式加入 `car_base_simulation_node` 的 `['gun',1]` 行，选 native mode 2。所有八个原 proxy 和原双向 binding 保留。载具 / 枪手姿态适配的资源检查见 `tests/test_edf41bike.py`。
- 不改现有 503 sidecar，也不为普通 Car 改驾驶 / 火控。该模块要求 Car vtable、schema 和独立 marker 同时吻合。

## 原始单骨双轴算法与物理同步

旧 `VehicleBike` 不是直接设置猜测角速度：旧 vtable `0xAA0670` 的 pose 方法 `0x2F5900`，调用 `0x2D6BE0` 时把同一个 gun local 矩阵传两次。相等分支先形成 `X(-pitch)` 再叠 `Y(-yaw)`，保留平移，然后由基类进行 world build / 物理同步。

EDF6 `VehicleWeaponAim` vtable `0x17D8A68` / `0x17D8A90` 的 slot 3 都是 `0x5FACD0`，同样支持矩阵地址相等的单骨双轴分支。它不再取负，因此原 source 允许范围转换为 native yaw `[-20,+30]°`、pitch `[-30,+15]°`。

408 的 pose 顺序严格为：

1. `0x6EDCA0`：动态物理代理读回骨。
2. `0x5FACD0(Aim, gunLocal, gunLocal)`：原生单骨双轴姿态。
3. `0x1100B90`：构建骨的 world 矩阵。
4. `0x6ED800`：将动画驱动骨同步到真实物理 proxy。

GenericCarBase `0x65C587` 传 mode 1 给 VehicleBase `0x629450`；但这不是最终模式。CarBase 在读取 `car_base_simulation_node` 后，`0x6640A9` 无条件调用 `0x6E40B0`，循环以 `0x6E40F0 / 0x6E40F8` 将**所有** proxy 先切到 mode 4。仅从动态名单排除 gun 仍会留下 mode 4，这是上轮遗漏。

现在生成器显式加 `['gun',1]`：`0x664279` 检查行值 1，`0x66428F` 将值加 1，`0x664292` 调 `0x6ED480(record,2)`，使真实 gun body 在 `0x6ED800` 走 kinematic target。三个轮子的行值 0 则经 `0x6642A2 → 0x6E94E0 → 0x6E972A` 恢复构造器默认 mode 1；两种行值含义不能混淆。

### 原生 Car 的开火后坐力

旧 408 setup 使用 `['AimRecoil',[.002618,.0062832]]`。Car fired callback `0x65A740` 先经 `0x65A750 → 0x62EA10` 保留 weapon 的真实 `FireRecoil`，随后 `0x65A76B / 0x65A76D → 0x5FA150` 请求 variant 0（BodyRecoil）。错误 variant 返回 null，而 `0x65A83A → 0x5F9FB0` 消费 payload 前没有检查 null。

`native_gun_rows` 因此只将 holder 的 hull impulse 参数转换成合法 `[0,0]`。实体 `vehicle_setup`、`mission_setup`、独立测试场 SGO 和五个呼叫的覆盖 setup 全部使用同一转换，机枪 SGO 的真实 `FireRecoil=.3` 保留。测试同时检查源值未被清零、坏 variant / 缺显式 mode / 错 mode 被拒绝。

## 副座初始化、输入与联机

Car 的 setup build `0x65B37E` 原本始终把 seat 0 的 Aim 交给 `0x633330`。专属 wrapper 只在模块全部安装成功且 schema 匹配时改为 seat 1 Aim，其余参数不变。原生 `0x5FB810` 读取源机枪第三项 `[90,.015,.1]` 初始化速度 / 加速度，避免复制含共享引用的 Aim 内存。

该调用发生在所有座位建立之后：VehicleBase `0x629B36 / 0x629C47` 完成 seat vector，Car ctor `0x6598E5` 设最终主 vtable `0x17E01B0`，再由 slot 46 `0x65A910` 执行 setup。构造期不会因“两座尚未建立”漏掉副座。

原生 Car frame 只读取 seat 0 Aim；408 的 frame 先执行原 next，再对 seat 1 使用 `[-RX,+RY]` 调原生 Aim slot 2。开火只从该座的 primary `>=.8` 拉它唯一 holder。原生 Car slot 5 `0x65B5E0` 因 seat 0 weapon count 为 0 跳过控制路径，不会再替驾驶座重复拉枪。

**非空角度不等于有网络同步资格。** 原生发送 `0x6770B9` 调 `0x5FB7E0` 检查两轴 mapping vector 是否非空。模块以原生 `0x5FB790` 注册两份 12 字节 `AxisMap{gunBone,lo,hi}`，由原生分配并管理 mapping；模型记录尚未就绪时待其首次可用，只初始化空 mapping，之后不重复分配。

- Car NetObject vtable `0x17E0458[7]` → `0x676B40 / 0x676F50`，`0x677065` 按 seat count 遍历、`0x6770A4` 按该座权限、`0x677119` 调 `0x5FBBC0` 发 yaw / pitch。
- receive `[8]` → `0x66BA60 / 0x66BAC0`，`0x66BBFC` 检查 seat index，`0x66BC1F..33` 以 `seatIndex*0x340+0xE0` 调 `0x5FAAE0` 更新目标。
- Car 的控制权同步由 `0x672AD0` → `0x62E6C0` 遍历所有座位，选择 `0x5FBA20` 远端插值或 `0x5FB880` 本机控制。
- 本机玩家由公共 `IsPlayer` 排除 remote rider；NPC 使用现有 `AiGunner` 权威判断。远端仅调用原生 Aim 插值，从不重复拉火。远端 NPC / Dummy 可能没有占座副本，故 `Aim+C0` 为 remote 时即使空座也只放行插值，仍无 input / fire 权限。
- 地图占键、死亡、空座、本地无权、Enabled=0 都不生产新的射击输入。毁车 / 非本功能车型走原 next pose。部分安装失败时 `on=false`，建枪 wrapper 原样转发原 Aim，不留下半安装的座位改接。

## 已验证与尚待实机

- `tests/edf41bike_native_audit.py [EDF.dll] [EDF41.exe]`：实际读取两份本机 PE，核八组 runtime signatures、CarBase 全体 mode 4 重置 / 显式 mode 2 覆盖、开火 BodyRecoil variant 0 消费、原 pose 分支、Car 构造 / pose 顺序、NetObject 的逐座发收与 mapping 准入。
- `tests/edf41bike_test.cpp`：12 项范围与权限边界。
- `tests/edf41bike_runtime_test.cpp`：直接包含生产 cpp；仅在测试进程私有 fake image 中以记录 trampoline 替代 native endpoint，验证原参转发、seat 1 路由、只初始化一次 mapping、真实调用顺序、普通 Car 原样、远端 / NPC / 地图 / 死亡 / 关闭、profile 拒绝及部分安装失败。
- 生产 cpp 和两个 C++ 测试均以 MSVC `/W4 /WX /utf-8 /permissive-` 实际编译运行。

以上是源资源、真实原生合同和离线 production routing 验证。未启动或操作游戏，未写游戏目录；三轮实际驾驶 / 转向稳定性、乘员动作、gun 接触与毁车效果，以及两机联机显示 / 射击仍需实机验收。

## 2026-10-19 反馈「4.1 的载具召唤出来在空中，而且会自动装填」

### 自动装填（已修）

EDF6 读 `ReloadType`（武器 `+0x208`）在 `0x693E8D`：0 / 3 手动换弹；1（`0x693EA4` 与 `edi=1` 比较）每帧把 `+0xE68` 减 1，即按时间自动回满（EDF6 文本「[自動]$0秒」：手雷、无人机、鲸鱼 105mm）；2 在 `0x693EAC..0x693F18` 扣掉本帧新增的积分（文本「$0pt」：所有载具请求、轰炸机、暴风）。EDF4.1 只有 0 / 1 两种，它的 1 是积分装填（全部 150 个空爆兵呼叫，文本「リロード 900pt」）。移植原样抄了 1，于是 84 个呼叫（载具、鲸鱼炮艇、轰炸机）和 11 把 4.1 激光引导呼叫在 EDF6 里按帧计时自动回满。`pylib/edf5port.py RELOAD_TYPE41` 把 4.1 的 1 换成 EDF6 的 2（`weapon41` 与 `edf41calls._use` 同用），登记表快照按 selftest `ported_weapons_registry_current` 的同一转换刷新。车载武器本身（`ReloadType 0`，`ReloadTime -1`）在 4.1 与 EDF6 一致，不补弹，未改。

### 408 呼叫的驾驶参数（已修）

`Vehicle_Car` 的设定 slot 46 `0x65A910` 从 `setup[1]` 读 7 个数：[0] 缩放 `car_base_brake_and_friction`（`0x66EEC0` / `0x66EF20`），[1] [2] 缩放齿轮表（`0x66E1A0`），[3] [4] 转向速率（`0x66B730` → `+0x1B44 / +0x1B40`），[5] [6] 牵引曲线（`0x66EE60`）；越界读作 0。408 对象自己用 V512 的 `[0.8,30,0.0075,0.7,0.075,0.3,0.6]`，但 5 条请求仍带 4.1 VehicleBike 自己的四数块（如 `[2.0,0.9,30,100]`），请求生成的车齿轮表按 0.9 / 30 缩放、转向 100 / 0、牵引系数 0；M 地图摆放走 `mission_setup` 不受影响。现在请求带对象自己的 `edf41bike.CAR_DRIVE`（生成时校验与 V512 一致），保留 4.1 的装甲 [0] 与机枪 [2]。4.1 各型号之间的速度 / 动力差异没有已核实的 Car 对应量，未映射。

### 炮手适配在实机从未安装（已修）

本机日志每次加载都是 `EDF41BIKE refused: native Car/Aim/physics profile differs`：`plugin.cpp` 先装 `InstallTurretCam` / `InstallStabilizer`，它们链接了 Aim vtable slot 2（`0x17D8A90` / `0x17D8A68`），而本模块要求 slot 2 仍是原版指针。本模块经 vtable 调 slot 2，链上的钩子照常对所有座位生效，所以只要求 slot 2 非空、slot 3（按地址调用的 pose）仍是原版。`tests/edf41bike_runtime_test.cpp` 增加「被链接的 slot 2 仍安装且经链调用」「slot 3 被换仍拒绝」。

### 悬空（未定根因，需实机）

本机 2026-10-19 20:05 的会话（`EDF6Coop.log` 打开 `EDF6VC_E41_WEAPON753` 与 `VEHICLE408_MOTORBIKE.SGO`）：第一辆 408 生成时 `pos=(-272,2,234)`，附近地面 y≈1（同区 `(-250,1,212)`、`(-294,1,207)`），此后十余分钟停在 y=2 不再下落；21 秒后第二辆生成在 `(-274,4,235)`，即叠在第一辆上方，第一辆位置不变。原版集装箱出车点本就离地约 1 m（`docs/airdrop-vehicle-re.md` §2.3，Grape 出车后落到地面），所以 408 底盘在出车后没有受重力下落，且对第二辆的穿透推挤表现得像不可动刚体。

已静态排除：碰撞层（4.1 SHKT 全部 layer 0 / group 1，与 V503 的 TAG0 相同）、重力系数（hkp `gravityFactor` 1.0）、`car_base_all_rigid_body` 的 [1] [2]（`0x6660D0` 写的是 motion properties 线 / 角阻尼，不是重力）、`car_base_rigid_body`（-1 字段全部跳过）、上面那组错误驾驶参数（只影响刹车、齿轮、转向、牵引）。仍可疑、无法静态定论的差异：408 是唯一把代理设为 mode 2（`0x6ED480` → 运动学 `motionType 1`，碰撞 `0x6ED2D0(…,3)`）的车辆，且 `front_sus` / `handle` 为动态代理（V503 只有 body 在世界里；4.1 源 `vehicle_dactive_node` 把 gun / front_sus / handle / front_tire 列为动画驱动），以及 hkp→hknp 迁移（`0x160B950`）生成的 motion / 激活状态。下一次实测请在出车后读每个代理的 `+0x9C` 模式、`0x11B14A0` motion type 与激活状态，再决定修改；不以降低出车高度或强制落地掩盖。

## 2026-10-20 审查（#219）：版本混装与悬空现状

### 版本混装：门控与提示

#219 的三项改动在两台机器上必须一致才不出岔子：4.1 呼叫按积分装填（武器 SGO 的 `ReloadType`，安装进 Mods 的数据）、408 请求带 Car 的七数驾驶块（呼叫 SGO 的数据）、边三轮炮手座位（本 DLL 的 `edf41bike.cpp`）。每台机器自己跑投送链与载具设定（`docs/online-re.md` §2），所以一台旧版机器会在自己那边按计时回满、按四数块造车、炮手座位按原版 Car。

- **提示**：每个成员经 EDF6Coop 成员属性 `EDF6X_AF_ROOM` 发布「版本 + 功能位」（`src/host_settings_logic.h`：`kFeatCreditReload41` / `kFeatBikeCarDrive` / `kFeatBikeGunner` 等）。房间菜单的状态行在有成员没有这个值（旧版）或功能位不全时写「N 名成员的全军出击版本与本机不同，可能不一致：…」并列出缺的项；日志 `HOSTCFG the room's builds: ...`。功能位是该构建的声明（DLL 与安装器一起发布）；没法从成员属性得知对方 Mods 里实际装的是哪一版数据。
- **门控**：炮手座位照 `Edf5RoomAllowsBvmPlayers` 的做法，全员声明支持才开放：`MissionSync_Begin` 时（`host_settings.cpp Read(true)`）房间内每个成员的值都解析成功且都带 `kFeatBikeGunner`，本任务才启用 `edf41bike.cpp` 的座位改造（`Edf41BikeGunnerAllowed`）；否则这一任务里所有新版机器都把 408 当原版 Car（不改造座位、不接管帧与姿态），与旧版一致，日志 `HOSTCFG this mission's 408 sidecar gunner seat: closed`。单机永远开放。离线：`tests/edf41bike_runtime_test.cpp`「a room without the gunner seat」。积分装填与驾驶块是数据，无法按任务关掉，只提示。

### 悬空（根因仍未定，未修）

沿代码与本文上面的静态结果再查了一遍，没有能离线证实的根因，**不宣称已修**，也没有用降低出车高度或强制落地掩盖：

- 仍然成立的线索：408 是唯一显式把一个代理（`gun`）选成 mode 2 的车辆（`car_base_simulation_node` 的 `['gun',1]` → `0x66428F` 加 1 → `0x6ED480(record,2)`，运动学目标）；三个轮子行值 0 经 `0x6E94E0 → 0x6E972A` 回到构造器的 mode 1；`front_sus` / `handle` 是动态代理。一个运动学刚体与动态底盘之间若有约束（原 `hkpRagdollConstraintData` 双轴约束保留），运动学一侧质量无穷，底盘会被它「吊住」：与「出车后不下落、第二辆叠上去第一辆不动」的表现一致（推测）。但 gun 的运动学目标又来自底盘自身的姿态（`Pose408`：`0x6EDCA0` 物理 → 骨骼，`0x5FACD0`，`0x1100B90`，`0x6ED800` 骨骼 → 物理），是否真的形成这种闭环，取决于 hknp 迁移（`0x160B950`）后 mode 1 / mode 2 的实际 motion type 与激活状态，离线读不到。
- 下一次实机请记录：出车后每个代理的 `+0x9C` 模式、`0x11B14A0` 的 motion type、激活状态，以及把 gun 行临时改成动态（删掉 `['gun',1]`，只在测试场 SGO 里）时是否下落。确认之前不改发布数据。
