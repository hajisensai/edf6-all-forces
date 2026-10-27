# 生成载具的网格合并与 LOD（2026-10-27）

用户反馈：生成的载具「为了限面所以难看」。根本做法不是继续限面，而是：(1) 远处换低模（LOD），(2) 同材质网格合并减少绘制批次。
代码：`pylib/model_lod.py`（合并、LOD 生成、SGO LOD 表、检查）、`pylib/mesh_simplify.py`（简化器），
接入点是每个载具生成器 `build()` 的最后一步 `model_lod.finish(files)`；测试 `tests/test_model_lod.py`（ctest `model_lod`）。

置信度：**H** = 指令 / 实物数据直接读到；**M** = 有代码证据的推断；**未验证** = 需实机。LOD 的切换本身**只有静态分析，没有实机看过**。

## 1. 证据（EDF.dll，TimeDateStamp 0x678CCB46，RVA）

- **H** 通用模型加载 `0x6BB890`（经 `0x6BF380(model, ?, sgo, L"animation_model", 1)`）在 `0x6BC6B0..0x6BCB2F` 遍历
  `animation_model[0][2]` 的每一项 `[mdb 名, 距离]`：按资源类型 Model 在同一档案里找该 mdb，`0x11031E0` 建 LOD 项（`model+0x250 + i*0xF0`），
  距离以**平方**存进 `+0xE8`（`0x6BCB03 mulss` / `0x6BCB13`）。
- **H** 绘制 `0x6BF9F0` 从基础 LOD（`model+0x160`）开始顺序遍历 LOD 数组，`+0xE8 > d²` 即停：表必须**按距离升序**；所有 pass（含阴影）同一选择。
  `d` 取 `model+0x2C` = max(镜头距离 − 包围半径, 0)（`0x6BDCB5` 把选择字节写成 2；字段语义 M，docs/fp-render-re.md §3）：**米，到包围球表面**。
- **H** `0x11031E0`：`lod+0x58 = model+0xA0`，**所有 LOD 共用基础模型的骨架实例**；`lod+0x38` 按该 LOD mdb 的骨骼数分配。
  原版 45 对 HD↔LOD（30 个档案）骨骼数与顺序全同。→ LOD 必须与 HD 同骨架（数、序、父子、绑定）、同材质 / 贴图表，只减三角形。
- **H** 载具走同一路径：`VehicleBase` 构造 `0x629450` 在 `0x629614..0x629629` 以 L"animation_model" 调 `0x6BF380`。
  EDF.dll RTTI 里派生自 `VehicleBase` 的全部类（类层次描述符逐个读出）：`Vehicle402_Rocket 403_Tank 404_Tank 501_FortressRobo
  502_GroundRobo 503_Bike 504_begaruta 505_Tank 506_Helicopter 507_Rescuetank 510_Maser 511_Bike 601_Tank 603_Flak 607_RoboTruck
  60X_Truck 612_nix VehicleBegaruta VehicleBigBegaruta VehicleHelicopter409/410 VehicleHelicopterBase Vehicle_BikeBase Vehicle_Car
  Vehicle_CarBase Vehicle_TankBase` = `model_lod.VEHICLE_CLASSES`。原版没有一辆载具带 LOD（Root.cpk 155 个带 LOD 表的 SGO 全是兵、虫、UFO）。
- **H** 索引是 u16（原版 738 个 OBJECT mdb 无 32 位索引先例）；`SND_BRDF_*` VS 的 `g_xgl_world_skinning[1024]`、`BLENDINDICES` ubyte4：
  一个网格可引用任意 <256 号骨骼，所以「一材质一网格、每顶点自带骨骼」合法（原版 257 个 mdb 有 556 个多骨骼单权重网格）。
- 插件侧 `src/firstperson.cpp:85-92`（`kBaseLod / kLods / kLodCount / kLodInst`）本来就按 LOD 数组遍历骨架实例；LOD 共用基础实例，座舱壳骨骼的隐藏照样生效。

## 2. 设计

### 2.1 合并（`model_lod.merge_meshes`、`model_lod.pack_rows`）

- 每个 object 内，(材质, flags, 顶点布局) 相同的网格把顶点行与索引直接拼接（字节不变），只在 65535 顶点处切（u16）。同一份三角形、同一份顶点，绘制次数减少
  （每个网格在 ZPass、G-buffer、每级阴影级联各提交一次）。
