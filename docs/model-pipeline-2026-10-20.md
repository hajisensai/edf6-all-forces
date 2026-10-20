# 2026-10-20 模型 / 碰撞管线：借鉴 EDF6 SHKT Editor v1

## 用户原话

> 模型之类的也可以优化下制作之类的 https://codeberg.org/Ktaro/EDF6_SHKT_Editor.git 可以看看…有用就抄过来。有问题全部根本性修复

## 来源与许可证

- EDF6 SHKT Editor v1（commit `8ade8fd`，作者 Ktaro）：C# / .NET。`LICENSE`：**MIT**，Copyright (c) 2026 Ktaro。
  随附的 V-HACD 为 BSD-3-Clause；**本次没有移植 V-HACD 的代码**（凸分解用的是自己的简化切分，见下），所以安装包里只带 MIT 声明。
- 移植的文件头都写了出处与许可：`pylib/hkfile.py`、`pylib/hkshape.py`、`pylib/model_pieces.py`、`pylib/shkt_check.py`（规则）、
  `pylib/vehicle_check.py`（规则）。许可原文 `pylib/LICENSE.edf6-shkt-editor`；`tools/build_release.py notices()` 把 `pylib/LICENSE.*`
  拼成发布 zip 里的 `THIRD-PARTY-NOTICES.txt`（selftest `release_third_party_notices` 守护）。
- 参考源和手册只读，其中的 exe / 脚本 / 构建一律没有运行。

## 架构（改后）

| 层 | 模块 | 内容 |
|---|---|---|
| tagfile | `pylib/hkfile.py` | Havok 2020 TAG0 读成对象图（Block / Value / TypeTable），按 Havok writer 的规则写回（对象按发现顺序编号，闭包内数组 / 字符串广度优先，1024 元素分块，字符串按内容共享，空数组写 null）；`Physics` 视图（系统、刚体、关节、ragdoll 骨架）；`content_equal` / `changed_blocks` / `clone`。移植自 HkTagfile / HkTypeTable / HkType / HkLayout / HkBlock / HkValue / HkCompare。 |
| 旧 tagfile | `pylib/hk2014.py` | 4.1 移植物的 Havok 2014 二进制 tagfile（原有），现在 `shkt_check` / `vehicle_check` 也读它。 |
| 偏移视图 | `pylib/hktag.py` | 地图碰撞原地改高度（`hkcms`）还用的偏移读取器；类型表改由 `hkfile.TypeTable` 解析（只剩一份 TBDY 解析）。 |
| 形状 | `pylib/hkshape.py` | QuickHull、Havok 凸包（252 顶点上限、共面合并、faceLinks / vertexEdges / minHalfAngle）、hull_problems、质量属性（Steiner 体积 + 多面体惯量 × 惯性系数、压缩网格的实心盒规则、复合合成、Jacobi 特征分解）、hkPackedVector3 打包（EDF.dll 0x1305DE0 算法）、AABB 4 字节编解码（带裕量版给地图树、单精度精确版对原版字节）、唯一的 SIMD 树构建器（`hkcms_build.simd_tree`，native 加速）、写凸包 / 复合 / 凸包压缩网格、`affine_convex`、`refresh_compound`。 |
| 贴模型 | `pylib/model_pieces.py` | 连通块（按位置焊接，ModelPieces）、凹度切分（面上采样点到网格的距离，V-HACD 的简化思路）、凸体与盒求交（实体，不是表面）、凸包限点（MeshShapes.Reduce）、按凸半径收缩。 |
| 检查 | `pylib/shkt_check.py`、`pylib/vehicle_check.py`、`tools/check_shkt.py` | 见下。 |
| 网格构建 | `pylib/procmesh.py` | 侧三轮与履带车各自的「外向缠绕」基本体（flip_outward / facing / cap_ends / flat_hexahedron / closed_tube / hard_polygon / hard_hexahedron / hard_box / hard_cylinder）收进这里。 |

替换掉的重复实现：

- 4 份 SHKT 读取器：`shkt_check.Shkt`、`ragdoll_fit.Shkt`、`sidecar_model._Shkt`、`tracked_collision` 的读取函数 → 全部基于 `hkfile`。
- 3 份编辑器：`aircraft_collision._Edit`、`tracked_collision._Editor`、`hkcms_build.Doc` → `hkfile` 图编辑 + `hkshape` 写形状。
- 3 份 SIMD 树：`aircraft_collision._tree`、`sidecar_model._refresh_tree`（原地刷新）、`hkcms_build.simd_tree` → 只剩后者（`hkshape.simd_tree` / `set_simd_tree` 包装）。
- `tracked_collision._pack_vec / _unpack_vec / _mass_cache`、`hkcms.encode_box / decode_box` → `hkshape`。

