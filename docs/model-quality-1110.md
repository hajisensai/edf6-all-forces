# 外部模型转换链质量（2026-11-10）

维护者原话（2026-11-10）：「优化一下模型质量顺便，类似的根本性优化一下」。

范围：把外部模型（`D:\APP\edf6-models\downloads` 下的 Sketchfab / War Thunder 源、glTF）和程序生成模型转成游戏 MDB / 贴图的整条链。三路审计的发现去重后按「视觉收益 / 风险」排序如下；前 4 条已做，后面的没做，原因写在各条里。

所有数字都是离线算的（脚本在会话 scratchpad，不入库）；没有启动游戏，所以「实机效果」一栏都还要实机看一次。

## 总览

| # | 发现 | 结论 | 改了什么 |
|---|---|---|---|
| 1 | 源模型逐像素的粗糙度 / 金属度 / AO 被丢掉，全部变成常量 (140, 90) | 已修 | `pylib/pbr_maps.py`（新）、`pylib/gltf.py`、`tools/glb_to_wt_obj.py`、`tools/prep_glb_model.py`、`tools/prep_wt_tracked.py`、`pylib/wt_tracked.py`、`pylib/emplacement_model.py` |
| 2 | 源法线被丢掉，按 50° 贪心分组重算，和法线贴图烘焙 / Sketchfab 显示用的法线不一致 | 已修 | `pylib/mesh_normals.py`（新）、`tools/prep_wt_tracked.py`、`tools/glb_to_wt_obj.py`、`tools/prep_glb_model.py`、`tools/prep_xmorph.py` |
| 3 | DXT1 编码器只沿颜色包围盒最长轴取两端，不做主轴、不做最小二乘回拟 | 已修 | `pylib/texfile.py`、`pylib/texfile_accel.py`、`native/dxt1.cpp`（新）、`pylib/edf6native.py`、`pylib/obj_model.py` |
| 4 | 大模型在模型空间用 half4 存位置，无畏舰误差达厘米级、343 个面塌成 0 面积 | 已修，待实机 | `pylib/obj_model.py`、`pylib/wt_tracked.py`、`pylib/cockpits.py` |
| 5 | 透明 / 镂空（伪装网、玻璃、履带镂空、X-Morph 旋翼）全部按不透明画 | 没做 | 需要先逆向原版 Alpha / Clip 材质，并实机看排序和阴影 |
| 6 | 游戏能读 DX10 头的 BC7，我们的颜色图只能走 BC1 | 没做 | 需要 DX10 头 / .lod 布局逆向 + BC7 native 编码器 |
| 7 | 自发光丢失（X-Morph 空中 / 步行 / 步兵单位、glTF emissive） | 没做 | 见下 |
| 8 | 源材质都是 doubleSided，转换后单面、背面剔除 | 没做 | 需要先渲染确认哪些开放块真的露背面 |
| 9 | 外部材质沿用 v603_body 的 diffuse 0.842 等标量参数 | 没做 | 需要先逆向 BRDF_Common_Basic 里 diffuse 的作用 |
| 10 | mip 在伽马空间盒滤波、法线通道约定、分辨率上限 | 排除，不改 | 与原版一致（审计已对照原版 mip 回归） |

## 1. 逐像素粗糙度 / 金属度 / AO

**问题**：`gltf.Material` 只保留颜色和法线贴图；`glb_to_wt_obj` 只写颜色和法线；`prep_wt_tracked.texture_set` 只取 `map_Kd` / `map_normal` / `map_ao`，WT 每个材质都有的 `map_Ks`（`<stem>_n_s`）被丢掉。结果仓库 13 个 model.json 没有一个材质带逐像素参数图，全部走 `wt_tracked._param_dds(occ, PLAIN_RMO)`：整张图 R=140、G=90，没有 AO 时 B 也是常量 200。天启、TOS-1、Flak88、railgun 连 AO 都没有。