- WT / X-Morph 履带车在 `wt_tracked.pack` 就按材质跨骨骼打包（以前一部件一骨骼一网格、21000 三角形一切），用的是同一个 `pack_rows`。
  OBJ 路线（沙扎比、无畏舰、钻地车、双炮塔、座舱……）`obj_model.build_meshes` 早已按材质合并；`finish` 对所有载具模型再跑一遍，已合并的不变。
- 碰撞不受影响：`tracked_collision` 用 model.json 的部件（`wt_tracked.parts`），不读 MDB 网格。

### 2.2 简化器（`mesh_simplify.simplify`）

- 二次误差（QEM）**半边坍缩**：顶点只会坍到已有顶点上，所以 LOD 的顶点是 HD 打包行的子集——不重编码、不插值、不改蒙皮，包围盒只会缩小。
- 位置相同的行焊接成拓扑；行分「类」：除法线 / 切线 / 副法线外完全相同的行是同一类（**折痕**，可跨越；WT 的 CAD 模型几乎每个顶点都有硬边法线），
  不同类在边两侧即**接缝**（UV、材质、骨骼），接缝顶点只能沿接缝走，两侧一起动（不开裂）；每个行坍到目标点上同一类、共边面上的行，否则法线最接近的行。
- 开放边界只能沿边界走，边界 / 接缝边加垂直平面；骨骼与材质由 `group` 键保证：**永不把一个三角形拉到两根骨骼上**。
- 非流形边、绕序不一致、边界 / 接缝交汇点、接缝端点锁死；每次坍缩过链接条件（不生成非流形边），面法线转角 ≤ ~78°。
- 误差：每个面的平面计一次（不按面积加权），坍缩误差 = 目标点到所携带平面距离平方和的根（≥ 到任一平面的最大距离）。
  按面积加权（Garland-Heckbert 原式）时细长件的平面没有分量，V608 的 1.3 m 天线整根坍掉；按平面数取均方时车轮侧面大量平面把轮缘被拉进去的误差摊薄，
  2S6 的负重轮在 LOD2 成了星形（离线渲染比对后改）。
- 坍缩成一张薄片的闭合细件（天线、炮管）保留为双面薄片，不删；`drop_small` 先删掉对角线小于该级误差（一个像素）的独立小件（螺栓、灯）。
- 并行：每轮每个顶点取最便宜的有效坍缩，取最便宜的 30%，再用 Luby 式极大独立集（排名 = 半倍频误差档 + 固定打乱的顶点号）选出互不相干的一批同时执行。
  稳定排序、平手按下标：同样输入同样输出（测试覆盖）。纯 numpy（安装器只打包 numpy / Pillow，不带 scipy）。

### 2.3 LOD 级别与距离（`model_lod.LEVELS`）

- 距离按模型大小缩放：原版兵（中心到包围盒角约 1 m）LOD 在 15 / 30 m（docs/edf41-outfits.md；4.1 / EDF6 飞翼、剑兵同），
  所以 `d = reach × 模型半对角线`，reach = 15、30：一辆半对角线 4.5 m 的坦克在 ~67 / ~135 m 换，与它在那个距离上的屏幕大小等于兵在 15 / 30 m 时一致。
- 每级目标三角形 35% / 10%（相对外壳），但**误差封顶一个像素**：60° 垂直视角、1080 行时每米距离 `PIXEL ≈ 1.07e-3 m`，即 LOD1 允许 ~0.016 × 半对角线。
  到不了目标就停在误差上（多出来的三角形在那个距离上看不出），比上一级省不到 20%（`KEEP_AT_MOST`）的那一级不写（白占内存），下一级从同一份三角形、更远的距离再试。
- 座舱（材质 `edf6vc_cpit_*`）不进 LOD：镜头在座舱里时距离为 0，画的是基础模型。
- 档案成员 `<mdb>_lod1.mdb` / `_lod2.mdb`，与基础模型同目录（`insert_member`）、同压缩；SGO `animation_model[0] = [档案, 模型, [[lod1, d1], [lod2, d2]]]`
  （DSGO 与旧 SGO 两种格式都支持，旧 SGO 用能逐字节复现原文件的那个写法回写）。

