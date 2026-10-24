# EDF4.1 Vehicle408_motorbike：真实三轮 / 副座枪手适配

2026-10-19。源资源是本机 EDF4.1 的 `Vehicle408_motorbike`，不是将 EDF6 Freed 改名。运行模块 `src/edf41bike.cpp/.h`，数据适配 `pylib/edf41bike.py`。下列地址均为本机 Steam EDF6 `EDF.dll`（TimeDateStamp `0x678CCB46`）的 RVA；特别标出的旧地址属于 EDF4.1 `EDF41.exe`（`0x57AAB039`），绝不拿旧地址写 EDF6。

## 不能直接套现有 sidecar 的原因

现有 `src/sidecar.cpp` 的专属标记为 `edf6vc_sidecar`：503 双轮物理，第三轮仅外观，右侧固定站立点 `(-1,.30,.35)`，乘员在步行状态使用自己的武器。源 408 是左侧 `+x` 边斗、三个真实轮、八个 proxy、原生副座与挂载机枪，因此适配到 `Vehicle_Car` 的真实动态轮表和两座位，保留源骨、模型、MAB、SHKT / 双向 ragdoll binding。

源代理：`body`、`handle`、`front_sus`、`front_tire`、`rear_sus`、`rear_tire`、`side_tire`、`gun`。gun 的原约束是双轴 `hkpRagdollConstraintData`，无需拆成虚构 yaw / elevation 刚体。GenericCarBase 的 type 3 虽能解析，但 `0x5EC1C0` 只验证轴并写类型，没有建立马达；不能把它当可用双轴控制。

## 专属合同

- 新 marker：`edf41_motorbike`；SGO key：`edf41_bike`，schema 1，原 `bike_gun_ctrl=[-30,20,-15,30]` 与 operator seat 1。
- `Vehicle_Car` 使用真实的 V512 空 `car_aim_node=[]` 基底；三个原轮子分别有源位置和自己的驱动 / 转向标志。单机炮 holder 为 `['gun',1]`，驾驶座没有武器。
- gun 不列入 `car_base_rigid_body` 动态名单；`car_base_simulation_node` 给它行值 0（与三个轮子、原版 V603 旋转雷达同一做法：移出物理世界、删掉它到 body 的关节，骨骼由炮手 Aim 摆）。2026-10-19 版本的行值 1（mode 2 运动学）就是悬空的根因，见文末「悬空：根因与修复」。所有八个原 proxy 和原双向 binding 保留。载具 / 枪手姿态适配的资源检查见 `tests/test_edf41bike.py`。
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

两种行值：值 1 经 `0x664279 / 0x66428F / 0x664292` 调 `0x6ED480(record,2)`，proxy 变运动学（motion type 1）、留在世界里、**关节保留**；值 0 经 `0x6642A2 → 0x6E94E0 → 0x6E96C0`（递归到子 proxy）先删掉它到父 proxy 的关节（`0x6E9725 → 0x6EC840 → 0x11C1180`），再恢复构造器默认 mode 1（移出世界，`0x6ED800` 的 `0x6EDAB6` 分支只把 body 跟着骨骼搬）。2026-10-19 起生成器给 gun 的是值 1；2026-10-21 实机证实它把底盘吊在空中，改为值 0（下节）。

### 原生 Car 的开火后坐力

旧 408 setup 使用 `['AimRecoil',[.002618,.0062832]]`。Car fired callback `0x65A740` 先经 `0x65A750 → 0x62EA10` 保留 weapon 的真实 `FireRecoil`，随后 `0x65A76B / 0x65A76D → 0x5FA150` 请求 variant 0（BodyRecoil）。错误 variant 返回 null，而 `0x65A83A → 0x5F9FB0` 消费 payload 前没有检查 null。

`native_gun_rows` 因此只将 holder 的 hull impulse 参数转换成合法 `[0,0]`。实体 `vehicle_setup`、`mission_setup`、独立测试场 SGO 和五个呼叫的覆盖 setup 全部使用同一转换，机枪 SGO 的真实 `FireRecoil=.3` 保留。测试同时检查源值未被清零、坏 variant / 缺显式 mode / 错 mode 被拒绝。

