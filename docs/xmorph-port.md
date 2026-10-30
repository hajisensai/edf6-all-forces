# X-Morph: Defense 人类单位移植（2026-10-27）

把 X-Morph: Defense（EXOR Studios）里人类一方的地面单位做成 EDF6 的友军载具。本文件只记模型转换这一半
（`tools/prep_xmorph.py` 和它读的库）；单位登记、武器、SGO、安装链见 `pylib/xm_units.py`、`tools/make_xmorph.py`。

## 来源与授权立场

- 素材取自用户本机正版 `D:\steam\steamapps\common\X-Morph Defense` 的 `packs/`，**只读**；包里的 Lua 等一律当数据，不执行。
- 转换结果是 EXOR 的游戏资产，**不进公开仓库**：和 sazabi / dreadnought 一样放模型包（开发机 `D:\APP\edf6-models`，
  `obj_model.DEV_MODELS` / `model_roots()`），由分享流程的模型包分发给用户自己的安装。每个文件夹的 `LICENSE.txt` 写明出处与此立场。
- 仓库里只有读取与转换代码、测试；CI 没有 X-Morph，资产相关的测试自动跳过。

## 包结构（`pylib/xm_pack.py`）

- `packs/**/*.zip` 都是普通 zip，游戏把它们挂成一棵树：文件名前导数字大的覆盖小的（`00_win32_data.zip` < `01_…` < `02_…`），
  `Packs` 按这个顺序叠加，`read(path)` 拿到的是当前生效的那份。
- 地图另有一层版本：同一张图出现在 `maps/<图>`、`maps_6/<图>`、`maps_7/<图>`…，编号最大的是游戏实际用的（`map_dir()`）。
- 单位：`entities/units/<类>/<名>.ent`（EXOR 花括号文本：`RenderableDescComp.mesh`、`SkeletonDescComp.name`、`TurretComp`、
  `WeaponComp` …）；网格 `meshes/units/…/<名>.mesh` + `.skeleton` + `.att`（挂点：`Attachment {name, parent, offset "x y z"}`，
  offset 在父骨骼绑定坐标系里，`scene_node` = 模型原点）；材质脚本 `materials/scripts/<组>.material`，
  `set_texture_alias diffuse|specular|normal|emissive <路径>`。
- 文本格式：一行的首词带走本行其余部分（`set_texture_alias diffuse a.dds` 是一个值），后面跟 `{` 就是子块；同一 key 可重复。
  **一行只写一个键值**（游戏文件都是这样），同一行写多个键会被并成首词的值。

## Ogre 格式的坑（`pylib/ogre_mesh.py`）

- 网格头是原版 `[MeshSerializer_v1.41]` 或 EXOR 的 `[ExorSerializer_v1.41]`（单位用后者），块布局相同；骨架 `[Serializer_v1.10]`。
  包里全部 25,628 个网格都能读。
- **容器块（mesh、submesh、geometry）存的长度不可信**：Ogre 写容器长度时不算紧跟其后的子块（如 submesh 的骨骼权重）。
  所以按「允许的 id 集合」顺序读：容器停在读者停下的地方，只有叶子块按存的长度跳过。
- 骨架的 bone 块长度也不含名字和缩放：按内容读，缩放是否存在看下一个 u16 是不是已知块 id。
- 动画块总在最后、长度同样不可信：遇到就停止，不往后读。
- 顶点已在模型空间（绑定姿态），单位是刚性件：每个顶点取权重最大的骨骼（`rigid_bones`）。

## 坐标系与比例（`pylib/xm_model.py`）

- X-Morph：+X 前、+Y 左、+Z 上，米，地面 z = 0（炮管指 +X；左翼 / 左履带在 +Y；`att_smoke_wheels_left` 在 +Y）。
- EDF：+Z 前、+X 左、+Y 上。换算 `EDF (x, y, z) = XM (y, z, x)`：循环置换，是真旋转（行列式 +1），缠绕方向不变；
  `prepare()` 用车体有符号体积复核，7 台全部 `winding_flipped = false`。