## 验证（本机游戏目录 `D:\steam\steamapps\common\EARTH DEFENSE FORCE 6`，只读）

### hkfile（问题 2）

- 原版 Root.cpk 的 **316 个 TAG0 SHKT 与 IG_HEIGEN601.MAD 的 collision.hkt 全部逐字节往返**（`tests/test_hkfile.py Stock`，无游戏跳过；可移植部分用 `tools/frozen_collision_probe.py` 的合成类型表测写出规则）。
- 生成器新旧产物（旧 = 本分支第一个提交 `f74336b3`（基于 merge/1020-all，只改了检查器、没动生成器）的代码导出到临时目录运行）：

| 生成器 | 结果 |
|---|---|
| artillery（ragdoll_fit 原地改） | 逐字节相同 |
| sidecar（问题 2 阶段） | 逐字节相同 |
| sedan / 7 个机身 / M163 / BMPT / 地图碰撞（追加式编辑器） | 字节不同，`hkfile.content_equal(旧根, 新根)` 为真：同一对象图。差异只是旧文件里留着供体被替换掉、已无引用的条目（机身 SHKT 小约 100 KB），以及 Havok 规则的重新排布。 |

### hkshape（问题 3）

全部在 `tests/test_hkshape.py Stock`：

- hkPackedVector3：原版 22,562 个向量逐位重现。编辑器的写法（2 的幂缩放）在原版数据上也全对，但对 2⁻⁴⁶ 以下的向量与 EDF.dll 不同（随机 20 万例差 3.2 万），所以保留 EDF.dll 算法（纯 Python 重写，不再要 numpy）。
- 82 个凸包压缩网格：按其存储数据重写，**对象内容全部相同**；从外部凸包重新计算：域到舍入、键位数相等、质量按盒规则（重心一个字内、主轴字相同、惯量 2e-3、体积 1e-5）；原版 section 树逐轴用单精度编码重现（2000+ 轴）。
- **发现的缺陷**：82 个原版网格的 majorAxisSpace 全是 `F52F`（29999/30000），旧 `tracked_collision` 写的是 `F530`。已改。
- 1,834 个凸包复合：重算 aabb 到两个 float32 步长（18 个巨大物体差 1–2 ulp），包围半径从不小于原值，键位数 / 键估计全相同。
- 8,047 个原版凸包：从顶点重建全部自洽（3 个平面体拒绝），> 95 % 保留原顶点集；`from_hull` 体积中位误差 4.9e-4（手册写约 5e-4）。原版凸包本身有 3,027 个过不了编辑器的 1e-4 平面校验（Havok 简化过平面），所以这条校验只用于我们自己建的凸包。

生成器改用 hkshape 后（与原输出几何比较）：

- M163 / BMPT（仿射路径）：22 个刚体的凸块顶点与凸半径**逐位相同**；差在派生数据（每个凸块自己的质量属性、F52F、穿透容差按原版规则 0.165 ×（最小宽 + 2r）、外部形状改成规范路由 k→k）。
- 侧三轮：凸块几何逐位相同；树重建、复合质量属性合成。
- 轿车：车身 5 个凸块逐位相同；**车门修了一个真缺陷**：旧代码用实例缩放 0.001 做「小车门」，没有一个原版文件缩放实例，而且 Havok 不缩放凸半径，旧文件的车门 aabb / 树过期（`check_shkt` 8 个 error，就是装在游戏里那份）。现在把凸包本身缩到毫米级，0 error。
- 机身：格子盒改由 `hkshape` 写（各自质量属性 + 复合合成），`tools/aircraft_collision_check.py --game` 11 项全过。

### ragdoll 规则（问题 4）

`shkt_check.problems()` 新增（手册 2.6 / 2.7 / 12.1，原版校准）：

| 规则 | 原版 | 级别 |
|---|---|---|
| 有关节的两个刚体被过滤器允许碰撞（子 no-collide ≠ 父 subsystem） | 5 个文件 112 处（火星人、乌贼、Barga 炮） | info |
| 非关节、未排除的部件凸块重叠 ≥ 5 cm（GJK） | 0（只有 2 处 1–2 cm） | warning |
| ragdoll 里有刚体不连到其余刚体 | 0 | warning |
| 改了形状却保留供体的 massDistribution（`stale_mass_distributions(新, 供体)`） | — | warning（生成器自检） |

