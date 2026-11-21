# 外部模型的透明 / 镂空材质（2026-11-21）

接 [model-quality-1110.md](model-quality-1110.md) 第 5 条。改前：伪装网（BMPT / 2S6 / M163 / PzH 2000 / Sturmtiger 的 `net_f` / `net_b`）、车灯 / 观瞄 / 侧窗玻璃、履带镂空、机枪弹链与 PzH 2000 的格栅装甲全部按不透明画（黑底方块、实心履带带）。

本次全部是离线工作：反汇编 DX11.cpk 着色器、静态读 EDF.dll、只读扫 Root.cpk 原版 MDB、离线生成并解码产物。**没有启动游戏**，排序、阴影和观感都还要实机看（见末节）。

可信度：H = 指令 / 数据直接证明；M = 有直接证据但没覆盖全部路径；U = 推测，待实机。

## 1. 原版着色器与材质（逆向结论）

着色器从本机 `DX11.cpk` 的 `SHADER/DX11/*.SGO` 取出，按 technique 拆出 DXBC，用 `d3dcompiler_47!D3DDisassemble` 反汇编（`tests/edf41_material_shader_contract_audit.disassemble`）。原版材质从 `Root.cpk` 的 OBJECT / WEAPON 543 个归档、2097 个材质只读扫描。

### 1.1 `alpha_clip`（Clip 一族与 `snd_BRDF_Mech_Catapillar`）

- **阈值方向与通道（H）**：`discard` 当 `albedo.a - alpha_clip < 0`，即保留 `A ≥ alpha_clip`。取的是 albedo 贴图的 **A 通道**（`sample ... t0.wxyz` 取 `.x` = A，或 `t0.xyzw` 后用 `.w`），**不乘** `diffuse`。实例：
  - `SND_BRDF_CHARA_2COLORCHANGE_CLIP` Deferred PS：`sample r0.xyzw, v6.xy, t0` / `add r0.w, r0.w, -cb2[3].w` / `lt r0.w, r0.w, 0` / `discard_nz r0.w`（`alpha_clip` 在 cb2 偏移 60）。
  - `SND_BRDF_MECH_CATAPILLAR` Deferred / Forward / ZPass PS 同样的三条，`alpha_clip` 在偏移 56（`cb2[3].z`）。原版 V603 / V601 / V505 等履带 `alpha_clip = 0`（`A ≥ 0` 恒真，等于不裁）。
  - `SND_BRDF_COMMON_SIMPLE_CLIP` 是固定阈值 `albedo.a × fall-off - 0.5`，没有 `alpha_clip` 参数、也没有法线 / 参数图，不适合做车体宿主。
- **进不进阴影 / Z pass（H 着色器层面，U 引擎层面）**：Clip 族的 `ZPass` technique 的 PS 也采样 albedo 并做同一个 discard（`2COLORCHANGE_CLIP` ZPass PS `a5e9e544753e`、Catapillar ZPass PS `a3863b7d2f9f`），ZPass VS 会输出 UV（Catapillar 的 ZPass VS 也读 `scroll_texture`，滚动后的 UV 与 Deferred 一致）。不透明 `Common_Basic` / `Common_Alpha` 的 ZPass PS 是常量 `mov o0, (1,0,0,1)`。所以只要引擎用 ZPass 画深度 / 阴影，镂空就会在深度和阴影里一并挖掉。ZPass 是否就是阴影 pass 没在 EDF.dll 里追到（U）。
- **Catapillar 的 albedo 通道互换（H，新发现）**：`snd_BRDF_Mech_Catapillar` 的 Deferred 与 Forward PS 都是 `mul r0.xyz, r0.xzyx, cb2[0].xyzx`，即输出颜色 = `(albedo.r, albedo.b, albedo.g) × diffuse`，**G 与 B 互换**。原版履带贴图接近灰色（V603：均值 26.8 / 25.0 / 24.4），看不出来；我们的 WT 履带偏棕（G−B 平均差 2–7 / 255），会微微偏品红。现在履带 albedo 编码前先互换 G / B（`wt_tracked.albedo_dds(swap_gb=True)`），显示出来就是源颜色。

### 1.2 `snd_BRDF_Common_Alpha`（半透明）