- UV：Ogre 是左上原点，和 EDF 一致；因为 `prep_wt_tracked.prepare()` 按其 OBJ 来源写 `(u, -v)`，喂给它时先取 `(u, -v)`。
- 比例 1.0（`SCALE`，写进 `model.json` 的 `xmorph.scale`）：X-Morph 的米是真实的（民用车 4.4 × 1.8 × 1.45 m），只是它的坦克本来就大
  （中型 13 m、重型 18 m 长），和已接受的天启（14.5 m）同一量级，所以保持游戏原比例。
- 贴图：diffuse（DXT1）原样转 PNG；normal 是 DXT5 的「X 在 alpha、Y 在 RGB（三通道相等）」布局，重建为 RGB 切线空间（Z 由单位长度求），
  只在材质脚本声明了 normal 时使用（large_tank、artillery、shield_generator、large_tank_emp 的材质没声明，即使包里有文件也不用，与游戏一致）。
  specular / emissive 丢弃：EDF 的 `v603_body` 模板没有对应槽；X-Morph 没有 AO 图，遮蔽通道用 wt_tracked 的默认值。

## 骨骼映射（自动，读 `.ent` + 骨架）

`tools/prep_xmorph.py rig()`，新单位不需要手写配置：

| V603 骨骼 | 取自 |
|---|---|
| `cannon_main` | 第一个由 TurretComp 炮塔瞄准、且 yaw / pitch 都是骨骼的武器（主武器）的 yaw 骨骼及其子树（炮除外）；外加挂在车体上、但立在炮塔座圈内（离转轴水平距离小于炮塔网格半径、且高于转轴）的骨骼（如防空车炮塔顶的 `be_radar`） |
| `cannon_l` | 主武器 pitch 骨骼及其子树中除驻退骨骼外的部分（一门炮一个摇架，`cannon_r` 无网格，同 PzH 2000） |
| `cannon_slide_l / _r` | 主武器各炮口的 `recoil_bone`（没写就取炮子树里 `recoil*` 骨骼）：两根按左右分；一根放 `_l` |
| 炮口 | 主武器 Muzzle 的 attachment（`.att` 挂点或同名骨骼）：两个按左右分；一个则左右同一点（`prep_wt_tracked.Vehicle.mouths`） |
| `doppler_radar` / `tracking_radar` | 不放网格；物理代理拟合到炮塔顶部 30% 的切片，左右各一半（与 `cannon_main` 的碰撞上下、两者之间左右各留 0.2 m） |
| `tire_moveA..H_l/_r` | 合成负重轮（见下） |
| `catapi_body` | 履带带体的行走面（材质名 `v603_kyata_L / _R`） |
| `body` | 其余：车体，以及车体自己的副炮塔、发射架（重坦的 `turret_3`、EMP 坦克的两座防空炮和导弹塔、护盾车的护盾发生器——它们不随主炮塔转） |

- 唯一的手写例外 `MOUTHS`：防空坦克主武器的 `att_muzzle_3` 在两个炮舱中间（炮塔中线），实际炮口是 `att_muzzle_1 / _2`（`barrel_1` 上 y ±1.52）。
- 炮的碰撞分成兄弟体 `cannon_l / cannon_r`（D93250 兄弟互碰）：两个炮口横向相距 > 0.5 m 时左右切（防空坦克、重坦、EMP 坦克），
  否则沿炮膛切（炮管前段归 `cannon_l`，摇架 / 后段归 `cannon_r`），中间留 0.2 m。
- 主武器之外的武器（重坦的机枪塔与导弹、EMP 的防空炮 / 导弹、主动防御激光）不映射：`model.json` 的 `missiles` 为空，
  `make_xmorph` 侧的槽位只用左右两门。

