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
- **比例 0.7**（`xm_units.SCALE`，`prep_xmorph.SCALE` 取它，写进 `model.json` 的 `xmorph.scale` / `scale`；2026-10-30 重做时改，
  原为 1.0）。X-Morph 是俯视角游戏，民用车是真实尺寸（4.4 × 1.8 × 1.45 m），军队却画得大：战斗坦克宽 5.5 m、含炮管长 12.7 m、
  炮塔顶 3.4 m，是 EDF 的 Blacker（`V505_TANK` 3.2 × 2.6 × 9.2 m，实测网格包围盒）的 1.7 / 1.3 / 1.4 倍，比真实主战坦克
  （M1 约 3.7 × 2.4 × 9.8 m）也大一半；站一个 1.8 m 的人在旁边一眼就看出来（`tools/xmorph_view.py --ranger`）。统一乘 0.7 后
  战斗坦克 3.8 × 2.4 × 8.9 m，重坦 12.5 m 长（天启量级），单位之间的相对大小保持原作。行里的 `eye` / `camera` 仍按原作尺寸量，
  经 `xm_units.eye()` / `camera()` 乘比例后给读者（炮台的视点按底座顶 1.3 m 以上的部分缩放）。
- 法线：用网格**自带**的顶点法线（`UnitModel.normals`，经 `prep_wt_tracked.Source.normals` 交给 `prepare()`），不再按 50° 自动平滑重算：
  X-Morph 的法线图是在它自己的法线上烘焙的，换一套法线，法线图的凹凸就错位（EMP 坦克 17k 三角形全部拆顶点，是有意的硬边）。
- 贴图：diffuse（DXT1）原样转 PNG；normal 是 DXT5 的「X 在 alpha、Y 在 RGB（三通道相等）」布局，重建为 RGB 切线空间（Z 由单位长度求），
  只在材质脚本声明了 normal 时使用（large_tank、artillery、shield_generator、large_tank_emp 的材质没声明，即使包里有文件也不用，与游戏一致）。
- specular / emissive（灰度图，R = G = B）**现在都用上**（以前丢弃，车身粗糙度 / 金属度是全车常数 140 / 90，偏暗偏金属、灯带不亮）：
  - specular → 参数图的 R 粗糙度、G 金属度逐像素（`wt_tracked.SPECULAR_RM`：0 → 215 / 20 的哑光漆，255 → 95 / 170 的亮金属）；
  - emissive 有亮像素（> 16）时，车身材质换成复古巴拉姆的 `light` 材质（`snd_BRDF_Common_Light_NoOcc`，无畏舰已在用的同一个，
    `dreadnought_model.LIGHT_HOST`），emissive 进参数图 B 通道当发光遮罩，按 albedo 颜色自发光（`light_color` 2.0）；
    `diffuse` / `specular_color` 参数沿用 `v603_body` 的，不用巴拉姆那套更亮更光的。9 个单位的材质全部都有灯带（红色条纹、青色灯）。
  - 原作贴图只有 256–512（俯视角够用），`min_side 1024`：编码前按整数倍 Lanczos 放大（法线按向量放大后归一化），DXT1 / BC5 的 4×4 块
    不再压在原图细节上。只放大编码输入，模型包里仍是原尺寸 PNG。
  - 同一张图集被车身和两条履带三个材质共用：`wt_tracked` 按内容去重，只打一份（战斗坦克 MRAB 6.8 → 2.8 MB）。

## 骨骼映射（自动，读 `.ent` + 骨架）

`tools/prep_xmorph.py rig()`，新单位不需要手写配置：