## 副座初始化、输入与联机

Car 的 setup build `0x65B37E` 原本始终把 seat 0 的 Aim 交给 `0x633330`。专属 wrapper 只在模块全部安装成功且 schema 匹配时改为 seat 1 Aim，其余参数不变。原生 `0x5FB810` 读取源机枪第三项 `[90,.015,.1]` 初始化速度 / 加速度，避免复制含共享引用的 Aim 内存。 **2026-10-21 实机更正**：这段循环原版只为 seat 0 的枪跑（`0x65B152` 先看 seat 0 的 holder 数，`0x65B31B` 再逐行跳过别的座位），408 的枪在 seat 1，所以以前从未经过这里；现由「炮手机枪从未建成」一节的两道门放行。

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

### 悬空（10-19 静态排查；根因见文末「2026-10-21 实机」）

本机 2026-10-19 20:05 的会话（`EDF6Coop.log` 打开 `EDF6VC_E41_WEAPON753` 与 `VEHICLE408_MOTORBIKE.SGO`）：第一辆 408 生成时 `pos=(-272,2,234)`，附近地面 y≈1（同区 `(-250,1,212)`、`(-294,1,207)`），此后十余分钟停在 y=2 不再下落；21 秒后第二辆生成在 `(-274,4,235)`，即叠在第一辆上方，第一辆位置不变。原版集装箱出车点本就离地约 1 m（`docs/airdrop-vehicle-re.md` §2.3，Grape 出车后落到地面），所以 408 底盘在出车后没有受重力下落，且对第二辆的穿透推挤表现得像不可动刚体。

已静态排除：碰撞层（4.1 SHKT 全部 layer 0 / group 1，与 V503 的 TAG0 相同）、重力系数（hkp `gravityFactor` 1.0）、`car_base_all_rigid_body` 的 [1] [2]（`0x6660D0` 写的是 motion properties 线 / 角阻尼，不是重力）、`car_base_rigid_body`（-1 字段全部跳过）、上面那组错误驾驶参数（只影响刹车、齿轮、转向、牵引）。仍可疑、无法静态定论的差异：408 是唯一把代理设为 mode 2（`0x6ED480` → 运动学 `motionType 1`，碰撞 `0x6ED2D0(…,3)`）的车辆，且 `front_sus` / `handle` 为动态代理（V503 只有 body 在世界里；4.1 源 `vehicle_dactive_node` 把 gun / front_sus / handle / front_tire 列为动画驱动），以及 hkp→hknp 迁移（`0x160B950`）生成的 motion / 激活状态。下一次实测请在出车后读每个代理的 `+0x9C` 模式、`0x11B14A0` motion type 与激活状态，再决定修改；不以降低出车高度或强制落地掩盖。

## 2026-10-20 审查（#219）：版本混装与悬空现状

### 版本混装：门控与提示

#219 的三项改动在两台机器上必须一致才不出岔子：4.1 呼叫按积分装填（武器 SGO 的 `ReloadType`，安装进 Mods 的数据）、408 请求带 Car 的七数驾驶块（呼叫 SGO 的数据）、边三轮炮手座位（本 DLL 的 `edf41bike.cpp`）。每台机器自己跑投送链与载具设定（`docs/online-re.md` §2），所以一台旧版机器会在自己那边按计时回满、按四数块造车、炮手座位按原版 Car。

- **提示**：每个成员经 EDF6Coop 成员属性 `EDF6X_AF_ROOM` 发布「版本 + 功能位」（`src/host_settings_logic.h`：`kFeatCreditReload41` / `kFeatBikeCarDrive` / `kFeatBikeGunner` 等）。房间菜单的状态行在有成员没有这个值（旧版）或功能位不全时写「N 名成员的全军出击版本与本机不同，可能不一致：…」并列出缺的项；日志 `HOSTCFG the room's builds: ...`。功能位是该构建的声明（DLL 与安装器一起发布）；没法从成员属性得知对方 Mods 里实际装的是哪一版数据。
- **门控**：炮手座位照 `Edf5RoomAllowsBvmPlayers` 的做法，全员声明支持才开放：`MissionSync_Begin` 时（`host_settings.cpp Read(true)`）房间内每个成员的值都解析成功且都带 `kFeatBikeGunner`，本任务才启用 `edf41bike.cpp` 的座位改造（`Edf41BikeGunnerAllowed`）；否则这一任务里所有新版机器都把 408 当原版 Car（不改造座位、不接管帧与姿态），与旧版一致，日志 `HOSTCFG this mission's 408 sidecar gunner seat: closed`。单机永远开放。离线：`tests/edf41bike_runtime_test.cpp`「a room without the gunner seat」。积分装填与驾驶块是数据，无法按任务关掉，只提示。