## 合成负重轮

X-Morph 的负重轮和履带都烘焙在 `body` 网格里，没有轮骨骼；V603 需要每侧 8 个轮位（`tracked_collision._wheel_overrides` 读
`wheel_{A..H}_{l,r}` 部件盒子，B..G 的半径写进 `car_base_wheel` 决定接地点）。做法（`running_gear()`）：

1. 每侧：车体上整片低于 0.15 m 的三角形是履带接地段；它们的横向范围就是履带带的宽度（slab），沿 z 的区间是接地段。
2. 相邻接地段之间，如果带内一直有低于 0.35 m 的几何（容忍 0.3 m 的面间空隙），视为同一条履带抬离地面的部分，合并；否则是分开的履带舱（进阶坦克前后各一个）。
3. 带顶 = 段上方最低的朝上面（带的上回程）的中位高度。
4. 8 个轮位沿接地段总长均匀排（A 在前，H 在后），半径 = min(0.42 × 段长 / 8, 0.4 × 带顶)，轮心高 = 半径（轮底贴地，物理接地点即履带底），
   宽度 = 带宽的 60%，居中：藏在带体里看不见；用车体材质、整轮一个履带面的 UV。
5. 带内不朝侧面（|n_x| < 0.5）、不高于带顶、在接地段前后坡范围内的车体三角形归 `track_l / _r`（`catapi_body`、履带材质名）。

`tracked_collision` 没有「无可见轮」模式，`check` 也按部件盒子核对轮心，所以合成轮是最直接的办法，不是兼容分支。

| 单位 | 三角形 | 轮半径 | 炮塔原点 (x,y,z) | 炮口 L / R | 炮碰撞切法 |
|---|---|---|---|---|---|
| xm_medium_tank_regular | 5,183 | 0.30 | (0, 1.84, −0.11) | (0, 2.25, 7.55) 同点 | 沿炮膛 |
| xm_medium_tank_advanced | 8,163 | 0.24 | (0, 3.27, −0.94) | (0, 4.31, 7.00) 同点 | 沿炮膛 |
| xm_medium_tank_aa | 3,874 | 0.28 | (0, 3.16, −2.43) | (±1.52, 4.0, −1.22) | 横向 |
| xm_large_tank | 9,415 | 0.51 | (0, 3.49, 0.63) | (±1.07, 4.55, 10.84) | 横向 |
| xm_large_tank_emp | 17,761 | 0.36 | (0, 5.33, 1.90) | (±0.49, 6.54, 9.26) | 横向 |
| xm_medium_artillery_regular | 4,715 | 0.26 | (0, 2.05, −2.89) | (0, 2.45, 5.53) 同点 | 沿炮膛 |
| xm_medium_shield_generator | 4,849 | 0.27 | (0, 3.38, 1.55) | (0, 4.20, 3.08) 同点 | 沿炮膛 |

（三角形含合成轮；每个文件夹 0.3–0.8 MB。`model.json` 的 `xmorph` 段记了全部骨骼归属、接地段、带顶等转换记录，wt_tracked 不读它。）

## 产物与复用

```
python tools/prep_xmorph.py tracked [unit ...] [--out DIR] [--game DIR]
  → <out>/xm_<名>/  model.json（FORMAT 1）、model.npz、textures/（PNG）、LICENSE.txt
```

- 复用 `tools/prep_wt_tracked.py`：把 Ogre 单位组装成它的 `Source`（对象名按其约定 `<节点>_<序号>`）和 `Vehicle`，调用 `prepare()`。
  为此只加了两个小钩子，战雷车行为不变（M163 重新生成后 `model.json` / `model.npz` 与仓库内逐项相等）：
  - `Vehicle.mouths`：每侧炮口点不取炮管节点盒子前端时给出（炮管不是独立节点的来源）；
  - `Vehicle.clip` 的值可以是一组 `(轴, min/max, 值)`（雷达切片要同时切高度和左右）。