| V603 骨骼 | 取自 |
|---|---|
| `cannon_main` | 第一个由 TurretComp 炮塔瞄准、且 yaw / pitch 都是骨骼的武器（主武器）的 yaw 骨骼及其子树（炮除外）；外加挂在车体上、但立在炮塔座圈内（离转轴水平距离小于炮塔网格半径、且高于转轴）的骨骼（如防空车炮塔顶的 `be_radar`） |
| `cannon_l` | 主武器 pitch 骨骼及其子树中除驻退骨骼外的部分（一门炮一个摇架，`cannon_r` 无网格，同 PzH 2000） |
| `cannon_slide_l / _r` | 主武器各炮口的 `recoil_bone`（没写就取炮子树里 `recoil*` 骨骼）：两根按左右分；一根放 `_l` |
| 炮口 | 主武器 Muzzle 的 attachment（`.att` 挂点或同名骨骼）：两个按左右分；一个则左右同一点（`prep_wt_tracked.Vehicle.mouths`）；再沿炮膛推到炮管网格的最前端（炮膛线 0.6 m 内的炮网格的最大 z：原作挂点是画炮口焰的位置，战斗坦克在管内 0.6 m、自行火炮 1.1 m、先进坦克 0.2 m，炮弹从管里出来） |
| `doppler_radar` / `tracking_radar` | 不放网格；物理代理拟合到炮塔顶部 30% 的切片，左右各一半（与 `cannon_main` 的碰撞上下、两者之间左右各留 0.2 m） |
| `tire_moveA..H_l/_r` | 车体网格里原作自己的负重轮 / 诱导轮 / 主动轮（见下「行走机构」） |
| `catapi_body` | 履带带体的行走面（材质名 `v603_kyata_L / _R`） |
| `body` | 其余：车体，以及车体自己的副炮塔、发射架（重坦的 `turret_3`、EMP 坦克的两座防空炮和导弹塔、护盾车的护盾发生器——它们不随主炮塔转） |

- 唯一的手写例外 `MOUTHS`：防空坦克主武器的 `att_muzzle_3` 在两个炮舱中间（炮塔中线），实际炮口是 `att_muzzle_1 / _2`（`barrel_1` 上 y ±1.52）。
- 炮的碰撞分成兄弟体 `cannon_l / cannon_r`（D93250 兄弟互碰）：两个炮口横向相距 > 0.5 m 时左右切（防空坦克、重坦、EMP 坦克），
  否则沿炮膛切（炮管前段归 `cannon_l`，摇架 / 后段归 `cannon_r`），中间留 0.2 m。
- 主武器之外的武器（重坦的机枪塔与导弹、EMP 的防空炮 / 导弹、主动防御激光）不映射：`model.json` 的 `missiles` 为空，
  `make_xmorph` 侧的槽位只用左右两门。

## 行走机构（`running_gear()`）

X-Morph 的负重轮、诱导轮、主动轮和履带都烘焙在 `body` 网格里，没有轮骨骼；V603 需要每侧 8 个轮位（`tracked_collision._wheel_overrides`
读 `wheel_{A..H}_{l,r}` 部件盒子，B..G 的半径写进 `car_base_wheel` 决定接地点，A / H 只按滚速画）。

**2026-10-30 之前**是在带体里按段长均匀放 8 个合成圆柱：轮径、轮位与原作的轮子对不上，战斗坦克、重坦、护盾车的侧视图里合成轮从
原作负重轮之间露出来（一排「多出来的轮子」），原作的轮子又不转。**现在**用原作自己的轮子：

1. 每侧：车体上整片低于 0.15 m 的三角形是履带接地段，它们的横向范围是履带带（band），沿 z 是接地段（相邻段带内一直有低于 0.35 m
   的几何就合并，否则是分开的履带舱）；带顶 = 段上方最低朝上面的中位高度。
2. 轮子（`axles()`）：车体这一侧按位置焊接后的连通块里，高与长相差不到 25%（圆）、半径 ≥ 0.12 m、轴心在带的横向范围 ±0.3 m 内的块；
   同一轴（z、y 都差 < 0.1 m：轮与轮毂、双排轮的内外两排）合为一根轴。轮底离地 < 0.2 m 的是负重轮。
