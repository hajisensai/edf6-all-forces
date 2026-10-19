# 2026-10-19：4.1 沙滩变深棕色的地形迁移修复

反馈截图 `04c41a62ecebe194874fe6ff2f346fa1.png` 显示海岸沙地变成深棕色。截图尚不能独立证明任务编号；
本轮直接核对海岸源地图 SK_KAIGAN01，以及全部七张 SHFB 地形的颜色阵列、资源绑定、权重和两代原生 shader。
已确认两个转换错误，均在安装期地图转换器修复。没有调亮原纹理或改固定沙地颜色。

## 1. 缺省乘色被错误写成有效采样

SK_KAIGAN01 的 `.cols.dds` 是 DX10 BC1_UNORM（DXGI 71），1024×1024、11 slices、11 mips。
原始 `.cols.sgo` 实际绑定为 cliff01=0、fldsub01=1、fldsub02=2、grass01/02/03/05=3/4/5/6、rock01=7、
sand01=8、sand02=9、soil02=10。转换现在使用这张显式绑定表并校验阵列大小，避免靠名字推断切片。

SHFB 的 layer 0 与 4 只有一个 colour setting，因此 `sk_terrain.Layer.multip=-1`：原本只采样 sand01/sand02，
不再乘第二张图。其它六张 SK 地图每层都有 multiplier。源 SHFB SHA256：
`18c6476ff1da5494a33a88a720845eb08453193abe33155b2d5bf320b1a36fb3`。

EDF41.exe 的地形设置构造（RVA 0xF84F0）先把四个 texture-index bytes 初始化为 `FF FF FF FF`，
再按实际 setting count 写入；沙地的 packed 值为 `[8,255,1,255]` / `[9,255,1,255]`。
原生 terrain PS（文件偏移 0x9BE120，SHA256
`44d4d972a0f004b85a76011e4cc2fcc58b7cc22a4f73cf01b96e6afd13703dae`）用 `ine ... 255` 加分支跳过第二次乘色。

EDF6 `MAP/SHADER/DX11/MAP_FIELD_RENDER1_1_14_0.SGO` 的 Deferred PS（SHA256
`38c06e615c108e1a39010bc5b6facfa8aae642cc60fd3a3a6a08c361b42218ef`）则从
`g_materials +48` 读主 multiplier，`utof` 后直接采样颜色阵列并乘到 base colour，没有 -1/FF 跳过分支。
FMB 记录 +0x28 的 multiplier 由 loader（RVA 0x10CF4B..0x10CF4F）直接送入 GPU +0x30；
次轴不存在时的回退不改变这个主 multiplier。因此旧转换器把 -1 原样写进去会产生错误的乘色。

实际 D3D11 WARP 用源 DDS 测得，这个无效 `uint(-1)` 采样在本机等于 slice 0（cliff），**不是**最后的 soil slice；
不依赖这个未定义/边界采样行为来修复。修复是把乘法恒等元 RGBA=(1,1,1,1) 作为一张新 BC1 slice 追加，
每个 mip 都由 opaque-white block 编码。原有全部像素和 mip 不动，只有缺省 multiplier 的层绑定这张合法 slice。
海岸沙地现为 colour=8/9、multiplier=11；已有 multiplier 的层绑定不变，tile size 保持合法正数。

## 2. top-five 会删除真正存在的地表

旧 `fmb_build.leaf_blob` 按整片叶子的总权重只留前五层。某个局部顶点的全部有效层都可能被删光，然后
`_corner_weights` 会把它填成留下来的第一层。此问题与上面的缺省 multiplier 独立，会把局部沙地替换为草等材质。

全部七张源图需要超过五层的叶子：

| 地图 | 单叶最大层数 | 超过五层的叶子数 |
| --- | ---: | ---: |
| SK_CRATER01 | 9 | 49 |
| SK_HEIGEN02 | 6 | 62 |
| SK_KAIGAN01 | 10 | 103 |
| SK_SANGAKU01 | 13 | 137 |
| SK_SANGAKU02 | 10 | 106 |
| SK_STEPMOUNTAIN01 | 10 | 113 |
| SK_VALLEY01 | 9 | 79 |

海岸叶子 lattice (r0=200,c0=140) 的源顶点 (r438,c320) 原权重为 layer0=.85382831、layer4=.14617169；
旧 top-five 留下 `[1,2,3,7,9]`，把两层沙地全删掉并填成 layer1。山地 SANGAKU01 的叶子 (320,200)
有 13 层，源顶点 (640,408) 纯 layer7 也曾被删成 layer0。

EDF6 实际包含 n=1..15、pass=0/1/2 的 45 个 `MAP_FIELD_RENDER<n>_1_14_<pass>.SGO`；
高层数变体确实有完整采样程序，并非占位：n15/pass0 PS 有 156 次 structured load 和 45 次 array texture sample。
权重结构 80 bytes、layer-list 16 bytes、material 80 bytes、orientation 64 bytes，pass2 去掉 normal 后寄存器前移一格。
Loader（RVA 0x10DBAE..0x10DBE8）按实际数量上传权重和 layer-list；shader selector（RVA 0x10E068..0x10E089）
按实际 count 格式化 shader 文件名，未限制为五。修复保留叶子中所有正权重层，按源 layer index 排列；
超过原生可用的 15 层时明确报错，不再截断。

## 3. 安装、隔离和验证

`edf41_maps.VERSION` 3→4，使旧缓存地图在下次安装时重新转换；保留既有 source fingerprint、归属账本和
4.1 私有 material shader alias 契约。未写任何游戏目录；EDF6 原生地形 shader、天气资源、非4.1材质不变。

验证：

- `tests/test_sk_terrain_materials.py`：11 项通过，含 DDS 头/格式/完整 mip 校验、缺省 multiplier 需显式 identity、
  已有 binding 不变、非法主 index 拒绝、13 层序列化、超过15拒绝、真实沙地/山地关键顶点不再丢层；
  旧版本3安装在源 fingerprint 不变时重建一次，随后命中版本4缓存，归属和卸载保持正确。
- `tests/terrain_shader_native_audit.py --probe tmp/terrain_sampler_native_test.exe`：45 个真实 shader 契约、源 FF 分支通过；
  用生产 `with_identity_multiplier` 输出的海岸 DDS 在 D3D11 WARP 验证沙地 slices 8/9、各11 mip、每 mip 16 UV。
  source RGB 变化=0，修复后乘色相对 source RGB 最大误差=0；旧非法乘色在 mip0 最大改变 .288474/.172004。
  Probe 以 `/W4 /WX` 编译通过；它测实际 D3D11 texture-array 采样及乘法路径，非完整场景光照截图。
- 真实源 `edf41_maps.convert(SK_KAIGAN01)`：16 个输出、625 叶、最大10层、12-slice阵列、原颜色 payload SHA256
  `eec9c51ec3ebb0a92de6418acbf52339d5be65a4163e501ee6d6db331c26a150` 不变；
  MAC 验证、FMB 独立 parser/selftest 五项、2500 tile 碰撞和 bounds 均通过（81.68 秒）。

- 既有 `selftest.sk_terrain_real`、`fmb_build_real`、`edf41_sk_maps_real` 全通过（16.02、6.06、20.13 秒），
  覆盖七图解码及 HEIGEN02 完整迁移。

仍需实机确认截图所示任务的最终地面和叶子边界。原生光照/天气、原图法线阵列、非等比/旋转 UV、mip bias
与碰撞表面类别是已有独立迁移限制，本轮不将它们误称为完整4.1渲染还原；海岸这次两个已证实的错误已修。