**契约**（`pylib/pbr_maps.py` 文件头）：游戏参数图 `param_r_m_occ_hr` 的 R = 粗糙度、G = 金属度、B = 遮蔽，都是 0..1 线性存成字节。依据：Deferred combine 取 `m = t3.x`，`F0 = S(0.04 + 0.96m)`，漫反射乘 `(1 - m)`（`docs/edf41-material-shader-contract.md`）；EDF6 自己的金色涂装就是把 glTF 式的 roughness / metallic 直接写进 R / G（`pylib/goldcoat.py`），`sazabi_model` 也是这样从 glTF 系数生成 rmo。所以 glTF 的值原样搬过去，不另拍常数：

| 源 | R（粗糙度） | G（金属度） | B（遮蔽） |
|---|---|---|---|
| glTF metallic-roughness | 贴图 G × roughnessFactor | 贴图 B × metallicFactor | occlusionTexture 的 R，按 strength：`1 + s(R - 1)`，再按 `OCC_FLOOR` 抬黑（与 WT 的 AO 同一处理） |
| glTF specular-glossiness | `1 - A × glossinessFactor` | 0（见下） | 同上 |
| 无贴图 | 材质自己的系数，4×4 图 | 同左 | 同上 |
| OBJ 的 ORM（`map_Pr` = `map_Pm` = `map_ao` 同一张，GDF-005） | ORM 的 G | ORM 的 B | ORM 的 R |
| War Thunder `map_Ks`（`<stem>_n_s` 灰度高光图） | `wt_tracked.SPECULAR_RM`（与 X-Morph 同一条映射：s=0 → 215，s=255 → 95） | s=0 → 20，s=255 → 170 | WT 的 AO，原样 |

specular-glossiness 的金属度取 0：这个工作流里颜色贴图就是漫反射，combine 还会再乘 `(1 - m)`，给了金属度就会把漫反射压暗两次（Khronos 的转换器给出金属度时会同时重算 base colour，我们不改颜色图）。本仓库的 SG 源里高光最高 0.2（WT 上传 0.05，Flak88 0.06 / 0.2），都是电介质量级，取 0 不丢东西。

model.json 新增材质键 `roughness_metal`（R/G 已是游戏值的图），WT 材质新增 `specular`；`wt_tracked.material_textures` 按 `specular` → `roughness_metal` → 常量的顺序生成参数图，B 通道都是该材质的遮蔽图。

**前后对比**（安装时实际生成的 rmo，DXT1 解码后统计，均值 / 标准差；改前这些全是 R 140/0、G 90/0、无 AO 时 B 200/0）：

| 模型 / 材质 | 尺寸 | R 粗糙度 | G 金属度 | B 遮蔽 |
|---|---|---|---|---|
| wt_bmpt 车体 | 1024² | 206.2 / 17.2 | 30.7 / 21.6 | 189.2 / 39.5 |
| wt_m163 车体 | 1024² | 209.4 / 10.9 | 26.4 / 13.9 | 181.1 / 45.9 |
| wt_apocalypse m0 | 1024² | 144.0 / 24.9 | 48.7 / 82.4 | 154.9 / 41.8（改前没有 AO） |
| wt_apocalypse m10 | 1024² | 156.7 / 28.0 | 64.9 / 73.6 | 158.3 / 53.2（改前没有 AO） |
| wt_tos1 m1 | 1024² | 255 / 0 | 203.3 / 20.5 | 197 / 0 |
| emp_gdf005 | 2048² | 140.8 / 44.4 | 12.0 / 31.2 | 199.8 / 34.1 |
| emp_railgun | 1024² | 132.2 / 103.7 | 37.1 / 86.7 | 254.9 / 0.9 |
| emp_phalanx 三个材质 | 4×4 | 189 / 74 / 140 | 247 / 239 / 0 | 197 |
| emp_flak88 | 1024² | 181 / 0 | 0 | 250 / 3.8（源 strength 只有 0.04） |

