# X-Morph 重做补完：非履带单位的材质、自发光、法线（2026-11-21）

分支 `feat/1121-xm-materials`。接 2026-10-30 的「重做」（[xmorph-port.md](xmorph-port.md)「重做（2026-10-30）」只做了 7 履带 + 2 炮台）
和 [model-quality-1110.md](model-quality-1110.md) 第 1、2、7 条的「没做」。结论标可信度：H = 读代码 / 数据直接得出，M = 强推断，U = 估计。
全部离线：没有启动游戏，「实机效果」都还要实机看一次。

## 1. 重做在非履带单位上缺的项（改前）

| 项 | 履带 / 炮台（10-30 已做） | 直升机 5 / 喷气机 4 | 步行机甲 2 / 步兵 | 货车 | 说明 |
|---|---|---|---|---|---|
| 高光图 → 逐像素粗糙度 / 金属度 | 有（`SPECULAR_RM`） | 无：`xm_air_model.ROUGH_METAL` 常量 (150, 70) | 无：`PLAIN_RMO` 常量 (140, 90) | 无：`PLAIN_RMO` | 模型包文件夹里根本没写 `specular` |
| 发光图 → light 材质遮罩 | 有 | 无 | 无 | 无 | 同上，没写 `emissive` |
| 网格自带法线 | 有（`Source.normals`） | 无：按面法线逐顶点平均（无角度阈值） | 无：同左 | `auto_smooth 45°`（与烘焙法线图的法线不一致） | 炮台也是面法线平均 |
| 贴图放大到 1024 | 有 | 有（11-10） | 有（11-10） | **无**（car 文件夹没有 `min_side`） | |
| 旋翼盘（透明） | — | 直接丢弃，连网格和贴图都没留 | — | — | |
| `prep_xmorph.py heli|jet|mech|infantry` | — | **选不到任何单位**（`select` 只认有 `like` 的行，这几族的行都没有 `like`） | 同左 | 正常 | 也就是模型包文件夹没法用现行代码重新生成 |

Boss（美洲虎 `xm_highway_tank`）走履带路径，10-30 已经是逐像素材质 + 发光 + 网格法线，不在本次范围。

## 2. 逆向：light 材质与 X-Morph 的自发光

### EDF6 `snd_BRDF_Common_Light_NoOcc`（H）

`DX11.cpk` 的 `SHADER/DX11/SND_BRDF_COMMON_LIGHT_NOOCC.SGO`（sha256 `a33f353b…ff917dd`），`Deferred_Skinning1` 像素着色器用
`d3dcompiler_47!D3DDisassemble` 反汇编（复用 `tests/edf41_material_shader_contract_audit.py` 的 `disassemble`）。`param_r_m_light_hr`
以 `.zxyw` 取样，即 `r1 = (B, R, G, A)`：

```
m       = saturate(B)                          ; 发光遮罩 = 参数图 B 通道，0 = 不发光
a       = albedo.rgb * diffuse                 ; cb2[0]
o0.rgb  = (1 - m) * a                          ; 漫反射基色：发光处按遮罩扣掉
o6.rgb  = m * a * light_color                  ; 自发光输出（cb2[2]，float3，可以是彩色）
o3.x    = G * metallic                         ; 金属度
o3.y    = A * R * roughness                    ; 粗糙度（A：DXT1 不透明为 1）
o1.rgb  = (1 - m) * lerp(specular_color, a * specular_color, o3.x * specular_diffuse_blend)
o2.xyz  = normalize(法线)                      ; NoOcc：没有遮蔽
```

对照 `SND_BRDF_COMMON_BASIC.SGO`（`v603_body` 等普通车身）：同样 R 粗糙度、G 金属度，但 B 是遮蔽（`o2.xyz = 法线 × B`），没有 o6。

所以：遮罩在 B 通道（0 不亮，255 全亮）；**发光颜色 = albedo × diffuse × light_color**，`light_color` 是 float3；全亮处漫反射 / 高光被扣成 0（纯发光体）；
light 材质没有遮蔽（NoOcc）。与 `docs/gundam-plan.md` 第 2 节的旧结论一致，本次补了 B/R/G 取样顺序、`(1 - m)` 扣除和 o3 的来源。

材质参数（读 Root.cpk）：复古巴拉姆 `light`（`V515_RETROBALAM.MRAB`，我们的 LIGHT 模板）`metallic`/`roughness`/`normal_amount` 都是 1、
`specular_diffuse_blend` 0.5、`light_color` (0, 5, 0.1)；V603 自己也有一个 `v603_light`（同一着色器，`light_color` (0.43, 0.42, 0.47)，
`specular_color` 0.137）。原版 `light_color` 取值范围约 0.43–30。

