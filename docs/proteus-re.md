# 普罗透斯（Proteus）重做：逆向笔记（静态，2026-10-09 重写）

EDF.dll TimeDateStamp 0x678CCB46，地址都是 RVA。H = 反汇编里直接看到，M = 由代码结构推出，L = 推断、需要进游戏看。
用到它的地方：`src/proteus.cpp`（单元生命周期、姿态、座位、力场、HUD 发布）、`src/proteus_weapons.inc`（原版三座武器的借用）、
`src/proteus_shield.inc`（原版电磁碉堡护盾）、`src/proteus_visual.inc`（支撑桩）、`src/proteus_net.inc`（联机）、
规则 `src/proteus_logic.h`（离线检查 `tools/proteus_check.cpp`），资源 `tools/make_proteus.py`。
2026-10-09 用户：「普罗透斯的护盾用原版的护盾样式吧。还有这个普罗透斯做的稀烂，给我完全重做」。重做的理由与取舍见
`docs/feedback-2026-10-09-proteus.md`。**全部未进游戏验收**；日志依据是 `Debug=1` 的 `PROTEUS` 行。

## 0. 设定核对（EDF5 原资料）

EDF5 `Root.cpk`（`D:\steam\steamapps\common\EARTH DEFENSE FORCE 5`，CPK 文件偏移按 2048 基址读）里的
`OBJECT/VEHICLE407_BIGBEGARUTA.SGO`（类 `VehicleBegaruta`）：`vehicle_riding_position` 四个座位
`407_BIGBEGARUTA_DRIVER / _GUNNER_L / _GUNNER_R / _GUNNER_C`，`vehicle_weapon_setting` 左炮 → 座 1、右炮 → 座 2、
`rocket_launcher` → 座 3，武器 `v_407bigbegaruta_cannon`（两门）和 `v_407bigbegaruta_missile`。和 EDF6 的 V407 / V614 完全同构（H）。
所以「原版普罗透斯是双人机甲」并不成立：原版是**驾驶员 + 三个炮手**，驾驶席没有武器；「两席」是用户 2026-10-06 的需求（「座位：两」）。
重做保留两席，但不再造任何替代武器：空着的炮位由在座的人借用原版武器（§3）。

## 1. 类与 SGO

| 项 | 值 | 可信度 |
|---|---|---|
| SGO | `OBJECT/V614_PROTEUS_MK2[_AI/_CALL/_FLAME_AI/_LASER_AI/_MISSION].SGO`、`VEHICLE407_BIGBEGARUTA[_AI/_FLAME_AI/_MISSION].SGO`，`xgs_scene_object_class='VehicleBigBegaruta'` | H |
| vtable | `0x17DEC40`；`veh+0x120` 的接口 vtable `0x17DEE10` | H |
| 插件入口 | `crew.cpp kClasses` 挂 slot 4 = `0x644350`（家族共用的玩家每帧更新） | H |
| 座位 | 0 驾驶员（无武器）、1 左炮、2 右炮、3 导弹架；每个 SGO 都是四座 | H |
| 武器 | `V_407BIGBEGARUTA_CANNON`：RocketBullet01，FireInterval 20，200 发；`V_407BIGBEGARUTA_MISSILE`：MissileBullet01，追踪，LockonRange 600，FireBurstCount 30 | H |
| 脚下胶囊 | `begaruta_rigid_body = [6, 5, 0.6, 50, 75]`：半径 5 m，最陡可走 50° | H |

## 2. 腿、跳跃、台阶（未改，沿用）

- `0x645A20`（由 `0x645790` 调）：移动 `veh+0x1950` 向「摇杆 × `+0x1978`」以 `+0x197C` 缓动，转向 `+0x1974` 向「−LX × `+0x1980`」缓动。
  签名 `0x645A2E` / `0x645A94` / `0x645AA3` / `0x645AC3`。`+0x1978/+0x1980` 只由 setup `0x647EA0` 写（H）。硬直 / 架设时插件同时清零 `+0x1950`、`+0x1974`。
