# X-Morph: Defense 地图移植（2026-10-30）

把 X-Morph: Defense（EXOR Studios）的 5 张地图的**地图本体**（地形、建筑、道具、碰撞）做成 EDF6 能加载的地图。
范围按用户 2026-10-27 的决定：只做地图本体，不做任务；地图上人类一方的单位（兵营、民用车）作为静态模型放在原处，并按
`pylib/xm_units.py` 的行登记（`xm_maps.json` 的 `units`），供以后的任务脚本按行生成真正的单位。

代码：`pylib/xm_map.py`（读 X-Morph 地图 → 场景）、`pylib/map_build.py`（场景 → EDF6 的 MAC / MAD / RAB）、
`tools/make_xm_maps.py`（开发机生成到模型包；安装器从模型包装进 Mods）。

## 来源与授权立场

- 素材取自用户本机正版 `D:\steam\steamapps\common\X-Morph Defense` 的 `packs/`，**只读**。
- 生成的地图文件是 EXOR 的游戏资产，**不进公开仓库**：和 X-Morph 单位一样放模型包（开发机 `D:\APP\edf6-models\xm_maps`，
  `obj_model.model_dir`），`LICENSE.txt` 写明出处与此立场。仓库里只有转换代码与测试；CI 没有 X-Morph，相关测试自动跳过。
- 借用的天气（`<MAP>_<天气>.MAE/.RAB`）与寻路网格（`.NAVMESH*`）是 EDF6 自己的文件，安装时从玩家本机的 EDF6 读，不进模型包。

## 五张图

| EDF6 名 | X-Morph | 借用天气 / 寻路的 EDF6 地图 | 天气 |
|---|---|---|---|
| `XM_STORY_JAPAN` | `maps_7/story_japan` | `NW_RINKAI01`（临海城区） | FINE, CLOUDY, EVENING |
| `XM_STORY_HAWAII` | `maps_7/story_hawaii` | `IG_KAIGAN502`（海岸） | FINE, CLOUDY, SUNSET |
| `XM_DLC_ICELAND` | `maps_12/dlc_3_iceland` | `NW_HENDEN_WINTER`（雪） | FINE, CLOUDY |
| `XM_DLC_FRANCE` | `maps_8/dlc_1_france` | `NW_EUROPE01`（欧洲城区） | FINE, CLOUDY, EVENING |
| `XM_DLC_FINLAND` | `maps_8/dlc_1_finland` | `NW_HENDEN_WINTER` | FINE, CLOUDY |

X-Morph 文件夹取 `xm_pack.Packs.map_dir()` 的最高修订版。天气名都已核对借用图确有这些 `MAE` / `RAB`。

## X-Morph 地图里有什么（`maps_N/<图>/game/`）

- `<图>_chunk_<k>a.mesh`：一块地面。`terrain/...` 子网格是最多 3 层平铺贴图按顶点混合（`BLEND_WEIGHTS`；层 = 地图
  `.material` 里的 texture_unit，每 3 个一组，第一个是 diffuse，uv0 是世界坐标平铺）；`cliffs/...` 子网格是悬崖图集
  （uv0：3 种变体左右并排，1536×512；uv1：地形层坐标；uv2：变体权重；`cliff_blend` 遮罩黑处露出地形层）。
- `<图>_chunk_<k>b.mesh`：地面上的东西：道路、静态贴花、水面（`base/water`，每块约 10 万三角形，给波浪着色器用）和泡沫。
- `<图>.entities`：所有放置的实体（蓝图 `props/…`、`units/…`、`logic/…`、`effects/…`，带 Position / Orient（四元数）/ Scale）。
- `<图>.game`：SceneParams（`terrain_limits`、水色、雾、天空）；`.nxs` 是 PhysX 碰撞（不用，碰撞取网格）。
- 道具：`RenderableDescComp.mesh`（可带 `SubMeshes` 换材质），或 `BehaviourComp.behaviour_defs.BuildingDef`：
  `mesh_prefix.mesh` + `.building` 拓扑里每个 `ChunkDesc.graphics_mesh`（建筑完好时就是这些碎块拼在一起，已在建筑本地坐标），
  `chunk_material` 是皮肤。碰撞网格是 `<mesh>_c<n>.mesh`（植被和小杂物没有）。
- 材质：全局索引 `materials/scripts/**/*.material` + 地图自己的 `.material`，`material a : b` 继承；贴图 DDS 或 PNG。
  游戏自带的 `materials/scripts/manmade.material` 第 ~509 行有一个多余的 `}`，`xm_pack.parse(stray_close=True)` 跳过它。