### X-Morph 的发光与高光（H）

`packs` 里的 `materials/programs/deferred_gbuffer_fp.hlsl`（单位材质 `base/default_enemy_outline : base/default`，`USE_EMISSIVE_MAP`）：

```
emissiveColor = diffuse.rgb * cGlowAmount * cGlowFactor * emissiveMap.r   ; cGlowFactor = 1.0, cGlowAmount = surface_glow
NormalSpecGloss.z = specularMap.r                                         ; 高光强度逐像素
NormalSpecGloss.w = encodeGloss(cGloss)                                   ; 光泽度每材质一个常量（surface_shininess，base/default 60）
```

X-Morph 的发光也是「发光图 × albedo × 系数」，和 EDF 的 light 材质同构，所以 **遮罩 = 发光图、颜色 = albedo 是精确对应（H）**。系数
`surface_glow` 是 Ogre 的自定义自动参数：完好单位的材质不写 `glow`（只有 `_dest_0` 被毁材质写 `glow 45`），默认值在 X-Morph 的 exe 里，
本次没有逆向 → **亮度仍是估计（U）**：`wt_tracked.LIGHT_STRENGTH = 2.0`（每单位 albedo 的发光）。
X-Morph 的高光图是「高光强度」，不是粗糙度 / 金属度；`SPECULAR_RM` 的映射（0 → 粗糙度 215 / 金属度 20，255 → 95 / 170）仍是 10-30 按观感定的（U），
本次按任务要求所有家族用同一条。

细节差异（M）：X-Morph 发光处仍保留受光的漫反射（发光叠加），EDF 的 light 材质按遮罩扣掉漫反射。遮罩 1 的纯灯带两边一样；
中型直升机的发光图 95% 的像素是 19（底灰），在 EDF 里整机漫反射被扣 7.5%、加 0.15×albedo 的自发光——这是原作数据的忠实换算，实机看是否发灰。

## 3. 契约（改后）

- model.json 材质键（`pylib/wt_tracked.py` 文件头）：`specular`（灰度高光图 → R/G）、`roughness_metal`（R/G 原样）、`emissive`（灰度遮罩，
  配 `template: "light"`）、**`light`：每单位 albedo 的发光**（数，或 `[r, g, b]`，缺省 `LIGHT_STRENGTH`）。`wt_tracked._lit` 把
  `light_color = light / 宿主模板的 diffuse`，于是 o6 = 遮罩 × albedo × `light`，和用哪个宿主的 diffuse 无关。
  **语义变化**：以前 `light` 直接写进 `light_color`，履带车 2.0 → 现在 light_color 2.374（v603_body diffuse 0.8425），即实际发光强度从 1.685 变为 2.0×albedo。
- `wt_tracked.add_surface_material(game, md, host, body, m, slots)`：light 材质 → 复古巴拉姆 `light` 材质 + 宿主 `body` 材质的
  diffuse / specular_color + 上面的 light_color，顶点布局取巴拉姆 light 网格；其余 → 宿主 `body` 材质。履带（V603 `v603_body`）、
  飞机（V505 `v505_tank`）、步行（V603 `v603_body`）、货车（V512 `KT01L`）四条路径共用。
- light 材质的参数图（`material_textures`）：R/G 来自 `specular`（`SPECULAR_RM`）或 `roughness_metal`，B = `emissive`。
- `prep_xmorph.py`：`unit_material`（diffuse / normal / specular / emissive + `min_side`，发光图最大值 > `LIT`=16 才换 light 模板）与
  `part_arrays`（网格自带法线）给飞机、步行、步兵、货车、炮台共用；`select` 对非 V603 家族取 `status == 'ported'`。
- 旋翼盘：`prepare_air` 把 `unlit_transparent` 材质写成 `{"transparent": {"blend": "alpha", "frames": [textures/<k>_frame<i>.png…], "frame_time": s}}`，
  网格照常作为部件写进 model.npz（挂在主旋翼 / 尾旋翼骨骼上）；帧来自 `<贴图>.txpk`（`xm_model.txpk_frames`：`'TxPk'`、u16 帧数、u32 每帧字节、
  帧首尾相接，每帧一个 DXT5 DDS；实测小 / 中型直升机各 5 帧 256²），解成 RGBA PNG（alpha 即旋翼覆盖）。`xm_air_model._parts` / `build_model`
  跳过 transparent 材质（不绘制，与以前丢弃时外观相同）。接 alpha 接口时只要在 `build_model` 里给它选原版 Alpha 宿主材质。