- 贴图写到临时目录当作 MTL 的文件，`prepare()` 照常复制进 `textures/`。
- `pylib/xm_model.py` 与家族无关（实体、网格→EDF 坐标、刚性骨骼、挂点、炮塔 / 武器、材质贴图），轮式 / 直升机家族可直接复用。

## 炮台（`prep_xmorph.py emplacement`）

- 堡垒的两种防空炮（`fort/aa_cannon`、`fort/aa_cannon_machinegun`，同一网格、不同武器）转成 `tools/prep_glb_model.py` 的文件夹格式
  （按角色分件：`body` 归 base、炮塔归 turret、炮归 gun；枢轴：炮塔 yaw 骨骼为座圈、pitch 骨骼为耳轴，炮口取主武器挂点），
  由 `pylib/emplacement_model.py` 放上加固底座（V603），与 88 高炮、GDF-005 同一路径；`emplacement_model.KINDS` 从
  `xm_units.ported('emplacement')` 取行。V603 上的两个家族（履带、炮台）的登记表统一从 `xm_units.v603()` 取，是否部署统一看
  `xm_units.deployed()`（文件夹里 model.json + model.npz 齐全）。
- 兵营（`bunker_barracks*`）没有武器，归 `structure` 家族（地图摆件），不是炮台。

## 新增一个单位

1. `pylib/xm_units.py` 加一行（实体路径、家族、`like`、状态）。
2. `python tools/prep_xmorph.py tracked xm_<名>`（写进模型包）；看生成日志里的骨骼归属、接地段，必要时离线渲染核对。
3. 只有蓝图误导时（主武器炮口不在炮上之类）才在 `MOUTHS` 加例外，并写明证据。

## 验证（本机，离线）

- `python tests/test_xmorph.py`：合成 Ogre 网格 / 骨架（含不可信的容器长度、带缩放的骨骼、动画块）、文本格式、坐标换算、法线图重建、
  合成单位的骨骼规则与轮位都在 CI 上跑；装有 X-Morph 时再从包里读 medium_tank_regular、准备到临时目录，有 EDF6 Root.cpk 时再走
  `wt_tracked.build_with_info`（含 `check`）与 `tracked_collision.build`（含 SHKT `shkt_check`、布娃娃 `ragdoll_fit.problems`）。
- 7 个文件夹逐个：`wt_tracked.build_with_info` + `check_skeleton` + `tracked_collision.build` + `animation` + 炮口世界坐标回代全过；
  已登记的 xm_medium_tank_regular 走完整 `make_tracked_combat.authored` + `check`。
- 离线渲染（贴图侧 / 顶 / 前视图、按骨骼着色、只画轮与履带）核对了贴图 UV、朝向、骨骼归属与轮位。

## 未做 / 风险

- **履带贴图滚动**：X-Morph 的履带是车体图集上的静态贴图，`v603_kyata_L / _R` 只是给 SGO 的 `tank_caterpillar_animation` 留名字；
  若 SGO 照 V603 的速率滚动，会把图集相邻区域滚进履带面。需要 SGO 侧对 xm 单位关闭或置零滚动（`make_xmorph` 的事），未实机核对。
- 法线图 Y（绿）通道：X-Morph 是 OpenGL 约定（+G = 贴图上方），原版载具与我们的副法线（+v，贴图下方）是 DirectX 约定，`xm_model.normal_rgb` 取反 Y。证据（旋度符号与高度 / 颜色相关）见 `docs/texture-quality.md`。
- 合成轮在履带带体内，带体若一侧不封闭（个别单位的带侧敞开）可能露出；EMP 坦克有一个轮位落在梯子附近。离线渲染看不出明显外露，未实机看。
- 炮口用 X-Morph 的挂点：常规坦克炮口在炮管末端后约 0.6 m（炮口制退器起点），炮弹从管内出；未实机核对是否打到自身。
- `artillery` 的 `doppler_radar` 切片在左半边没有可拟合的块，退回把供体凸包拉伸到目标盒（`tracked_collision._fit_shape`，`fitted_pieces 0`）。
- 主武器之外的武器、主动防御、护盾 / 力场效果、摧毁阶段网格（`_dest_*`）都没移植。
- 7 辆履带车全部开放呼叫（2026-10-27，武器表第 85–91 行）。耐久 ≈ 原作 max_health 的比例（战斗坦克 170 → 2500 为基准，
  装甲很薄的防空车 / 自行火炮给到 1500 保底）。视角与瞄准镜位置按各模型实测的炮塔顶与炮口（`xm_units.py` 各行注释）。

