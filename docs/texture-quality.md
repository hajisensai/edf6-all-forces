# 生成载具的贴图质量：BC5 法线、原生分辨率、绿通道方向（2026-10-27）

起因：用户反馈生成载具「看起来很差」，要求根本修复。只读调查见 `tmp/model-quality-re.md` §3/§4；本文记录贴图部分的证据、格式选择、
实测数字与未实机验证的点。网格 / LOD / 批次是另一项工作，不在这里。

置信度：**H** = 指令 / 实物数据直接读到；**M** = 有代码或数据证据的推断；**L / 未验证** = 需实机。

## 1. 原版法线贴图长什么样（H）

- 原版载具本体法线：V505 / V601 / V603 / V614 body、kyata、light，V505 / V506 EDF6BENEFITS：**BC5U**，旧式 124 字节头、
  FourCC `BC5U`（**没有** DX10 扩展头），flags `0xA1007`，depth 字段 `1`（抽样 39 / 39 个 BC5U 文件都是 1；DXT1 0 / 1 都有），
  caps `0x401008`，完整 mip 链到 1×1（2048² 为 12 级、5 592 560 字节），`.lod` 成员与其它贴图同一规则（2048 → 128，`texfile.texture_pair`）。
  V506 / V515 是 DXT5nm（R 恒为 255，X 在 alpha）。
- shader（`SND_BRDF_COMMON_BASIC` 等）：X = R·A、Y = G、Z = sqrt(1 − x² − y²)，B 不用（RE 笔记 §3）。BC5 采样 A = 1，所以
  **R = X、G = Y**，与我们以前的 DXT1 通道含义相同，材质不用改。
- 我们以前：所有法线都写 DXT1（X 5 位、Y 6 位、共用一条插值直线），WT 法线源本身还是 JPEG。

## 2. 编码器（`pylib/texfile_accel.py`）

- `bc5_normal_dds(image)`：每像素解成单位向量（零长度 → 正上），逐级 2×2 盒滤波（与 DXT1 路径 `texfile._half` 同样的取样足迹，
  奇数边重复末行 / 列）后**重新归一化**，每级量化成 8 位 R / G（`x·127.5 + 127.5`），各自编一个 BC4 块。头与原版逐字节一致
  （`tests/test_bc5_normals.py` 拿 V601 / V603 / V505 的原版文件比对前 128 字节和总长度）。
- BC4 块：只用 8 值模式（e0 > e1；整块同值时 e0 = e1、索引全 0）。端点：块内 max / min；平方误差超过 `REFINE_ERROR = 4`（RMS 半级）
  的块再做两轮最小二乘重拟合（按当前索引解 v ≈ (1−w)e0 + w·e1）与最优解两端各 ±1 的 8 个邻居（无畏舰 97% 的块不需要，BMPT 约 62% 需要）；8 值调色板等距，所以最近索引是取整而不是搜索。确定性（无随机、无并行差异）。
- `texfile.solid_bc5(xy, size)`：纯 Python 的平法线（与编码器对均匀图的输出逐字节相同，有测试）。`FLAT_NORMAL_XY = (128, 128)`
  （0.004，8 位里 0 没有精确码）。以前的平法线 DXT1 (128,128,255) 解出来是 R 132 / G 129，实际是歪的。
- 辅助：`decode_bc4 / decode_bc5 / decode_dxt1`（测试与对比用），`green_down_score`、`directx_normals`（§4）。

### 误差（顶层，与源单位法线的夹角，度）

| 贴图 | 尺寸 | BC5 平均 / p99 | DXT1 平均 / p99 |
|---|---|---|---|
| WT BMPT 车体 `bmpt_terminator_body_n_n.jpg` | 2048 | 0.67 / 3.8 | 3.43 / 19.7 |
| WT M163 车体 | 2048 | 0.48 / 3.7 | 2.56 / 18.7 |
| 无畏舰 ID01（新 2048 PNG） | 2048 | 0.12 / 1.7 | 2.23 / 18.5 |
| 天启 m0（新增的法线） | 1024 | 0.46 / 2.6 | 2.71 / 16.7 |
| X-Morph 中型坦克 | 256 | 0.75 / 3.1 | 6.27 / 33.4 |