### 2.4 检查（`model_lod.check_lods` / `check_files`，`finish` 每次都跑）

- LOD 与基础模型：骨骼、名表、材质表、贴图表、objects **完全相等**；每个网格的布局是基础模型该材质已有的；索引合法、≤65535 顶点；
  三角形逐级减少；距离升序；LOD 的包围盒不超出基础模型、每边缩进不超过对角线的 5%（`TOLERANCE`）。
- 生成器自己的检查（`make_tracked_combat.check`、`make_artillery / drill / katyusha / sazabi / sidecar / sub / dreadnought.check`）
  改为只比 `animation_model[0][:2]`，有 LOD 表时用 `model_lod.check_files` 对档案核对。

## 3. X-Morph 履带不滚动

- **H** `tank_caterpillar_animation`（`0x67E4B0`）固定读 **2** 行 `[材质名, 参数名]`（`edi < 2` 循环，`0x6DCC80` 绑定材质参数），
  `tank_caterpillar_animation_info[0]`（float）存 `+0x2960`、`[1]`（int，分量号）存 `+0x2964`。
- **H** 每帧 `0x67EADB`：`offset = frac(info[0] × K × 履带速度 + offset)`，写进参数向量的 `info[1]` 分量。
- X-Morph 的履带是车体图集上的静态图，`prep_xmorph` 只是借 `v603_kyata_L / _R` 这两个名字。清空列表会让固定读 2 行的代码越界，
  改材质名会让绑定找不到材质——都不是根因修法。**做法**：`make_tracked_combat.STATIC_TRACKS`（全部 X-Morph 履带单位）的 SGO 把
  `tank_caterpillar_animation_info[0]` 置 0：偏移恒为初值，贴图不动；两行绑定保持原样。`check` 校验：X-Morph 单位为 `[0, …]`，其余与 V603 原版一致。
- 未验证：偏移初值（`entry+0x64`）在构造时是否为 0 未追；即使不是 0 也是常数，不滚动。

## 4. 覆盖范围

`model_lod.finish` 接在这些生成器 `build()` 的最后（座舱嫁接之后，基础模型此时已是最终形态）：
`make_tracked_combat`（vulcan、bmpt、quadvulcan、全部炮台、tunguska、pzh2000、sturmtiger、apocalypse、tos1 …、X-Morph 单位）、
`make_jets`、`make_sub`、`make_sazabi`、`make_dreadnought`、`make_katyusha`、`make_artillery`、`make_drill`、`make_sidecar`、`make_sedan`、
`make_chute`、`make_optics`。它只处理「类在 `VEHICLE_CLASSES` 里、`animation_model[0]` 指向本次输出里的档案」的 SGO。

不做 LOD 的，及原因：

- **make_chute**（降落伞）：类 `FarEventObject`，RTTI 不派生自 `VehicleBase`，它的模型加载是否读 LOD 表未查 → `finish` 不碰。
- **make_proteus / make_emc / make_ultimate / make_barrage / make_bigmap**：不产出载具模型（护盾、弹、地图），`finish` 没有接。
- **省不到 20% 的那一级**：不写，下一级从同一份三角形在更远处再试（emp_short / emp_long ~3k 三角形只有 LOD2 距离上的一级；无畏舰轰炸机 936 三角形一级都没有）；合并照做。
- **原版档案**（没被生成器写进输出的，例如直接引用 `v506_heli.mrab` 的 SGO）：不是我们的模型，不改。
- **测试场**（`testrange/gen.py vehicle_sgo`）：它从生成器的 `vehicle_sgo()` 现做任务用 SGO，没有 LOD 表；装好的档案里有 LOD 成员，
  只是测试场不切换（不影响正常游戏）。

## 5. 数字

（见下表；离线用各生成器 `build()` 生成到 scratch，不进游戏。「前」= 同一份代码把 `finish` 换成恒等、`wt_tracked.pack` 换回旧的一部件一网格。）