- **透明度公式（H）**：Deferred PS 末段：`sample r1, t0` / `mul r1.xyzw, r1.xyzw, cb2[0].xyzw` / `mul_sat r0.w, fall_off, r1.w`，即
  `alpha = saturate(albedo.a × diffuse.w × saturate((1 − (N·V)^max(amount, 0.01)) × scale + offset))`。
  所有 5 个 G-buffer 输出的 `.w` 都写这个 alpha；Forward PS 的 `o0.w = 雾衰减 × alpha`。`diffuse` 是 float4（类型 0x403），其余参数与 `Common_Basic` 同位。
- **顶点着色器（H）**：`Common_Alpha` 的 Deferred / Forward 及 Skinning1–4 VS 与 `Common_Basic` 是**同一份 DXBC**（哈希 `4566468c862b` / `e24da64be6d6` / …），所以可以直接沿用 `v603_body` 的顶点布局（vsize 60，half4 位置、BLENDWEIGHT / BLENDINDICES）。ZPass 也与 Basic 相同（只读 POSITION，PS 为常量）。
- **材质头 b3（H）**：MDB 材质记录 `<HBB…>` 的第 3 字节（`b3`）在材质构造 `EDF+0x110F110` 处拷到材质对象 `+0x13`（`0x110F1FF`），`b2` 拷到 `+0x12`。渲染项注册时 `EDF+0x11034DF` `cmp byte [mat+0x13], 2`：等于 2 的放进列表 `+0xC8`，否则 `+0xA8`；`EDF+0x10FEF88` 同样按 `b3 == 2` 另入一个列表。模型初始化 `0x6BDC82` / `0x6BDCA2` 查这两个列表是否非空：不透明列表非空 → 置渲染标志位 28 与 30（`0x11B3450` / `0x11B3490`），半透明列表非空 → 只置位 29（`0x11B34C0`）。即 **b3 = 2 的材质不进不透明（大概率也是阴影）那两层，单独走半透明层**（H：分桶；M：位 30 是阴影层；U：半透明层内是否按深度排序、用 Forward 还是 Deferred technique）。
- **b3 的取值（H，数据）**：全仓 2097 个材质 b3 只有 0（1917）、2（174）、1（6）。b3 = 2 全是 `*_Alpha` / 护盾 / BeamShield 类；Clip 一族（`2ColorChange_Clip`、`hair_Clip`、`Simple_Clip`、`Decoy_damage_clip`）全部 b3 = 0；b3 = 1 只出现在 4.1 移植车 `VEHICLE402_ROCKET` / `VEHICLE403_TANK` / `V402_ROCKET_EDF6BENEFITS` 的镂空履带（Catapillar，`alpha_clip` 0.2 / 0.5，albedo DXT5）。EDF.dll 里没有找到与 1 比较的读点（只在 `0x1118AF0` 当作状态哈希的一个字节），所以 b3 = 1 对镂空不是必需的（M）。
- **b2（U）**：只被拷贝（`0x1110870`、`0x111D04A` 等）和参与 `0x1118AF0` 的哈希，没找到比较。取值 0 / 1 / 2 / 3 / 5 / 6 / 7 / 10 / 20，半透明材质上较常见（卡车车窗 10、摩托 2），也出现在不透明材质上；推测是同层内的绘制顺序偏置，未证实。我们一律沿用宿主的 0。

### 1.3 原版贴图格式与参数（H，数据）

| 用途 | 原版例子 | albedo 格式 | 关键参数 |
|---|---|---|---|
| 车辆玻璃（半透明） | `V613_BIKE / v613_bikeGlassmat`（b2 0, b3 2）、`V512_KEITRUCK / win000B`（b2 10）、`V507_RESCUETANK / light_4` | DXT1（A 恒 1，透明度只靠 `diffuse.w` 0.5）、DXT5（A 211–255） | fall-off：V613 (1, 1, 1) 恒 1；卡车 (1, 1, 0.8) 正视 0.8、掠射 1 |
| 昆虫翅膀 / 蛋囊（半透明） | `GIANTBEE / wing`、`E663_SPIDER_EGG_SAC` | DXT5（A 0–255） | fall-off 多为 (1, 1, 1) |
| 镂空履带 | `VEHICLE403_TANK / tank_catapi_*` | DXT5（A 0 / 255，约 1.7 % < 128） | `alpha_clip` 0.2，b2 5，b3 1 |
| 镂空（角色） | `P601_PROTO_RANGER / dx11Shader3`（全仓唯一的 `2ColorChange_Clip`）、翼装头发 `hair_Clip` | DXT5 | `alpha_clip` 0.2356 / 0.55，b3 0 |