`tools/check_shkt.py --stock`：0 error / 0 warning / 112 info / 0 skipped。我们所有生成的 SHKT：0 error / 0 warning / 0 info。

massDistribution（手册：null 时游戏按形状算；改形状应删除）逐个查了生成器：

- **机身主体 `RagDollProxys.body`：保留的是 V506 直升机的质心 / 惯量，形状却换成整架飞机——已改：删除 massDistribution**，形状写正确的合成质量属性，让游戏按机身算。这与 `docs/feedback-2026-10-12-ragdoll-twitch.md` 的 E8（当时清掉 massDistribution「照样升空」）不同：E8 时形状的质量属性被 `_compound` 清零了，游戏没东西可算；现在两者都对。残骸抽搐的第二来源仍未定位，这一改**未实机**，不能说修好了。
- 机身其余 7 个代理（旋翼、桨叶、尾桨、后轮）：保留 V506 部件的 massDistribution（它们是 V506 部件的替身，关节按 V506 布局；1 cm 小盒只是不参与碰撞的占位，按形状算会得到极小惯量）。生成器显式断言「只保留这 7 个」。
- 轿车车身与车门、侧三轮车身：保留供体（V512 / V503）的 massDistribution。原版载具的值明显是调过的（质心比形状高、惯量各向同性 1.008），与形状算出的不同；车身形状是同尺寸的重新拟合 / 只加了碰撞用的底板，删掉会改操控。生成器显式断言保留集合。
- M163 / BMPT / 自行榴弹炮：供体 V603 的刚体本来就没有 massDistribution，游戏按形状算——所以形状质量属性的正确性在这里直接生效（见上，F52F 与按凸包算的子形状质量）。tracked 自检要求每个刚体都无 massDistribution 且形状带质量属性。

### 车辆参数（问题 5）

`pylib/vehicle_check.py`（VehicleMath 移植）接进 `tools/check_shkt.py`：car_base_* 名称（经 ragdoll 绑定的骨骼→刚体映射，EDF.dll 0x6E7E40）、cast_mode 条数、总质量、静止时每个轮子的悬挂压缩（不触地 / 压到底）、路轮半径与其刚体形状之比。旧版 2014 SHKT 也读（4.1 坦克 / 摩托）。

- 原版 48 辆（三个 cpk）：0 error 0 warning（轮径比原版 0.94–1.62，所以只在 0.85–1.75 之外报；压缩占行程 1.9 %–41 %）。
- Mods + 本分支生成物 92 辆全部通过：edf41tank（VEHICLE301_TANK 各涂装）19–22 %、edf41bike（VEHICLE408_MOTORBIKE）16–22 %、侧三轮 15 %、BMPT 23–35 %、M163 23–48 %（最高，后轮；在行程内）、轿车 12–15 %、自行榴弹炮 24–26 %。

### 贴模型碰撞（问题 6）

`model_pieces.fit`：连通块 → 凹度切分（面上采样点离网格 > 2 cm 就沿最长轴对半切）→ 每块实体与目标盒求交 → 按凸半径 5 mm 收缩 → 凸包（≤ 64 点）→ 压缩网格（≤ 127 块）。

M163 / BMPT（`tracked_collision.MESH_FITTED = True`，**默认新路径**；`build(..., fitted=False)` 回到旧路径），离线对比（目标盒、关节、过滤器、`_sweep` 的兄弟间隙都不变）：

| | 旧（供体凸包拉伸到盒） | 新（模型自身的块） |
|---|---|---|
| 各代理覆盖的模型顶点 | 0–33 %（车体 1.3 % / 16.9 %） | 100 %（全部 22 + 22 个代理） |
| 总体积 M163 / BMPT | 12.7 / 23.7 m³ | 15.8 / 23.9 m³（盒 21.6 / 42.9） |
| 炮塔转盘（M163，内接盒） | 0.10 m³ 薄片 | 0.27 m³（盒内的实体部分；操作员井仍在盒外为空） |
| 生成时间 | < 1 s | M163 约 10 s（六根炮管是空心管，切成 60 块）、BMPT 约 2 s |

