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

## 步行单位（机甲、步兵：`pylib/xm_rig.py`，2026-10-30）

```
python tools/prep_xmorph.py mech|infantry [unit ...] [--out DIR]
  → <out>/xm_<名>/  model.json（FORMAT 1，kind/family、parts 每件一骨、bones 绑定点、attachments、weapons）、model.npz、textures/、LICENSE.txt
```

- **宿主**（保留它的骨架、CAS、布娃娃、座位、SGO 类与请求，只换几何）：
  - 轻型机甲（4.5 m，双腿 + 两臂各一挺枪）→ 尼克斯 `V504_BEGARUTA_BLUE`（Vehicle504_begaruta，请求族 EWEAPON356–365 的 v504 行）。
  - 重型机甲（11.4 m，鸟腿 + 炮塔两侧炮舱 + 导弹）→ 普罗透斯 `V614_PROTEUS_MK2_CALL`（VehicleBigBegaruta，请求族 EWEAPON387/388/390/392）。
    任务原文写的是「Proteus / Balam」：巴拉姆 49 m，是重型机甲的四倍多，按比例放上去腿比例全不对，没用。
  - 步兵（3.2 m 动力装甲，T 姿）→ 游骑兵 `N601_COMMON_RANGER_AF`（AssultSoldier，`EDF6ArmySoldier.cas`），新的支援种类
    `SupportWeapon::xmInfantry`（`pylib/xm_infantry.py` 生成 `OBJECT/EDF6VC_XM_SMALL_INFANTRY[_LEADER].SGO`）。
- **重定向**（`xm_rig.RIGS`，每个 X-Morph 骨骼一个 `Seg`）：全局比例 = 宿主髋高 / X-Morph 髋高（轻型 ×2.35、重型 ×1.46、步兵 ×0.46）；
  每段把近端关节放到宿主骨骼原点，有 `to` 时沿到远端关节的方向转过去并沿该方向拉伸到宿主段长；有 `far` 时整段按到两端的距离
  线性蒙皮到近、远两根骨（膝→踝：宿主的小腿中间还有一节，刚性放置膝或踝会脱开 0.4–3.7 m）；`@ground:` 点把脚拉伸到地面。
  步兵的脚关节本身就在地面（2 cm），改为小腿伸到踝下地面、脚不拉伸（`_leg(boot=True)`）。
- **材质**：用 V603 的 `v603_body` 材质模板与顶点布局（`tracked_combat_model.BODY_MATERIAL`），贴图走 wt_tracked 的转换；
  宿主的着色器不用（尼克斯的 snd_BRDF_Common_Light 有光照遮罩风险，游骑兵的 2ColorChange 会重新染色）。双骨顶点的网格标志 [0,1,4,0]，同原版角色。
- **实测缝隙**（`xm_rig.gaps`：每个关节两侧的件在各动作每帧把关节带到的位置之差的最大值，及模型最低点高度；`tests/test_xmorph.py WalkerAssetTests` 守住）：
  - 轻型机甲：髋 0.1–0.33 m（X-Morph 髋关节与尼克斯 momo 的位置差），肩 ≈0.2 m（尼克斯 kata 的转动），其余 0；走 / 跑时脚最低 −0.35 m。
  - 重型机甲：髋 0.12–0.31 m，炮塔 0–0.07 m，其余 0；地面 ≈0。
  - 步兵：躯干 0.04–0.07 m，肩 0.03–0.08 m，其余 0；地面 −0.09..0.06 m。
  - `jump_start` 里宿主自己的根下沉 1.4–2.0 m，地面检查跳过该动作。
- **武器**（`pylib/xm_walker.py WALKERS`）：每个挂载（宿主 `vehicle_weapon_setting` 的骨骼）一个武器文件，模板是无模型（v_null）的原版载具武器
  （机枪 `V_403TANK_MACHINEGUN`、炮 `V_407BIGBEGARUTA_CANNON`、导弹 `V_407BIGBEGARUTA_MISSILE`），数值按原作蓝图换算（`xm_units` 行的注释），
  炮口移到 X-Morph 模型的炮口挂点（换到挂载骨骼坐标）。尼克斯的 8 个挂载只用双手两个，其余为空（`[0.0]`）；请求的武器列表在 setup[4]
  （`GroundVehicle.weapons_at`），`tools/call_weapons.py vehicle_sgo` 按挂载逐个写入，原版武器从预载列表去掉。
  **多武器**：用现有机制（主炮在主攻击、导弹在普罗透斯原本的导弹挂载上）；「多武器机制」会话的新字段（`Store.key/auto`、`Arm.key`）合入后再迁。
- **请求倍率**：机甲按宿主的请求族定倍率（`call_weapons.request_tier`）。轻型机甲取等级 1.6（尼克斯族 ×8.3，接近战斗坦克 1.5 级的 ×8.1），
  重型 2.2（普罗透斯族 ×8.7）：有效耐久约 5800 / 76000，对应原作 45 / 600 对战斗坦克 170（20200）。
- **步兵**：原作的枪与拳头是同一件（`Be_L_Hand` 的散块），用 `Rig.trim` 只留距手关节 0.3 m 内的块，士兵拿该种类的 EDF 武器。
  种类没有涂装与背包（模型没有可换色材质，Root.cpk 里也没有可重新染色的模板）。LOD：模型太轻，`model_lod.lods` 不生成，SGO 去掉 LOD 表。
- **安装与测试场**：机甲和步兵由 `tools/make_xmorph.py` 自己的生成器（安装器 `make_xmorph.LABELS` / `OWN`，账本主人 = 单位键）建；
  测试场的 M 地图「摆放」能放两台机甲（`testrange/gen.py WALKER_MISSION`，`CATALOG_ONLY`，宿主 SGO 经 `as_mission_sgo`；普罗透斯的
  呼叫 SGO 自带 `mission_setup`，武器两份 setup 一起换）。步兵不进测试场目录（士兵由支援栏 / 运输机小队带来）。
- **未实机**：全部（机甲的行走 / 转向 / 跳跃外观、挂载开火方向、步兵的持枪姿势与手的位置、联机）。

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
