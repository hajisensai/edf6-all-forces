# EDF4.1 Vehicle408_motorbike：真实三轮 / 副座枪手适配

2026-10-19。源资源是本机 EDF4.1 的 `Vehicle408_motorbike`，不是将 EDF6 Freed 改名。运行模块 `src/edf41bike.cpp/.h`，数据适配 `pylib/edf41bike.py`。下列地址均为本机 Steam EDF6 `EDF.dll`（TimeDateStamp `0x678CCB46`）的 RVA；特别标出的旧地址属于 EDF4.1 `EDF41.exe`（`0x57AAB039`），绝不拿旧地址写 EDF6。

## 不能直接套现有 sidecar 的原因

现有 `src/sidecar.cpp` 的专属标记为 `edf6vc_sidecar`：503 双轮物理，第三轮仅外观，右侧固定站立点 `(-1,.30,.35)`，乘员在步行状态使用自己的武器。源 408 是左侧 `+x` 边斗、三个真实轮、八个 proxy、原生副座与挂载机枪，因此适配到 `Vehicle_Car` 的真实动态轮表和两座位，保留源骨、模型、MAB、SHKT / 双向 ragdoll binding。

源代理：`body`、`handle`、`front_sus`、`front_tire`、`rear_sus`、`rear_tire`、`side_tire`、`gun`。gun 的原约束是双轴 `hkpRagdollConstraintData`，无需拆成虚构 yaw / elevation 刚体。GenericCarBase 的 type 3 虽能解析，但 `0x5EC1C0` 只验证轴并写类型，没有建立马达；不能把它当可用双轴控制。

## 专属合同

- 新 marker：`edf41_motorbike`；SGO key：`edf41_bike`，schema 1，原 `bike_gun_ctrl=[-30,20,-15,30]` 与 operator seat 1。
- `Vehicle_Car` 使用真实的 V512 空 `car_aim_node=[]` 基底；三个原轮子分别有源位置和自己的驱动 / 转向标志。单机炮 holder 为 `['gun',1]`，驾驶座没有武器。
- gun 不列入 `car_base_rigid_body` 或 `car_base_simulation_node` 的物理写骨名单；所有八个原 proxy 和原双向 binding 保留。载具 / 枪手姿态适配的资源检查见 `tests/test_edf41bike.py`。
- 不改现有 503 sidecar，也不为普通 Car 改驾驶 / 火控。该模块要求 Car vtable、schema 和独立 marker 同时吻合。

## 原始单骨双轴算法与物理同步

旧 `VehicleBike` 不是直接设置猜测角速度：旧 vtable `0xAA0670` 的 pose 方法 `0x2F5900`，调用 `0x2D6BE0` 时把同一个 gun local 矩阵传两次。相等分支先形成 `X(-pitch)` 再叠 `Y(-yaw)`，保留平移，然后由基类进行 world build / 物理同步。

EDF6 `VehicleWeaponAim` vtable `0x17D8A68` / `0x17D8A90` 的 slot 3 都是 `0x5FACD0`，同样支持矩阵地址相等的单骨双轴分支。它不再取负，因此原 source 允许范围转换为 native yaw `[-20,+30]°`、pitch `[-30,+15]°`。

408 的 pose 顺序严格为：

1. `0x6EDCA0`：动态物理代理读回骨。
2. `0x5FACD0(Aim, gunLocal, gunLocal)`：原生单骨双轴姿态。
3. `0x1100B90`：构建骨的 world 矩阵。
4. `0x6ED800`：将动画驱动骨同步到真实物理 proxy。

GenericCarBase `0x65C587` 传 mode 1 给 VehicleBase `0x629450`；后续 `0x6298F4 / 0x62990D` → `0x6EB4C0` → `0x6EB760` → `0x6EB9F5..0x6EBA15` 将每个 proxy 以 `0x6ED480(record,1)` 初始化。CarBase 的动态名单随后才覆盖所选 body；排除 gun 后，它保持 **mode 1 动画驱动**，`0x6ED800` 直接更新真实 body transform。并非只转外观、把 collider 留在原处。旧 4.1 的 mode 2 是 kinematic target 分支；新原生 mode 1 的接触表现仍需实机确认。

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

- `tests/edf41bike_native_audit.py [EDF.dll] [EDF41.exe]`：实际读取两份本机 PE，核八组 runtime signatures、原 pose 分支、Car 构造 / pose 顺序、NetObject 的逐座发收与 mapping 准入。
- `tests/edf41bike_test.cpp`：12 项范围与权限边界。
- `tests/edf41bike_runtime_test.cpp`：直接包含生产 cpp；仅在测试进程私有 fake image 中以记录 trampoline 替代 native endpoint，验证原参转发、seat 1 路由、只初始化一次 mapping、真实调用顺序、普通 Car 原样、远端 / NPC / 地图 / 死亡 / 关闭、profile 拒绝及部分安装失败。
- 生产 cpp 和两个 C++ 测试均以 MSVC `/W4 /WX /utf-8 /permissive-` 实际编译运行。

以上是源资源、真实原生合同和离线 production routing 验证。未启动或操作游戏，未写游戏目录；三轮实际驾驶 / 转向稳定性、乘员动作、gun 接触与毁车效果，以及两机联机显示 / 射击仍需实机验收。