3. 上站（`stations()`）：B..G 取负重轮——正好 6 个照用；多于 6 个时，前后没有诱导 / 主动轮就由最前 / 最后一个负重轮当 A / H，其余均匀挑 6 个，
   没挑上的留在车体上不转（EMP 坦克每侧 8 个负重轮 + 4 个诱导轮，挑 6 + 2）；少于 6 个时，最长间隔前端的轮子与一个**填充轮**同轴。
   A / H 取负重轮前后、轮底低于 1.2 m（`IDLER_LIFT`，更高的圆件是车体上的鼓、灯）、半径不小于负重轮一半的轮；没有就是填充轮。
4. 原作轮的三角形离开车体，挂到 `tire_move<站>_<侧>`（随车速转）；填充轮是藏在同轴原作轮里的圆柱（半径、宽度都是它的 0.8 倍，
   12 边形的平面在 0.966 处，看不见），只为 V603 的轮位与物理接地点存在。
5. 带内不朝侧面（|n_x| < 0.5）、不高于带顶、在接地段前后坡范围内、**不是轮子**的车体三角形归 `track_l / _r`（`catapi_body`、履带材质名）。
6. 车体上一个轮子都没有的单位仍走旧的合成轮（现有 7 台都不需要：先进坦克的履带舱里也找到了 12 根轴）。

接地点是原作轮的半径：负重轮底比地面高出履带厚度（战斗坦克 0.03 m、防空车 0.06 m，乘比例后），车会下沉这么多，与战雷履带车相同。

| 单位 | 三角形 | 尺寸 宽 × 高 × 长 (m) | 轴 / 侧 | 轮位（小写 = 填充轮） | 负重轮半径 | 炮塔原点 (x,y,z) | 炮口 L / R | 炮碰撞切法 |
|---|---|---|---|---|---|---|---|---|
| xm_medium_tank_regular | 4,479 | 3.8 × 3.6 × 9.3 | 9 | `ABCDEFGH` | 0.33 | (0.00, 1.29, −0.08) | (0.00, 1.57, 5.72) 同点 | 沿炮膛 |
| xm_medium_tank_advanced | 7,459 | 4.8 × 3.9 × 9.0 | 12 | `ABCDEFGH` | 0.32 | (0.00, 2.29, −0.65) | (0.00, 3.02, 5.06) 同点 | 沿炮膛 |
| xm_medium_tank_aa | 3,258 | 3.5 × 4.4 × 6.2 | 12 | `ABCDEFGh` | 0.25 | (0.00, 2.22, −1.70) | (±1.06, 2.80, −0.73) | 横向 |
| xm_large_tank | 8,711 | 5.8 × 5.3 × 12.4 | 8 | `ABCDEFGH` | 0.49 | (0.00, 2.44, 0.44) | (±0.75, 3.18, 7.59) | 横向 |
| xm_large_tank_emp | 17,057 | 6.2 × 6.2 × 12.7 | 23 | `ABCDEFGH` | 0.32 | (−0.02, 3.73, 1.33) | (±0.34, 4.58, 6.48) | 横向 |
| xm_medium_artillery_regular | 4,011 | 2.8 × 3.3 × 8.1 | 13 | `ABCDEFGH` | 0.25 | (0.00, 1.43, −2.02) | (0.00, 1.71, 4.61) 同点 | 沿炮膛 |
| xm_medium_shield_generator | 4,497 | 4.5 × 3.8 × 8.2 | 9 | `aBcdEFGh` | 0.53 | (0.00, 2.37, 1.08) | (0.00, 2.94, 2.23) 同点 | 沿炮膛 |

（比例 0.7 之后的模型坐标；高度含天线。「轴 / 侧」是识别到的全部圆件轴数，含不上站的托带轮、车体鼓。每个文件夹 0.3–0.9 MB，
炮台 1.3 MB。`model.json` 的 `xmorph` 段记了骨骼归属、接地段、轮位等转换记录，wt_tracked 不读它。）

## 重做（2026-10-30）：问题清单与修复