| 生成器 | 档案 | HD 三角 | LOD1 / LOD2 | 网格 前→后 | LOD 网格 | 档案 MB 前→后 | 生成耗时变化 |
|---|---|---|---|---|---|---|---|
| jets | EDF6VC_CENTIPEDE.MRAB | 1972 | 1546 (78%) @112 m / 1044 (53%) @223 m | 4 → 1 | 1/1 | 8.77 → 8.83 | -6 s |
| jets | EDF6VC_DRAGONFLY.MRAB | 2788 | 1346 (48%) @148 m / 823 (30%) @296 m | 5 → 5 | 5/5 | 10.97 → 11.02 | -6 s |
| jets | EDF6VC_CARRIER.MRAB | 112295（外壳 4406） | 2312 (52%) @740 m | 40 → 39 | 1 | 15.06 → 15.17 | -6 s |
| jets | EDF6VC_DRONE.MRAB | 4176 | 1909 (46%) @53 m / 1238 (30%) @106 m | 2 → 1 | 1/1 | 2.06 → 2.18 | -6 s |
| jets | EDF6VC_JET.MRAB | 104877（外壳 1762） | 892 (51%) @296 m | 44 → 40 | 2 | 18.77 → 18.83 | -6 s |
| jets | EDF6VC_GUNSHIP.MRAB | 108279（外壳 478） | 313 (65%) @817 m | 40 → 39 | 1 | 7.44 → 7.46 | -6 s |
| jets | EDF6VC_INTERCEPTOR.MRAB | 72392（外壳 1762） | 880 (50%) @193 m | 42 → 38 | 2 | 17.06 → 17.12 | -6 s |
| jets | EDF6VC_MULTIROLE.MRAB | 104740（外壳 1758） | 946 (54%) @205 m / 701 (40%) @409 m | 44 → 40 | 2/2 | 17.63 → 17.73 | -6 s |
| jets | EDF6VC_HELI_TRANSPORT.MRAB | 55813（外壳 10264） | 4461 (43%) @240 m / 3007 (29%) @479 m | 38 → 36 | 4/4 | 19.70 → 19.87 | -6 s |
| jets | EDF6VC_PRIMER_FIGHTER.MRAB | 106622（外壳 2070） | 904 (44%) @169 m / 501 (24%) @339 m | 40 → 39 | 1/1 | 11.87 → 11.91 | -6 s |
| jets | EDF6VC_TRANSPORT_PLANE.MRAB | 108710（外壳 478） | 311 (65%) @817 m | 45 → 44 | 1 | 9.06 → 9.07 | -6 s |
| sub | EDF6VC_SUB.MRAB | 111926（外壳 5896） | 2144 (36%) @13372 m / 1374 (23%) @26745 m | 40 → 39 | 1/1 | 45.25 → 45.45 | +10 s |
| sazabi | EDF6VC_SAZABI.MRAB | 276072 | 96626 (35%) @325 m / 27608 (10%) @650 m | 10 → 10 | 6/3 | 9.24 → 13.55 | +39 s |
| dreadnought | EDF6VC_DN_BOMBER.MRAB | 936 | — | 1 → 1 | — | 1.58 → 1.57 | -14 s |
| dreadnought | EDF6VC_DN_DEFENDER.MRAB | 1170 | 778 (66%) @267 m / 582 (50%) @533 m | 1 → 1 | 1/1 | 1.08 → 1.14 | -14 s |
| dreadnought | EDF6VC_DN_FIGHTER.MRAB | 1936 | 1486 (77%) @516 m | 1 → 1 | 1 | 1.79 → 1.87 | -14 s |
| dreadnought | EDF6VC_DN_INTERCEPTOR.MRAB | 4064 | 2653 (65%) @274 m / 1944 (48%) @549 m | 1 → 1 | 1/1 | 1.11 → 1.32 | -14 s |
| dreadnought | EDF6VC_DN_DREADNOUGHT.MRAB | 71389 | 33379 (47%) @4204 m | 10 → 10 | 10 | 23.52 → 24.92 | -14 s |
| katyusha | EDF6VC_KATYUSHA.MRAB | 55893（外壳 13434） | 6020 (45%) @70 m / 4189 (31%) @139 m | 37 → 34 | 2/2 | 21.18 → 21.43 | +6 s |
| artillery | EDF6VC_ARTILLERY.MRAB | 32382（外壳 7478） | 4302 (58%) @69 m / 3132 (42%) @137 m | 38 → 33 | 6/5 | 18.03 → 18.29 | +4 s |
| drill | EDF6VC_DRILL.MRAB | 57308 | 27584 (48%) @115 m / 19312 (34%) @230 m | 3 → 3 | 2/2 | 4.23 → 5.91 | +10 s |
| sidecar | EDF6VC_SIDECAR.MRAB | 37064（外壳 18294） | 7394 (40%) @33 m / 5376 (29%) @66 m | 32 → 28 | 3/3 | 10.59 → 11.28 | +2 s |
| sedan | EDF6VC_SEDAN.MRAB | 7753（外壳 2927） | 2022 (69%) @71 m | 13 → 13 | 2 | 4.64 → 4.77 | -0 s |
| chute | （无载具 SGO 引用本次生成的档案） | | | | | | |
| optics | EDF6VC_OPTIC_V402_ROCKET_EDF6BENEFITS.MRAB | 46527（外壳 6668） | 4058 (61%) @67 m / 2646 (40%) @135 m | 38 → 37 | 4/4 | 14.20 → 14.36 | +23 s |
| optics | EDF6VC_OPTIC_V503_BIKE.MRAB | 29814（外壳 12688） | 5951 (47%) @29 m / 4753 (37%) @58 m | 28 → 28 | 3/3 | 10.40 → 11.04 | +23 s |
| optics | EDF6VC_OPTIC_V503_BIKE_EDF6BENEFITS.MRAB | 29814（外壳 12688） | 4786 (38%) @29 m / 3193 (25%) @58 m | 29 → 29 | 4/4 | 12.04 → 12.41 | +23 s |
| optics | EDF6VC_OPTIC_V504_BEGARUTA.MRAB | 51079（外壳 23839） | 16194 (68%) @95 m | 54 → 51 | 8 | 14.13 → 14.84 | +23 s |
| optics | EDF6VC_OPTIC_V504_BEGARUTA_GRAY.MRAB | 49247（外壳 22007） | 15469 (70%) @86 m | 52 → 49 | 6 | 13.69 → 14.39 | +23 s |
| optics | EDF6VC_OPTIC_V504_BEGARUTA_BLUE.MRAB | 49247（外壳 22007） | 15469 (70%) @86 m | 52 → 49 | 6 | 13.31 → 14.01 | +23 s |
| optics | EDF6VC_OPTIC_V504_BEGARUTA_HAISPEED.MRAB | 49247（外壳 22007） | 15469 (70%) @86 m | 52 → 49 | 6 | 13.80 → 14.50 | +23 s |
| optics | EDF6VC_OPTIC_V504_BEGARUTA_YELLOW.MRAB | 49247（外壳 22007） | 15469 (70%) @86 m | 52 → 49 | 6 | 13.67 → 14.37 | +23 s |
| optics | EDF6VC_OPTIC_V504_BEGARUTA_GOLD.MRAB | 49245（外壳 22005） | 12491 (57%) @86 m / 9323 (42%) @172 m | 52 → 49 | 6/6 | 5.28 → 6.13 | +23 s |
| optics | EDF6VC_OPTIC_V504_BEGARUTA_PINK.MRAB | 51085（外壳 23845） | 16196 (68%) @95 m | 54 → 51 | 8 | 15.38 → 16.09 | +23 s |
| optics | EDF6VC_OPTIC_V504_BEGARUTA_RED.MRAB | 49245（外壳 22005） | 15458 (70%) @86 m | 52 → 49 | 6 | 13.14 → 13.83 | +23 s |
| optics | EDF6VC_OPTIC_V505_TANK.MRAB | 33205（外壳 11748） | 5725 (49%) @76 m / 3810 (32%) @152 m | 38 → 32 | 5/4 | 15.79 → 16.04 | +23 s |
| optics | EDF6VC_OPTIC_V505_TANK_EDF6BENEFITS.MRAB | 33205（外壳 11748） | 5725 (49%) @76 m / 3810 (32%) @152 m | 34 → 32 | 5/5 | 14.07 → 14.33 | +23 s |
| optics | EDF6VC_OPTIC_V506_HELI.MRAB | 59822（外壳 17472） | 8164 (47%) @133 m | 42 → 40 | 4 | 12.33 → 12.56 | +23 s |
| optics | EDF6VC_OPTIC_V506_HELI_EDF6BENEFITS.MRAB | 59822（外壳 17472） | 6115 (35%) @133 m / 3239 (19%) @267 m | 42 → 40 | 4/2 | 9.21 → 9.43 | +23 s |
| optics | EDF6VC_OPTIC_V507_RESCUETANK.MRAB | 70368（外壳 13161） | 6323 (48%) @113 m / 4599 (35%) @226 m | 90 → 76 | 14/14 | 17.05 → 17.44 | +23 s |
| optics | EDF6VC_OPTIC_V507_RESCUETANK_HAPPY.MRAB | 71654（外壳 14447） | 6777 (47%) @113 m / 4775 (33%) @226 m | 83 → 75 | 13/13 | 10.91 → 11.32 | +23 s |
| optics | EDF6VC_OPTIC_V510_MASER.MRAB | 48733（外壳 20780） | 13533 (65%) @263 m | 36 → 35 | 8 | 14.92 → 15.50 | +23 s |
| optics | EDF6VC_OPTIC_V512_KEITRUCK.MRAB | 40300（外壳 2396） | 1806 (75%) @75 m | 48 → 43 | 2 | 3.87 → 3.91 | +23 s |
| optics | EDF6VC_OPTIC_V512_KEITRUCK_BGP.MRAB | 40858（外壳 2954） | 2224 (75%) @40 m | 51 → 45 | 4 | 8.39 → 8.44 | +23 s |
| optics | EDF6VC_OPTIC_V513_TRAILERTRUCK01CAB.MRAB | 46966（外壳 3206） | 2510 (78%) @112 m | 39 → 34 | 2 | 4.10 → 4.15 | +23 s |
| optics | EDF6VC_OPTIC_V601_TANK.MRAB | 39040（外壳 12456） | 6818 (55%) @83 m / 5386 (43%) @166 m | 32 → 31 | 4/4 | 12.71 → 13.14 | +23 s |
| optics | EDF6VC_OPTIC_V602_HELI.MRAB | 64337（外壳 22104） | 7736 (35%) @140 m / 5442 (25%) @280 m | 40 → 38 | 2/1 | 12.35 → 12.51 | +23 s |
| optics | EDF6VC_OPTIC_V603_FLAK.MRAB | 34712（外壳 6444） | 3639 (56%) @67 m / 2581 (40%) @133 m | 32 → 31 | 4/4 | 12.08 → 12.35 | +23 s |
| optics | EDF6VC_OPTIC_V608_OLDROBOT.MRAB | 54854（外壳 24326） | 12406 (51%) @100 m / 8812 (36%) @200 m | 44 → 43 | 1/1 | 11.34 → 12.24 | +23 s |
| optics | EDF6VC_OPTIC_V612_NIX_BLUE.MRAB | 52770（外壳 27367） | 15673 (57%) @83 m / 11864 (43%) @165 m | 43 → 42 | 1/1 | 11.01 → 12.16 | +23 s |
| optics | EDF6VC_OPTIC_V612_NIX_BLACK.MRAB | 52770（外壳 27367） | 15673 (57%) @83 m / 11864 (43%) @165 m | 43 → 42 | 1/1 | 10.97 → 12.12 | +23 s |
| optics | EDF6VC_OPTIC_V612_NIX_RED.MRAB | 52770（外壳 27367） | 15673 (57%) @83 m / 11864 (43%) @165 m | 43 → 42 | 1/1 | 11.06 → 12.22 | +23 s |
| optics | EDF6VC_OPTIC_V613_BIKE.MRAB | 28829（外壳 11449） | 5623 (49%) @31 m / 4244 (37%) @62 m | 27 → 27 | 2/2 | 10.34 → 10.79 | +23 s |
| optics | EDF6VC_OPTIC_V614_PROTEUS_MK2.MRAB | 52658（外壳 18353） | 10610 (58%) @215 m | 47 → 46 | 3 | 15.32 → 15.71 | +23 s |
| optics | EDF6VC_OPTIC_VEHICLE401_STRIKER.MRAB | 52159（外壳 6474） | 3519 (54%) @58 m / 2440 (38%) @116 m | 72 → 66 | 6/6 | 11.82 → 12.04 | +23 s |
| optics | EDF6VC_OPTIC_VEHICLE402_ROCKET.MRAB | 46527（外壳 6668） | 4058 (61%) @67 m / 2646 (40%) @135 m | 38 → 37 | 4/4 | 24.21 → 24.37 | +23 s |
| optics | EDF6VC_OPTIC_VEHICLE403_TANK.MRAB | 30315（外壳 10635） | 5334 (50%) @89 m / 3414 (32%) @178 m | 32 → 31 | 4/4 | 24.91 → 25.22 | +23 s |
| optics | EDF6VC_OPTIC_VEHICLE404_BIGTANK.MRAB | 41005（外壳 13482） | 6051 (45%) @248 m / 3881 (29%) @495 m | 33 → 31 | 4/4 | 26.10 → 26.44 | +23 s |
| optics | EDF6VC_OPTIC_VEHICLE407_BIGBEGARUTA.MRAB | 50333（外壳 14888） | 8953 (60%) @218 m / 6366 (43%) @435 m | 51 → 49 | 6/6 | 21.20 → 21.91 | +23 s |
| optics | EDF6VC_OPTIC_VEHICLE409_HELI.MRAB | 51546（外壳 8832） | 3324 (38%) @150 m / 2136 (24%) @300 m | 39 → 38 | 2/2 | 13.50 → 13.69 | +23 s |
| optics | EDF6VC_OPTIC_VEHICLE410_HELI.MRAB | 40898（外壳 10264） | 4543 (44%) @240 m / 3119 (30%) @479 m | 31 → 30 | 4/4 | 17.24 → 17.51 | +23 s |
| optics | EDF6VC_OPTIC_VEHICLE502_GROUNDROBO.MRAB | 35232（外壳 12697） | 8428 (66%) @66 m / 6738 (53%) @132 m | ? → 42 | 3/3 | 12.48 → 12.98 | +23 s |
| optics | EDF6VC_OPTIC_VEHICLE502_GROUNDROBOGOLD.MRAB | 35337（外壳 12802） | 8559 (67%) @66 m | ? → 43 | 4 | 5.43 → 5.59 | +23 s |
| vulcan | EDF6VC_VULCAN.MRAB | 112233（外壳 107361） | 37577 (35%) @57 m / 18199 (17%) @113 m | 43 → 22 | 10/10 | 24.11 → 26.41 | +18 s |
| bmpt | EDF6VC_BMPT.MRAB | 423394 | 148188 (35%) @71 m / 66432 (16%) @143 m | 58 → 22 | 19/17 | 45.26 → 53.76 | +61 s |
| quadvulcan | EDF6VC_QUADVULCAN.MRAB | 120615（外壳 115743） | 40511 (35%) @57 m / 19776 (17%) @113 m | 46 → 22 | 10/10 | 24.35 → 26.80 | +22 s |
| tunguska | EDF6VC_TUNGUSKA.MRAB | 217688 | 76191 (35%) @75 m / 34938 (16%) @149 m | 39 → 14 | 12/11 | 35.55 → 39.93 | +34 s |
| pzh2000 | EDF6VC_PZH2000.MRAB | 233758 | 81815 (35%) @99 m / 30952 (13%) @198 m | 51 → 21 | 18/16 | 27.83 → 31.86 | +25 s |
| sturmtiger | EDF6VC_STURMTIGER.MRAB | 154562 | 54097 (35%) @61 m / 31695 (21%) @122 m | 29 → 9 | 7/7 | 16.90 → 20.12 | +23 s |
| apocalypse | EDF6VC_APOCALYPSE.MRAB | 183630 | 64271 (35%) @127 m / 33809 (18%) @254 m | 49 → 19 | 19/19 | 47.72 → 51.93 | +11 s |
| tos1 | EDF6VC_TOS1.MRAB | 6698 | 5357 (80%) @69 m / 3859 (58%) @138 m | 23 → 3 | 3/3 | 0.90 → 1.22 | +1 s |
| emp_mid | EDF6VC_EMP_MID.MRAB | 3375 | 2671 (79%) @99 m | 21 → 4 | 4 | 9.67 → 9.80 | -3 s |
| emp_short | EDF6VC_EMP_SHORT.MRAB | 3486 | 2334 (67%) @189 m | 21 → 4 | 4 | 9.66 → 9.75 | -3 s |
| emp_long | EDF6VC_EMP_LONG.MRAB | 2673 | 1917 (72%) @225 m | 21 → 4 | 4 | 9.84 → 9.91 | -3 s |
| ciws_phalanx | EDF6VC_CIWS_PHALANX.MRAB | 43157 | 15104 (35%) @80 m / 6665 (15%) @160 m | 27 → 6 | 6/6 | 2.46 → 3.30 | +6 s |
| flak88 | EDF6VC_FLAK88.MRAB | 46170 | 16160 (35%) @104 m / 5353 (12%) @209 m | 26 → 7 | 7/7 | 6.24 → 6.96 | +2 s |
| gdf005 | EDF6VC_GDF005.MRAB | 23261 | 9834 (42%) @84 m / 6663 (29%) @167 m | 22 → 4 | 4/4 | 9.00 → 9.65 | +1 s |
| laser | EDF6VC_LASER.MRAB | 5874 | 2056 (35%) @74 m / 1253 (21%) @149 m | 28 → 8 | 7/6 | 0.26 → 0.36 | +1 s |
| railgun | EDF6VC_RAILGUN.MRAB | 18698 | 6544 (35%) @135 m / 3104 (17%) @270 m | 22 → 4 | 4/4 | 0.94 → 1.29 | +2 s |
| patriot | EDF6VC_PATRIOT.MRAB | 11796 | 5665 (48%) @108 m / 3868 (33%) @217 m | 23 → 5 | 5/5 | 1.51 → 1.88 | +1 s |
| xm_medium_tank_regular | EDF6VC_XM_MEDIUM_TANK_REGULAR.MRAB | 5155 | 3606 (70%) @115 m / 2714 (53%) @230 m | 22 → 3 | 3/3 | 0.60 → 0.85 | +1 s |