（max 列的几十度来自源里 z < 0 或零长度的坏像素：shader 只能重建 z ≥ 0，任何格式都表示不了。）
编码速度（单线程 numpy，机器同时有别的负载）：2048² BC5 约 0.8–2.6 秒，DXT1 约 0.5–1.0 秒。对比图（光照着色，左源 / 中旧 / 右新）在会话 scratchpad：
`cmp_dreadnought.png`（无畏舰 1024 DXT1 → 2048 BC5）、`cmp_bmpt.png`（同尺寸 DXT1 → BC5）：DXT1 的 4×4 色块与条带在 BC5 里消失。

## 3. 切到 BC5 的调用点

- `pylib/wt_tracked.py` `material_textures`：有法线 → `_normal_dds`（BC5），没有 → `solid_bc5()`。WT 履带车、X-Morph、
  预制炮台（`emplacement_model` 经 `wt.Spec`）、无畏舰炮台（`emp_mid/short/long`）都走这里。
- `pylib/dreadnought_model.py`：法线 `bc5_normal_file`，颜色 / 参数改用 numpy `texfile_accel.dxt1_dds`（输出与纯 Python 版逐字节相同，
  原有测试保证）。
- 平法线：`artillery_model`、`cockpits`、`drill_model`、`sazabi_model` → `solid_bc5`。
- 不涉及：喷气机 / procmesh / tracked_combat 复用原版宿主贴图，不重编码。

## 4. 绿通道方向（H：数据测量）

方法一（相对符号，`texfile_accel.green_down_score`）：高度场的法线是梯度，旋度为 0。X = R、Y = G 时，+G 指贴图下方（DirectX）则
corr(∂X/∂行, ∂Y/∂列) > 0，+G 指上方（OpenGL）则 < 0。方法二（绝对符号，只作旁证）：把法线积分成高度，高通后与颜色亮度求相关
（凹处更暗）。

| 来源 | 方法一 | 方法二（DX / GL） | 结论 |
|---|---|---|---|
| 原版 V601 / V603 / V505 / V614 body、V601 kyata（BC5U） | +0.41 … +0.88 | V601：+0.22 / −0.04 | DirectX |
| 原版 V515、V506 benefits | +0.87、+0.90 | | DirectX |
| WT（BMPT、M163 全部法线，Sketchfab JPEG） | +0.18 … +0.40 | 弱 | DirectX，原样 |
| 无畏舰（UE 导出） | +0.49、+0.52 | +0.15 / −0.03 | DirectX，原样 |
| X-Morph 中型坦克 3 种 | −0.66 … −0.87 | +0.02 / **+0.63** | **OpenGL → 取反 Y** |
| 天启 glb（14 张） | −0.12 … −0.40 | −0.06 / +0.15 | **OpenGL → 取反 Y** |
| Flak 88 glb（4 张）、GDF-005 OBJ | +0.10 … +0.31 | +0.15 … +0.20 / ~0 | DirectX（glTF 规范说 +Y 上，下载件不守） |

网格侧：原版载具网格的 binormal 100% 与 dP/dv 同向（v 按存储，左上原点）、tangent 与 dP/du 同向；我们的 `tangent_frames`
（wt_tracked / obj_model）同样是 T = dP/du、B = dP/dv，UV 都转成左上原点。所以游戏要的是 DirectX 绿，与原版贴图一致。
修正：`xm_model.normal_rgb` 取反 Y；`glb_to_wt_obj.Source.normal_green` / `prep_glb_model.Model.normal_green` 逐模型声明，
`texfile_accel.directx_normals` 用方法一核对声明，明显相反（< −0.05）直接报错，不靠猜。
旋度测试不能区分「Y 反」和「X 反」；X = +u（向右）是 DirectX / OpenGL 共同的约定，方法二在 X-Morph、天启上也支持「Y 反」。

