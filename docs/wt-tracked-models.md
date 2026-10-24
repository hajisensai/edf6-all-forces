# M163 / BMPT 换成战雷模型（2026-10-21）

## 用户原话

> bmpt和火神炮……模型也不行，可以去偷战雷的或者哪里的模型

（来源授权用户明确不在意：以效果为准。）

## 素材来源

| 车 | 来源 | 面数 | 许可（上传者标注） |
|---|---|---|---|
| M163 | Sketchfab「M163 (War Thunder)」by KojfDiscord，https://sketchfab.com/3d-models/m163-war-thunder-4d6f3359f84e47caa49d717b91d4270a | 107,365 | CC BY 4.0 |
| BMPT | Sketchfab「BMPT (War Thunder) Line Of Contact」by KojfDiscord，https://sketchfab.com/3d-models/bmpt-war-thunder-line-of-contact-3f2e54b873a14b45b99007b7cefdf2c8 | 423,607 | CC BY 4.0 |

- 用 Sketchfab API（`/v3/models/<uid>/download`）下载 `source` 包，原件解压在开发机 `D:\APP\edf6-models\downloads\wt_m163|wt_bmpt\`（不入库）。
  里面是战雷原始导出：OBJ 的对象名保留了战雷节点名（`vehicle#bone_turret_59_0`、`wheel_l_01`、`gun_barrel_02`……），贴图是战雷原名 JPEG
  （`*_c_*`、`*_n_n`、`*_ao_ao`）。glTF 包被 Sketchfab 按材质合并过、节点名没了，所以用 source 包。
- 本机没有可解包的战雷 / 坦克世界客户端：`D:\steam\steamapps\common\War Thunder` 只剩 1.9 GB 的 `content/pkg_local`
  （没有 `content/base`），Lesta 只装了 Korabli（舰船）。KojfDiscord 的上传本身就是战雷模型（仓库里 `plugin_wt_f16a` 座舱也是他的），
  所以直接用 Sketchfab。
- **风险**：这些是战雷（Gaijin）的游戏资产，CC BY 4.0 是上传者自己标的，并非 Gaijin 授权；仓库是公开的。

## 管线

```
source OBJ ──tools/prep_wt_tracked.py──> models/wt_m163|wt_bmpt/   （入库，随 CI 安装包，同 models/cockpits 的约定）
                                           model.npz   每个部件（一骨骼 × 一材质）的顶点/法线/UV/三角形；每个物理代理的碰撞凸包
                                           model.json  部件、材质、骨骼新原点、旋转管、炮口/抛壳点、轮心/轮径、碰撞目标盒
                                           textures/   战雷原贴图 JPEG + 由 AO 生成的遮蔽 PNG；LICENSE.txt
installer ──make_tracked_combat──> vulcan_model / bmpt_model ──> pylib/wt_tracked.py（模型文件夹在就用它，否则原自建几何）
```

- 坐标：战雷 OBJ 已是游戏坐标（+Y 上、+Z 前、+X 车左，`wheel_l_*` 在 +x），米，履带接地 y=0；UV 取 `(u, -v)`（左上原点）；
  缠绕方向用车体有符号体积判定（实测本就是逆时针，不翻）；源文件无法线，按 50° 自动平滑生成。
- 部件归属：`prep_wt_tracked.py` 的 `RULES`（按节点名正则）。未匹配的一律车体 `body`。
  - M163：`wheel_*_drive`→A（前主动轮），`wheel_*_01..05`→B..F，`wheel_*_back`→G（后诱导轮，外半边放 H，与原自建模型同一死亡物理方案），
    `gun_barrel`（六管）→ 旋转管 `edf6vc_m61_rotor`，`bone_gun / bone_roll_* / gun_mask_03_anim`（摇架、供弹槽）→ `cannon_r`（俯仰），
    雷达天线盘 → `doppler_radar`、天线臂 → `tracking_radar`，炮塔、弹鼓、瞄具盒、鞭状天线 → `cannon_main`。
  - BMPT：`wheel_*_front`→A（T-72 诱导轮在前），`wheel_*_01..06`→B..G，`wheel_*_drive`→H（主动轮在后），`wheel_*_top_*`（托带轮）→车体；
    `gun_barrel_02`（+x 左炮）→`cannon_slide_l`、`gun_barrel_01`→`cannon_slide_r`；武器模块 `bone_gun / ex_decor_01 / bone_mg_gun_twin` → `cannon_l`（俯仰）；
    Ataka 发射筒 `gun_mask_03/04 / 9m120_rocket` 留在 `cannon_main`（导弹的挂点本来就在 `cannon_main`，发射筒不随炮俯仰，筒口=导弹出口）；
    车长周视镜 → `doppler_radar`，炮长舱盖瞄具块 → `tracking_radar`；车体两侧的 `bone_turret_01..04`（车体榴弹发射器）→ 车体。