- 跳跃：控制器在 `veh+0x1720`（`0x63937A`），跳跃速度 `+0x290`（`0x6393C8`）。非行走写 0。
- 台阶：可走地面判据 `+0xD4`（`0x11B9D67` 写、`0x11B95EB` 每个接触读）；`proteus::StepNormal` 把 `ProteusStepHeight` 换算成法线高度，只放宽不收紧（M：几何推断）。

## 3. 武器：原版三座武器的借用（`src/proteus_weapons.inc`）

旧实现给驾驶员造了两种插件弹（炮舰机的 40 mm 机炮弹、炮舰机火箭弹齐射），要「标记」目标、要预载、要伤害钩子补档位，还要把原版导弹架「停住」。
重做后**只有原版武器**：一座武器的座位空着时，由它链条里第一个在座的士兵借用（`proteus_logic.h kMounts` / `Operator`）：

| 武器（座） | 借用链 | 用谁的扳机 |
|---|---|---|
| 左炮（1） | 1 → 0 | 驾驶员借用时：驾驶员主扳机 |
| 右炮（2） | 2 → 1 → 0 | 炮手借用时：左炮自己的扳机锁存（`+0x139/+0x13A`，原版输入已按炮手填好，玩家或 NPC 都一样）；驾驶员借用时：主扳机 |
| 导弹架（3） | 3 → 0，**只在架设时** | 驾驶员副扳机（手柄 `seat+0x2E0` = LT；键鼠 `ProteusSalvoKey`，默认鼠标右键） |

座位上有任何乘员（玩家、NPC、Dummy）时这座武器完全是原版的。原生入口（全部 H）：

- **瞄准**：BigBegaruta 的 slot 5 是 `0x644910`（`jmp 0x630250`）。`0x630250` 先跑 slot 45 姿态，再逐座调用武器的有人 / 空座回调，最后把炮骨矩阵写进 `weapon+0x150`。
  插件在 slot 5 之前（`ProteusWeaponPost`）把被借用武器座位的瞄准轴（`seat+0xE0` 内嵌的 VehicleWeaponAim）抄成操作者座位的角度、夹在自己的限位里，
  再对每根轴调 `0x5FC280(axis,true)` 更新骨骼。所以本帧的姿态、炮口矩阵和开火都是真炮管。旧版的两个瞄准 vtable 钩子（`0x17D8A68/0x17D8A90`）删了。
- **激活 / 开火**：`0x6302B0` 处空座回调的 `lea rax,[0x690230]` 指到 `ProteusEmptyWeapon`：被借用的武器调原版激活 `0x68F8E0`，
  并在操作者所在的机器上、扳机按着时调原版 holder 拉扳机 `0x62C000`；其他所有武器（所有载具）照旧调原版停用 `0x690230`。
  导弹架因此走原版的锁定 tick（`0x6963A0`：武器激活且被拉着时才搜索，`docs/lockon-re.md` §2）和原版追踪导弹（M：锁定方向取武器矩阵，未实机）。
- **使用者**：开火步 `0x690C0E` 问接口 slot 11（BigBegaruta 的在 `0x17DEE68`，原版 `0x62D950`）「谁在操作这把武器」，答空就不打。
  `0x62D950` 对空座返回该座的「上一任乘员」（`seat+0x300/+0x308`），可能已不在或在别的机器上。`UserHook`：重构中的普罗透斯、座位空着的武器，
  答借用者（其 `+0x120`，和原版返回值同形），没人借用就答空；其他照旧问原版。签名不符或挂不上时 `userOk=false`，什么都不借用（空座回调保持原版）。
- 架设 / 行走的炮数值：`weapon+0xE10`（倒计时速率，`0x693A58`）、`+0xE14`（散布倍率，`0x691AFA`），只改两门炮，导弹架保持原值；还车时写回接管时读到的值。

