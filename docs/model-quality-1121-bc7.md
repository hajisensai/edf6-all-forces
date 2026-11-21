# 外部模型颜色图改用 BC7（2026-11-21）

`docs/model-quality-1110.md` 第 6 条「没做」的那一项：游戏能读 DX10 头的 BC7，我们的颜色图只能走 BC1。本次把它做完。所有数字都是离线算的（脚本在会话 scratchpad，不入库）；没有启动游戏，**实机画面还没看过**。

## 结论

- 外部模型的颜色图（albedo）全部改成 **BC7_UNORM（DXGI 98）+ DX10 头**，HD / .lod 配对规则与原版 DXT1 完全相同（.lod 仍是 HD 的 mip 尾巴）。法线 BC5U、参数图 DXT1 不动（原版参数图虽然也有 BC7，但本次只改颜色图，见「没做的」）。
- `pylib/texfile` 读写 DX10 头（只接受原版模型用的那一种：2D、BC7_UNORM、无标志、数组 1）；`pylib/bc7.py` 是纯 Python 参考编码器 + 全模式解码器；`native/bc7.cpp` 是逐字节一致的移植（进 `edf6cmpl.dll`，多线程，按块行切分，与线程数无关）。
- 画质：同一张图的顶层 PSNR 比现在的 BC1 编码器高 **+10 ~ +11 dB**（下表），体积是 BC1 的 2 倍（未压缩）/ CMPL 后约 2.4 倍。

## 1. 原版怎么存 BC7（Root.cpk，只读）

统计脚本扫了 Root.cpk 的 OBJECT / WEAPON / MAP 全部 RAB / MRAB：HD 成员 5033 个旧头、239 个 DX10 头。

| 范围 | DX10 HD 数 | DXGI | 说明 |
|---|---|---|---|
| OBJECT + WEAPON | 179（OBJECT 70） | 全部 98 BC7_UNORM | 槽位：param_r_m_occ_light 148、albedo 16 + albedo2 + bubble_albedo、dirt 6、normal / damage_normal 6、window_frame 4…（同一贴图可占多个槽） |
| MAP | 60 | 98 ×32、95 BC6H_UF16 ×23、99 BC7_UNORM_SRGB ×5 | 99 只有 5 张天空立方体图（cube_sky_sun*_BC7），模型里一张都没有 |

（1110 文档说 OBJECT 里 69 张；按 HD 成员数是 70，其中 EV609_VIRTUALDOME 的 `triangle.DDS` 放在 TEXTURE 文件夹、没有 .lod，带 .lod 的正好 69。）

OBJECT / WEAPON 的 179 张逐项相同的头（H）：

- 旧头部分：size 124、flags `0xA1007`、pitchOrLinearSize = 第 0 级字节数、depth **1**、mip 全链到 1×1、像素格式 size 32 / flags `0x4` / FourCC `DX10` / 其余 0、caps `0x401008`、caps2 0。也就是 `texfile._header(..., b'DX10', depth=1)`。
- DX10 头（+0x80，20 字节）：`dxgiFormat 98, resourceDimension 3 (TEXTURE2D), miscFlag 0, arraySize 1, miscFlags2 0`（唯一例外是 E614 的立方体图 miscFlag 4，不是颜色图）。
- 文件长度 = 148 + 全链字节（16 字节 / 4×4 块）。
- .lod（178 对）：同样的 DX10 头；尺寸 = 每边 /16、短边不小于 16（两条规则在这 178 对上一致）；mip 数 = `lod_mips()`（长边止于 2 像素）178/178；成员 TEXTURE / flag 0，排在 HD 之前 178/178。和 2176 对旧头配对的规则一模一样，所以 `texture_pair` / `pair_problem` / `obj_model.add_texture` 的成员顺序全部沿用。
- .lod 的数据不是 HD mip 尾巴的拷贝（216/218 不同，原版是单独重采样编码的）；旧头的 2176 对也是这样，我们一直用 HD 的 mip 尾巴切出 .lod，本次不变。
- 原版 BC7 用了多种模式（N603 上校 .lod 首块 mode 7、HD 首块 mode 4，GIANTANT ariwing 首块 mode 3），`bc7.decode_block` 全部 8 种模式都解。

MAP 里的 DX10 配对不同：40 对 .lod 比 `lod_mips()` 多一级（MAP 的旧规则，`tools/make_sedan.py` 早有注释）；本次只碰模型，不改 MAP 规则。