## 转换（`pylib/xm_map.py`）

- 坐标：X-Morph +X 前、+Y 左、+Z 上；EDF (x, y, z) = X-Morph (y, z, x)（`xm_model.to_edf`，真旋转）。整图平移到
  `terrain_limits` 中心为 EDF 原点（高度不动）。
- **地面**：每块 `a` 一个模型。EDF 的地图着色器没有逐顶点层混合，所以把地形烘焙成一张俯视颜色图（0.125 m/texel，边长
  2 的幂、64–2048）；悬崖按（变体，地形层）分组，各自把变体合成到地形层上。碰撞 = 这块的全部三角形，地形材质调色板。
- **覆盖层**：每块 `b` 一个模型，无碰撞：道路 / 贴花 alpha 测试，水面重画成 2 m 格子按行合并的几个四边形（`water_grid`），
  水色半透明（`WATER_ALPHA` 0.55）。泡沫跳过。
- **道具**：每个（蓝图，残余变换）一个模型。放置的偏航作为 mapb 记录的 rot y；倾斜与缩放烘进顶点。X-Morph 几乎每个道具
  都略微不水平、缩放略偏 1，所以倾斜 < 2.5° 的抹平、缩放按 0.03 取整（1 附近归 1）后再去重（日本图 551 → 526 个模型）。
- **单位**：`units/...` 实体（夏威夷的兵营、冰岛的旧巴士）同道具一样做静态模型，另按 `xm_units` 行登记到 `Scene.units`。
- **不转**：`logic` 标记、体积、特效（火、蒸汽：EDF 没有对应物）、`decal_cube` 投影贴花（路面标线：EDF 地图着色器不能投影），
  计数在 `Scene.skipped`，构建日志打印。

## EDF6 文件（`pylib/map_build.py`）

每个模型是一个地图物件（`.mdx` def）：网格 `<m>.mdb` 在 RAB，碰撞体 `<m>.<m>` 在 MAD，由 mapb 的记录放置。

### mapb

按原版无电线、无路点的最小布局（`IG_CAVE605`、`NW_RINKAI01` 实测）从头写：

| 位置 | 内容 |
|---|---|
| 0x00 | 头 0x90：`MAPB`、版本 2；0x08/0x0C 名表偏移 / UTF-16 长度；0x18/0x1C def 表；0x20/0x24 记录；0x70/0x74 = (两个空 SGO 的偏移, 0)；0x78 / 0x7C 两个点块偏移 |
| 0x90 | def 表（0x3C 一个） |
| | 记录（0x48 一个） |
| 0x78 块 | `{0, 0, 0, 0, 相对偏移 → "elec_wire_default.elec"}` |
| 0x7C 块 | `{0, 0, 0, 0, -4, 1}`（原版空块原样） |
| | 各 def 的 SGO（4 字节对齐），两个空 SGO（`SGO\0`、0x102、全空） |
| | 名表：各 def 名（`<m>.mdx`），再 `elec_wire_default.elec` |

- def（0x3C）：+00 类型（地面 3，物件 1），+04 → 自己的 SGO（相对 +04），+08 0（原版也是 0），+0C → 名字（相对 +0C），
  +10 -1，+18 100.0，+1C 远裁剪距离（1000），+28 -1.0，+30 -1.0，其余 0。
- def 的 SGO：`IG_TEST_BLUEFLD` 地面 def（`ig_transparentfld01.mdx`）的那份，改 `filename`、`Disappear`（1000）、
  `Durability` 0；物件再把 `Type` 从「地形パーツ」改成「ビル」（原版岩石类物件如此）。
- 记录：+00 → def（相对记录），+04 位置，+10 旋转（度；只用 y），+1C 0（变体：0 = 用 GUID 和），+34 GUID（uuid5，按图名与序号）。
- **旋转约定**（在 `NW_DANTI01` 的栅栏链上实测，282 对 162 票）：rot y = θ 是右手 R_y：本地 +X → (cos θ, 0, −sin θ)，
  本地 +Z → (sin θ, 0, cos θ)。X-Morph 绕 +Z 的偏航与它同角。

### mapo

`IG_TEST_BLUEFLD` 的 mdx 布局（头里的偏移相对 mapo 开头；条目里的偏移相对条目自身）：头 0x60；一个零 vec3（0x30/0x34）；
每条记录一个 0x3C 物件结构（0x20/0x24：+00 4，+04 → `<m>.hkt`，+0C → 零 vec3，+14 → `<m>_hk.mdb`，+20 → `<m>.mdb`，
都相对结构开头）、一个自相对 int 指向它（0x28/0x2C）、一个 0x1C 条目（0x08/0x0C：+04 → 那个 int，+08 1）、一个 0x0C 具名条目
（0x50/0x54：+00 / +04 → 记录序号的十进制字符串，+08 0）；最后是排序去重的 UTF-16 字符串池（0x48/0x4C）。
`<m>_hk.mdb` 不存在于 RAB 也没关系（原版 BLUEFLD 同样）。