## 5. prep 工具里的自限分辨率 / 有损步骤

| prep 工具 | 以前 | 现在 | 是否重新 prep |
|---|---|---|---|
| `prep_dreadnought.py` | `TEX = 1024`：2048 源减半、512 源放大 | 每张图取其源图中最大的原生尺寸，不再重采样到更大（df：diffuse / colour mask / AO；nm：法线；rml：mask / AO） | 是，`D:\APP\edf6-models\dreadnought` 的 PNG |
| `prep_xmorph.py`（`xm_model.normal_rgb`） | 绿通道 OpenGL 原样 | 取反 Y | 是，只换 3 张 `*_normal.png`（其余文件逐字节相同） |
| `prep_glb_model.py`（预制炮台） | `MAX_TEXTURE = 1024`；丢掉源里的法线 | 原生尺寸；有法线就写 `<name>_normal.<ext>` 并记入 model.json（DirectX 源原字节写出，OpenGL 源翻转后存 PNG） | 是：`emp_gdf005`（颜色 2048、法线）、`emp_flak88`（4 张法线）；其余 4 个源 ≤ 1024 且无法线，输出不变 |
| `glb_to_wt_obj.py`（天启、TOS-1） | `MAX_TEXTURE = 2048`；颜色 JPEG 解码后 q90 再存（二次有损）；丢掉法线 | JPEG 原字节写出；非 JPEG 仍存 q90；有法线写 PNG（DirectX）+ `map_normal` | 是：`wt_apocalypse`（14 张颜色原字节、14 张法线）、`wt_tos1`（2 张颜色原字节） |
| `prep_wt_tracked.py`（BMPT、M163、2S6、PzH、Sturmtiger） | 源 JPEG 原样复制 | 不变 | 否（安装器编码） |
| `prep_cockpit_*`（座舱） | 上限 1024 / 4096 | 不变 | 否：所有源图 ≤ 1024，上限从未触发；座舱法线都是平的 |
| `prep_cockpit_mech.py` | 照片裁剪后缩到布局尺寸 | 不变 | 否（尺寸是贴图布局决定的，源是 Commons 1280 px 版本） |
| 沙扎比 / 双联坦克 / 钻地坦克 | 无 prep 编码（沙扎比是调色板图；另两个文件夹手工放置，安装器编码） | — | 否 |

所有改动的仓库模型文件夹都先用当前代码复现了一遍（与已提交文件逐字节相同），再改代码重跑：`model.npz` 未变，diff 只有贴图与
model.json 里新增的 `normal` 键。

**已知遗留**：`D:\APP\edf6-models\dreadnought\dreadnought.json` 与 HEAD 的 `prep_dreadnought.py` 输出不一致（`bay_*_out` 标记，
a318b6cd「finer collision grid」之后没重跑）。这次只替换了 PNG、保留旧 json，没有顺带改它；要同步需要单独决定。

## 6. 体积与安装耗时

（离线构建：`build()` 在内存里生成，不写游戏目录；before = HEAD 代码 + 旧模型文件夹，after = 本工作区。
网格打包同时被另一项工作修改，所以「档案」列含网格变化，「贴图」列只算 DDS 成员。）

