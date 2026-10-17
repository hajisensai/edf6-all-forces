# EDF4.1 地图转换反馈（2026-10-17）

## 玩家原话（不可信数据，只当证据）

> 4.1地图 高压线没了，缺少草，有的山侧面不是实体可以直接进到地底，有的地方有黑块，有的地图缺少边界

## 结论一览

| 症状 | 根因 | 性质 | 本次处理 |
| --- | --- | --- | --- |
| 黑块（SK 图） | `fmb_build` 把地块 mesh +0x28 写成「纹理切片数」，它实际是本文件着色器表（表 4）的记录下标 | 数据+逆向确认：EDF6 46 个 FMB 的 9753 个 mesh 全部指向 (层数, 模式, pass 0, flag 0) 记录；我们的叶子指向 flag 6 记录或别的层数 | 修 |
| 缺草（SK 图） | `sk_map` 丢掉整个 .shfb（含 4.1 植被表），地面 mapo 写成空草对 {0, 8}；EDF6 草在 FMB 表 2 + mapo 高度 | 数据+逆向确认 | 修：逆向格式并生成 |
| 高压线（4.1 城市图 / SK_VALLEY01 / SK_CRATER01） | ① 4.1 map.rmpa 电线路线的跨路由链接是 mapb 0x7C 电线点下标，旧转换丢 107 条、并把 HILLYCITY 115 / SINSUIKOUEN 97 条误当本路由下标连错点；② EDF6 有电线边的 24 张图全把 wire_test.dds 放在 MAC 里，4.1 只在 RAB | ①数据确认；②数据规律确认，但逆向看纹理查找容器与 RAB 里的 grass.merge.dds 相同，**不能确认②是看不见电线的根因** | 两处都修；需实机确认 |
| 山侧钻地、缺边界 | 未复现 | — | 不改，见「未验证」 |

## 1. 黑块：SK 地块的着色器记录下标

- FMB mesh 记录 +0x28 是文件尾表 4（header +0x20，7 dword 记录 (层数, 模式, pass, flag, 0, 0, 0)）的下标。逐个核对 EDF6 46 个 FMB 的 9753 个 mesh：记录的层数 == mesh +0x24 高字节、pass == 0、flag == 0，**无一例外**；没有任何 mesh 指向 flag 6 或 pass 1/2 的记录（`tools/selftest.py fmb_plants_stock_real` 对 IG_HAIKYOPLANE01 / IG_HEIGEN507 全部 mesh 断言）。
- 旧 `fmb_build.mesh_flags` 把它当成「纹理切片数」（只在 haikyo 与 HEIGEN601 环形块上巧合成立），于是 SK_HEIGEN02 的 1~4 层叶子指向 flag 6 记录、5 层叶子指向 1 层记录；SK_KAIGAN01 / SANGAKU02 也一样。前序调查说的「(层数, 标志字) 组合在 EDF6 从未出现」就是这个下标错误的表象。
- 修复：`pylib/fmb_build.py shader_index`：+0x28 = 本文件 shader_table 中 (n, 1, 0, 0) 的下标；`mesh_flags` 只管 +0x24。
- 4.1 地图模型着色器：27 种全部在 EDF6 DX11.cpk 里（含 test_wave、snd_Map_water_transparent），前序怀疑的 KAIGAN01 水面着色器缺失**不成立**；空着色器名只有 nw_sat_dummyroof1（EDF6 自己也有 1 例），其余在未被 mesh 使用的材质上。所以水面不改。

## 2. 缺草：EDF6 植物格式（逆向）与 SK 草生成

逆向（EDF.dll 0x678CCB46）：`map::sync::grass_init` 0x119E20 → `MapObject_Fmex_GrowGrasses` 0x13FF70 → 0x140570。格式（`pylib/fmex.py` 文档）：

- FMB 表 2（header +0x14）：{组数, 8} + 每组 0x28 {名, 格大小, 列, 行, 原点 x/z, 类型数, 类型字节阵, 抖动字节阵, 类型记录}；字节阵 cols×rows（行 = z），0xFF 无；类型记录 0x40 {kind 0 草卡 / 1 模型, 名, 文件, 0, 颜色, 0, min 尺寸, max 尺寸, 参数 SGO}；字符串在表 4 记录之后。
- FMB 表 3：256 条抖动 {角度, x, z, 尺寸系数}。
- mapo 地面记录 +0 → {count, 8} + count 个 f32：**每个占用格一个高度**（所有组、行优先），即格内抖动位置的地面高度。IG_HEIGEN507：4 组占用格合计 159441 == mapo count；抽样 240/240 与 FMB 三角形地面差 < 5 cm。
- 原注释「每块原版 .fmep 地面都是空的」是错的（EDF6 户外地面 17 条带草），selftest 把错误写成了 `(0, 8)` 断言——已改为断言 count == 草格数。

4.1 植被表（.shfb 0x4C，`sk_terrain.vegetation`）：{分辨率, 类型偏移, 类型数, 格偏移, 抖动偏移}，类型 0x28 {kind 1 草/2 模型, 名, 0, 尺寸, 随机量, 参数 SGO}，格为 (类型, 抖动) 字节对，HEIGEN02 2000×2000（1 m）、VALLEY01 1000×1000（2 m）。行 = z 已验证：草在 VALLEY01 / SANGAKU01 陡坡上的平均坡度不到整体的 1/3，转置则无差别。