## 武器（攻击方式按原作，2026-10-27 用户：「攻击方式按照游戏里面那样搞」）

- 数据来源：单位蓝图 `WeaponComp` 的每个 `Weapon`（`weapon_definition`：`fire_rate` = 一轮间隔秒数，`FirePattern.burst_rows` 发、
  间隔 `burst_rate` 秒，`muzzles_per_fire`），弹头是 `ammo_blueprint` 指向的**具名蓝图**（定义在 `entities/projectiles/*.ent` 这类
  一文件多蓝图的组文件里：`xm_model.named()` 按名字索引全部 EntityBlueprint）：`MovementComp.speed`、`DamagePattern.Damage.value/type`、
  `Area.radius`、`AimPattern`（导弹追踪什么）。
- 换算（`xm_units.DAMAGE` / `BLAST`）：anti_core、artillery、laser 原作 1 点 = 120（战斗坦克 5 → 600），anti_player、emp（原作打玩家的
  轻型飞行器）1 点 = 30；Area 半径 × 1.5 m。射速、连发、齐射（`muzzles_per_fire 2` → 两个挂点同时，`GroundVehicle.alternate`
  可按单位覆盖：`Unit.alternate`）照原作。BeamWeaponDef（持续光束）= 每 4 帧一段的激光流。
- 主武器 = V603 的原生一对挂点；其余每个 `Weapon` 是一个 `xm_units.Arm`：`vcobjects.STORES` 里一个 independent_secondary 的挂载
  （`<单位>_<臂>`，和 BMPT 的 Ataka 同一机制：座位本来不读的第二扳机，与主武器同时开火，多个时互相轮换），从原作该武器的
  `Muzzle` 位置射出（`prep_xmorph.arm_muzzles` → model.json `muzzles['xm_<臂>']`；`make_tracked_combat` 的挂载槽从唯一的 `missiles`
  推广为任意槽：`slot_points` 把槽的炮口按模板的定位点数轮流分配，`store_weapon_sgo` 生成）。导弹挂在炮塔（`cannon_main`），
  枪炮挂在炮身（`cannon_l`：跟主炮瞄准）。
- 做不到的：原作的副炮塔各自独立瞄准（EDF 里随主炮）；主动防御激光（拦截来袭弹药，EDF 没有这个机制）；护盾；
  重坦两门炮之间的 0.25 秒间隔（这里同时）；EMP 坦克两座防空机炮塔共四个炮口，机枪模板只有一个定位点，从第一个射出（每次两发）。
- 材质名沿用 `prepare()` 的 `wt_<贴图名>` 前缀（仅 MRAB 内部名字）。

## 直升机（`prep_xmorph.py heli`，2026-10-30）

5 架：`small_helicopter_core`（轻型）、`medium_helicopter_core`、`large_helicopter_core`（重型）、`large_helicopter_transport`
（运输）、`large_gunship_core`（炮艇），呼叫行在 `tools/calls.py` 末尾（`Call.heli`），比例 1.0 照原作（轻型 5.6 m 宽、炮艇 25 m 长）。