- 飞机比例：`xm_units.AIR_SCALE = 1.0`（见第 6 节风险）。
- glTF（`pylib/gltf.py`、`tools/prep_glb_model.py`）：`emissiveFactor × KHR_materials_emissive_strength`；有发光的材质写
  `template: light`、整面遮罩 `<name>_emissive.png`、`light = 发光 / albedo 线性均值`（逐通道）。albedo 在某通道为 0 却要在该通道发光、
  或带 `emissiveTexture`（颜色不一定是 albedo 的）→ 报错拒绝：现有源里没有这两种（H：逐个读了 6 个 emp 源的材质 JSON，只有激光炮台 phong4 发光）。

## 4. 前后对比

### 参数图（模型包重新 prep 后，高光图按 `SPECULAR_RM` 换算的 R / G 均值 / 标准差；改前全图常数、标准差 0）

| 单位 | 改前 R/G | 改后 R | 改后 G | light 模板 | 发光像素（>16） |
|---|---|---|---|---|---|
| 无人机 small_helicopter_core | 150/70（ROUGH_METAL） | 139/25 | 114/31 | 是 | 14.1% |
| 中型直升机 medium_helicopter_core | 150/70 | 196/28 | 44/34 | 是 | 96.3%（底灰 19） |
| 重型直升机 large_helicopter_core | 150/70 | 182/36 | 61/45 | 是 | 6.6% |
| 运输直升机 large_helicopter_transport | 150/70 | 181/36 | 62/45 | 是 | 3.4% |
| 炮艇 large_gunship_core | 150/70 | 169/34 | 77/43 | 否（原作没有发光图） | — |
| 战斗机 medium_fighter_aa | 150/70 | 176/28 | 69/35 | 是 | 5.6% |
| 攻击机 medium_gunship_aa | 150/70 | 174/40 | 71/50 | 是 | 16.1% |
| 重型攻击机 large_gunship_aa | 150/70 | 193/25 | 47/31 | 是 | 4.7% |
| 重型轰炸机 large_bomber_core | 150/70 | 170/31 | 76/38 | 是 | 6.7% |
| 轻型机甲 small_mech | 140/90（PLAIN_RMO） | 174/32 | 71/40 | 是 | 14.8% |
| 主宰者 large_mech | 140/90 | 158/30 | 91/38 | 是 | 5.4% |
| 步兵 small_infantry | 140/90 | 149/46 | 102/57 | 是 | 29.9% |
| 货车 medium_truck_cargo | 140/90 | 179/29 | 66/36 | 是 | 9.5% |
| 防空炮台 aa_cannon(_machinegun) | （10-30 已是逐像素） | 178/32 | 66/40 | 是 | 14.2% |

生成后的材质（读回 MDB，`tmp/` 脚本）：无人机 `snd_BRDF_Common_Light_NoOcc`、diffuse 0.6 / specular 0.4（V505 的）、light_color 3.333；
炮艇 `snd_BRDF_Common_Basic`；货车 light（KT01L 的 diffuse 1.0）light_color 2.0；步行三台 light、light_color 2.374；
激光炮台 `edf6vc_laser_4` light、light_color (11.87, 0, 0)（= 10 / 0.8425），其余四个材质照旧 Basic。

### 法线（与改前模型包文件夹同一三角形、同一角点顺序比较；改前是按面平均 / 货车 auto_smooth）

| 单位 | 偏差 >1° | >15° | p99 |
|---|---|---|---|
| 无人机 | 61.1% | 10.9% | 90.0° |
| 中型直升机 | 57.9% | 32.6% | 49.6° |
| 重型直升机 | 55.7% | 31.5% | 55.9° |
| 运输直升机 | 43.7% | 19.5% | 41.8° |
| 炮艇 | 75.0% | 12.9% | 31.8° |
| 战斗机 / 攻击机 / 重型攻击机 / 轰炸机 | 54.1 / 38.1 / 44.8 / 40.1% | 23.1 / 6.1 / 24.9 / 8.1% | 42.4 / 54.8 / 47.5 / 37.7° |
| 货车（auto_smooth 45° → 自带） | 25.0% | 19.5% | 76.0° |
| 防空炮台 | 19.2% | 2.9% | 32.1° |

位置、UV、三角形逐个相同（只换法线）；步行三台的文件夹比例从 1.0 变成 0.7（见下），但 `xm_rig` 按宿主骨骼高度重新缩放，
用新旧文件夹生成的模型包围盒、挂点、骨骼映射逐个相同（最大差 0.0 m），只有材质和法线变。

