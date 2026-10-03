# EDF6 MDB0 模型格式 + RAB/MRAB 容器 + 运行时骨骼驱动

目标：给 `bomber501` 喷气机做一版「舵面是独立骨骼」的模型，插件在运行时转这些骨骼。
本文所有字段都对照游戏实物验证过（`tools/mdb.py verify`）。可信度标注：**[H]** 实物逐字节 / 反汇编直证；**[M]** 多个样本一致但没有直接看到引擎消费它；**[L]** 推测。

- 工具：`tools/mdb.py`（读/写/dump/verify，CMPL 编解码，RAB 读写）、`tools/mdb_jet.py`（bomber501 舵面拆分）。
- 产物：`build/jetmodel/`（`bomber501.mdb`、`bomber501.mrab`、`hinges.json`、`preview.png`）。**不写游戏目录。**
- EDF.dll 版本：TimeDateStamp `0x678CCB46`；下面所有 RVA 都基于它（`tools/edfre.py` / `fnrefs.py` / `dump.py` 复核）。
- 社区参考：KCreator 的 [EDF-MDB-Viewer](https://github.com/KCreator/EDF-MDB-Viewer)（`MDBParser.cpp`，为 EDF4.1 写）给出了头、名字表、骨骼、贴图、材质、object/mesh/layout 的大框架；
  [Earth-Defence-Force-Documentation](https://github.com/KCreator/Earth-Defence-Force-Documentation)（`CMPLCompress.cpp`）给出了 CMPL 是 Okumura LZSS。
  两者都只当线索用，每个字段都在 EDF6 实物上重新核过；骨骼字节语义、材质 param 结构、object 结构、文件内排列顺序、RAB 头是本次新补的。

---

## 1. 容器：RAB / MRAB（`SSA\0`）[H]

`OBJECT/*.RAB|*.MRAB`（Root.cpk 内）。全部小端。

| 偏移 | 类型 | 含义 |
|---|---|---|
| 0x00 | char[4] | `SSA\0` |
| 0x04 | u32 | 版本 `0x110` |
| 0x08 | u32 | 数据区起点（= 字符串池末尾） |
| 0x0C | u32 | 所有文件中**存储**大小的最大值 |
| 0x10 | u32 | 所有文件中**解压后**大小的最大值（CMPL 头里的 raw size） |
| 0x14 | u32 | 文件数 |
| 0x18 | u32 | 文件表偏移（每项 0x20） |
| 0x1C | u32 | 按名字排序的索引表偏移（每项 8 字节：i32 名字相对偏移、u32 文件序号） |
| 0x20 | u32 | 目录数 |
| 0x24 | u32 | 目录表偏移（每项 i32 相对偏移 → UTF-16 目录名） |

文件表项（0x20）：`i32 名字相对偏移`、`u32 存储大小`、`u32 目录序号`、`u32 flag`（HD-TEXTURE 目录里的文件为 1，其余 0）、`u32 0`、`u32 0`、`u64 数据绝对偏移`。

之后是**一个**排序去重的 UTF-16 字符串池（目录名 + 文件名），然后数据首尾相接、**不对齐**。所有相对偏移都相对于「存放该偏移的那个字段」所在表项的起点。

BOMBER501.MRAB 的目录：`TEXTURE`（`*.lod.dds` 低清贴图）、`MODEL`（`bomber501.mdb`、`bomber501_2.mdb`）、`HD-TEXTURE`（`*.dds`）。
**.mdb 里的 texture 表的 `filename` 字段（如 `bomber501_df.dds`）就是 RAB 里同名文件**；`.lod.dds` 由引擎按名字派生（[M]）。

`rab_write(rab_read(x)) == x`：BOMBER501 / V506_HELI / VEHICLE409_HELI 已逐字节验证，全量 185 个 OBJECT 档案的结果见 §6。

### 1.1 CMPL [H]

`CMPL` + `u32 大端 raw size` + Okumura LZSS（N=4096、F=18、阈值 2、窗口初始填 0x00 从 4078 开始，flag 字节 LSB 先）。
**与社区 `CMPLCompress.cpp` / Okumura 原版的唯一差别：引用的两个字节是「位置高位在前」**：
`b0 = pos >> 4`，`b1 = ((pos & 0xF) << 4) | (len − 3)`。
`tools/mdb.py` 的 `cmpl_compress` 用 Okumura 二叉树匹配，对游戏里的流**逐字节复现**（bomber 的全部 lod dds、两个 mdb、v506_heli.mdb 已测）。
游戏也接受未压缩的成员（大量 HD 贴图就是裸存的）；新 mdb 仍按原样压缩以免踩到未知分支。

---

## 2. MDB0 布局 [H]

全部小端。「相对偏移」= 相对于存放该偏移的那条记录的起点（不是字段本身），除非另注。

### 2.1 文件头（0x30）

| 偏移 | 含义 |
|---|---|
| 0x00 | `MDB0` |
| 0x04 | 版本 `0x20` |
| 0x08 / 0x0C | 名字数 / 名字表偏移（固定 0x30） |
| 0x10 / 0x14 | 骨骼数 / 骨骼表偏移 |
| 0x18 / 0x1C | object 数 / object 表偏移 |
| 0x20 / 0x24 | 材质数 / 材质表偏移 |
| 0x28 / 0x2C | 贴图数 / 贴图表偏移 |

### 2.2 名字表

每项 `i32`，相对**该项自身**的偏移 → UTF-16 字符串；`0` 表示空槽。
顺序固定为：骨骼名（按骨骼序）→ 材质名 → object 名 → 若干尾部空槽（bomber 13 项用 4，v506 34 项用 16）。空槽用途未知 [L]；新文件照样保留同数量空槽。

### 2.3 骨骼（每个 0xC0）

| 偏移 | 类型 | 含义 | 可信度 |
|---|---|---|---|
| +0x00 | i32 | 序号 | H |
| +0x04 | i32 | 父骨骼，-1 = 根 | H |
| +0x08 | i32 | 下一个兄弟，-1 | H |
| +0x0C | i32 | 第一个子骨骼，-1 | H |
| +0x10 | i32 | 名字表序号 | H |
| +0x14 | i32 | 子骨骼数 | H |
| +0x18 | u8 | kind：0 = 纯变换节点；1 = 刚体 mesh 挂的骨骼；2 = 蒙皮 object 挂的骨骼；3 = 蒙皮骨骼（有顶点权重指向它） | M（全量统计一致） |
| +0x19 | i8 | depth_delta = depth(文件中下一根骨骼) − depth(本骨骼)；最后一根 = 自身深度 | H（全量成立，引擎拷进实例 +0x1C） |
| +0x1A | u8 | bounded：1 = 参与实例包围盒 | M |
| +0x1B | u8 | 0 | H |
| +0x1C | i32 | 0 | H |
| +0x20 | f32[16] | local 矩阵，行主序、**行向量约定**（`p' = p · M`，第 3 行是平移），相对父骨骼 | H |
| +0x60 | f32[16] | inverse bind（蒙皮用，引擎拷进实例 +0x30）。见下方说明 | H/M |
| +0xA0 | f32[4] | 包围盒半尺寸（w = 1） | M |
| +0xB0 | f32[4] | 包围盒中心，**骨骼 bind 坐标系内**（w = 1） | M |

inverse bind 的全量统计（738 个 mdb，`tools/mdb.py verify` + 分类脚本）：
- 绝大多数骨骼满足 `inv_bind = inverse(local 链乘出的模型空间矩阵)`。
- 不满足的 1239 根分四类：
  - 902 根是没有任何几何引用的定位骨骼（`aim_center`、导弹挂点等），inv_bind = 单位阵。
  - 86 根是**刚体 object 的挂接骨骼**（kind 1），inv_bind = 单位阵。例：`AntEgg_Break.mdb` 的 `AntEggShell1..6` 的 bind 平移各不相同，但顶点质心都在原点附近 → **刚体 mesh 的顶点存的是骨骼局部坐标**，inv_bind 不用 [M]。
  - 226 + 13 根是蒙皮骨骼，inv_bind 的平移与 bind 相同、旋转不同，即 local 存的「静止姿态」≠ 蒙皮 bind 姿态（例 `e504_monsterA` 的 `kawanSub1_l`）。所以**蒙皮以 inv_bind 为准**，local 只是初始姿态。
  - 12 根带缩放（`inverse_affine` 只处理正交矩阵，这类其实 `bind × inv = I`）。
- 新做的模型让两者一致即可。

**骨骼必须按 DFS 先序排列**：引擎算子树范围 `[firstChild, subtreeEnd)` 并按序更新（§4）。

### 2.4 贴图（每个 0x10）

`i32 序号`、`i32 name 相对偏移`（UTF-16，如 `bomber501_df_dds`）、`i32 filename 相对偏移`（UTF-16，如 `bomber501_df.dds`，= RAB 成员名）、`i32 0`。

### 2.5 材质

先是全部材质头（每个 0x20），然后**依次**是每个材质的 params 与 texrefs：

材质头：`u16 序号`、`u8`、`u8`、`i32 名字表序号`、`i32 shader 相对偏移`（UTF-16，如 `snd_Mech`）、`i32 params 相对偏移`、`i32 param 数`、`i32 texref 相对偏移`、`i32 texref 数`、`i32 unk = 3`（全部样本）。

param（0x20）：`f32[4] 值`、`i32 ×2`、`i32 名字相对偏移`（ASCII，相对 param 起点，如 `diffuse`/`roughness`）、`u32 type`（见到 0x0402 / 0x0201 / 0x0100，大致是分量数/类型编码 [L]）。

texref（0x1C）：`i32 贴图序号`、`i32 kind 相对偏移`（ASCII：`albedo`/`normal`/`param_reflect_spec_XXX_hspec`/`param_occ_XXX_XXX_XXX`…）、`i32 ×5 = 0`。

### 2.6 object 与 mesh

object（0x10）：`i32 名字表序号`、`i32 挂接骨骼`、`i32 mesh 数`、`i32 mesh 头相对偏移`。全部 object 头之后，依次是每个 object 的 mesh 头数组，每个 mesh 头后面紧跟它的 layout 表。

mesh 头（0x28）：

| 偏移 | 含义 |
|---|---|
| +0x00 | 4 字节 flags：[1] = 1 蒙皮；[2] = 每顶点最大影响数（1 或 2）；[0][3] = 0 |
| +0x04 | 材质序号 |
| +0x08 | unk |
| +0x0C | layout 表相对偏移 |
| +0x10 | u16 顶点大小 |
| +0x12 | u16 layout 项数 |
| +0x14 | 顶点数 |
| +0x18 | mesh_index |
| +0x1C | 顶点数据相对偏移 |
| +0x20 | 索引数（u16，三角形列表） |
| +0x24 | 索引数据相对偏移 |

layout 项（0x10）：`i32 格式`、`i32 顶点内偏移`、`i32 channel`、`i32 ASCII 名相对偏移`。
格式：1 = float4，4 = float3，7 = half4，12 = float2，21 = ubyte4。
名字：`position` `normal` `binormal` `tangent` `texcoord` `BLENDWEIGHT` `BLENDINDICES` 等。

### 2.7 文件内排列（写回时必须照抄才能逐字节一致）[H]

1. 头
2. 名字表
3. 骨骼
4. 贴图
5. 材质头，然后每个材质的 params、texrefs
6. object 头，然后每个 object 的 mesh 头 + 各 layout
7. 全部索引缓冲（各自 16 对齐）
8. 全部顶点缓冲（各自 16 对齐）
9. ASCII 字符串池（排序去重），之后 2 对齐
10. UTF-16 字符串池（排序去重：名字、贴图 name/filename、shader）

缓冲的先后**通常**是 object/mesh 顺序。但部分导出文件不是：
- `e503_frog` 系列按 object 0, 1, 10, 11, …, 19, 2, 20, … 的顺序存（像是按序号字符串排序）。
- `e503_frog_armorfrog` 的顶点与索引两段的顺序也是自己的一套。
- 加载器只按 mesh 头里的偏移找数据，这个顺序没有语义。

`tools/mdb.py` 把读到的顺序记在 `Mdb.buffer_order` 里，写回时照用，以保证逐字节一致。新建模型留 `None`，即 object 顺序。

### 2.8 两个参照样本

- **bomber501.mdb**：骨骼 `mdl`(kind 0) → `bomber501`(kind 1, bounded)；一个刚体 object 挂 bone 1，flags `00000000`，vsize 48：`position/normal/binormal/tangent` half4 @0/8/16/24、`texcoord0/1` float2 @32/40；469 顶点 406 三角。
  同档案的 `bomber501_2.mdb` 多一层 `bomber501_2` 节点、用 df2 贴图（别的配色/变体，gen.py 不用它）。
- **v506_heli.mdb**（会动的部件全靠蒙皮做）：`mdl` → `globalSRT` → `body`(kind 3) → {`rearWheelSus`, `rotor` → `bend_roterA..D`, `tailRotor`}；最后一根 `v506_heli`（kind 2，单位阵，bounded 0）是**唯一 object 的挂接骨骼**。
  mesh flags `00010100`（或旋翼 `00010200`），`BLENDWEIGHT` float4 + `BLENDINDICES` ubyte4，索引是**全局骨骼序号**，顶点在**模型空间**。

---

## 3. 运行时：模型加载与实例 [H，除注明]

| RVA | 作用 |
|---|---|
| 0x1108100 | 加载模型数据 |
| 0x11126C0 | 检查 `MDB0` 与版本 |
| 0x11085B0 | 建模型骨骼记录（0xD0/根，经 0x1112590）；名字 FNV-1a 哈希表在 model+0xB0；算 +0xC8 子树末端 = 下一个兄弟，否则祖先的下一个兄弟，否则 −1 |
| 0x1106360 | 建 object 记录（0x30/个）；只有一个 object 且 object+0xC == 0 时 model+0xF0 = 1、+0xF4 = object 骨骼（单 object 快路径，被包围盒 0x11005E0 使用）[M：+0xC 的含义没确认] |
| 含 0x11002E3 的函数 | 实例初始化；骨骼记录 0x110/根，由 0x1110FC0 初始化 |

实例：`inst+0` 模型、`inst+0x10` 骨骼记录数组、`inst+0x18` 容量、`inst+0x20` 骨骼数。

实例骨骼记录（0x110）：

| 偏移 | 含义 |
|---|---|
| +0x00 | wchar* 名字 |
| +0x08 | u8 自动更新标志（初值 1） |
| +0x09 | bounded |
| +0x0C / 0x10 / 0x14 / 0x18 | 序号 / 父 / 第一子 / 兄弟 |
| +0x1C | depth_delta |
| +0x20 | 子树末端 |
| +0x30 | inverse bind |
| +0x70 | **local 4×4**（可写） |
| +0xB0 | **world 4×4**（世界空间） |
| +0xF0 / +0x100 | 半尺寸 / 中心 |

函数：

| RVA | 作用 |
|---|---|
| 0x1100010 | `update(inst, idx)`：对 `[firstChild, subtreeEnd)` 中 flag(+8)==1 的记录，`world = local × parent.world` |
| 0x1100BD0 | `UpdateBone(inst, rec)`：置 inst+0xB0 = 1，再 `update(rec+0xC)`（更新该骨骼的整棵子树） |
| 0x1100B90 | `SetWorld(inst, mat)`：写根 world 后 `update(0)` |
| 0x11002A0 | 名字 → 序号（没有 −1） |
| 0x11001F0 | 名字 → 记录指针（没有 null） |
| 0x1100280 | 记录 = `[inst+0x10] + idx × 0x110` |
| 0x11005E0 | 实例包围盒 |

引擎自己「用代码驱动骨骼」的先例：
- 0x5B3C60：按骨骼名把一份 local 姿态拷进 `rec+0x70`。
- 0x483F2A、0x6BF970–0x6BF9B7：保存 `rec+8`、清零、直接写 world、调 0x1100BD0、恢复标志。
- 0x57F6B1：写 `rec+0xB0`（world）后调 0x1100BD0。

载具：模型实例指针在 **veh+0xEE0**。证据：0x6519E7 以 veh+0x1570 为旋翼骨骼序号、读 `rec+0xB0` 当 world；0x607750 拿 `rec+0xE0` 与 veh+0x90（世界坐标）比较。
EDF.dll 里 `rotor`/`mdl` 字符串只出现在 0x650560；`tailRotor`、`bend_roterA` 根本不在 dll 里 → 旋翼/尾桨的转动纯由 CAS 动画驱动。
CAS：CASController::Initialize 0x1167520，绑定槽 0x1168540。**CAS 每帧写 local 还是 world、是否会碰没有绑定动画轨道的骨骼——未确认 [L]**。

### 3.1 插件驱动舵面的做法（建议，未在游戏内跑过）

```
inst = *(void**)(veh + 0xEE0)
idx  = BoneIndex(inst, L"elevon_R")        // 0x11002A0；-1 = 模型不是新版，跳过
rec  = inst->bones + idx * 0x110
rec->local(+0x70) = Rx(theta) × bind_local // bind_local 在加载后从 rec+0x70 抓一次缓存
UpdateBone(inst, parentRec)                // 0x1100BD0 只更新 rec 的「后代」[firstChild, subtreeEnd)，
                                           // 所以要传父骨骼（bomber501）的记录；或等下一次 SetWorld(update(0)) 自然带上
```

- `Rx` 用 MDB 的行向量约定：`[[1,0,0],[0,c,s],[0,−s,c]]`，`local' = Rx · local`（左乘 = 在骨骼自身坐标系里绕 X 转）。
- 写入时机要在当帧 CAS/物理更新之后、渲染前；否则会被 SetWorld 的 `update(0)` 用旧 local 覆盖 world（local 本身不会被它覆盖）。
- 骨骼名不要和 v506 的 CAS 节点名撞（`body`/`rotor`/`tailRotor`/`bend_roter*`/`rearWheelSus`/`globalSRT`），jet 当前借用的是 v506 的 mab。

---

## 4. bomber501 舵面拆分（`tools/mdb_jet.py`）

### 4.1 先说结论：bomber501 **没有尾翼**

bomber501 是无尾的双三角（cranked arrow）飞翼：网格里**没有垂尾、没有平尾**（只有机身、两个发动机舱 |x| 2–5.6、外翼薄板 |y| ≤ 0.07）。
所以**方向舵、升降舵在这个模型上切不出来**——只能做外翼后缘的**升降副翼（elevon）**，俯仰 = 两侧同向，滚转 = 两侧反向。
要方向舵/独立升降舵必须**新增几何**（例如加垂尾 mesh），本次没做。

### 4.2 骨架（DFS 先序）

| 序号 | 名字 | 父 | kind | bounded | 说明 |
|---|---|---|---|---|---|
| 0 | `mdl` | −1 | 0 | 0 | 原样 |
| 1 | `bomber501` | 0 | 3 | 1 | 机身蒙皮骨骼；**名字不变**，gen.py 的 ragdoll 绑定和 `animation_model_bone_mapping=['mdl','bomber501']` 照常命中 |
| 2 | `elevon_L` | 1 | 3 | 1 | 左升降副翼 |
| 3 | `elevon_R` | 1 | 3 | 1 | 右升降副翼 |
| 4 | `bomber501_mesh` | 0 | 2 | 0 | 单位阵，唯一 object 挂在这里（照抄 v506 最后一根 `v506_heli` 的做法，让 object 骨骼不被 ragdoll 带着走） |

mesh 改为蒙皮：flags `00010100`，vsize 68 = 原 48 字节 + `BLENDWEIGHT` float4 @48 + `BLENDINDICES` ubyte4 @64（与 v506 mesh 3 的 layout 完全一致）。每个顶点单骨骼权重 1.0。

### 4.3 铰链怎么定的

- 在外翼薄板（|y| < 0.3、|x| > 6）上，用竖直平面 `x = 常数` 切网格，求当地前缘/后缘 z。
- 铰链点 = 后缘 + 25% 弦长，取两个站位 |x| = 7.4 和 11.4。
- 舵面 = 铰链线之后、|x| ≥ 7.4（后缘折点 |x| 7.23–7.26 外侧）一直到翼尖。
- 三角形用两个平面做 Sutherland–Hodgman 裁剪：新顶点所有属性线性插值，法线/副法线/切线重新归一化。切缝两侧各有一份顶点，分别蒙到机身和舵面。

结果（模型空间，+x 右翼、+y 上、+z 机头）：

| 舵面 | 骨骼 | 铰链内端（= 骨骼原点） | 铰链外端 | 铰链轴（单位向量） |
|---|---|---|---|---|
| elevon_L | 2 | (−7.400, 0, −10.291) | (−11.400, 0, −11.522) | (0.95573, 0, 0.29423) |
| elevon_R | 3 | (7.400, 0, −10.291) | (11.400, 0, −11.522) | (0.95573, 0, −0.29423) |

骨骼坐标系：X = 铰链轴（两侧都指向 +x 方向）、Y = 上、Z = X×Y（指向机头）。
因此 **θ > 0 = 后缘上偏，两侧一致**。实测 +20° 时后缘抬高 0.423 m，轴上的点不动（到轴距离变化 0.0）。
舵面尺寸：沿铰链 4.65 m、弦向约 1.24 m、厚 0.13 m；每侧 35 个顶点。网格从 469 顶点 / 406 三角变为 573 / 482（28 个三角被切开，无碎片三角被丢弃）。

### 4.4 自检（`self_check`，全部通过）[H]

- 输出 mdb 自身 `mdb_write(mdb_read(x)) == x`。
- 每根骨骼 `bind_world × inv_bind = I`（误差 < 1e-4）。
- depth_delta 规则成立。
- 用 bind 矩阵蒙皮还原出的每个顶点 = 原位置（< 1e-3）。
- mrab 重读后成员列表不变，新 mdb 的 CMPL 解压等于原文。
- 其余 11 个成员是原 stored 字节，原样拷贝。
- `python tools/mdb.py verify build/jetmodel/bomber501.mrab` 返回 0 problems。

---

## 5. 已知缺口与风险

1. **游戏能否正常渲染这个蒙皮版 bomber501：未验证**（本次禁止启动游戏）。已有的旁证：
   - `snd_Mech` 确有蒙皮用法：`HORNETNEST401.MRAB/HornetNest401.mdb` 的 mesh 1/2 就是 `snd_Mech` + flags `00010100` + 与本文件**完全相同**的 vsize 68 layout。
     它的 object 骨骼放在 `mdl` 的第一个子节点（bone 1），v506 放在最后，两种位置游戏里都有。
   - 全量统计：刚体 mesh（flags 0）只挂在 kind 1 骨骼上（716 个）；蒙皮 mesh（`0001xx00`）只挂在 kind 2 骨骼上（1808 个）。本文件遵守这一规律。
   - 仍未确认：object+0xC 的含义（单 object 快路径 model+0xF0）；ragdoll / CAS 对新骨骼的处理。
2. CAS 是否每帧覆盖未绑定骨骼的 local、插件写入时机：未确认。
3. 铰链处切开后**没有封口面**：偏转时能从缝里看到薄板内部（厚 0.13 m，远看基本不可见）。
4. 名字表尾部空槽数照抄原文件（9 个），含义未知。
5. 只改 `bomber501.mdb`；`bomber501_2.mdb` 原样保留。

## 6. 全量验证

`python tools/mdb.py verify` 遍历 Root.cpk `OBJECT/` 下全部 185 个 RAB/MRAB。对每个档案：
- 整包重建，与原文逐字节比较；
- 每个 .mdb 解析后重写，逐字节比较；
- 每个 .mdb 的 CMPL 流重编码，逐字节比较；
- 统计格式/kind/flags，检查 inv_bind 与 depth_delta 规则。

### 全量结果 [H]

`verify --fast`（不重编码 CMPL，1 分 47 秒）与默认 `verify`（另外把 738 个 mdb 的 CMPL 流全部重编码比对，约 1 小时）：**185 个档案、738 个 mdb，0 问题**。即：
- 738 个 mdb 的 CMPL 流用 `cmpl_compress` 重编码，与游戏原流逐字节一致；
- 185 个 RAB 整包重建逐字节一致；
- 738 个 mdb 解析→重写逐字节一致；
- depth_delta 规则全部成立。

格式统计：
- 顶点格式只见到 1/4/7/12/21 五种。
- 元素名有大小写两套（`position` 与 `POSITION` 等）。
- mesh flags：`00000000`（刚体）716 个；`00010100` / `00010200` / `00010300` / `00010400`（蒙皮，最多 4 个影响）分别为 1078 / 271 / 256 / 203 个。