原版对照（审计读 Root.cpk 的数）：v603 / v601 / v505 车体 rmo 的 R 标准差 12 / 19 / 48，G 标准差 37 / 26 / 52。改后外部模型也是逐像素变化，不再是 0。

WT 涂装车按源的意思是哑光非金属（Sketchfab 上 glossiness 0.117，即粗糙度约 0.88），所以改后粗糙度比原版车体均值（约 140）高、金属度低（约 30 对 90）：这是源的材质，不是常数拍出来的。

**没做**：X-Morph 非履带路径（`prepare_air` / `prepare_walker` / `prepare_car`）仍不调用 `material_maps`，`xm_air_model` 仍是常量 `ROUGH_METAL`。这些模型在模型包（`D:\APP\edf6-models\xm_*`），要从 X-Morph 游戏包重新 prep，本次没有动。

## 2. 法线

**问题**：

- `glb_to_wt_obj` 只写 `v` / `vt`，glTF 自带的法线（天启的 14 张法线贴图就是在这套法线上烘的）被丢掉。
- WT 源 OBJ 没有 `vn`，`prep_wt_tracked.corner_normals` 按 OBJ 顶点序号、种子面 + 50° 贪心分组重算：不按焊接位置（每条 UV / 缓冲缝都硬断开），也不沿边连通。
- `prep_glb_model` 读 OBJ（GDF-005）时按部件内的顶点序号整片平均，没有角度阈值；`read_skinned`（railgun）没读 glTF 的 NORMAL。

**根因验证**：同一次下载里 Sketchfab 给 WT 车生成的 `gltf/scene.gltf` 带 NORMAL（源 OBJ 没有，所以这是 Sketchfab 按上传计算的，也就是 Sketchfab 页面上的样子）。把 OBJ 三角和 glTF 三角按顶点位置 + UV 一一对上（M163 87%、BMPT 87% 的三角对上），逐角点比较：

| 方案 | M163 >1° | M163 >15° | BMPT >1° | BMPT >15° | BMPT p99 |
|---|---|---|---|---|---|
| 原 `corner_normals`（50°，按序号，贪心） | 11.3% | 4.9% | 14.5% | 7.2% | 37.7° |
| 按 OBJ 序号整片平均 | 27.6% | 22.1% | 29.8% | 22.4% | 51.0° |
| **焊接位置 + 沿边连通 + 45° + 面积加权（新 `mesh_normals.auto_smooth`）** | **0.03%** | **0.01%** | **0.68%** | **0.14%** | **0.03°** |

30° / 50° / 60° / 80° 和不焊接的变体都对不上，45° 焊接沿边的版本与 Sketchfab 的法线基本逐角点一致，所以它就是 Sketchfab 的算法。

**做法**：

- `pylib/mesh_normals.auto_smooth`：位置按 1e-5 m 焊接，共享焊接边且面法线夹角 ≤ 45° 的两个面在这条边两端的角点合为一组（连通分量），每组取面积加权和。源没有法线时（WT OBJ、GDF-005 OBJ、X-Morph 车辆路径）一律用它；它也比原实现快（BMPT 0.6 s 对 4.1 s）。
- 源有法线就原样带过来：`glb_to_wt_obj` 写 `vn` 和 `f v/vt/vn`，`prep_wt_tracked.read_source` 读 `vn`（`Source.tri_n`），`prep_glb_model.read_skinned` 读 NORMAL。
- model.json 的 `auto_smooth_deg` 换成 `normals`（`source` 或 `auto_smooth 45`）。

**前后对比**：