用户：「做出来的成品太烂了」，点名外观 / 贴图、武器 / 攻击、比例 / 碰撞。逐台离线渲染对照原作（`tools/xmorph_view.py`：左原作网格、
右模型包文件夹，亮面 / 纯贴图 / 法线 / 骨骼着色四种模式，`--ranger` 放一个 1.8 m 的人）后的问题与处理：

| 问题 | 涉及单位 | 处理 |
|---|---|---|
| 合成负重轮从原作轮子之间露出，原作轮子不转 | 全部履带车（战斗坦克、重坦、护盾车最明显） | 原作轮子上轮位（「行走机构」） |
| 尺寸是俯视角游戏的放大尺寸，比 Blacker 宽 1.7 倍 | 全部 9 个 | 统一 ×0.7（「坐标系与比例」） |
| 高光图丢弃，车身粗糙度 / 金属度全车常数（偏暗、偏金属） | 全部 9 个 | 高光图 → 逐像素粗糙度 / 金属度 |
| 自发光图丢弃，红 / 青灯带不亮 | 全部 9 个 | 车身换 `light` 材质，自发光图当发光遮罩 |
| 256 / 512 的小图直接 DXT1 / BC5，4×4 块压在细节上 | 全部 9 个 | 编码前放大到 1024 |
| 法线按 50° 重算，与原作烘焙法线图的法线不一致 | 全部 9 个 | 用网格自带法线 |
| 炮口挂点在炮管里（炮弹从管内出） | 战斗坦克 0.6 m、自行火炮 1.1 m、先进坦克 0.2 m、防空车 0.2 m | 炮口推到炮管末端 |
| 一张图集打包三份 | 全部履带车 | 按内容去重 |

逐台补充（离线渲染看到的）：

- **战斗坦克**：轮系 9 根轴（6 负重轮 + 前诱导轮 + 后主动轮 + 车尾的鼓，鼓在 1.2 m 以上不上站），8 个站全是原作轮。
- **先进坦克**：四个独立履带舱，每侧 6 负重轮 + 4 诱导轮 + 2 个车体圆件；A / H 取最前 / 最后的诱导轮，B..G 前后舱各 3 个。
- **防空坦克**：6 负重轮 + 前诱导轮 + 4 个托带轮（太小不上站）；后面没有主动轮，H 是藏在最后一个负重轮里的填充轮。
- **重型坦克**：6 负重轮，前后两个抬高的诱导 / 主动轮（底高 0.85–1.0 m）当 A / H。右侧识别到 19 个圆件（车体侧面的圆形装甲件），
  都不在负重轮行，不影响选站。
- **EMP 坦克**：前后两个履带舱，每舱内外两排轮（同轴合并）；8 负重轮挑 6，4 个诱导轮取最前最后两个，其余 2 + 2 不转。
  车体后部有半径 1.0 m 的圆件（导弹塔鼓），旧规则会把它当主动轮，`IDLER_LIFT` 排除。
- **自行火炮**：7 负重轮里 6 个 + 前后诱导轮，3 个托带轮留在车体。
- **护盾车**：半履带——前面一对大车轮 + 后面每侧 3 个负重轮，只有 4 根接地轴：A、C、D、H 是填充轮（藏在前轮和后部第一个负重轮里）。瞄准镜眼点原来压在炮两侧的滑轨护板里（缩小后离几何 3.5 cm），抬到护板上方（表里 4.1 → 4.6）。
- **两座防空炮台**：网格、碰撞（按部件盒子）未变，只有比例、材质（灯带、粗糙度）、炮口（本来就在管口）随上面改。

武器（副炮塔独立瞄准 → 独立开火键 / 可开关自动炮塔 / 独立座位）**等多武器机制的会话合入 main 后再接**，这次没有动武器数值与表现。

## 产物与复用
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

## 轮式（`prep_xmorph.py wheeled`，2026-10-30）