- **飞行**：原版 `V506_HELI.SGO`（`Vehicle506_Helicopter`，请求保留 Eros 模板的 heli 参数，不写插件 mark）。玩家按原版直升机驾驶，
  NPC 由 `src/heli.cpp` 驾驶（任何 506 都按 506 行飞）。SGO（`pylib/xm_heli.py`）换模型 / CAS / 碰撞盒（模型包围盒，切在原点）/
  门与座位视角 / 第三人称相机（按机体长度 / 12 m 放大）/ 武器与挂点 / 耐久；运输直升机照 `make_jets.with_passenger_seats` 加 10 个乘客座。
- **模型**（`prep_xmorph.prepare_air` → `pylib/xm_air_model.py`）：MDB 保留 V506 的全部骨骼（mdl、globalSRT、body、rearWheelSus、
  rotor、bend_roterA–D、tailRotor：原版的音效 / 布娃娃 / 残骸效果引用它们），CAS 用 V506 的，`default` 片段经
  `cas_pose.retarget` 平移到我们的枢轴。整机往上抬到最低点 y = 0。贴图挂在 V505_TANK 的 `v505_tank` 宿主材质上；没有法线图的给平法线，
  粗糙度用常量金属值。透明的旋翼模糊材质（`unlit_transparent`）丢弃。
- **旋翼**：骨骼名含 `rotor` 的子树各成一组（共轴的合并），主旋翼 = 半径最大的一组，映射到 V506 的 `rotor`（绕本地 y 转）；尾旋翼 =
  主旋翼的左右镜像伙伴（炮艇的成对旋翼：两侧都转），没有则取没有伙伴的最大一组（重型的尾桨、运输的后旋翼），映射到 `tailRotor`
  （绕本地 x 转）。每个旋翼挂在转过轴向的安装骨骼上（`xm_rotor_mount` / `xm_tail_mount`）。**其余旋翼静止**（炮艇前一对、重型两侧）：
  V506 的 CAS 只驱动它自己的两根骨骼。
- **武器**（现有机制：V506 的机枪对 + 导弹 + 油箱四个挂点；多武器会话的新字段合入后再迁）：机枪扳机（0x2020）打挂点 0、1，导弹扳机
  （0x2021）打挂点 2。武器 SGO 是原版 506 武器改数值与名字（`xm_units.HeliArm`），数值按原作蓝图换算（同上节 `DAMAGE` / `BLAST`）：

  | 单位 | 耐久 | 机枪扳机 | 导弹扳机 |
  |---|---|---|---|
  | 轻型 | 800 | 撞击炸药（同右） | 撞击炸药：原作自爆撞核心、没有机炮（接触伤害 22 → 2640，爆炸半径 4.5 m），建模为机首各一发近程炸药 |
  | 直升机 | 1500 | 左右导弹舱 | 左右导弹舱（360 伤害、半径 6 m，每 3 秒 4 发） |
  | 重型 | 6000 | 机首炮塔（150 伤害，3+3 连发 / 120 帧） | 四个导弹舱（每 3 秒 16 发） |
  | 运输 | 3000 | 原版 506 机炮（原作无武器） | 原版 506 导弹 |
  | 炮艇 | 12000 | 左右炮塔（6 连发 / 90 帧） | 重型导弹（15000 伤害，半径 18 m） |

- **没做 / 风险**：额外旋翼静止；没有第一人称座舱（不在 `COCKPIT_GROUPS`）；不在测试场 / 生成目录；运输直升机保留原版武器；
  布娃娃代理沿用 Eros 尺寸；**未实机验证**（飞行姿态、碰撞盒、座位视角、旋翼转向都只做了离线核对：CAS 默认姿态的世界坐标 = 绑定姿态）。

## 固定翼（`prep_xmorph.py jet`，2026-10-30）

4 架：`medium_fighter_aa`（战斗机）、`medium_gunship_aa`（攻击机）、`large_gunship_aa`（重型攻击机）、`large_bomber_core`（轰炸机），
比例 1.0 照原作（战斗机 5.7 m 翼展、7.7 m 长；轰炸机 20.1 m 翼展）。

