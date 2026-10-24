# EDF4.1 移植模型偏白：材质与 shader 契约核验

2026-10-18 调查，2026-10-19 补实现。状态：**已实现 EDF4.1 专属 shader 的基色兼容修复，保留 EDF6 其余 PBR；尚未还原 EDF4.1 的完整反光效果。** 验证只读三代游戏资源，并在独立 WARP 设备校验修改后的着色器；没有启动游戏或写游戏目录。

## 本次落地的窄修

`pylib/edf41materials.py` 从玩家自己的 EDF6 读取已核验的 shader 家族（完整清单见 `SUPPORTED`），生成 `SHADER/DX11/EDF41_<原名>.SGO`，只供 EDF4.1 独享模型引用。原版 shader 名和共享的 EDF5/6 模型不改。

每个 Deferred / Forward PS 只将已审阅的白化 MAD 的第一个乘数（`f`）改为浮点字面量 0，恢复 `b`；Alpha 两类同一指令操作 RGBA，因此同时恢复旧 diffuse 的 RGB 与 alpha，保留其后的饱和、雾、距离透明衰减。反射变量 `f` 的生产和其他使用保持不变，EDF6 的 metallic、roughness、GGX、阴影、点光源、环境光、蒙皮和色彩遮罩继续使用原实现。环境光自然随恢复后的基色重新着色，这不等于旧版反射外貌完全一致。

这个修改不重新编译或重排 SGO：源 SGO 和 PS 均要求精确 SHA256 匹配；按 DXBC 指令边界匹配整条 9-DWORD MAD，只将两个 DWORD 的 TEMP source 换成同宽 immediate32 scalar `[0x4001, 0]`，然后重算 DXBC checksum。所有 VS、输入输出签名、techniques、其他 pass、常量表、资源绑定和元数据位置保持原样。Mech_Catapillar 原本没有 Forward，生成器保留这一形状。

`tools/edf41_materials.py:apply(files, root6, enabled)` 只返回计划文件，不写游戏目录。调用方先校验原始资源登记 hash、排除共享模型，再以本模块转换，并在原有 owner 下记录实际引用的 shader；禁用时重新使用登记的原始 bytes。已存在的 shader 别名必须由 EDF4.1 套件或其已登记的测试场消费者持有、没有用户编辑且与计划 bytes 相同，否则拒绝覆盖；测试场消费者可以在原 writer 卸载后继续持有，重新安装时按相同 bytes 重新接管。

验证命令：

```powershell
python -B tests/test_edf41materials.py -v
```

本机 10 项通过。其中真实资源检查覆盖全部 14 个已登记 SGO、21 个独立 PS：源 checksum 与独立实现一致；改后反汇编每个只差一条 MAD 的首乘数；VS/所有 pass 元数据与形状一致；全部 PS 均被 `ID3D11Device::CreatePixelShader` 的 WARP 设备接受（`S_OK`）。模型与归属测试覆盖纹理/动画/成员顺序保留、别名幂等、禁用返回原始资源，以及外来文件和用户编辑保护。**这些检查不代替游戏内画面验收。**

UFO/Mech/Dino/Common 的发光输出保持原样；Common_Basic Forward 把 RGB 打包在 `r4.xyw`，同一寄存器的 AO 分量 `z` 没有改写。BeamShield、ShieldBearer 等独立特效，以及已换用 EDF6 BRDF 的素材不在别名清单中。

动态别名加载也已核实：材质构造 `EDF+0x110F110` 在 `0x110F238` 从 MDB 材质头 `+8` 取得 UTF-16 shader 名，复制配置 `+0x28` 的目录，依次追加反斜线、完整 shader 名和 `.sgo`，在 `0x110F322` 把结果传给 `0x1148E20` loader。默认目录 `app:\Shader\DX11` 的初始化链是 `0x700F19 → 0x10FFC50 → 0x1110580`；没有固定 shader 名白名单。故 `EDF41_SND_MECH` 会读取 `app:\Shader\DX11\EDF41_SND_MECH.sgo`。