- **底盘选 V603**（`Vehicle603_Flak`），和履带车同一条生成链（`wt_tracked` / `tracked_collision` / `make_tracked_combat` / `make_xmorph`，
  所有登记表经 `xm_units.v603()` / `hulls()`）。看过的现有轮式车：EDF 的轮式炮车类是 Grape 的 `Vehicle401_Striker`（4 轴 8 轮、前两轴转向、
  只有一个武器挂点 `Barrel`、骨架自成一套），仓库里没有在它上面建模型的生成器；喀秋莎（`make_katyusha`）是把卡车放在履带类 Naegling
  （`Vehicle402_Rocket`，轮行 `[11] = -1` 不转向）的轮骨上，即「轮式外观 + 履带类」的先例；轿车（V512）没有武器。V603 有两个原生挂点、
  `Arm` 的独立副武器、座舱 / 测试场 / 请求整套现成，所以轮式单位也放 V603。**代价：转向是 V603 的原地差速转向，不是前轮转向**
  （见「未做 / 风险」）。
- 车轮：X-Morph 的轮子烘焙在车体里（装甲车的 `wheel_1..4` 是横跨两侧的轴骨），每个轮是一块独立的连通网格。`wheels()` 取车体上
  触地（最低点 < 0.2 m）、高≈长（差 ≤ 15%）、高 ≥ 0.5 m、宽 < 1 m、偏离中线的连通块为轮，按侧从前到后排。
- 轮位：V603 每侧 8 个（A..H，B..G 是有弹簧的物理轮，A / H 只画）。B..G 按 `round(k·(n−1)/5)` 分到 n 个轮上（首个在前轮、末个在后轮），
  A = 前轮、H = 后轮。一个轮分到几个轮位时，轮子自己的网格挂在它的第一个轮位上转动，其余轮位各一个藏在轮胎里的圆柱（半径 0.98×，
  宽 60%），给物理接地半径与轮心——同一轴上多个弹簧轮，与喀秋莎在 Naegling 上的做法相同。
- 履带：V603 类按名字绑定 `v603_kyata_L / _R` 两个材质，轮式单位每侧放一个藏在前轮轮毂里的小圆柱（半径 3 cm）占这两个名字，
  `make_tracked_combat.STATIC_TRACKS` 对所有 `hulls()` 把滚动速率置 0。
- 导弹车的发射架是一根骨骼同时做 yaw 和 pitch（`barrel_1`，枢轴在架尾 z −5.19）：`rig()` 把整个发射架当炮（`cannon_l`，绕枢轴俯仰），
  在枢轴下合成一个藏在架子里的转台（`TURNTABLE`）当 `cannon_main`；四个发射管取上面一对为原生左右挂点（`MOUTHS`）。架子绕尾部转，
  转太多会扫到驾驶室，`Unit.traverse = 30`（`TURRET_LIMITS` 左右各 30°）。
- 装甲车的骨架以车门为根（`be_door_*`）：车体骨骼取 `body`（`hull_bone()`）。

| 单位 | 三角形 | 轮（每侧） | 轮半径 | 主武器（原生一对） | 副武器（`Arm`） | 耐久 |
|---|---|---|---|---|---|---|
| xm_medium_missile_launcher | 8,684 | 5 | 0.60 | 重型导弹：每 5 s 一枚、曲射（TOS-1 的落点标记），15000 / 爆炸 18 m，两管轮流 | — | 800 |
| xm_medium_apc | 4,753 | 4 | 0.84 | 双联重机枪对空弹：每 1.3 s 三发、两管齐射，300 | 对地弹（同一对枪管轮流，600） | 3700 |
| xm_small_humvee | 1,857 | 2 | 0.52 | 机枪对空弹：每 1.5 s 三发，150 | 对地弹（240，每 2 s 三发） | 800 |
| xm_small_buggy | 3,048 | 2 | 0.49 / 0.77 | 防空导弹：每 3 s 五发、追踪（泰坦侧导弹的外观与锁定，`Unit.homing`），240 | 车头机枪（每 1.5 s 六发，240） | 800 |