### 悬空（10-20 静态审查；根因见文末「2026-10-21 实机」）

沿代码与本文上面的静态结果再查了一遍，没有能离线证实的根因，**不宣称已修**，也没有用降低出车高度或强制落地掩盖：

- 仍然成立的线索：408 是唯一显式把一个代理（`gun`）选成 mode 2 的车辆（`car_base_simulation_node` 的 `['gun',1]` → `0x66428F` 加 1 → `0x6ED480(record,2)`，运动学目标）；三个轮子行值 0 经 `0x6E94E0 → 0x6E972A` 回到构造器的 mode 1；`front_sus` / `handle` 是动态代理。一个运动学刚体与动态底盘之间若有约束（原 `hkpRagdollConstraintData` 双轴约束保留），运动学一侧质量无穷，底盘会被它「吊住」：与「出车后不下落、第二辆叠上去第一辆不动」的表现一致（推测）。但 gun 的运动学目标又来自底盘自身的姿态（`Pose408`：`0x6EDCA0` 物理 → 骨骼，`0x5FACD0`，`0x1100B90`，`0x6ED800` 骨骼 → 物理），是否真的形成这种闭环，取决于 hknp 迁移（`0x160B950`）后 mode 1 / mode 2 的实际 motion type 与激活状态，离线读不到。
- 下一次实机请记录：出车后每个代理的 `+0x9C` 模式、`0x11B14A0` 的 motion type、激活状态，以及把 gun 行临时改成动态（删掉 `['gun',1]`，只在测试场 SGO 里）时是否下落。确认之前不改发布数据。

## 2026-10-21 实机：悬空根因与修复

### 诊断

`src/edf41bike_diag.cpp`（`Debug=1` 时，Car vtable 帧钩子 `Frame` 里调 `Edf41BikeDiagFrame`，全程 SEH，所调原生函数先核签名）：每辆 Vehicle_Car（408 与同类的 V512）出车后第 1–3、6、10、20、40、80、160、320 帧，之后每 600 帧到 7200 帧，记 `pos` / 逐帧速度 / 正下方地图高度（`edf41c::GroundHit`）/ `clearance` / 各座位乘员、驾驶座输入 `drive=(+0x2C0,+0x2C4,+0x2E0,+0x2E4)`、本机玩家的距离与在车体坐标里的位置 / 朝向（`player= local= facing=`）；有人乘坐或玩家在 40 m 内时，前 7200 帧内每 30 帧也记一次。第 1、40、600 帧另记 ragdoll 每个代理（`+0x9C` 模式、`+0xA0`、`+0xA4`、`0x17E720` 是否在世界里、`0x11B14A0` motion type 0 静态 / 1 运动学 / 2 动态、hknpBody `+0x54` flags（8 = active）、body 高度）与每条代理关节（两端代理、`0x11C1220` 是否已加入世界）。日志前缀 `EDF41BIKE DIAG`。`edf41bike.cpp` 另在 Debug=1 时记 `EDF41BIKE gunner`（炮手有输入权时每秒一行、每次运行至多 120 行：枪对象与剩余弹数 `rounds`、两轴角度、限位、转速、Aim `+0x90..+0x98` 转速参数、`+0xC0`、输入、扳机）与 `EDF41BIKE weapon build`（Car 建枪调用的路由结果 `why` 与建好后的转速参数）。

### 实机证据