DXBC checksum 使用独立的标准 MD5 压缩实现及 DXBC 专用尾块布局；压缩核心对照 Python 标准库的边界长度样本，DXBC 结果逐一对照原版签名。尾块格式参考 [AMD DXBCChecksum](https://github.com/GPUOpen-Archive/common-src-ShaderUtils/blob/master/DX10/DXBCChecksum.cpp) 的公开算法描述；原版可被微软 [D3DDisassemble](https://learn.microsoft.com/en-us/windows/win32/api/d3dcompiler/nf-d3dcompiler-d3ddisassemble) 与驱动读取，修改后的文件也进行相同校验。

## 复现

Windows、三代游戏已安装时，在仓库运行：

```powershell
python -B tests/edf41_material_shader_contract_audit.py
```

也可以用 `--edf41 DIR --edf5 DIR --edf6 DIR` 指定目录；EDF5 可缺省。脚本从各代 `DX11.cpk` 读取 shader，调用 Windows 的 `d3dcompiler_47.dll / D3DDisassemble` 输出实际 Deferred 像素着色器指令、technique 清单、输出寄存器和文件 SHA256；同时对 EDF4.1 的三个模型包逐模型验证转换保留材质。无输出文件、无 Mods 写入。

本机结果：转换 **16 个模型、58 个材质、108 个 DDS 成员**全部通过；三代各两个 shader 的输出契约检查通过。

## 材质没有被转换器调亮

真实路径是 `tools/make_edf41_objects.py:convert` → `pylib/legacy_assets.py:convert` → `pylib/mdb_legacy.py:mdb_from_legacy`；武器和角色的旧模型同样经过 `legacy_assets`。DDS 成员原样保留；模型转换保留 shader 名、参数名、有效分量、纹理槽与 sampler 字段，只改变必要格式（4.1 参数分量数编码加 `0x100`、binormal 手性、object 名等）。

此次实物验证覆盖：

- `OBJECT/VEHICLE301_TANK.RAB` 全部模型。`Vehicle301_tank.mdb / Material9` 使用 `snd_Mech`，`specular_pow=100`，`fall_off_amount=0.3`，`fall_off_scale=0.2`，`fall_off_offset=0.1`。
- `OBJECT/ARMYANDENGINEER.MRAB` 全部模型。`Engineer.mdb / metal` 使用 `snd_Chara_2ColorChange`，`specular_pow=60`，`fall_off_amount=0`，`fall_off_scale=0`，`fall_off_offset=0.08`。
- `OBJECT/HECTOR.MRAB` 全部模型。`Hector301.mdb / ufosurface` 使用 `snd_UFO_LightScroll`，`specular_pow=100`，`fall_off_amount=1`，`fall_off_scale=0.7`，`fall_off_offset=0.2`。这里只验证转换不改材质；下面两个 shader 的公式结论不直接泛化成此 shader 的完整公式。

这些旧参数在 EDF6 中缺少 `metallic` / `roughness` 并不是格式错误。EDF6 两个受查 Deferred shader 的 RDEF 中，`metallic` 明确为 `[unused]`，`roughness` 缺省值为 0；给所有模型添加统一 metallic/roughness 不能还原原版反光。

## 同名 shader 的颜色语义不同

以下公式仅指 `SND_MECH.SGO` 与 `SND_CHARA_2COLORCHANGE.SGO` 的 `Deferred` 像素着色器。`b` 是纹理 albedo 乘 diffuse 后的基色；角色 shader 还乘 `change_color0/1` 与 `param_cm0_cm1_occ` 的遮罩。`f` 是旧 reflect 纹理 R 通道乘视角相关 falloff：

```text
f = reflect_R * saturate((1 - pow(saturate(dot(-view, N)),
                                  max(fall_off_amount, 0.01)))
                        * fall_off_scale + fall_off_offset)
```

| 游戏 | 输出基色 `o0.rgb` | Deferred 输出寄存器 |
| --- | --- | --- |
| EDF4.1 | `b` | `o0` 至 `o3`（4 个） |
| EDF5 | `b` | `o0` 至 `o5`（6 个） |
| EDF6 | `b + f * (1 - b)` | `o0` 至 `o4`（5 个） |

EDF6 的实际指令（`snd_Mech`）：

```text
mul r2.xyz, r1.yzwy, cb2[0].xyzx
mad r1.yzw, -r1.yyzw, cb2[0].xxyz, l(0,1,1,1)
mad o0.xyz, r0.wwww, r1.yzwy, r2.xyzx
```

角色 shader 在两色遮罩混合后执行同样的 `b + f*(1-b)`。因此旧 reflect 值越高，EDF6 的这一项输出越趋向白色。数学例子：`b=0.2, f=0.25` 时，输出从 0.2 变为 0.4；这不是截图测量值。

同时反射/高光输出也变了：EDF4.1 这两个 shader 把 `f` 写入 `o3.y`、纹理加权的 `specular_pow` 写入 `o3.z`；EDF6 把 `sqrt(f)` 写入 `o3.x`，把旧 specular 指数换算为 roughness 后写入 `o3.y`（下限为材质 roughness）。**寄存器含义属于渲染管线契约，不能照搬旧值到同位置。**

这里看到的是明确的 shader 算法变化，没有看到 albedo 采样到 `o0` 之间新增显式 gamma 变换。纹理创建时 SRGB 标志、光照合成、曝光与环境贴图尚未逆向核完，因此不能把整张画面的明暗差都归为上述一项。

### EDF6 光照消费端核验

消费端不是 DX11.cpk 内另一个可随意替换的 SGO，而是 `EDF.dll` 内嵌的 Deferred combine CS。本机 DLL 的 TimeDateStamp 为 `0x678CCB46`，SHA256 为 `0d5092393a1b9d8f1b75b0c93d8a8f65615a36a92c46e40349c2b66c733d0916`；主 combine DXBC 位于文件偏移 `0x1FC9A90`，长 14872，SHA256 为 `a041d27e839baded1b88287f29404baf7c63162ffb7c56a7b81e1cdcf238ce73`。

实际绑定为 `t0=g_albedoRGB`、`t1=g_specularRGB`、`t2=g_normalXYZ`、`t3=g_metallic_roughness`、`t4=g_viewXYZ_id`。令 `A=t0`、`S=t1`、`m=saturate(t3.x)`、`q=t3.x²`、`r=t3.y`，方向光 `F0=S*(0.04+0.96*m)`，方向漫反射有 `(1-m)` 衰减。主变体的环境合成核心是：

```text
(1-q)*D + E*[q*AO*A + 0.2*(1-r)*(1-q)]
```

独立 `specularRGB` 不参与这个环境项，因此改 `S` 无法抵消基色染色；改 `m` 则会连带改变漫反射与环境混合系数。本次保留所有输出契约，仅恢复输入 `A=b`，由原 EDF6 光照继续处理它。阴影 combine（文件偏移 `0x1FCD4B0`）采用相同核心，另一变体（`0x1FD1540`）把雾衰减提前参与环境项；不把它们的完整公式视为相同。

受查 shader 文件 SHA256（脚本每次会重新输出）：

| 游戏 | SND_MECH.SGO | SND_CHARA_2COLORCHANGE.SGO |
| --- | --- | --- |
| EDF4.1 | `efe70b69cda0ea26330b1065b508f2529b3368f06ac06ca54fb44d1a27cda934` | `7a12f60be7e4cefa9594641f47efa41d1843b1e613fe039fbc962c63685af3df` |
| EDF5 | `1bb9dfac2f7292dd7d7530ca04d136a25adc50e0bb800630367beb2c70c0dab4` | `8b40da92916342080f4e0fbbf6cee63b9abd76074961ca838d5912f2f1d49ce0` |
| EDF6 | `1256766c15b1c7f8dac0834c3a9ad36d3cc5fad38f3e51122f1b4bd76c807821` | `352aae5236f38631a82705ce0f5584c64ca1c948f6c0ac5ae236b21ea13c0bf8` |

## 还原方案的边界

1. **不能统一调暗 diffuse。** 上述附加量依赖 reflect 纹理、法线与观察方向；固定倍率无法抵消所有角度的偏白。代数反解 `b_target=(b_old-f)/(1-f)` 还可能为负，并在 `f=1` 时无解。
2. **不能把 falloff 清零当还原。** 这样会同时删掉用户要求保留的反射特征。
3. **不能直接装 EDF4.1 的同名 shader。** 两代 Deferred 的输出数量与语义不同；VS 输出的 `VIEW_POSITION` 等输入也不同，原样覆盖会破坏 EDF6 的渲染契约。
4. 本次专属 shader 只恢复旧基色与 diffuse alpha，保留 EDF6 其余 PBR，不能称等效还原旧版反射。完整还原还需要适配 Forward/透明、发光、双面及其他 shader 家族，并决定如何处理旧 Phong 与 EDF6 GGX 的差别。
5. `tools/edf41_maps.py` 当前借用 EDF6 donor 的 MAE、环境反射纹理和天气。这是独立的已知观感差异；只改模型材质也不会还原 EDF4.1 全场光照。应在相同灯光、曝光与环境贴图条件下比较，再决定每个 shader 的适配。

原始只读审计脚本保护的是“格式转换没有无依据改坏原始材质”和“三代实际 shader 契约仍与此次调查一致”；新回归测试检查专属 shader 的窄修与目标 ABI。**尚未验收游戏内画面，也未实施完整旧版反射重建。**

## 金色涂装：改用 EDF6 原版金色涂装的金属金材质（2026-10-25）

用户反馈「金色一定要有金色」：BM03 维加泰金色涂装是平涂黄色、像塑料；深渊爬行战车金色涂装是米白色。两者都写在 EDF5 时代的 reflect 着色器上（`snd_Mech_Light` / `snd_Mech`），金色本来靠旧反射：

- 爬行战车金色是 **EDF6 原版自带**的 `VEHICLE502_GROUNDROBOGOLD.MRAB`（材质 `groundrobo`，`snd_Mech`），不经过 EDF41_ 别名。EDF6 的 `o0 = b + f(1-b)`，reflect.R 约 0.63、fall-off 50/2/0.5 使 f 几乎处处等于 reflect.R：基色 (0.25, 0.13, 0) 被抬到约 (0.72, 0.68, 0.63)，即截图里的米白。
- BM03 金色（EDF4.1 `Vehicle406_begarutagold.mdb` / `Material1`）已由别名去掉抬白，但 `specular_pow=10` 经 EDF6 公式 `(1 - spec·pow/150)^(2f+3)` 得到粗糙度约 0.75，镜面反射被糊成漫反射；基色是贴图乘 (1, 0.698, 0) 的饱和黄：平涂塑料感。

EDF6 自己的金色涂装（`V504_BEGARUTA_GOLD.MRAB` / `v504_begaruta`）是 `snd_BRDF_Common_Basic`：棕金基色（面板 123,93,57）、metallic 0.9、roughness 0.1、specular_color 0.4、blend 0.5；EDF.dll 的 combine 以 metallic² 为权重叠加被基色染色的环境反射，这才是金属金。`pylib/goldcoat.py` 把登记的金色涂装材质统一换成 EDF6 原版同输入的 `snd_BRDF_Common_SeparateOcc` / `snd_BRDF_Common_Light_SeparateOcc`（顶点输入、technique 与旧着色器逐项相同，遮蔽仍取第二套 UV），生成两张新贴图（`<原名>_goldcoat.dds`）：基色按色相识别涂装像素、按各像素亮度 / 涂装中位亮度重着参考金色（保留明暗细节），其它像素保留原色；参数图 R 粗糙度、G 金属度（涂装处 0.1 / 0.9，其它处为 EDF6 原先对该像素的计算值），B 为旧灯光遮罩。源贴图 sha256 不符（别的 mod 的贴图）时原样保留。

接入：EDF4.1 的 BM03 在 `edf41materials.transform_models`（`Edf41MaterialCompatibility=1`）里先于别名转换；原版爬行战车在 `vehicle_optics.build_model` 里转换，`ready()` 对登记了金色涂装的原版模型恒为真（没有座舱素材也重定向到转换后的模型）。所有名称含 金色 / ゴールド / Gold 的呼叫载具必须登记在 `goldcoat.GOLD_VEHICLES`（尼克斯金色号用的 `v504_begaruta_gold` 本来就是参考材质）。验证：`tests/test_goldcoat.py`（离线 + 有游戏时对真实资源转换：中位色等于参考金、亮度逐像素相关 >0.95、金属度 / 粗糙度、贴图能绑定、幂等）。**未实机验收**：游戏内金属反射观感、灯光发光颜色。