| 模型 | 改前与源 / Sketchfab 法线的偏差 | 改后 | 顶点行数 |
|---|---|---|---|
| wt_apocalypse | 40.6% 角点 >1°，3.9% >30°，p99 49.0° | 0（glTF 原样） | 292475 → 289174 |
| wt_tos1 | 96.1% >1°，61.3% >30°，p99 73.8°（源是整体平滑，被 50° 拆硬） | 0 | 9225 → 8144 |
| wt_bmpt | 14.5% >1° | 0.68% | 493028 → 503989 |
| wt_m163 | 11.3% >1° | 0.03% | 124930 → 125684 |
| wt_pzh2000 / sturmtiger / tunguska | 同 BMPT 的算法 | 同上 | 326640 → 333031 / 182765 → 186887 / 278434 → 285925 |
| emp_gdf005 | 整片平均，无硬边 | 45° 自动平滑 | 26271 → 29469 |
| emp_railgun | 部件内整片平均 | glTF 原样 | 19756 → 19756 |

三角数不变；顶点行数 −12% 到 +12%。

**没做**：

- 源 TANGENT 原样写入（替换 `wt_tracked.tangent_frames`）：审计也标为推测，烘焙器的切线约定没有逐像素核对，没改。
- `tools/prep_sazabi.py`：网格先经 `fast_simplification` 减面，源法线对不上减面后的顶点，仍用它自己的 `split_normals`；沙扎比在模型包里，本次没重新 prep。
- X-Morph 车辆路径改用了 `auto_smooth`，但 X-Morph 文件夹没有重新 prep（同第 1 条）。

## 3. DXT1 编码器

**问题**：端点取颜色包围盒最长单通道轴上的两个极端像素，量化到 565 后不再回拟。原版同槽位也是 DXT1，格式没错，差在编码质量。

**做法**：每块取三个候选里误差最小的（误差按 1/3 单位平方算，整数精确）：原来的范围拟合；主轴（协方差矩阵在 1/1024 步长上做 4 次整数幂迭代）上的两个极端像素；最多 2 轮按当前索引的端点最小二乘回拟（整数解 2×2 正规方程，四舍五入到 565）。调色板在一条线上，所以每个像素按投影到中点阈值取最近项，是精确最近。范围拟合误差已 ≤ 1296（每通道均方误差 3）的块不再搜索（BMPT 车体只差 0.04 dB，省三分之一时间）。

全部是整数运算，所以三个实现逐字节相同：`texfile._dxt1_block`（纯 Python 参考）、`texfile_accel._level_numpy`（无 DLL 时）、`native/dxt1.cpp`（`edf6cmpl.dll` 的 `edf6_dxt1_level`，`texfile_accel._level` 有 DLL 时用它）。`tests/test_native_dxt1.py`、`tests/test_texture_acceleration.py` 对比字节。`obj_model.texture_dds` 改走 `texfile_accel`（同样的字节，不再跑纯 Python）。

**前后对比**（顶层，同一 565 量化与解码；时间是整条 mip 链）：

| 贴图 | 尺寸 | 改前 PSNR | 改后 PSNR | 最差 1% 块 RMSE | 改前耗时（numpy） | 改后耗时（native） |
|---|---|---|---|---|---|---|
| BMPT 车体 | 2048² | 39.41 dB | 41.14 dB | 8.1 → 6.2 | 0.58 s | 0.26 s |
| M163 车体 | 2048² | 37.61 dB | 39.25 dB | 9.1 → 7.2 | 0.57 s | — |
| 天启 m0 | 1024² | 37.38 dB | 39.31 dB | 9.2 → 7.1 | 0.15 s | 0.06 s |
| 无畏舰 ID01 | 2048² | 39.33 dB | 40.61 dB | 8.5 → 7.0 | 0.61 s | 0.21 s |
| Phalanx | 1024² | 33.07 dB | 34.64 dB | 21.0 → 17.8 | 0.15 s | 0.05 s |
| GDF-005 | 2048² | 38.87 dB | 40.42 dB | 8.1 → 6.6 | 0.58 s | 0.24 s |