## 2. EDF.dll 怎么读（逆向，Steam 版 `0x678CCB46`）

- DDS 头解析 `0x146B200`：`[pf.flags] & 4` 时看 FourCC（`0x146B51A`）。旧 FourCC 映射成引擎自己的格式号：`DXT1` → 120（`0x146B74E`）、`DXT2/3` → 130、`DXT4/5` → 140、`BC4U/ATI1`、`BC5U/ATI2` → 160（`0x146B7C4`）、`BC4S` → 151、`BC5S` …。（H）
- `DX10`：读 20 字节扩展头（`0x146B542`），拿 dxgiFormat 调 `0x1473720`——在格式表 `0x2140120`（0x116 项 × 0x38 字节，+0x34 = DXGI）里线性查找，返回项号作为格式号；查不到就记错误返回。arraySize 存到描述 +0x8C（`0x146BAB5`，不足 1 按 1）；格式号统一写到 +0x84（`0x146B9CE`）。（H）
- 格式表在静态初始化 `0x1471990` 里逐项写入（直线代码，离线模拟出全表）：

  | 格式号 | DXGI | 名称 | +0x2C（线性号） | +0x30（sRGB 号） |
  |---|---|---|---|---|
  | 120 | 71 | BC1_UNORM（旧头 DXT1 映射到这里） | 120 | 121 |
  | 121 | 72 | BC1_UNORM_SRGB | 120 | 121 |
  | 160 | 83 | BC5_UNORM（旧头 BC5U） | 160 | 160 |
  | 180 | 98 | BC7_UNORM | 180 | 181 |
  | 181 | 99 | BC7_UNORM_SRGB | 180 | 181 |

  引擎给每个格式记了线性 / sRGB 两个变体。旧头 DXT1 落在 120（线性那一个），DX10 写 98 落在 180（同样是线性那一个，sRGB 变体 181），两者处在完全相同的位置。所以**颜色图写 BC7_UNORM（98）与现在的 DXT1 颜色图走同一条 sRGB 处理**（不管引擎在哪里把颜色槽换成 +0x30 的 sRGB 变体，BC1 和 BC7 都一样换）。原版 OBJECT / WEAPON 的 albedo BC7 全是 98 也印证这一点。（表与映射 H；+0x30 在采样时由谁使用没有继续追，结论「与 DXT1 一致」为 M，等实机看颜色是否发灰 / 过暗。）
- 写 99（_SRGB）会落在 181，与 DXT1 的路径不同，模型里也没有先例，所以不用。

## 3. 编码器（`pylib/bc7.py`、`native/bc7.cpp`）

不透明：每块解码 alpha 都是 255（与 DXT1 四色块一致，原来 DXT1 也丢 alpha）。只用三种模式：

- **mode 6**：单子集，4 bit 索引，7 bit 端点 + p-bit；alpha 255 要求两个 p-bit 都是 1（端点只能取奇数，靠插值补偶数）。
- **mode 1**：双子集（64 种划分），3 bit 索引，6 bit 端点 + 每子集一个 p-bit。
- **mode 3**：双子集，2 bit 索引，7 bit 端点 + 每端点一个 p-bit。

全整数（为了 C++ 逐字节一致），每个子集：协方差幂迭代 4 次（起点是颜色包围盒最长轴，1/1024 步长）求主轴 → 像素投影在过均值的主轴直线上的两端作端点 → 所有 p-bit 组合量化（最近码、相等取小）→ 每像素取最近调色板项（相等取小）→ 最多 2 次最小二乘回拟端点、误差变小才接受；全部像素同色的子集走 `_solid`（每种 p-bit 和每个索引下，在最近码 ±2 内找插值最近的一对码，相等取先）。mode 6 误差 ≤ `GOOD = 32`（每通道均方误差 2/3）的块直接用 mode 6；否则对 64 种划分用未量化 3 bit 拟合打分，取最好的 `CANDIDATES = 1` 个划分编 mode 1、mode 3，误差最小者胜（相等取先：6 → 1 → 3）。`GOOD` 必须小于 48：一块纯色（或近纯色）mode 6 每通道差一级时误差正好 48（黑色 0 在 mode 6 里取不到，码全是奇数），这类块要靠 mode 1 / 3 的 p-bit 0 才精确。