后台 `tests/autopilot/drive.py run range`（游戏窗口在屏幕外，前台计数 0），测试场任务临时换成只放这几辆车的脚本（`CreateVehicle2`，与调试召唤 / 摆放同一 `CreateObject` + `mission_setup` 生成），测完 `--place` 逐字节还原；每次都是本分支（基于当时最新 `origin/main` `48bbd468`）现编的 DLL，日志首行与当次构建对得上（如 run 2 `EDF6VehicleCrew 0.8.0 loading (build 48bbd4685a+dirty, 22940672 bytes, written 2026-10-20 17:40:47 UTC)`，run 1 DLL sha256 `0dcafebf…`）；测前备份 `Mods/Plugins`、`Mods/MISSION` 与整个 `Mods` 的大小 / 时间清单，测后逐字节还原，清单比对 0 差异，游戏目录 DLL 回到原来的 `2213ef80…`。

1. 生成点就在地面（run 1，出车 `clearance` 0.02–0.07）：原数据与改数据都停在地面，看不出悬空——出车点离地才会暴露。原数据的代理状态：`proxy 1 gun parent=0 mode=2 ... world=1 motion=1 flags=6004E`、`constraint 0: proxy 1 - proxy 0 added=1`，即 gun 是在世界里的运动学刚体（flags bit2 keyframed、bit3 active），它到 body 的 `hkpRagdollConstraintData` 关节已加入世界。
2. 出车点抬高 1.5 m（run 2，`rmpa.raised`，同场三辆）：
   - 原数据（gun 行值 1）`v=…766E90 408 f=1 pos=(-281.418,2.700,186.427) ... clearance=1.568` → `f=40 ... clearance=1.570` → `f=600 ... clearance=1.458` → `f=2400 ... clearance=1.250`：60 秒不落地，只在空中漂。**复现悬空。**
   - 只把 gun 改行值 0（同一 SGO 的副本）`v=…75F060 f=1 ... clearance=1.520` → `f=40 ... clearance=-0.244`（落地反弹）→ `f=600 ... clearance=-0.031`，与贴地生成时的静止值相同；代理 `gun ... mode=1 ... world=0`、`constraint 0 ... added=0`。
   - 对照 V512（同为 Vehicle_Car）`clearance=1.059` → `f=600 ... 0.028`。
3. 用改后的生成器真实产物（run 3，两份 SGO 与安装的相比只差 `car_base_simulation_node` 与 `car_base_fall_out`）：出车 `clearance` 1.36 / 1.52 的两辆在 80 帧内到 `-0.023`，12 秒后在旁边 2.5 m 再出一辆 `1.519 → -0.021`，各自落地，不叠。

4. 驾驶与炮手位（run 6–11，改后数据；玩家站在车侧上车点 1.2 m 半径内，`pad_b` =「搭乘 / 救援」）：
   - 驾驶座 `seats=[P-]`，左摇杆不驱动 Car，原生油门是座位 `+0x2E0`（`pad_lt`）：`f=2100 pos=(-253.433,0.871,225.024) ... drive=(0.00,0.00,1.00,0.00)` → `f=2220 pos=(-256.219,0.832,218.856)` → 松开后 `f=2340 ... clearance=-0.031`，约 2 秒开出 7 m，全程贴地。
   - 炮手座 `seats=[-P]`：`EDF41BIKE gunner ... input=(0.32,0.00) fire=0`、扳机时 `fire=1`，座位改造的帧路径（`Update` → 原生 Aim slot 2 → 拉唯一 holder）在跑。**但 yaw / pitch 一直是 0**：`turn=(0.000,0.000,0.000)`，即 seat 1 Aim 的转速参数（`+0x90..+0x98`，应由 `0x633330 → 0x5FB810` 从机枪行第三项 `[90,.015,.1]` 填入）从未初始化。run 13 证实脚本 / 摆放生成的 408 根本没经过 `SetWeapon` 包装（`0x65A910` 的 `0x65B37E`，Debug=1 日志 `EDF41BIKE weapon build` 一行都没有），而 seat 1 仍有 holder（后来查明：holder 有，枪对象根本没建）。这与本节的物理修复无关（gun 行值不影响 Aim），根因与修复见下节「炮手机枪从未建成」。
   - 对照：同一位置用安装着的旧数据（run 9）`f=1 ... clearance=0.418` → `f=600 ... clearance=0.513`，又一次悬空，且玩家够不着上车点（`seats=[--]`）。