## 4. 护盾：原版空袭兵电磁碉堡（`src/proteus_shield.inc`）

旧实现给两种模型生成了私有 MRAB/CAS（36 块 Fencer 盾材质的平板 + 36 根骨），并改写了全部 10 个 VehicleBigBegaruta SGO 去引用它；
伤害由插件钩 `0x54A586` 按角度扣。重做后护盾就是**空袭兵「電磁トーチカ」（WEAPON/EWEAPON196.SGO）那面能量墙本身**：弹种 `BarrierBullet01`。

| 项 | 内容 | 可信度 |
|---|---|---|
| vtable / ctor / 更新 | `0x17A45E8` / `0x28FB00` / slot 5 `0x2917B0`；受击 slot 9 `0x2913B0` | H |
| Ammo_CustomParameter | [0] 弧（弧度），[1] 半径，[2] 高度，[3] 缩放 (sx,sy,sz)，[4] 局部偏移；分段数 = int(弧 / 0.0872)（常量 `0x17A46C8`，`0x2900A7`），弧的中心在局部 +Z；网格下面另有 2 m 裙边；碰撞网格层 14，key2 = BarrierInfo（`+0x1530`） | H |
| 阵营 | 构造时从 IFC 的阵营（`+0xD0`，发射者 `+0x314`）复制，之后不刷新；子弹候选收集 `0x232AA0` → `0x22E800` 只收敌对阵营的弹：**敌弹挡住，己方 / 友军的弹穿过** | H |
| HP | `+0x14D8` float，构造时取伤害（`0x28FD62`）；`0x2913B0` 对消息 0x10000000 扣 `[msg+0x50]`（`[msg+0x60]&0x40` 的重放不扣）；HP ≤ 0 或 `+0xC34&1` 时 slot 5 走 `0x291B73` 删除（`0x29189E`、`0x2918AB`）；没有碎裂特效；联机时先发删除包，除非 `+0x154C` 已置（`0x291B99`） | H |
| 位置 | slot 5 每帧把 `+0xC90..+0xCC0`（right / up / forward / pos，子弹核心的矩阵）复制到对象矩阵 `+0x60`（`0x2918BE`），再把碰撞 body 放过去（接口槽 0x88）；slot 3 用 `+0x60` 生成渲染矩阵（乘 AmmoSize） | H |
| 粘附 | 子弹自己碰到东西时 `+0xD40` 置 1（`0x2918F0` 读），之后位置被锚定（`0x236AC6` / `0x236B4C`），插件写矩阵无效 | H |
| owner | 弱引用在 `+0x880/+0x888`（核心初始化 `0x231FF2`） | H |
| 装置模型 | ctor `0x28FD02` 用 **InitParam+0x1B0**（发射武器 SGO 的 `animation_model`，武器初始化 `0x68DAD5` 填）建模型；`0x6BB890` 遇空 variant 抛 `sgs::ut::InvalidVariantException`。IFC 的 InitParam（IFC+0x40）这一格永远为空，所以插件必须自己填（`src/ifc_model.h`；2026-10-10 B 键闪退，`docs/feedback-2026-10-10-proteus-shield-crash.md`） | H |

插件做法：

- **资源**：`tools/make_proteus.py` 生成 `OBJECT/EDF6VC_PROTEUS_SHIELD.SGO`：原版 `DEMOGUNSHIPFIRESOLID.SGO`（DemoIndirectFire）改成一发 `BarrierBullet01`，
  速度 0、重力 0、寿命 2^30 帧、AmmoSize 1、命中半径系数 0.01（不碰东西，避免被锚定）、颜色和展开音效取自 EWEAPON196，
  Ammo_CustomParameter = [120°, 12 m, 17 m, [1,1,1], [0,0,0]]（MK2 机体前伸 9.7 m、侧宽 8.9 m、高 15.2–15.9 m）。分段数 24（电磁碉堡是 27）。