native 版的划分打分按「已保留的第 CANDIDATES 名」剪枝（打分超过它就停，划分按序遍历、同分不顶替），协方差用 n²Σpp − n·s·sᵀ 一次求和，误差用 int32——结果与 `bc7.py` 的「全部打分再排序」逐字节相同（测试核对），单线程 CPU 降到约 2/3。

参数取舍（单线程 CPU，三张图 1024² 裁剪合计 / 各自 PSNR）：

| GOOD / CANDIDATES | CPU | bmpt 车体 | flak88 | t72b3 履带 |
|---|---|---|---|---|
| 16 / 2 | 2.55 s | 52.51 dB | 54.72 | 51.48 |
| 16 / 1 | 1.80 s | 52.45 | 54.65 | 51.40 |
| 24 / 1 | 1.23 s | 52.30 | 54.58 | 51.31 |
| **32 / 1（采用）** | **0.92 s** | **52.21** | **54.50** | **51.00** |
| 48 / 1 | 0.44 s | 52.08 | 50.41 | 48.97 |

（48 的断崖就是上面说的纯色块。）

分区表、锚点表与 Pillow `_imaging` 里的表逐字节核对过（P2 64 项、P3 64 项、三张锚点表），解码器与 Pillow 对随机块（8 种模式各 512 块）、我们编出的块、原版 .lod 全部逐像素一致。

`texfile_accel.bc7_dds` 只走 native：`bc7.py` 编一张 2048² 要按小时计，没有 DLL 的安装就是坏的，直接报错（`TextureError`，提示 build.cmd / 安装不完整），**不写 Python 回退**。`texfile.bc7_dds` 是参考实现（测试 / 冻结包自检用）。`texfile_accel` 在 `_bc7_level` 里 import `bc7`，让 `buildcache.source_recipes` 把编码器定义算进每个组的缓存键（改 `bc7.py` + `bc7.cpp` 会让缓存失效）。

## 4. 改了哪些生成点

| 位置 | 之前 | 之后 |
|---|---|---|
| `wt_tracked.material_textures` albedo（所有 wt_* / emp_* / xm 履带、`xm_rig` 步行 / 步兵、`xm_air_model` 空中单位的 `_df`） | DXT1 | BC7（`wt._albedo_dds`）；`param_occ` 仍 DXT1（`wt._dds_of`） |
| `obj_model.texture_dds`（artillery、drill 的 albedo） | DXT1 | BC7 |
| `c130_model` / `dreadnought_model` / `helicarrier_model` 的 `_df` | DXT1 | BC7；`_rm` 仍 DXT1，`_nm` 仍 BC5U |
| `sazabi_model` 调色板 albedo | DXT1 | BC7（rmo / rml 仍 DXT1） |
| `cockpits.albedo_dds`（座舱件颜色，图像与纯色） | DXT1 | BC7（`texfile_accel.texture_dds`） |
| `map_build.texture_dds`（X-Morph 地图不透明贴图） | DXT1 | BC7；clip / blend 仍 DXT5 |
| `tools/make_sedan.py` | 原版 BC7 车窗玻璃被 texfile 拒绝后解成未压缩 RGBA | `texfile` 认 BC7 了，车窗保持原版 BC7（alpha 保留），.lod 照旧按车辆规则重切 |

没改：法线（BC5U，原版法线绝大多数是 BC5U）、参数图 / 遮蔽图（DXT1）、goldcoat（重编原版 4.1 贴图）、magic_circle、世界地图（UI 用 DXT1，有测试约定）。

冻结安装包自检（`installer.py --check-package`）加了一条：`texfile_accel.bc7_dds == texfile.bc7_dds`（DLL 打进包且与参考一致）；`build_release` 的 hidden import 加 `bc7`。

## 5. 前后对比

顶层 RGB PSNR（Pillow 解码对原图），native 编码耗时（本机，多线程；同时有别的会话在跑，耗时只看量级）：

| 图 | 尺寸 | BC1 | BC7 | 提升 | DDS 字节 BC1 → BC7 | CMPL 后 |
|---|---|---|---|---|---|---|
| bmpt 车体 camo（jpg） | 2048² | 41.04 dB / 0.32 s | 51.24 dB / 0.83 s | +10.20 | 2.80 → 5.59 MB | 2.31 → 5.40 MB |
| t72b3 履带（jpg） | 1024×256 | 40.16 / 0.01 s | 51.00 / 0.06 s | +10.84 | 0.17 → 0.35 MB | 0.11 → 0.27 MB |
| flak88_0（png） | 1024² | 43.44 / 0.06 s | 54.50 / 0.13 s | +11.06 | 0.70 → 1.40 MB | 0.42 → 0.99 MB |