### 根因

`pylib/edf41bike.py` 给 gun 的 `car_base_simulation_node` 行值 1。CarBase 先把所有代理设为动态（`0x6E40B0`）并把所有代理关节加入世界（`0x6E4126 → 0x6E4A70 → 0x11C11A0`），再按行处理：值 1 只把该代理改成运动学（`0x66428F → 0x6ED480(record,2) → 0x11B1960(body,1)`），**关节保留**；值 0 才删掉到父代理的关节（`0x6E96C0 → 0x6E9725 → 0x6EC840 → 0x11C1180`）。408 的 SHKT 里 gun 的关节直连 body（`RagDollProxys.gun <-> RagDollProxys.body`，`hkpRagdollConstraintData`）。运动学刚体质量无穷，关节把动态的底盘钉在它身上；而 gun 的运动学目标是上一帧底盘姿态下的骨骼位置（`0x6ED800` mode 2 分支），于是底盘停在出车的位置，第二辆撞上去也推不动第一辆。原版车辆没有一个用行值 1（`tests/edf41bike_native_audit.py` 与 `pinned()` 文档），会动的部件（V603 雷达、轮子）都用 0。

### 修复

- gun 改行值 0：移出世界、删掉到 body 的关节，骨骼仍由 `Pose408`（`0x5FACD0`）按炮手 Aim 摆，`0x6ED800` 的 mode 1 分支只把它的 body 跟着骨骼搬。炮手座位、Aim、开火、联机逐座同步的代码都不读物理代理，不变；失去的只是 gun 自身对外的碰撞。
- `check()` 加通用不变量 `pinned()`：按上面原生规则推出每个代理的最终模式与仍在的关节，任何「运动学 ↔ 动态」关节都拒绝生成；`joints()` 从 SHKT 读出 7 条关节。
- 同一批实测发现的崩溃：run 1 让第二辆生成在第一辆里，两车互推后 `CRASH unhandled exception: exception C0000005 at EDF.dll+0x673F5A`。`car_base_fall_out` 照抄模板 V512 的 `[1,2,3,4]`（轻卡车斗里的四个座位），`0x673F41` 拿它们直接下标座位数组（`seat * 0x340 + [v+0x608]`，再读 `+0x268`，无越界检查），408 只有两个座位，撞得够重就读出界。改成本车除驾驶座外的座位 `[1]`，`check()` 拒绝任何越界座位号。
- 联机：两项都是物体 SGO 的数据，各机器各自加载（`docs/online-re.md` §2），无法按任务关闭，按 #219 的约定只提示：新增功能位 `kFeatBikeGrounded`（`src/host_settings_logic.h`），房间状态行对缺这一位的成员写「408 落地（不悬空）」可能不一致。混装时旧版机器自己生成的 408 仍会悬空；两机之间同一辆车的位置怎么收敛未做两机实测（推测由该车的权威机器决定）。

### 离线测试与变异

- `tests/test_edf41bike.py`：`test_no_keyframed_proxy_holds_the_chassis`（7 条关节、原 gun 行值 1 推出 `[('gun','body')]` 被拒、把 rear_sus 设运动学同样被拒、行值 0 连子树一起移出）与 `test_fall_out_names_only_the_408s_own_seats`。
- `tests/edf41bike_native_audit.py`：CarBase 加全部关节、行值 0 递归删关节、mode 2 是运动学、fall-out 下标无检查这四条原生合同。
- `tests/coop_attr_test.cpp`：缺 `kFeatBikeGrounded` 的成员被列出。
- 变异：生成器改回 `['gun',1]` → `ValueError: 408 gun needs its one animation-driven simulation row 0`；去掉 fall-out 修正 → `ValueError: 408 car_base_fall_out names seats it does not have: [1, 2, 3, 4] of 2`；`pinned()` 不再把行值 1 记为运动学 → `FAIL: test_no_keyframed_proxy_holds_the_chassis`；`kFeatures` 去掉 `kFeatBikeGrounded` → `FAIL a member whose 408 still hovers: named in the notice`；`Frame` 不再调诊断 → `FAIL: Debug=1: the diagnostic samples each Car frame after the native one`（`tests/edf41bike_runtime_test.cpp`）。