### RAB

版本 272，文件夹 `TEXTURE`、`MODEL`、`HD-TEXTURE`，成员顺序贴图在前（`mdb.insert_member`），全部 CMPL：
`<贴图>.lod.dds`（TEXTURE，flag 0）、`<m>.mdb`（MODEL）、`<贴图>.dds`（HD-TEXTURE，flag 1）。另有每图一份平法线
（BC5U (128,128)）、平 rmo（DXT1 (181,0,255)，同原版 `ig_flat_rmo`），以及 `wire_test.dds`（电线类 `elec_wire_default.erb`
引用的贴图；本图没有电线，`cable_init` 找不到贴图只会丢掉该电线类型，放一份纯色的以免多余的告警）。
不透明贴图 DXT1，alpha 测试 / 半透明贴图 DXT5（BC4 alpha + `texfile_accel` 的颜色块），带全 mip 链，`texfile.texture_pair` 切 lod。

mdb（版本 0x20）：一根骨骼（kind 2、depth_delta 1、bounded，同原版 `ig_transparentFld01`），一个物件，每个部件每 65535 / 3
个三角形一个网格（u16 索引）。顶点 64 字节（原版城区地图常见布局之一）：BINORMAL / TANGENT / NORMAL half4（w = 1）、
POSITION float3、TEXCOORD float2、BLENDWEIGHT float4 (1,0,0,0)、BLENDINDICES ubyte4 0；网格 flags `00 01 01 00`。
材质按贴图一个：不透明 `snd_BRDF_Map_Common`，alpha 测试 `snd_BRDF_Map_Common_Clip`（先加 `alpha_clip` 0.5），
半透明 `snd_BRDF_Map_Common_Alpha`（b3 = 2，diffuse.w 0.5，加三个 `translucent_fall_off_*`）；贴图 albedo / 平法线 / 平 rmo。

### MAD

`hkcms_build.build_collision`：以 EDF6 自己的 `IG_HEIGEN601.MAD` 为供体（类型表、库、物理系统），不建高度格网
（`heights=None`），每个有碰撞的模型一个静态体 `<m>.<m>`（模型本地三角形；加载器按 `<m>.` 前缀找到物件的碰撞体并用物件矩阵放置），
地面模型用地形材质调色板，其余用物件的。

## 生成与安装

```
python tools/make_xm_maps.py build                # 开发机：5 张图写进 D:\APP\edf6-models\xm_maps（每张约 5 分钟）
python tools/make_xm_maps.py install <游戏目录>    # 安装器也会调用；remove 卸载
```

- `xm_maps.json`：版本、每张图的借用图 / 天气 / 文件 SHA-256 / 单位登记 / 地面包围盒。安装时文件缺失或哈希不对就跳过该图并说明。
- 安装写进 `Mods/MAP`，账本属主 `xmmaps`；Mods 里已有别的 MOD 的同名文件就跳过该图，不覆盖。
- 发布：`build_release.RELEASE_MODELS` 含 `xm_maps`，进模型包（分享流程 `run_export.py` 的 `EXTERNAL_MODELS` 要同步加，
  那个文件不在本仓库）。

## 实测

`python tools/make_xm_maps.py trial <图> <目录>` 生成一个只加载该图（第一个天气）、把玩家放在图中心最高地面上方 8 m 的任务
（`MISSION.AC` + 从 M045 的点位文件挪过来的 `MISSION.RMPA`），配合 `tests/autopilot/drive.py run M001 0 60 <log> --shots <目录>
--place <文件>=MISSION/EDF6/M001/... --place <地图文件>=MAP/...` 在后台跑：结束后 drive.py 把放过的文件逐字节还原。

## 已知限制

- 天空、光照、反射是借用图的；寻路网格也是借用图的，与 X-Morph 的地面不符：AI 的行走路线不对（与 4.1 地图同一问题）。
- 建筑不可破坏（`Durability` 0）：X-Morph 的破坏碎块拼成完好外观，没有做成 EDF 的破坏模型。
- 植被、小杂物没有碰撞（X-Morph 原本就没有碰撞网格），可穿过，同 EDF 自己的树。
- 投影贴花（路面标线）、特效实体、水面泡沫没有转。