| 组 | 贴图变化 | 贴图字节 前 → 后 (MB) | 档案 前 → 后 (MB) | 贴图编码 前 → 后 (秒) |
|---|---|---|---|---|
| 无畏舰（5 个档案合计，`make_dreadnought --out`） | 30 张 DXT1 1024 → 法线 BC5（2048×2、1024×8），颜色 / 参数按源原生 2048 / 1024 / 512 | — | 16.2 → 30.7 | 整个构建 3 分 26 秒 → 1 分 0 秒（纯 Python DXT1 → numpy） |
| emp_mid / short / long（无畏舰炮台） | 引用无畏舰船体贴图：1024 DXT1 → 2048 | 2.5 → 9.5 | 2.7 → 9.8 | 0.7 → 2.1 |
| vulcan（M163） | 法线 DXT1 → BC5（2048×2、1024×2、512×2） | 12.3 → 20.2 | 16.2 → 26.2 | 2.3 → 4.6 |
| quadvulcan | 同 M163 | 12.3 → 20.2 | 16.5 → 26.6 | 2.2 → 4.4 |
| bmpt | 法线 → BC5（2048×3、1024×6、512×1） | 18.4 → 30.4 | 33.3 → 53.3 | 3.6 → 7.5 |
| tunguska | 法线 → BC5（2048×3、1024×4） | 16.9 → 27.8 | 24.6 → 39.7 | 4.2 → 6.8 |
| pzh2000 | 法线 → BC5（2048×3、1024×4、512×3） | 12.3 → 19.7 | 20.4 → 31.7 | 3.3 → 5.7 |
| sturmtiger | 法线 → BC5（2048×1、1024×4、512×1） | 7.1 → 11.8 | 12.2 → 19.9 | 1.5 → 2.6 |
| apocalypse | 新增 19 张 BC5 1024 法线（以前平法线）；颜色不再二次 JPEG | 11.1 → 39.2 | 19.6 → 51.8 | 2.1 → 12.0 |
| tos1 | 颜色原字节（无法线） | 0.7 → 0.7 | 0.9 → 1.0 | 0.1 → 0.2 |
| flak88 | 新增 4 张 BC5 1024 法线 | 1.7 → 4.9 | 3.0 → 6.9 | 0.5 → 1.8 |
| gdf005 | 颜色 1024 → 2048，新增 BC5 2048 法线 | 0.7 → 8.2 | 1.5 → 9.6 | 0.1 → 2.3 |
| ciws_phalanx / laser / railgun / patriot | 只有平法线变 BC5 | 不变 | 档案变化来自网格 | ≤ 0.4 |
| artillery / drill / sazabi | 只有平法线变 BC5 | 不变 | 档案变化来自网格 | 不变 |
| xm_medium_tank_regular | 3 张 256 法线 DXT1 → BC5（绿通道翻转） | 0.3 → 0.4 | 0.6 → 0.8 | 0.1 → 0.1 |

before 的 X-Morph 用本工作区代码把 BC5 换回 DXT1、旧法线 PNG 构建（HEAD 没有 X-Morph）。
模型包（`RELEASE_MODELS` 全部文件夹）：约 205 MB / 416 个文件 → 245 MB / 435 个文件，内容哈希改变（版本号会变）；
主要增量：`wt_apocalypse/textures` 2.9 → 26 MB（14 张 1024 PNG 法线 + 原字节颜色）、无畏舰 29 → 34 MB、`emp_gdf005` 1.4 → 4.3 MB、
`emp_flak88` 3.4 → 5.3 MB。安装时贴图编码合计约多 30 秒（单线程）。

## 7. 未实机验证

- BC5 法线在载具 shader 上的实际观感（格式与原版逐字节同构，原版就这么用，风险低；但没进游戏看）。
- 绿通道：数据测量 + 网格副法线与原版一致，未在游戏里用灯光确认凹凸方向。
- 无畏舰 2048 贴图的显存 / 加载时间（档案大小在原版范围内：原版最大载具档案 179 MB）。
- 新增法线的炮台（Flak 88、GDF-005）与天启：以前是平法线，现在第一次有凹凸。

## 8. 细节层（只记结论，未实现）

DX11.cpk 170 个 shader 没有任何 detail / tiling 参数，**没有细节法线**。唯一的第二套 UV 平铺层是 `SND_BRDF_MAP_BUILD_*` 的 `dirt`
（只混颜色：`lerp(albedo, dirt.rgb, dirt.a·…)`，单权重蒙皮 technique 与我们的网格布局一致；原版 `SHELTERDOOR` 的
`snd_BRDF_Map_Build_NoOcc` 在可动物件上用过）。可作为后续：把无畏舰的 `TilingDirt 30` 映射成 Map_Build 污渍层，需要实机确认
Map shader 在载具上的系统常量、阴影与远景 pass；`DetailTiling`（细节法线）只能烘进更高分辨率的法线 / 颜色图。