## 2026-10-21 实机：炮手机枪从未建成（转炮 / 开火）

### 根因

所有出车路径都走 Car 设定 slot 46 `0x65A910`：呼叫 / 空投与调试召唤、M 地图摆放的 `CreateVehicle2` 步骤 `0x633280`（`0x6332BE call [rax+0x170]`），任务脚本的 `mission_setup` 路径（`0x926A7 → 0x926B8`）。它建枪的循环有两道原版门：

1. `0x65B14B..0x65B15A`：`mov rax,[r15+0x608]; cmp qword [rax+0xD8],0; jbe 0x65B3EF`，**seat 0** 没有 holder 就跳过整个循环；
2. 循环里逐行读 `vehicle_weapon_setting`（`0x65B1B2` 取该名），`0x65B31B test ebx,ebx; jne 0x65B3A6`：行的座位号（`[1]`）不是 0 就跳到下一行。

408 唯一的枪是 `['gun',1]`（炮手座），两道门都挡住，`0x633330` 一次都没调用：车的 holder 里没有枪对象，seat 1 Aim 的转速参数（`+0x90..+0x98`，`0x5FB810` 从机枪行第三项填）一直是 0，于是 `0x5FBC00` 每帧算出的转速为 0。上一版的 `SetWeapon` 包装（`0x65B37E`）写好了却永远不会被调到（实机 `EDF41BIKE weapon build` 无一行）。

### 修复（`src/edf41bike.cpp` `InstallGunGate` / `BuildsGun` / `BuildsRow`）

两道门各换成一个近址桩（`rel32 jmp`，原字节先核对）：桩里先执行原版测试，原版本来就会建（seat 0 有枪 / 本行是 seat 0）时照原路走；否则问 `BuildsGun(r15)` / `BuildsRow(r15, ebx)`：只有 408（`Tagged`）、炮手座开放（`Edf41BikeGunnerAllowed`）、seat 1 恰有一个 holder、且该行就是 seat 1 时，才进入原版循环 / 原版这一行的建枪。于是原版 `0x633330` 照常建枪，`SetWeapon` 把 Aim 换成 seat 1 的，`0x5FB810` 用数据里的 `[90,.015,.1]` 初始化转速（不写死），`RegisterMappings` 登记联机映射。其它 Car（实测 V512：`gun build gate ...: stock skip`）与关闭的房间都走原版跳过。

### 实机证据（同一租约，`drive.py` 后台，测后逐字节还原、清单 0 差异，游戏目录 DLL 回到 `2213ef80…`）

- 摆放（测试场脚本 `CreateVehicle2`，玩家站在炮手门旁 `pad_b` 上车）：
  `EDF41BIKE gun build gate v=000001B71E208680: the loop runs for the seat 1 gun`、`EDF41BIKE gun row ... seat 1: built`、
  `EDF41BIKE weapon build v=000001B71E208680 holder 0 why=4 aim=000001B71DA15D20 turn=(0.100,0.015,0.026)`；
  上炮手座后 `yaw=0.000 → 0.205 → 0.524`（右摇杆，到 +30° 限位）、`pitch → -0.455`，扳机 `rounds=1200 → 1193 → 1178 → 1163`。
- 呼叫（空降兵载具栏放 4.1 呼叫第 2028 行 `EDF6VC_E41_WEAPON753`，`pad_y` 点按；运输机投下后由一个临时闭环脚本（未入库，只调 `drive.py key`）按 `EDF41BIKE DIAG ... local=(..) facing=(..)` 走到炮手门上车）：
  `gun build gate v=00000287BC6D6670: the loop runs for the seat 1 gun`、`weapon build ... why=4 ... turn=(0.100,0.015,0.026)`；
  `yaw=0.000 → 0.185 → 0.384`、`pitch → -0.390`、`rounds=1200 → 1193 → 1178 → 1170`。
- 修前同一路径：`turn=(0.000,0.000,0.000)`、yaw / pitch 恒 0（run 11、G1）。