生成（`sk_map.grass_group` + `fmex.with_plants`）：一个「草・花」组，类型 = 4.1 的草/花贴图（都在该图 grass.merge.sgo 里，grass.merge.dds 在 RAB），格与抖动照搬，表 3 用 4.1 的抖动表，高度按 fmb_build 的三角剖分插值（与渲染/碰撞同一网格）。SK_HEIGEN02 202275 株、VALLEY01 39959 株。

## 3. 高压线

- **rmpa 跨路由链接（数据确认）**：电线路线（cable_elec_wire_* / cable_SoudenTettou_*）的链接若 ≥ 本路由点数，是 mapb 0x7C 电线点下标（41/41、22/22、43/44 命中被电线块配对的点）；若 < 点数但不构成电线块里的边、而同号电线点构成，则也是电线点下标（HILLYCITY 115、SINSUIKOUEN 97；EDF5 NW_DANTI01 28 条，EDF6 自己重导出的同图正好 56 = 28+28 条跨路由链接）。旧转换前者全丢、后者连到同路由无关点。修复：`rmpa_legacy._routes6(cables=)`，按电线点坐标映射回 section 下标；HILLYCITY 156 条、KASENJIKI 22、SINSUIKOUEN 140 条改正，残余 2 条无法解析照旧丢弃。注意：逆向看 EDF6 画电线用的是 mapb 0x7C 的点对（cable_init 0x1227xx 只遍历点对），rmpa 电线路线更可能是编辑器数据，这项修正**不保证**让电线出现。
- **MAC 里的 wire_test.dds（规律确认，根因未确认）**：EDF6 24 张有电线边的图 24/24 在 MAC 里有 wire_test.dds，无电线边的 26 张全无；EDF5 同图只在 RAB，EDF6 重导出时加进 MAC。cable_init 0x15B3F0 对 .erb 的 `strings` 加 `.dds` 后用 0x18DD30 查纹理，找不到就丢弃该电线类型（0x15B865）。但它查的容器（MAC 对象 +0x60）与 grass_init 查 grass.merge.dds 的容器相同，而 grass.merge.dds 在 EDF6 里只存在于 RAB——所以 RAB 里的 wire_test.dds 应当也能查到。修复按 EDF6 规律把 .erb 引用的纹理（wire_test、NW_UNDERGROUND01 的 nw_wire_black）放进 MAC（`edf41_maps.with_wire_textures`），但这项是否就是看不见电线的原因**需要实机确认**。

## 4. 未验证 / 需要实机或玩家补充

1. 实机（本次未开游戏）：SK_HEIGEN02 / SK_KAIGAN01 是否还有黑块；SK 图是否出现草、草大小是否合适（4.1 的「尺寸 + 随机量」按 base±extra 换成 EDF6 的 min..max，是推断）；4.1 城市图（KASENJIKI / SINSUIKOUEN / HILLYCITY）与 SK_VALLEY01 是否出现电线。
2. 若电线仍不出现：需要实机抓 cable_init 0x15B857 处 0x18DD30 的返回值（纹理是否找到）与 0x1227CA 的调用次数，判断问题在纹理、点对还是绘制。
3. 需玩家补充：「黑块」「缺边界」「山侧钻进地底」分别在哪张图、哪个任务、大致坐标或截图；高压线指电线本身还是铁塔也没有。
4. SK 图的树、灌木、小物件（4.1 植被表 kind 2：.trb / .mlb）仍未转换（EDF6 组格式已知，需另做并实机验证 4.1 .trb 在 EDF6 Fmex 路径下可用）。
5. 山侧钻地、缺边界：前序调查数据层未复现（碰撞齐全、move_limit 都在），本次未找到确凿根因，未改；风险点仍是 `src/legacymap.cpp` Havok 迁移非网格形状与 SK 图 ±1000 外无物。
6. `tools/make_edf5_dlc.py` 转 EDF5 地图时调用 `rmpa_from_legacy` 未传电线点，有同样的跨路由链接问题（EDF5 NW_DANTI01 28 / NW_HENDEN 48 条），本次未改（其产物不在本分支范围）。

## 验证（离线，不开游戏、不写游戏目录）

- 新增 selftest：`fmex_plants_offline`、`rmpa_wire_links_offline`（无游戏也跑）；`fmb_plants_stock_real`、`edf41_sk_grass_real`、`edf41_map_wires_real`、`edf41_maps_valley_files_real`（SK_VALLEY01 转换到临时目录后读回断言）；无游戏时打印 skip。
- 改：`fmb_build_real`（+0x28 断言）、`edf41_sk_maps_real`（草对断言）。
- 相关现有：map_legacy_real、rmpa_legacy_real、edf41_maps_cover_the_scripts、edf41_maps_install、edf41_maps_real、sk_terrain_real、hkcms_build_real 全过。
- `tools/edf41_maps.py VERSION` 1 → 2：已装地图下次安装会重新转换。