- 换算照「武器」节；`like` 都是 V603 上有模型文件夹的车（`WT_ONLY`）：导弹车 `tos1`，其余 `tunguska`（30 mm 炮弹外观）。
  耐久 = 原作 max_health × 2500/170，至少 `xm_units.WHEELED_FLOOR`（800；悍马 / 越野车原作 15、导弹车 40）。
- 原作同一挺枪的两种 `Weapon`（`main_weapon` 打空中、`core_weapon` 打地面）分成主武器与 `Arm`，两者同时开火（独立副武器）。
  多武器机制会话的新字段合入后改用它表达；目前仍是现有 `Arm`。
- 货车不在这一族：原作蓝图没有 `WeaponComp` / `TurretComp`，V603 没有无武器的路径，见下节「车（`car`）」。

## 车（`prep_xmorph.py car`，V512，2026-10-30）

无武器的车（货车，以后的 civilian 民用车）走轿车（`tools/make_sedan.py`）已验证的 V512 底盘（`Vehicle_Car`：轻卡的类、操控、
物理拓扑）。`xm_units` 的 `car` 族：`like = 'sedan'`（无武器的请求模板），`cab` 是驾驶座的座舱套件，`eye` 是驾驶员眼点，不写 `gun`。
每个登记表按族走同一个循环（`xm_units.v512()`）：`vcobjects.GROUND_VEHICLES`、`cockpit_fits_plugin.FITS`（`Fit(0, 'body', eye, cab)`）、
`testrange/catalog.py INSTALLER_VEHICLES`（同轿车：SGO 保留 V512 的 `mission_setup`，直接可摆放）、`make_xmorph.CARS`
（安装器的生成 / 写入 / 卸载循环）；`calls` / `ledger` / `buildcache` / `RELEASE_MODELS` 本来就按 `ported()`。

- 准备（`prepare_car`）：车轮 = `wheels()` 找到的地面圆块，每侧前到后按轴命名（`axle_bones`）：前轴 `tireF`（转向）、后轴 `tireB`（驱动）、
  中间的轴 `tireM<k>`；其余三角形都在 `body`。文件夹格式 `CAR_FORMAT = 1`（`model.json` 的 `wheels` / `bounds`，`model.npz` 同履带文件夹的部件数组）。
- 生成（`pylib/car_model.py`）：
  - 4 根轮骨移到前、后轴的轮心（只改原点），`car_base_wheel[8]` 半径改成轮胎的；
  - 中间轴是挂在 `body` 下的可视骨（`add_visual_bones`），只画不算物理：`car_base_draw_wheel_info [[-1, 骨], [半径]]`，与 V603 的诱导轮同一种行；
  - 材质用 V512 车身材质 `KT01L` 的布局配文件夹贴图；
  - 碰撞：先 `ragdoll_fit.fit` 把布娃娃挪到新骨位，车体按网格拟合（`tracked_collision._fitted_shape`），轮胎把 V512 轮胎凸包映射到轮盒；
    车门（模型没有）缩成 1 mm 放进车体中心（同轿车）；
  - 驾驶座、下车点、追尾相机、车灯的 MAB 点按模型包围盒重放；只留驾驶座，`vehicle_weapon_setting` 为空。
- `car_model.check`：SHKT `shkt_check`、布娃娃 `ragdoll_fit.problems`、`vehicle_check.vehicle_problems`（静止压缩、质量）、轮骨在轮心。

| 单位 | 三角形 | 每侧轮（物理 / 只画） | 轮径 | 耐久 |
|---|---|---|---|---|
| xm_medium_truck_cargo | 5,675 | 3（前 + 后 / 中轴） | 0.56 | 1500 |

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
2. `python tools/prep_xmorph.py tracked|wheeled|car xm_<名>`（写进模型包）；看生成日志里的骨骼归属、接地段 / 车轮，必要时离线渲染核对。
3. 只有蓝图误导时（主武器炮口不在炮上之类）才在 `MOUTHS` 加例外，并写明证据。