机身（`aircraft_collision.MODE = 'cells'` 保留默认，`'pieces'` 可切换）：pieces 模式体积小 40–80 %（贴合后掠翼与机身曲面），但覆盖模型顶点只有 77–100 %（< 5 cm 的小件被丢、薄片被盒切掉），每架生成 20–200 s（航母 199 s）。离线证据不足以换默认：格子覆盖 100 %、1–2 s。

这些都会改游戏里的物理，**全部未实机**。

### MDB 侧（问题 7）

- numpy 约束核实：完整安装器打包 numpy / Pillow（`build_release.BUNDLED`）；**精简生成器**（`tools/loadout_generator.py`，`GENERATOR_EXCLUDES`）排除 numpy、procmesh、jet_models，`tests/slim_generator_test.py` 守护。所以纯 Python 一族（`obj_model.Part`、`jet_skids.Shape`、`sazabi_arms` 的面构建、`cockpit_shapes`）不能并进 numpy 的 `procmesh.Part`。`graft_pure.py` 文件头关于「安装器纯 Python」的过时说法已改正。
- 合并：侧三轮（`_flip_outward / _facing / _cap_ends / hexahedron / tube`）与履带车（`polygon / hexahedron / box / cylinder`）的外向缠绕基本体搬进 `procmesh`，两处模块只留薄包装。产物：侧三轮 / M163 / BMPT 的 MRAB 与 SGO 与改前**逐字节相同**；其余生成器（降落伞、钻头车、喀秋莎、潜艇、EMC、终极支援、Sazabi、Proteus、光学、喷气机）MRAB / SGO 全部逐字节相同（见末节）。
- **没合并**：`procmesh.tube`、`jet_skids.tube`、`sazabi_arms.tube`、`cockpit_shapes.cylinder` 是不同的几何（共享环顶点的帽 / 带法线的独立帽顶点 / 棱柱），浮点运算顺序也不同；合成一个函数必然改它们的字节，与「逐字节相同」的要求矛盾。要合，得先接受模型字节变化并逐个看模型。

## 未覆盖 / 风险

- 所有改变游戏物理的改动都**未实机**：机身主体 massDistribution 删除（飞行刚体 `src/body506.cpp MakeAirframe` 也用这个复合形状，它现在带合成的质量属性；若该刚体按形状推导惯量，飞行手感可能变化）；M163 / BMPT 默认改贴模型碰撞；各形状的质量属性从「AABB 盒 ×1.5 / 清零」改为按凸包算；Havok 规则重排的文件布局（与原版文件同一种布局，但游戏只验证过我们旧的追加式布局）。
- 残骸抽搐的第二来源（`docs/feedback-2026-10-12-ragdoll-twitch.md`）仍未定位；机身 7 个替身代理保留 V506 的 massDistribution 是有意的例外。
- 车辆检查的轮径规则阈值很宽（原版跨度本来就大），只能抓明显错配；悬挂压缩是「刚体底盘 + 竖直弹簧」的静态估算，游戏的 g 值没有核对。
- V-HACD 未移植；`model_pieces` 的切分是简化版，空心细长件会切出很多块（M163 炮管 60 块）。
- `tools/check_shkt.py` 仍未接进安装流程（与 10-12 一样，生成器各自自检）。

## 全部生成器：改前 / 改后

旧 = `f74336b3`（同上）的代码导出后运行；新 = 本分支末尾；同一份游戏数据（只读）。

| 生成器 | 文件 | 逐字节相同 | 不同的文件 |
|---|---|---|---|
| make_jets | 84 | 77 | 7 个机身 SHKT：格子几何逐位相同，主体去掉 V506 的 massDistribution，凸块 / 复合带质量属性，树由统一构建器重建 |
| make_vulcan / make_bmpt | 6 / 10 | 5 / 9 | 碰撞 SHKT：贴模型的凸块（问题 6） |
| make_sidecar | 3 | 2 | 碰撞 SHKT：凸块几何逐位相同，地板凸包重建、树重建、质量属性 |
| make_sedan | 3 | 2 | 碰撞 SHKT：车身凸块几何逐位相同，车门改为真正缩小的凸包（旧文件 8 个 error） |
| make_artillery、make_chute、make_drill、make_katyusha、make_sub、make_emc、make_ultimate、make_sazabi、make_proteus、make_optics | 6、2、5、3、22、4、17、7、1、147 | 全部 | — |
| 地图碰撞（`hkcms_build.build_collision` 的两个合成场景） | 2 | 0 | 内容相同（`hkfile.content_equal`），只是 Havok 规则的排布 |