- **装置模型**：SGO 另带 EWEAPON196 的 `animation_model`；`EmcFire` 在 IFC 发射前把它写进 IFC+0x1F0（InitParam+0x1B0），拿不到就不发、本关停用护盾。
- **立起**：护盾该立着且没有墙时，`EmcFire(EmcRound::proteusShield, ...)`（`jet_bay.cpp` 的 DemoIndirectFire 路径，和 EMC / 沙扎比的弹同一张表，
  文件存在就随关卡预载）。新墙第一次更新时按「分段数 24 + owner 是这台普罗透斯或它的乘员 + 有待领取的立起」认领，写入 HP。
  30 帧内没看到就记日志、把护盾开关关掉，不反复重试。
- **跟随**：钩 `0x17A45E8` 的 slot 5；对自己的墙，原版更新之前把 `+0xC90` 写成「竖直、+Z 朝护盾方向、位置在机体脚下」的矩阵，原版随即移动渲染与碰撞。
  行走时朝车头，架设时朝驾驶员的水平视线。
- **HP**：墙的 `+0x14D8` 就是护盾 HP（满 = `ProteusBarrier` × 机体最大耐久）。注册 owner 上读它（`proteus::Sense`：下降 = 挨打，0 = 击碎）；
  别的机器把 owner 发来的数写回自己的墙。
- **收起**：不再需要（关掉、过热、击碎、下车、关重构、联机失控）或被粘住时，置 `+0x154C` 再把 HP 写 0：原版下一帧自己删除，不广播。
  只碰自己登记过的墙（指针 + owner 弱引用都对上），空袭兵的碉堡从不被认领或改动。

## 5. 姿态与支撑桩（`src/proteus_visual.inc`）

slot 45（`0x17DEC40+45*8`，原版 `0x6437D0`）之后，对重构中的普罗透斯把原版模型自带的桩骨（MK2 `pile_l/pile_r`，407 的四根 `Pile_*`）先回到 bind，
再按架设进度落到地面（`MapRay` 探地），用原版 `0x1100B90` 合成世界矩阵。还车那一帧把桩写回 bind 一次。不再生成任何模型。

## 6. 力场与友军优先（沿用）

- 力场：团队遍历 `0x5E11D0`；受伤 / 攻击倍率 `+0x384/+0x388`（`0x54BED5` / `0x54BEE5`）；士兵武器表（`0x59B53E`）；翼装兵能量（`0x580D6B`）；
  ReloadType（`0x693E8D`）与积分充能（`0x693F18`）。多个场取最强、不叠加（`proteus_field.inc`）。
- 友军优先：`SearchAttackTarget` slot 1 `0x598C50`（`0x598C60`、`0x598CE9`），行走且护盾立着时把普罗透斯附近 / 攻击它的敌人距离按 `ProteusPriority` 打折；
  EDF6AutoTurret 通过 `PriorityZoneV1` 同样打折。

## 7. 已删除的旧机制（及其地址）

伤害钩 `0x54A586 → 0x547C30` 与原生许可门、私有 MRAB/CAS 与 36 块盾板、驾驶员机炮 / 齐射插件弹、标记目标、导弹架「停住」（`kProteusHoldCountdown`）、
两个瞄准 vtable 钩子。对应测试（`proteus_damage_*`、`proteus_round_native_audit`、`proteus_cas_*`）一并删除。

## 8. 签名（安装时核对）