- **飞行**：本插件自己的喷气机（`src/jet.cpp`），与无畏舰战机同一条路：`pylib/vcobjects.py` `XM_JETS` 的 `Jet` 行（V506 机体、
  自己的模型），C++ `src/jet_internal.h` `kBodies` 的 `xmFighter` / `xmAttack` / `xmHeavyAttack` / `xmBomber`（mark 7051–7054，
  落在 `body506.cpp` 的 7001–7099 喷气机段），各按自己的角色飞（战斗机 fighter、两架攻击机 multirole、轰炸机 strike）；
  `src/playerjet_kinds.h` `kBoardable` 让玩家登机驾驶（任何兵种），弹射后的接机是玩家战斗机（`kXmCatch`）。
- **呼叫**：每架一行 `EDF6VC_CALL_XM_<KEY>`（`Call.jet` = 请求孪生体 `edf6tr_<key>_request_mission`，`Call.mark` = 机体 mark），
  和空袭兵的 `EDF6VC_CALL_FLY_*` 一样空投一架空机到信号弹处自己驾驶；不进 `REQUEST_KINDS`（那是 `make_jets` 的，它们的模型是模型包的），
  NPC 机体 SGO（`EDF6VC_XM_<KEY>.SGO`）与孪生体 SGO（`EDF6VC_FLY_XM_<KEY>.SGO`）由 `make_xmorph`（`pylib/xm_jet.py`）生成，
  没有模型文件夹就不部署、呼叫行是占位。NPC 机体在 `PreloadJets` 里按文件在不在预载。
- **模型**：与直升机同一个 `pylib/xm_air_model.py`（没有旋翼：V506 的旋翼骨骼空着留名），`jet_models.XM_FILES` 让
  `model_box` / `model_bounds` 量到它（门、座位视角由 `vcobjects.jet_sgo` 按模型拟合）；不做网格碰撞复合体（`aircraft_collision`
  对它返回 False：整机包围盒）。
- **武器**（现有机制：机枪对 + 挂载，`src/stores.inc` 由 `gen_stores.py` 生成；多武器会话的新字段合入后再迁）：机枪扳机打两个机枪挂点
  `xm_gun_l` / `xm_gun_r`，导弹扳机打挂载（多个时座舱轮换），挂点骨骼在原作的炮口（`xm_jet.holders`）。挂载是 `vcobjects.STORES`
  里照现有挂载做的（`Arm.store`：AIM-9X / Hydra 70 / Mk 82 的质量、阻力与外形，原作的伤害、爆炸半径与射速）：

  | 单位 | 耐久 | 机枪扳机 | 导弹扳机 |
  |---|---|---|---|
  | 战斗机 | 1200 | 喷气机机炮（原作没有机枪） | 对空导弹 8 发（240，每 3 秒 2 发） |
  | 攻击机 | 2000 | 喷气机机炮（原作没有机枪） | 火箭弹 38 发（300，每 2 秒 2 发） |
  | 重型攻击机 | 6000 | 机头两挺转管机枪（150，每 1.5 秒 10 连发） | 火箭弹 64 发（每秒 8 发）/ 对空导弹 16 发 |
  | 轰炸机 | 9000 | 两翼机炮塔（150，每 1.3 秒 3 连发） | 重磅炸弹 20 发（11400，半径 12 m，每 0.4 秒 1 发） |

- **没做 / 风险**：重型攻击机背上的炮塔（原作自己瞄准）没移植；轰炸机机炮塔、重型攻击机的转管机枪都顺机头方向射；没有第一人称座舱、
  不在测试场；模型很小（战斗机只有 EDF 战机的三分之一），NPC 飞行参数照角色行，未按尺寸调；**未实机验证**（飞行、起降、挂点位置、
  座位视角都只做了离线核对）。