## 5. 产物

- 模型包重新 prep（`python tools/prep_xmorph.py heli|jet|mech|infantry|car|emplacement --out D:\APP\edf6-models`），原文件夹先整份备份到
  `D:\APP\edf6-models\_backup-1121\<文件夹>`：`xm_small_helicopter_core`、`xm_medium_helicopter_core`、`xm_large_helicopter_core`、
  `xm_large_helicopter_transport`、`xm_large_gunship_core`、`xm_medium_fighter_aa`、`xm_medium_gunship_aa`、`xm_large_gunship_aa`、
  `xm_large_bomber_core`、`xm_small_mech`、`xm_large_mech`、`xm_small_infantry`、`xm_medium_truck_cargo`、`xm_aa_cannon`、
  `xm_aa_cannon_machinegun`（15 个）。7 个履带文件夹与 `xm_highway_tank` 没动。模型包版本（`pylib/modelpack.py` 内容哈希）随之变化，需要发新模型包。
- 入库：`models/emp_laser`（`model.json` 的 `edf6vc_laser_4` 改 light + `laser_4_emissive.png`；`model.npz` 与其余 PNG 重新生成后逐字节相同）。
  其余 5 个 emp_* 源没有发光，不重新 prep。
- 验证：15 个单位逐个 `EDF6VC_MODELS=<新文件夹> python tools/make_xmorph.py --kind <单位> --check`、`python tools/make_tracked_combat.py --kind laser --check` 全过。

## 6. 没做 / 风险

- **亮度未实机**：X-Morph 的 `surface_glow` 默认值没逆向（U），`LIGHT_STRENGTH` 2.0 仍是估计；激光炮台 glTF 的 10 倍发光搬成每单位 albedo 10，
  原版 light_color 上限约 30，泛光强度要实机看。
- **light 语义改变**：已有履带 X-Morph 单位的实际发光从 1.685×albedo 变 2.0×albedo（+19%），重建时自动生效。
- **飞机比例 1.0 与地面 0.7 不一致**：飞机分支按「比例 1.0 照原作」做、模型包和直升机 `eye` 都按 1.0 量；但合并后的 `prepare_air` 读的是
  `SCALE`（0.7），用现行代码重新 prep 会把飞机缩小 30% 而 `xm_heli._camera` 的 `eye` 不跟着缩。本次显式写 `xm_units.AIR_SCALE = 1.0`
  保持已发布的尺寸。要不要统一成 0.7（战斗机会只有 EDF 战机的约四分之一）由维护者决定；统一时 `xm_heli._camera` 要改读 `xm_units.eye()`。
- 旋翼盘透明：只保留网格和帧（`transparent`），没有绘制，等 alpha 接口（下一阶段）。
- `SPECULAR_RM` 本身仍是观感估计（X-Morph 的高光图是强度、光泽度是每材质常数 60）。
- light 材质没有遮蔽（NoOcc）：发光单位的材质失去 B 通道遮蔽，这与 10-30 履带一致（X-Morph 单位本来就没有 AO 图）。
- [xmorph-names.md](xmorph-names.md)「4. 待办」1、2 没做，证据缺口：
  - **无机炮喷气机**：`vcobjects.XM_LOADS` 要求机炮对，`src/jet_combat.cpp` / `src/stores.cpp` 把前两个挂载位当机炮、HUD 显示机炮键、
    NPC 飞行员走机炮攻击分支；改成可空要动 native 挂载布局与 HUD，离线只能编译，飞行员行为与 HUD 必须实机验证。
  - **无武器运输直升机**：扫描 Root.cpk 全部 `Vehicle506_Helicopter` SGO（`V506_HELI`、`V506_HELI_EDF6BENEFITS`）都是 3 把武器 + 油箱，
    没有一个少于 3 把的原版先例（`VehicleHelicopter410` 只有 1 把，但是另一个类）。V506 的开火输入是 `veh+0x2020`（两门机炮）/ `+0x2021`
    （导弹）由 slot 57 消费（`src/heli.cpp` 注释、`docs/heli-input-re.md`），武器数组变短或换占位武器后 slot 57 / HUD / `src/heli.cpp`
    的 Loadout 是否越界没有逆向，证据仍是 L，所以没改。
- 全部未实机：发光、粗糙度 / 金属度、网格法线下的明暗都只做了离线核对（MDB 读回、`make_xmorph --check`）。