### 联机（#219 门控）

新增功能位 `kFeatBikeGunMount`。炮手座的开放条件 `BikeGunnerAtSync` 改为全员同时有 `kFeatBikeGunner | kFeatBikeGunMount`：#219 / 本 PR 第一版的构建有座位、没有建成的枪，与新版混在一个房间时，新版这边会开火、旧版那边不会，所以整个任务都按原版 Car（不建枪、不改造座位），房间状态行列出「408 炮手机枪（已关闭）」。

### 测试与变异

- `tests/edf41bike_runtime_test.cpp`：两处补丁都整段核对原字节、跳到各自的桩；桩先放行原版、再问对的函数、去对的原版落点；408 / 关闭房间 / 别的 Car / 无 seat 1 holder / 别的座位行的判定；补丁失败则整个模块不装。
- `tests/edf41bike_native_audit.py`：两道门、`vehicle_weapon_setting` 名字、行跳转目标、三条出车路径都经过 slot 46。
- `tests/coop_attr_test.cpp`：缺 `kFeatBikeGunMount` 的成员让炮手座关闭；`tests/coop_attr_guard.py`：四处按任务门控。
- 变异：`BuildsGun` 恒 false → `FAIL: the 408 with its one seat-1 holder: its gun is built`；桩里原版跳转偏移改错 → `FAIL: a gun on seat 0 keeps the stock loop first`；门控只要 `kFeatBikeGunner` → `FAIL the gunner seat needs every member's built gun as well`。

## 2026-10-25 反馈「SDL1 驾驶姿势不对、按 W 不前进只缓缓向左」

用户原话：「4.1摩托SDL1会出现驾驶姿势问题 以及按W键不会前进 只是缓缓向左（图中为SDL1 Z型）」。截图：SDL1 Z型，HUD「CAR 速度 1 km/h」，坐席「1 驾驶席 你 / 2 炮手席」。所有 SDL1 型号（SDL1、Z 型、R 型、坏改装、爆裂涡轮）的请求都投同一个 `VEHICLE408_MOTORBIKE.SGO`（`edf41calls.vehicle_call`，[1] 统一换成 `CAR_DRIVE`），所以三处都在这一个对象上统一修，不逐个型号改。

### 根因 1：安装包里的 408 一直是 10-19 的旧数据（W 不前进、缓慢偏转）

安装器发的是仓库里提交的 `edf41port/objects/OBJECT/VEHICLE408_MOTORBIKE.SGO`（`tools/build_release.py` 打包 `edf41port/objects`），不是现场生成。10-21 的「悬空」修复（gun 行值 0、`car_base_fall_out [1]`）只改了生成器 `pylib/edf41bike.py`，提交 `8cdd3276` 没有重新生成这份 SGO；`git log -- edf41port/objects/OBJECT/VEHICLE408_MOTORBIKE.SGO` 最后一次是 `c12b9a88`（10-19）。修前提交的 SGO：`car_base_simulation_node [..., ['gun', 1]]`、`car_base_fall_out [..., [1, 2, 3, 4]]`，正是上文「悬空」的根因数据：运动学 gun 经保留的 `hkpRagdollConstraintData` 关节把动态底盘钉在上一帧的位置，油门只能让车一点点挪；gun 在车的左侧（边斗，`+x`），底盘绕它转，于是「缓缓向左」（这一联系是推测，运动学钉住本身由 10-21 实机证实）。10-21 实机 run 9「安装着的旧数据又一次悬空」就是这份文件。同一份数据还带着 V512 的跌落座位 `[1,2,3,4]`（`0x673F41` 越界读，10-21 的崩溃）。

### 根因 2：边斗轮被当成驱动轮