没有 DLL 时的 numpy 版是改前的 2.5–3 倍（BMPT 车体 1.64 s），纯 Python 版约 6 倍；安装包总是带 DLL（安装器的包检查要求 `edf6cmpl.dll`）。所有 DXT1 产物的字节都变了（颜色图、参数图、solid 图不变：单色块仍是同一编码）。

## 4. 大模型的位置精度

**问题**：v505 / v515 / v603 模板的 position 都是 half4（格式 7），蒙皮网格在模型空间。无畏舰居中落地后范围 228×110×500 m：

| | 位置误差 均值 / p99 / 最大 | 面法线偏转 >5° / >20° | 量化后 0 面积的面 |
|---|---|---|---|
| half4（改前） | 29.1 / 65.1 / 73.4 mm | 12.4% / 1.9% | 343 / 71669 |
| float4（改后） | 0.00 / 0.01 / 0.01 mm | 0 / 0 | 0 |

**做法**：`obj_model.precise_positions`（原 `cockpits.precise_template` 移过来，座舱改为调用它）把 position 加宽为 float4、vsize +8、后续元素偏移顺延；`obj_model.position_layout(template, extent)` 在模型最大坐标超过 `PRECISE_EXTENT = 16 m`（half 步长 ≥ 2⁻⁶ m，误差可达 7.8 mm）时使用。`obj_model.build_meshes`（无畏舰、C-130、沙扎比、钻地车、X-Morph 飞行单位等，调用方传整模型的 `reach`）和 `wt_tracked.build_with_info`（WT / 炮台 / X-Morph 履带）统一按整个模型的尺度选布局，不按模型名特例；`wt_tracked.part_rows` 的退化三角判定改为按实际存储格式。16 m 以内的模型（全部 WT 车和炮台）产物不变。

离线用本机 Root.cpk（只读）实际生成：无畏舰 71669 个三角全部写出（改前 343 个被 `drawn_triangles` 丢掉），position 为 (格式 1, vsize 68)，顶点缓冲 6.75 MB（vsize 60 → 68，+13%）；轰炸机（13.8 m）仍是 half4；C-130 运输型 / 炮艇型全部材质为 float4。

**按模型、不按材质选布局**（审查补正）：最初 `build_meshes` 按每次调用（一个材质）自己的最大坐标选布局，C-130 就成了机身 float4、玻璃 / 螺旋桨 half4。两种材质共有 171 个顶点（座舱玻璃与机身框等），half 量化后与 float 相差最多 3.9 mm（53 个超过 1 mm），改前两边同为 half、落在同一点，这样反而开出缝。现在 `build_meshes(..., extent=)` 必须传整个模型的 `obj_model.reach(...)`，一个模型的所有材质存法一致（与 `wt_tracked.build_with_info` 相同）；`tests/test_model_quality.py` 的 `test_one_model_one_position_format` 覆盖。

**待实机**：座舱网格已经用 float4 位置在载具 MDB 里正常显示，但载具外壳本体用 float4 还没有实机记录，需要实机看一次无畏舰和 C-130（阴影 / 深度 pass）。无畏舰顶点缓冲约 +13%。

## 5–9. 没做的