读表：「外壳」= 去掉座舱（`edf6vc_cpit_*`，不进 LOD）后的三角形，LOD 百分比相对外壳。「生成耗时变化」是同一生成器整体 `build()` 的墙钟差
（4–6 个生成器并行跑、前后不同时段，噪声有几十秒，只作量级参考；单个大模型的 LOD 生成实测：2S6 ~9 s、BMPT ~20 s、沙扎比 ~20 s）。「—」= 没有一级省到 20%，不写 LOD。

- WT / X-Morph 履带车：网格数降到原来的 1/2–1/7（BMPT 58 → 22，TOS-1 23 → 3）；LOD1 都到 35% 目标，LOD2 13–21%（误差封顶）。
- 战机、运输机、潜艇母舰：HD 的绝大部分三角形是座舱（如 EDF6VC_JET 104,877 中外壳 1,762），远处换 LOD 后座舱整块不画，是这批最大的收益。
- 档案变大：LOD 成员额外 10–40%（BMPT 45.3 → 53.8 MB）；纹理不变。
- 潜艇母舰 / 无畏舰半对角线数百米，LOD 距离按同一规则到了 4–13 km：比可视距离远，实际不会切换（无害，但也没收益）。
- 生成器检查在最终输出上：`make_tracked_combat.check` 在 bmpt / tunguska / flak88 / emp_mid / X-Morph 上通过；`make_katyusha / artillery / drill / dreadnought.check` 通过。
  `vulcan`（`materials differ from model.json`）与 `sazabi`（`bone names are not SKELETON's`）在**改动前的输出上同样失败**：
  这两个检查是为座舱嫁接前的 `authored()` 写的，嫁接会加材质 / 骨骼，与本次无关。

## 6. 未验证 / 风险

- **LOD 切换本身未实机看过**：静态分析表明载具类会读表；骨架共用、材质表相同按原版先例做，实机需看：远近切换是否发生、切换时有无跳变、
  阴影是否跟着切、多人联机下其他玩家的载具是否一样。
- 骨骼按序号对应还是按名对应未查（我们两者都满足：骨骼表逐字节相同）。
- LOD 的材质参数（如 `change_color`、受损贴图 `sys_damage_map`）跟 HD 同一张表，但 LOD 网格未在实机上看受损外观。
- 简化误差是二次误差的近似（对平面距离），不是 Hausdorff 距离；切换距离按 60°/1080p 估算，FOV 更窄（瞄准镜）时同一距离上像素更大：
  瞄准镜下远处载具可能看得出 LOD。
- 安装耗时：每个大模型多 10–20 s（表中「生成耗时变化」），由 buildcache 缓存，模型不变不重算。