原版没有 DX10 / BC7 的 Alpha 或 Clip albedo（个别 `param_r_m_occ_hr` 是 BC7）；DXT3 只在 4.1 旧素材里出现。所以带 alpha 的颜色图用 **DXT5（BC3）**，与原版同格式。

### 1.4 宿主选择

| model.json | 宿主 | 依据 |
|---|---|---|
| `v603_body` + `mask` | `P601_PROTO_RANGER.MRAB` / `dx11Shader3`（`snd_BRDF_Chara_2ColorChange_Clip`，`wt_tracked.CLIP_HOST`） | EDF6 没有 `Common_Clip`。这个着色器的 Deferred / Forward / Skinning VS 与 `Common_Basic` 同一 DXBC；Deferred PS 在 `param_cm0_cm1` 为黑（0, 0）时颜色 = `albedo × diffuse`，`o1`–`o4` 与 `Common_Basic` 逐条同式（metallic / roughness / normal_amount / specular_diffuse_blend 只是 cbuffer 偏移不同），只多一个 discard（H，Deferred；Forward 只核了结构与 discard，M）。 |
| `v603_body` + `blend` | `V613_BIKE.MRAB` / `v613_bikeGlassmat`（`snd_BRDF_Common_Alpha`，`wt_tracked.ALPHA_HOST`） | EDF6 自己的车辆玻璃，b2 0、b3 2、法线 BC5U，与我们的贴图同格式。 |
| `v603_kyata_L/R` + `mask` | 原生履带材质本身（`snd_BRDF_Mech_Catapillar`） | 同一材质的 `alpha_clip` 置为 cutoff；名字不变，SGO 的履带滚动照旧。4.1 移植履带就是这么做的（只是 b3 = 1，见上）。 |
| 履带 `blend`、发光材质（`light`）的任何 alpha | 拒绝（`WtModelError`） | 原版没有对应宿主。 |

CLIP / ALPHA 材质的 `diffuse`（RGB）、`specular_color`、`specular_diffuse_blend`、`normal_amount`、`metallic`、`roughness` 取 `v603_body` 的值（`wt_tracked.BODY_PARAMS`），所以与同模型的不透明材质同样受光；ALPHA 的 `diffuse.w` = 源的不透明度系数，fall-off 置为 (1, 0, 1)（恒 1，`wt_tracked.FALL_OFF`），透明度完全按源（与 Sketchfab 网页查看器一致）。

## 2. 契约：model.json 的 `alpha`

真相源是 `pylib/alpha_maps.py` 的模块文档。每个材质可选：

```json
"alpha": {"mode": "mask", "cutoff": 0.5, "map": "textures/net_f_c_a.png"}
"alpha": {"mode": "blend", "factor": 0.15, "map": "textures/headlight_glass_c_a.png"}
```

- `mode`：`opaque`（等于省略）/ `mask`（镂空）/ `blend`（半透明）。
- `cutoff`：只用于 `mask`，即宿主的 `alpha_clip`，缺省 0.5。
- `factor`：只用于 `blend`，即 `diffuse.w`，缺省 1。
- `map`：可选的灰度图（相对模型目录），它的值就是 albedo 的 A；省略时用 albedo 文件自己的 A 通道（glTF 的 RGBA PNG）。
- 不认识的键、`mask` 带 factor、`blend` 带 cutoff、`opaque` 带 map 都报错（`alpha_maps.entry`）。

读取侧：

- **WT 的 OBJ / MTL**（`tools/prep_wt_tracked.py`：`alpha_of`）：不透明度 = `d`（或 `1 − Tr`）× `map_d`。`d < 1` → `blend`（WT 的玻璃都是 `d 0.15`）；只有 `map_d` 时按贴图判：A ≥ 240 的纹素 ≥ 99.9 % → 不透明（M163 / BMPT 的 `mg_pkt` 的 map_d 全白），中间灰（16–239）占比 > 25 % → `blend`，否则 `mask`，cutoff 0.5。依据：网 0–12 %、履带 3–7 %、弹链 2.5–4 %、格栅装甲 6–14 % 的灰；玻璃 66–100 %。
- **glTF**（`alpha_maps.of_gltf`，`prep_glb_model` 与 `glb_to_wt_obj` 共用）：`OPAQUE` 忽略 alpha；`MASK` 用 `alphaCutoff`（缺省 0.5）；`BLEND` 时 baseColorFactor 的 A < 1 → `blend`，否则按贴图 A 同上判（Sketchfab 给 WT 模型的所有带 alpha 材质都标 `BLEND`，包括网和履带）。`prep_glb_model` 写 RGBA 的颜色 PNG；`glb_to_wt_obj` 写 `map_d`（`MASK` 先按 cutoff 二值化，回到 prep_wt_tracked 时判成 cutoff 0.5 的 mask，结果相同）和 `d`。
- 同一源材质的透明属性进了 prep 的材质键，所以同贴图、不同透明模式的材质分开（BMPT 的两种 `headlight_glass`）。