生成器按位置写 `car_base_wheel [10:13]`（前 `[0,1,1]`、后 `[1,-1,1]`、边 `[1,-1,0]`），边斗轮也是驱动轮。EDF6 Car 在 `0x679CFC..0x67A004` 读这三列：[10] 非 0 → 轮 `+0x64`（驱动；原版两款摩托 V503 / V613 只有后轮为 1，V512 后轴为 1），[11] ≠ -1 → `+0x65`（转向），再 `+0x70[[11]] = [12]`（[12] 只在转向时读）。4.1 `VehicleBike` 在 `EDF41.exe 0x2CC7AD..0x2CC803` 读自己的 `vehicle_tire`：[8] → `+0x64`（`0x2CBF2A` 给前两只轮预设 1：转向），[9] → `+0xA8`（默认 1），[10] → `+0x68`（默认 0.25）。408 的三行：前 `[8]=1,[9]=1,[10]=0.0`，后 `0,1,1.0`，边 `0,0,0.0`（且 [2..4] 抓地参数全 0）。按「[9] 驱动且 [10] 份额 > 0」读，4.1 的 408 是后轮驱动、前轮转向、边斗轮自由，与 EDF6 原版摩托一致。[9] / [10] 的语义是按全部 4.1 车辆的取值分布与读入默认值推断的（坦克全轮 1 / 0.1、Striker 后两轴 1），没有跟到 4.1 运行时读者。现由 `edf41bike.wheel_flags` 从源行推出：前 `[0,1,1]`、后 `[1,-1,0]`、边 `[0,-1,0]`。

### 根因 3：驾驶座用的是长凳坐姿

`seat_mab` 给两座都写 `Animation=vehicle_striker_rear`（卡车 / 救援车长凳），驾驶员腿向前伸、手不在车把上。座位动画是座位定位点 MAB 参数，按名字在乘员自己的 CAS 上播（`docs/all-classes-ride-re.md` §3）。EDF6 原版摩托（V503 / V613）驾驶座是 `vehicle503_bike_driver` / `vehicle613_bike_driver`，只有游骑兵、空降兵的 CAS 有；4.1 的 408 驾驶座掩码本来也是 9（游骑兵 + 空降兵），生成器却写成 15。

修复：驾驶座改播 `vehicle503_bike_driver`，两座掩码取 4.1 原值（9 / 15）。`pose_fit(games, masks)` 只对「有该姿势」的职业拟合座位定位点，座位允许的职业缺姿势就拒绝生成。游骑兵 / 空降兵骨盆：4.1 `vehicle_bike_driver` (0, 0.926, -0.079)，EDF6 `vehicle503_bike_driver` (0, 1.016, -0.091)，驾驶座定位点因此从长凳时的 (0.018, 0.604, 0.345) 改为 (0, -0.090, 0.012)，两职业骨盆误差 0（生成时校验 ≤ 8 cm）。炮手座仍是长凳姿势（四职业都有）。飞翼 / 剑兵只能经 `AllClassesRide` 放宽后开 408，与原版摩托一样落到 `allclass.cpp RideState` 的长凳回退，骨盆比车座低约 0.6 m（与他们骑 V503 时相同的已知观感问题，见 `docs/all-classes-ride-re.md` §8）。

### 防止再发：发的文件必须是生成器的产物

- `tests/test_edf41bike.py` `Shipped`（不需要游戏文件，ctest `edf41_bike_models`）：对提交的 SGO / 测试场副本 / SHKT 直接核 gun 行值 0、`pinned()` 为空、跌落座位、三轮的驱动 / 转向列、两座动画与掩码、`CAR_DRIVE`。修前提交的数据在这里 4 项红。
- `Native.test_shipped_object_is_this_generator_output`（有游戏文件时）：提交的 SGO / SHKT 与 `build()` 逐字节一致，测试场副本除 `mission_setup` 外一致。
- 联机：新功能位 `kFeatBikeRide`（`src/host_settings_logic.h`）。功能位 64 / 128 的构建实际发的仍是旧数据，所以另起一位；缺它的成员在房间状态行列为「408 驱动轮与骑姿」可能不一致（数据，只提示，不按任务关闭）。

### 尚待实机

未开游戏。需实测：SDL1 各型号按 W / 油门能正常起步、直行不偏、转向正常；游骑兵 / 空降兵驾驶姿势手在车把上、腿不穿模（503 的姿势套在 408 的车把位置上，手位只按骨盆对齐，未核）；飞翼 / 剑兵开车时长凳回退的观感；炮手座、开火、落地不悬空在新数据上回归。