## 验证（本机，离线）

- `python tests/test_xmorph.py`：合成 Ogre 网格 / 骨架（含不可信的容器长度、带缩放的骨骼、动画块）、文本格式、坐标换算、法线图重建、
  合成单位的骨骼规则与轮位（无轮车体的合成轮、带轮车体的原作轮上站与填充轮、`stations()` 的多 / 少 / 高位圆件三种情形、炮口推到管口）
  都在 CI 上跑；装有 X-Morph 时再从包里读 medium_tank_regular、准备到临时目录，有 EDF6 Root.cpk 时再走
  `wt_tracked.build_with_info`（含 `check`）与 `tracked_collision.build`（含 SHKT `shkt_check`、布娃娃 `ragdoll_fit.problems`）。
- 2026-10-30：9 个文件夹（7 履带 + 2 炮台）重新生成进模型包，逐个 `EDF6VC_MODELS=<文件夹> python tools/make_xmorph.py --kind <单位> --check`
  （完整 `make_tracked_combat.build` + `check`：模型、碰撞 SHKT、布娃娃、CAS、SGO）全过；战斗坦克的 MRAB 里车身材质是
  `snd_BRDF_Common_Light_NoOcc`、`light_color` 2.0、`diffuse` / `specular_color` 为 v603_body 的值（读回核对）。
- 离线渲染（`tools/xmorph_view.py`）对照了 7 台履带车的侧 / 俯 / 前 / 斜视图，轮子不再外露，骨骼着色下原作轮在轮位骨骼上。

## 未做 / 风险

- **全部未进游戏看**：灯带的亮度（`light_color` 2.0，按无畏舰的 4 减半估的）、高光图到粗糙度 / 金属度的映射（`SPECULAR_RM`，按观感估的）
  都可能要按实机调；轮子随车速转的观感、填充轮是否真的看不见、负重轮比地面高出的履带厚度（车会下沉 2–6 cm）都要实机看。
- 履带贴图滚动：已由 `make_tracked_combat.STATIC_TRACKS` 把 `tank_caterpillar_animation_info[0]` 置 0（`docs/model-lod.md` §3），贴图不动；
  履带带体现在只剩真正的履带面（轮子不再算进去），面数比以前少。
- 法线图 Y（绿）通道：X-Morph 是 OpenGL 约定，`xm_model.normal_rgb` 取反 Y（证据见 `docs/texture-quality.md`）；这次换成网格自带法线后
  切线框架仍是 dP/du、dP/dv，约定不变。
- 比例 0.7 是按「战斗坦克对齐 Blacker / 真实主战坦克」定的统一系数；用户若觉得某一台太小（防空车 6.2 m 长、自行火炮 8.1 m）只改
  `xm_units.SCALE` 会一起变，不建议逐台不同比例（原作的相对大小会乱）。
- **轮式车不用前轮转向**：V603 的轮行 `car_base_wheel [11]` 全是 −1（不转向），轮式单位照此原地差速转向。EDF6 Car 在 `0x679CFC..0x67A004`
  读 `[10..12]`（`docs/edf41-bike-re.md`），Striker 的前两轴是 `[11] = 1`；但 `Vehicle603_Flak` 的输入是否会给转向轮角度没有逆向过，
  改了可能开不动，需实机验证后再做（`tracked_collision._wheel_overrides` 按轮位改 `[11] / [12]` 即可）。
- 货车（`car`）未实机测试：V512 轻卡的质量、重心与悬挂行程原样保留，7 m 车身上的手感、中轴只画不着地、`truck_cab` 座舱是否与驾驶室对齐都只做了离线检查。
  只留驾驶座：轻卡车斗的 4 个乘员座没有搬到货厢（货厢有篷布，要做运兵需另定座位）。