生成侧（`pylib/wt_tracked.py`）：

- `template(m)`：材质建在哪个宿主上（`v603_body` / 履带 / `light` / `CLIP` / `ALPHA`）；`layout_template(m)`：网格用哪个原版材质的顶点布局（CLIP / ALPHA → `v603_body`）。
- `albedo_dds(folder, m, side, swap_gb=)`：不透明时与原来同一条 DXT1 路径、字节不变；透明时 DXT5（`texfile_accel.dxt5_dds`），`mask` 每级 mip 按 cutoff 保持顶层覆盖率。
- `see_through_host(game, CLIP|ALPHA)`、`see_through(md, index, m, body=None)`、`clip_textures()`：建材质、写透明参数，供其他建模路径复用。

## 3. 编码：DXT5

`texfile_accel.dxt5_dds(image, cutoff=None)`（独立函数，不碰 `dxt1_dds`，便于和 BC7 分支合并）：每块 = A 的 BC4 块（已有的 `_bc4_blocks`）+ 现有 DXT1 编码器的颜色块。DXT1 编码器输出总是 `color0 ≥ color1`（相等时索引全 0），DXT5 解码器固定按四色模式读，结果一致（测试逐块检查）。

镂空 mip：普通 2×2 盒滤波会让细网格的 A 逐级降到 cutoff 以下，远处网 / 履带变稀直至消失。`cutoff` 给定时，每级 mip 的 A 乘一个系数（二分 20 步，取覆盖率最接近顶层的一侧），使 `A ≥ cutoff` 的占比等于顶层（覆盖率保持 mip，`_coverage_scaled`）。随机网测试：顶层 0.45，盒滤波最低到 0.36，保持后各级与顶层差 < 0.05。`blend` 不做缩放。

## 4. 前后对比

（见下表，离线生成：同一份 Root.cpk，旧 = `origin/main` 的 `wt_tracked.py` / `texfile_accel.py` / `gltf.py`，新 = 本分支。）

| 车 | archive 字节 旧 → 新 | albedo 合计字节 旧 → 新 | 生成秒数 旧 → 新 | 新：材质着色器 | 履带 RGB RMSE 旧 → 新 | 履带 G / B 平均偏差 旧 → 新 |
|---|---|---|---|---|---|---|
| BMPT | 47 205 729 → 47 446 056 | 11 012 656 → 11 580 760 | 19.8 → 24.4 | Basic 11、Alpha 3、Clip 2、Catapillar 2 | 4.0 → 2.5 | −2.3 / +2.4 → 0.0 / +0.1 |
| M163 | 19 541 614 → 19 656 001 | 7 254 096 → 7 472 648 | 6.8 → 11.7 | Basic 5、Alpha 1、Clip 2、Catapillar 2 | 4.6 → 3.8 | −1.8 / +2.1 → +0.2 / +0.1 |
| Tunguska | 30 728 794 → 30 911 037 | 10 006 896 → 10 531 296 | 10.2 → 17.7 | Basic 6、Alpha 2、Clip 2、Catapillar 2 | 5.6 → 3.6 | −4.4 / +4.7 → +0.2 / +0.2 |
| PzH 2000 | 26 442 270 → 28 219 753 | 7 809 752 → 10 024 528 | 8.4 → 13.0 | Basic 8、Alpha 3、Clip 6、Catapillar 2 | 6.2 → 2.7 | −4.3 / +4.6 → +0.2 / +0.2 |
| Sturmtiger | 17 206 606 → 17 276 743 | 4 064 264 → 4 632 328 | 4.8 → 10.1 | Basic 3、Clip 2、Catapillar 2 | 7.4 → 3.2 | −6.3 / +6.5 → +0.1 / +0.1 |