单线程 CPU（1024² 一级）：DXT1 0.016–0.031 s，BC7 0.36–0.39 s，约 12–23 倍。

整车安装（`make_tracked_combat.build`，含座舱嫁接与 LOD，见下）：

| 模型 | BC7 墙钟 / 产物 | DXT1 墙钟 / 产物 | 体积增幅 |
|---|---|---|---|
| bmpt | 118.3 s / 67.9 MB | 83.9 s / 56.5 MB | +20% |
| tunguska | 90.0 s / 46.8 MB | 112.0 s / 36.1 MB | +30% |
| flak88（emplacement） | 15.1 s / 9.6 MB | 13.2 s / 7.4 MB | +30% |
| c130 | 30.1 s / 12.9 MB | 26.8 s / 8.7 MB | +48% |
| helicarrier | 6.4 s / 2.8 MB | 4.8 s / 2.0 MB | +40% |
| dreadnought | 38.4 s / 38.1 MB | 29.0 s / 29.2 MB | +31% |
| sazabi | 13.7 s / 11.2 MB | 12.4 s / 11.2 MB | ≈0 |
| 合计 | 312 s / 189.4 MB | 282 s / 151.1 MB | +25% |

（产物 = 该模型全部 MRAB 字节数，含 HD 与 .lod。墙钟在本机有其他会话跑测试时测得，噪声很大，tunguska 的 BC7 甚至比 DXT1 快，只能看出量级：多线程后整车墙钟多 10–40%。sazabi 的贴图大多是参数图 / 法线，颜色图是小调色板，几乎不变。）

模型包（`D:\APP\edf6-models`、仓库 `models/`）存的是源 PNG / JPG，不含编好的 DDS，**模型包体积不变**；变大的是安装到 `Mods/OBJECT` 的 MRAB（颜色图约 ×2.4，见上表）。1110 文档估的「模型包 +20%」实际体现在安装产物上。

## 6. 测试

- `tests/test_native_bc7.py`（ctest `native_bc7`）：native 与 `bc7.py` 逐字节一致（30 张图，线程数 1 / 2 / 5 / 全部）；`texfile_accel.bc7_dds == texfile.bc7_dds`；我们编出的块 Pillow 与 `bc7.py` 解码一致且 alpha 全 255；8 种模式随机块解码与 Pillow 一致；纯色块（含黑 0,0,0）精确；画质：仓库 `models/wt_bmpt` 车体图裁 256² 与合成图都比 BC1 高 6 dB 以上；DX10 头 / .lod 配对与原版逐字段一致、拒绝 _SRGB / BC1 DX10 / 3D / 立方体 / 数组；原版 N603 上校与 GIANTANT ariwing 的 HD / .lod：头与我们写的逐字节一致、`pair_problem` 为空、.lod 解码与 Pillow 一致（没有 Root.cpk 时跳过）。
- 改了期望的：`tests/test_texture_acceleration.py`（座舱 PNG 现在期望 BC7）、`tests/test_xm_maps.py` 注释。

## 7. 没做的 / 风险

- **没实机**：颜色是否与 DXT1 版一致（sRGB 处理）只到 M；需要实机看一辆 wt 车、一架 C-130、一个座舱。
- 参数图（`param_r_m_occ_*`）原版有 148 张 BC7，本次没改（任务只要颜色图；参数图换 BC7 体积同样翻倍）。
- 带 alpha 的贴图（X-Morph 地图 clip / blend、透明玻璃）仍是 DXT5：编码器只做不透明模式（4 / 5 / 7 的 alpha 模式没做）。
- 安装耗时：BC7 单线程 CPU 是 DXT1 的 12–23 倍；整车生成的墙钟见上表（DLL 开满硬件线程，所以墙钟增幅远小于 CPU 增幅）。安装器的模型组本来就是多进程并行，每个进程的 BC7 又开满硬件线程，会有超订（总工作量不变）；本机同时有别的会话跑测试，墙钟噪声很大，没有在空闲机器上量过首装总时长。
- `edf6native` 里只有 BC7 没有 Python 回退：开发 checkout 没跑 build.cmd 时生成模型会直接报错。