- 骨骼原点（`model.json origins`，`graft_pure.set_bone_origin`，旋转不变、子骨世界绑定不变）：
  - `cannon_main`：炮塔座圈圆心。对 `bone_turret` 顶点按 y 分层做圆拟合：M163 (0, 1.87, −0.27)，BMPT (0, 1.70, −0.25)（r≈1.2 m）。
  - `cannon_l/_r`（耳轴）：M163 取炮管轴高 y 2.31、炮塔前脸 z 0.05；BMPT 取侧视图上武器模块侧面轴承圆心 (y 2.44, z −1.47)。x 为各自炮管轴。
  - `cannon_slide_l/_r`：炮管轴在炮口平面（炮口 = 炮管包围盒最前端）；M163 两侧都用同一组六管（与原方案一致：两条弹链都从 `cannon_slide_l` 出）。
  - 负重轮 / 诱导轮 / 主动轮 `tire_move*` 与对应 `catapi*`：各轮包围盒中心；轮径 = 包围盒 y/z 半径的小者，写进 `car_base_wheel` 第 8 项。
  - 旋转管 `edf6vc_m61_rotor`：六管束轴线、管尾 z（插件 `vulcan_rotor` 绕骨骼自身 z 轴转，位置无关）。
- 移动骨骼后的派生数据：
  - 布娃娃：`tracked_collision.build(..., layout=)` 先 `ragdoll_fit.fit`（每个代理随骨骼平移、关节跟子体、参考姿态和绑定偏移重算），再走原有雷达代理重定位。
  - 动画：`cas_pose.retarget`（`doppler_radar_loop` 是绝对平移片段，炮管滑轨也在里面，不重定位会把炮管拉回原位），再去掉两根雷达骨的轨道；`fire_loop` 是增量片段，字节不变。
  - 炮口 / 抛壳点：模型空间点经新的逆绑定换到骨骼局部，写进武器 SGO 的 MAB。
- 材质：每套贴图一个材质，模板用 V603 原版的 `v603_body`（albedo / 法线 / `param_r_m_occ_hr`：粗糙度、金属度取原版平均 140、90，遮蔽通道用战雷 AO 映射到 110..255）；
  履带用 `v603_kyata_L/_R` 原名（SGO 的 `tank_caterpillar_animation` 按名滚动贴图）。法线图按 RGB 存 DXT1（着色器只读 RG）。
- 网格：按部件 numpy 打包到模板顶点布局（半精度位置），单网格 ≤21000 三角形（u16 索引）；M163 32 个网格 / 107,365 三角形，BMPT 58 个 / 423,607。
- 碰撞：每个物理代理的部件按 1 mm 焊接分连通块，取每块凸包（< 6 cm 的螺栓把手等丢弃，最多 48 块），再交给 `model_pieces.fit` 裁到目标盒。
  直接拿原网格（BMPT 车体 20 万三角形）喂 `model_pieces.fit` 纯 Python 跑十几分钟不出结果。
  兄弟体互碰（D93250）的处理与原方案相同：M163 炮管体从 z 0.75、摇架体到 z 0.55（间隙 0.20 m，`_sweep` 检查），后诱导轮两半 x 方向留 4 cm；
  BMPT 两根炮管各自是 `cannon_l/_r` 的碰撞体，武器模块的碰撞并入炮塔（否则会包住右炮管）。

## 验证（本机，离线）

- `python tests/test_wt_tracked.py`：9 项全过（生成器全部自检：模型、SHKT `shkt_check` 无 error/warning、布娃娃 `ragdoll_fit.problems` 为空、CAS、
  炮口；骨骼名/父子/旋转与 V603 一致、原点 = model.json；炮口世界坐标 = 炮管口；轮径 = 模型；贴图绑定 `texture_problems` 为空）。
- 原自建几何的回归测试（`test_tracked_combat`、`test_tracked_collision`）在类里把 `wt_tracked.ENABLED` 关掉照旧跑。
- 生成耗时（native CMPL）：M163 全套约 20 s，BMPT 约 45 s；MRAB 16 MB / 33 MB（贴图占大头）。
- 离线渲染（生成的 MRAB 按其贴图上色，炮塔转 30°、炮抬 25°）：`tmp/render/`（开发机，不入库）。

## 未覆盖 / 风险

- 实机效果见 PR 描述（是否实测、截图路径）。
- 履带贴图滚动方向 / 速度：战雷履带 UV 沿带走向，但比例和方向与 V603 不同，未实机核对。
- 车体悬挂臂（`suspension_*`）是静态挂在车体上的，负重轮上下跳动时不跟。
- BMPT 的 Ataka 发射筒不随炮俯仰（真车随武器模块一起俯仰）：保持导弹出口与挂点一致的取舍。
- 碰撞是凸包近似，未实机验证接触手感。