- 旧版全部材质是 Basic / Catapillar + DXT1，没有镂空、没有透明；新版镂空 / 透明材质 albedo 改 DXT5（每像素 1 字节，DXT1 的两倍），archive 增 0.2–6.7 %。
- 履带误差按 Catapillar 着色器实际读法（albedo.xzy）对源图解码比较（0–255）：旧版 G / B 对调造成系统性偏色，新版预先互换后偏差归零，剩下的是压缩误差。
- 生成时间每车多 5–7 秒：DXT5 的 BC4 半块与每级 mip 的覆盖率二分（纯 Python / numpy）。

模型目录：五个入库文件夹重新 prep（`wt_bmpt`、`wt_m163`、`wt_tunguska`、`wt_pzh2000`、`wt_sturmtiger`）。新 prep 结果与入库内容逐数组相同、model.json 除新增 `alpha` 键外逐项相同；每个文件夹多了 3–10 个 `*_c_a.png`（源的 map_d 原样复制）。`wt_apocalypse` / `wt_tos1`（glb → OBJ，无 alpha）和 `emp_*`（glTF 无 alpha、gdf005 的 MTL 无 map_d）不受影响，没有重新 prep。模型包 `D:\APP\edf6-models` 没有改动。

## 5. 测试

- `tests/test_model_quality.py` 的 `Alpha`：分类规则、契约校验、glTF 读取、DXT5 块布局（颜色半块与 DXT1 路径逐字节相同、四色模式、HD / .lod 对合法）、mask mip 覆盖率、宿主选择、albedo 的 alpha 图与履带 G / B 互换、WT MTL 的 `map_d` / `d`。
- `tests/test_wt_tracked.py` 的 `test_see_through_materials_sit_on_the_stock_hosts`（需要游戏文件）：每个带 `alpha` 的材质是 DXT5、在审过的宿主着色器上（ALPHA 的 b3 = 2、diffuse.w = factor、fall-off 恒 1；CLIP / 履带的 alpha_clip = cutoff 且解码后覆盖率与 map 相差 < 3 %），CLIP / ALPHA 的受光参数等于 `v603_body`；其余材质仍是 DXT1、原模板着色器。

## 6. X-Morph 怎么接

X-Morph 旋翼盘（`tools/prep_xmorph.py` 里 `type` 含 `transparent` 的材质现在直接丢弃）不在本分支做。接法：

1. `prep_xmorph` 不再丢弃这些材质：颜色图存成 RGBA PNG（`xm.texture_images` 的 diffuse 带 A），model.json 写 `"alpha": alpha_maps.classify(A, factor)`（或直接 `{"mode": "blend", "factor": …}`），不需要 `map`。
2. 走 `wt_tracked.build_with_info` 的单位（履带 / 步行 / 炮台）自动生效：`template()` 选宿主，`albedo_dds` 出 DXT5。
3. 空中单位走 `pylib/xm_air_model.py`（V505 的 `Common_Basic` 机身宿主，与 `v603_body` 同 VS）：颜色图改调 `wt_tracked.albedo_dds(folder, m, side)`；`kind = wt_tracked.see_through_kind(m)` 非空时用 `wt_tracked.see_through_host(game, kind)` 的材质 `add_material`（CLIP 还要 `wt_tracked.clip_textures()` 的 `param_cm0_cm1`），再 `wt_tracked.see_through(md, mi, m, body=<机身 Common_Basic 材质>)`；网格用机身材质的顶点布局。
4. 旋翼盘是旋转的半透明面：b3 = 2 进半透明层，不进不透明 / 阴影层；层内排序和与机身的前后关系要实机看（见下）。

## 7. 没做 / 待实机

- **实机未验**（全部）：网 / 履带的镂空在 Deferred 与阴影里是否一致、远处 mip 的覆盖率观感；半透明玻璃的层内排序、与不透明件的前后关系、是否进阴影（静态看 b3 = 2 只进半透明层）；ALPHA 宿主在 Forward / Deferred 哪个 technique 画。
- **背面剔除**：源材质都是 doubleSided；网是单层面片，镂空后从背面看会整片消失（1110 第 8 条，没做）。镂空让这个问题更显眼。
- **b2 的含义**未证实（我们沿用 0）。
- WT 玻璃按源是 `d 0.15 × map_d`，平均不透明度只有 0.04–0.15（车灯几乎看不见），这是源的观感，没有调。
- `prep_glb_model` 的 OBJ 读入（`read_obj`，只有 gdf005 用）不读 `map_d` / `d`：现有源没有。
- 履带 G / B 互换会改变所有 WT / 炮台车的履带贴图字节（安装器缓存会随生成器源码重建）。