| 地址 | 内容 | 不符时 |
|---|---|---|
| `0x645A2E` `0x645A94` `0x645AA3` `0x645AC3` `0x63937A` `0x6393C8` `0x11B9D67` `0x11B95EB` `0x6346FC` `0x693A58` `0x691AFA` `0x62C000` `0x5FC280` | 腿、跳跃、台阶、座位职业掩码、倒计时、散布、拉扳机、轴应用 | 整个重构关闭 |
| `0x6302B0`（`lea rax,[0x690230]`）、`0x630250` 序言 | 空座回调与 slot 5 | 整个重构关闭 |
| `0x62D950` 序言、接口槽 `0x17DEE68` | 武器使用者 | 不借用任何武器 |
| `0x2917B0` 序言、`0x29189E` `0x2918AB` `0x2918BE` `0x2918F0` `0x291B99` `0x28FD62`、vtable `0x17A45E8` 槽 5 | 电磁碉堡墙的更新与 HP | 没有护盾（HUD 显示 OFFLINE） |
| `0x28FCEC` `0x28FD02` `0x100321` `0x5B56F9` `0x5B5723` `0x5B5767`（`jet_bay.cpp kIfcModelSigs`） | 墙的装置模型从 InitParam 取、DemoIndirectFire 的 SGO 根与 visitor | 护盾 SGO 不预载（没有护盾） |
| `0x5E11D0` `0x54BED5` `0x54BEE5` `0x59B53E` `0x580D6B` `0x693E8D` `0x693F18` | 力场 | 没有力场 |
| `0x598C50` `0x598C60` `0x598CE9` | 士兵选目标 | 友军不优先 |

## 8b. 线程：子弹更新与车辆帧是串行的（2026-10-09 核对）

`proteus_shield.inc BarrierStep`（墙的 slot 5）与 `BarrierFrame` / `NetworkFrame`（车辆的输入帧，挂在 slot 4 链上）都读写 `units[]` 的 `barrier.obj`、`st.shield`，插件不加锁。依据（H）：
- 主循环 `0x705715` 以对象管理器 `*(0x20B2958)` 调 `0x1198DA0`（它唯一的调用点）。`0x1198DA0` 在同一函数里依次遍历：`+0x488` 表调 slot 7（`0x1199131`）、对象表调 slot 4（`0x1199261`，车辆每帧更新，即插件的 InputHook → `ProteusFrame`）、`+0xCC0` 表调 slot 8（`0x119939B`）、`+0x448` 表调 slot 5（`0x119940D`，含子对象递归 `0x1199BC0`）。都是普通 for 循环（夹在中间的 `0xDAC5B0` / `0xDB04B0` 都是空函数 `ret 0`，剖析桩）。slot 4 循环之前另有一次 `0x65E90` → `0x65F10` 对 `+0x358` 表的派发与等待（`0x1199174` / `0x119917D`，M：像并行任务加 join），它在 slot 4、slot 5 两个循环开始前就已返回，不与它们重叠。
- 墙由 IFC 的发射 `0x2B9F68` 调工厂 `0x1194280`，第一个参数就是同一个 `*(0x20B2958)`（`0x2B9F61`），所以墙也在这张表里，它的 slot 5 在同一线程、在当帧所有车辆的 slot 4 之后执行。
- 因此不需要锁或单写者队列。联机收包（`0x6325B0` 链上的 `Receive`）走对象事件处理，推断也在主线程（M），它只经 `GiveBack → DropBarrier` 清指针，不解引用墙。

## 9. 验证边界

- 离线 / 原生（不启动游戏）：`proteus_check`（规则）、`proteus_frame_test`（生产代码夹具：姿态、座位、借用、护盾认领 / 跟随 / 击碎 / 收起）、
  `proteus_weapon_native`（私有映射真实 EDF.dll：`0x62D950` 过期上一任被覆盖、`0x68F8E0` 激活 + `0x62C000` 拉扳机过 `0x6912F0` 就绪门、
  真实空座回调补丁、真实电磁碉堡签名与 slot 5 挂链、真实受击 `0x2913B0` 扣 HP 后护盾读到并击碎）、`proteus_net_runtime`、`proteus_assets`、`proteus_install`、`proteus_range`。
- 必须进游戏确认：墙的实际外观与大小、跟随移动时碰撞是否同步、敌弹是否被挡、行走时墙是否碰到地形被锚定（L）、墙的「装置模型」是否藏在机体里、
  驾驶员借用导弹架时原版锁定是否成立、借用的加农炮在画面上的弹道、联机两台机器上的墙。