- 轮式单位未实机测试：同轴叠放的多个弹簧轮（导弹车 5 轴、悍马 2 轴各分到 3 个物理轮）的悬挂手感、发射架 ±30° 是否擦到驾驶室、
  越野车车头机枪挂在炮身上（随炮塔瞄准，原作是车体上的固定挂点）都只做了离线检查。
- **履带贴图滚动**：X-Morph 的履带是车体图集上的静态贴图，`v603_kyata_L / _R` 只是给 SGO 的 `tank_caterpillar_animation` 留名字；
  若 SGO 照 V603 的速率滚动，会把图集相邻区域滚进履带面。需要 SGO 侧对 xm 单位关闭或置零滚动（`make_xmorph` 的事），未实机核对。
- 法线图 Y（绿）通道：X-Morph 是 OpenGL 约定（+G = 贴图上方），原版载具与我们的副法线（+v，贴图下方）是 DirectX 约定，`xm_model.normal_rgb` 取反 Y。证据（旋度符号与高度 / 颜色相关）见 `docs/texture-quality.md`。
- 合成轮在履带带体内，带体若一侧不封闭（个别单位的带侧敞开）可能露出；EMP 坦克有一个轮位落在梯子附近。离线渲染看不出明显外露，未实机看。
- 炮口用 X-Morph 的挂点：常规坦克炮口在炮管末端后约 0.6 m（炮口制退器起点），炮弹从管内出；未实机核对是否打到自身。
- `artillery` 的 `doppler_radar` 切片在左半边没有可拟合的块，退回把供体凸包拉伸到目标盒（`tracked_collision._fit_shape`，`fitted_pieces 0`）。
- 主动防御、护盾 / 力场效果、摧毁阶段网格（`_dest_*`）没移植；副炮塔独立瞄准等多武器机制合入后接。
- 7 辆履带车全部开放呼叫（2026-10-27，武器表第 85–91 行）。耐久 ≈ 原作 max_health 的比例（战斗坦克 170 → 2500 为基准，
  装甲很薄的防空车 / 自行火炮给到 1500 保底）。视角与瞄准镜位置按各模型（原作尺寸）实测的炮塔顶与炮口，`xm_units.eye()` / `camera()` 乘比例。

## 武器（攻击方式按原作，2026-10-27 用户：「攻击方式按照游戏里面那样搞」）

- 数据来源：单位蓝图 `WeaponComp` 的每个 `Weapon`（`weapon_definition`：`fire_rate` = 一轮间隔秒数，`FirePattern.burst_rows` 发、
  间隔 `burst_rate` 秒，`muzzles_per_fire`），弹头是 `ammo_blueprint` 指向的**具名蓝图**（定义在 `entities/projectiles/*.ent` 这类
  一文件多蓝图的组文件里：`xm_model.named()` 按名字索引全部 EntityBlueprint）：`MovementComp.speed`、`DamagePattern.Damage.value/type`、
  `Area.radius`、`AimPattern`（导弹追踪什么）。
- 换算（`xm_units.DAMAGE` / `BLAST`）：anti_core、artillery、laser 原作 1 点 = 120（战斗坦克 5 → 600），anti_player、emp（原作打玩家的
  轻型飞行器）1 点 = 30；Area 半径 × 1.5 m。射速、连发、齐射（`muzzles_per_fire 2` → 两个挂点同时，`GroundVehicle.alternate`
  可按单位覆盖：`Unit.alternate`）照原作。BeamWeaponDef（持续光束）= 每 4 帧一段的激光流。
- 主武器 = V603 的原生一对挂点；其余每个 `Weapon` 是一个 `xm_units.Arm`：`vcobjects.STORES` 里一个有自己开火键的挂载
  （`<单位>_<臂>`，`Arm.fire` → `Store.key`：第一个在座位本来不读的第二扳机（和 BMPT 的 Ataka 同一机制），其余在开火键 3、4…；
  与主武器、彼此同时开火，不轮换；`Arm.auto` 标自动炮塔。2026-10-30 起，见 `docs/multi-weapon-keys.md`），从原作该武器的
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