- **5 透明 / 镂空**：伪装网（BMPT net_f / net_b 54–58% 像素 alpha<128）、车灯 / 观瞄玻璃（46–87%）、T-72 履带镂空（49%）、X-Morph 旋翼盘（`prep_xmorph` 直接丢弃）。原版玻璃用 `snd_BRDF_Common_Alpha`（透明度在 `diffuse.w`，b3=2，全仓 55 个材质），V603 履带的 `snd_BRDF_Mech_Catapillar` 带 `alpha_clip` 参数（原版为 0），另有 `*_Clip` 一族。`alpha_clip` 的语义（阈值方向、取哪个通道）和 b2 / b3 的含义要先从 DX11.cpk 的 shader 逆向，透明排序和阴影 pass 要实机确认；拿不准就上，可能出现透明件消失或排序错乱，所以本次不做。下一步：读取侧（`map_d` / `d`、glTF `alphaMode` / `alphaCutoff` / baseColor 的 A）写进 model.json 的 `alpha`，编码加 DXT5（alpha 走已有的 BC4 块编码），模板按 alpha 模式选原版 Alpha / Clip 宿主材质。
- **6 BC7**：原版 OBJECT 里 69 张 DX10 头的 BC7_UNORM，引擎能采样；我们的 `texfile` 遇到 DX10 头直接拒绝。需要先统计原版 DX10 的 HD / .lod 布局（`pair_problem` 注释里的 62 对），再加 native BC7 编码器（bc7enc_rdo 等）；颜色图体积翻倍，模型包约 +20%。本次第 3 条已把 BC1 提高 1.3–2 dB，BC7 另开任务。
- **7 自发光**：X-Morph 空中 / 步行 / 步兵单位的发光图没进 model.json（模型在模型包，要重新 prep X-Morph）；glTF `emissiveFactor` / `KHR_materials_emissive_strength`（激光炮台发射口）需要把 `prep_glb_model` 的材质按 `wt_tracked.LIGHT` 模板走，并实机看 bloom 强度。
- **8 doubleSided**：源 glTF 材质全部 doubleSided，开放连通块面积占比 BMPT / M163 70%、Phalanx 53%、天启 38%；但其中多少在正常视角露背面没有渲染确认。盲补背面三角会让三角数 +10–25%，要先用 `tools/cabin_view.py --mark-back` 渲外视角图挑块。
- **9 v603_body 标量参数**：外部材质沿用 V603 的 diffuse 0.842、specular_color 0.164；它是否在 BRDF_Common_Basic 里乘 albedo（会整体压暗约 16%）没有反汇编确认，改了要实机比亮度。

## 10. 排除

mip 在伽马空间做 2×2 盒滤波（原版 mip1 对 mip0 的回归：伽马空间偏差 +0.1–0.3，线性空间 −4.8 到 −11.7）、BC5 法线与 DX / GL 绿通道声明、原生分辨率、.lod = mip4，都与原版一致，不改。

## 产物与缓存

- 重新 prep 的入库文件夹（13 个）：`wt_m163`、`wt_bmpt`、`wt_tunguska`、`wt_pzh2000`、`wt_sturmtiger`、`wt_apocalypse`、`wt_tos1`、`emp_flak88`、`emp_gdf005`、`emp_laser`、`emp_patriot`、`emp_phalanx`、`emp_railgun`。改前先用旧代码把 13 个都重跑一遍，与入库内容逐数组、逐 JSON 相同（只有几张 occlusion PNG 的压缩字节不同），说明差异只来自本次改动。
- 体积：wt_apocalypse 30 → 48 MB（每个材质加 1024² 的粗糙度 / 金属度图和 AO 图），emp_gdf005 4.7 → 12 MB（2048² ORM 转出的参数图），wt_tos1 0.9 → 3.0 MB，其余 ±1 MB。
- 模型包版本：`pylib/modelpack.version` 是所有 RELEASE_MODELS 文件路径加内容的哈希，这些文件夹变了，版本随之变化，是预期的。
- 安装器生成缓存：`pylib/buildcache` 的 recipe 哈希生成器及其传递导入的源码（含函数内导入：`texfile_accel`、`wt_tracked`、`obj_model`），inputs 哈希模型文件夹里每个文件的内容，所以这两类改动都会让对应分组重建，不会沿用旧产物。
- 安装时贴图耗时（`wt_tracked.material_textures` 生成一辆车的全部 DDS）：wt_bmpt 8.7 → 6.9 s、wt_m163 5.0 → 3.6 s、wt_apocalypse 10.3 → 9.6 s（多了 19 张 1024² 参数图）、emp_gdf005 1.9 → 2.0 s。prep 工具耗时与改前同一量级（每个文件夹 1–40 s）。
