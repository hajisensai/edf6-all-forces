# 飞机地平仪不显示 / 一直水平（2026-10-21）

用户原话：「飞机的地平线角度表盘没显示或者说一直是水平的」。

## 核查过程

先怀疑读数链路（俯仰 / 滚转在哪一步丢了或被钳成 0），实测证伪：

- 读数：俯仰取机头向量 `asin(nose.y)`（`src/cockpit_screens.inc` `NoseOf`，插件飞机取 `PlayerJetReadout.sym.nose`，即载具矩阵第 2 行 `m[8..10]`）；滚转取席位标记 `vc_eye_<seat>` 的正交化矩阵 `atan2(left.y, up.y)`（`src/firstperson.cpp` `FirstPersonInstrumentReadings`）。
- 绘制：`InstrumentPose` 对 `vc_i_<seat>_<screen>_g_<pitch>_<k>_<limit>` 骨骼按 `instruments::Pose` 的 `g` 分支先沿卡面竖直方向下移、再按滚转反向转动（`src/cockpit_instruments.h`）。
- 实机（后台 autopilot，测试场，`EDF6VC_JET` / bomber501，Debug=1 新增的 `FP attitude` 探针，每 2 s 一行）：
  - `readings: flight 1 pitch 16.2 deg ... roll 0.0` → `card ... dropped -0.0726`（16.2 × 0.0045 = 0.0729 屏高）；
  - 压坡时 `seat frame left.y -0.542 up.y 0.819 -> roll -33.5 deg`，绘制侧 `roll -27.3 deg -> card turned -27.3 deg`；
  - 骨骼行长 `0.0950 0.0950 1.0000`：引擎保留了按屏高缩放的骨骼轴，画面姿态数组里就是卡片该有的位置。
  读数、发布、姿态数组三步都对，卡片确实随俯仰下移、随滚转转动。

## 根因

地平仪在第一人称的视野之外。游戏镜头竖直视角 π/4（`docs/view-distance-re.md`，`0x118AFC0` 初始化 `+0x24 = π/4`），即视线上下各 22.5°。所有飞机座舱的姿态仪都在这之下：

| 座舱 | 姿态仪 | 视线下方 |
|---|---|---|
| F-16（截击机 / 战斗机 / 攻击机 / Primer） | 飞行屏 PFD（两膝之间） | 40° |
| 同上 | 备用姿态仪（standby 块左下格） | 34° |
| V506 / V602 / 409 直升机 | PFD / 备用 | 37° / 32° |
| C-130 驾驶舱 | 备用 / PFD | 27.5° / 32° |

实机截图（1280×720）里只看得到右侧 `right_flight` 块第一排的空速表与高度表（视线下 19°），姿态仪全在画面下沿之外；而有实体仪表的座舱又关掉了二维抬头显示（`src/hud.cpp` `headUp`），视野里没有任何地平线。

F-16 本来有一个在视野里的姿态仪：`right_flight` 块（`AIR_FIELDS` = 空速、高度、**姿态**、爬升……）第一排第 3 格，视线下 19°。这一格落在面板边缘外，`pylib/cockpit_retrofit.py` `apply` 的支撑探测判定装不下，于是**连同它的读数一起丢掉**（原 `apply` 每个读数固定占自己的格子，格子装不下就 `continue`），后面不重要的爬升、燃油等反而保留。离线复现：`right_flight_2` 从未调用 `_dial`。

「一直是水平的」：玩家要按 L3 低头才能看到姿态仪，看仪表时一般不在机动，卡片本来就接近水平（推测，未实机核对玩家操作）。

## 修复

`pylib/cockpit_retrofit.py` `placed_fields`：

1. 读数按块里的顺序（最重要在前）依次放进装得下的格子；装不下的格子把位置让给下一个读数，用完格子时丢的是末尾最不重要的读数，而不是中间某一个。
2. 姿态仪换到装得下的格子里离视线最近的一格（与原读数对调）：真实座舱面板偏低，离视线最近的那格就是还在视野里的那格；也符合基本 T 布局里姿态仪居中靠上。

结果（离线生成 F-16 套件）：`right_flight` 第一格是姿态仪，视线下 18.9°、偏 8°，整个表面在视野内；备用块的姿态仪也换到第一排（28.4°）。

## 测试

- `tests/cockpit_retrofit_test.py`：`placed_fields` 的让位与姿态仪就近；真实 F-16 套件（`jet_f16` / `jet_f16_narrow`）至少一个姿态仪的整个表面在 π/4 竖直视角、16:9 水平视角内。变异实测：把 `placed_fields` 改回「每个读数固定占自己的格子」，两项都红（视野项报 `(34.2, 3.6)`）。
- `tests/fp_ride_lifecycle_test.cpp`：生产路径 `FirstPersonInstrumentReadings` → 发布快照 → `InstrumentPose`：仰 10°、右坡 30° 的席位矩阵，g_ 骨骼绘制为转 30°、下移 0.045，固定飞机符号显示，绘制后逐字节还原。变异实测：把滚转读数乘 0，两项红。

## 未解决 / 风险

- 直升机与 C-130 的表盘仍在视野外（面板整体在视线下 24–38°），由下文「座舱模型上的 HUD 玻璃」在视线上给出地平线；表盘留作备份。
- 模型要重新生成（安装器重新生成座舱）才生效；DLL 不变也能用。
- 实机只验证了读数到姿态数组；重新生成后的 F-16 座舱没有在游戏里看过画面（后台测试的 `TestBoardVehicle` 三次里两次没登上飞机）。

## 追加：座舱模型上的 HUD 玻璃（2026-10-21，用户拍板）

拍板：第一人称座舱里的地平线等飞行信息做在座舱模型的 HUD 玻璃上（10-15 原话「这个表盘要是真的表盘，哪有覆盖上hud的道理。要么是"模型上的hud"，要么是模型上的指针」「为什么表盘上面会有这种hud」），表盘留作备份；不放大视角，不做贴屏幕的平面 HUD。

### 位置（统一管线：座舱套件的 `hud` 屏幕数据 → 模型的 `vc_scr_<seat>_hud_<w>x<h>` 标记 → `CockpitScreens`）

| 座舱 | HUD 玻璃 | 来源 |
|---|---|---|
| F-16 系（截击机 / 战斗机 / 攻击机 / Primer / 多用途窄座舱） | 组合玻璃，视线前 0.79 m、下 2.2°，0.14 × 0.15 m（窄座舱按比例） | 已有 `ScreenAt('hud', …)`（战雷 F-16A 模型两根 HUD 支柱之间） |
| AH-64 两席（V506 / V602 / 409 / Eros） | 头盔显示（IHADSS），视线前 0.9 m，0.36 × 0.27 m | 已有 `helmet_hud=True` |
| UH-60M（410 运输直升机驾驶席） | 同上，头盔显示 | 本次 `helmet_hud=True`（座舱里没有组合玻璃） |
| C-130J 驾驶舱（运输机 / 炮舰 / 母舰 / 潜航母舰驾驶席） | 驾驶员 HUD，视线正前 0.4 m，0.214 × 0.17 m（C-130J HUD 30° × 24° 视场） | 本次 `ScreenAt('hud', (0, 0, 0.4), …)`：原模型 HUD 板就在那里，备料时被剔除（`tools/prep_cockpit_plugin.py` C130_DECK） |

规则由测试钉住（不逐机型写）：每个带 `flight` 屏的驾驶席套件都要有一块 `hud`，中心偏离视线 ≤ 5°、正对眼睛（`tests/cockpit_retrofit_test.py`）。

### 内容（`src/cockpit_screens.inc` `HeadUpGlass`，复用座舱屏幕的画布管线：屏幕四角经视图投影、单应映射到玻璃上，符号裁到玻璃边内）

- 共形：地平线（左右各 60°）、俯仰梯（每 5°，下俯虚线，梯级端标度数）、机头水线 W、飞行路径标志。按世界方向求「从眼睛沿该方向的视线与玻璃平面的交点」（`canvas::Through`），所以透过玻璃看，画的地平线落在真实地平线上，与玻璃怎么倾斜无关。
- 固定在玻璃上：顶部航向带（±15°）与三位航向、左侧空速框（km/h）、右侧离地高度框（m）与爬升率、下方滚转刻度（0/10/20/30/45/60°）与指针。
- 读数复用：航向、俯仰取 `NoseOf`；滚转取 `instruments::BankOf`（与地平仪卡同一函数，从席位眼睛矩阵读）；空速、高度取屏幕读数 `FeedOf`。
- 只在有实体仪表的座舱（`physical`）、飞行中（`flies`）画；没有实体仪表的旧模型仍走原来的二维 HUD。
- 联机：只画本机画面，不读写任何同步数据，不发包。

### 自动登机（`TestBoardVehicle`）失败的根因

`BOARDING ... no seat of 1 taken in 90 presses ... +0x5D0=0000029e`：测试场里玩家每隔一段时间处于忙碌状态（`+0x5D0` bit 2，动作 0x29E，持续约 10 s），原版上车按钮（0x56D700）这时什么都不做；`TestBoard` 只发一次请求，`BoardStep` 的 1.5 s 按键窗口落在忙碌期内就放弃了。改为：上车按钮的门（`BoardButtonBlocked`：+0x128 bit 0、+0x5D0 bit 2、+0x39C）放开、且没有进行中的上车时才发请求；没上去就再发，最多 8 次。另外 `@bomber501_2` 匹配不到：截击机模型的根骨骼名也是 `bomber501`。

### 测试

- `tools/cockpit_canvas_check.cpp`：三组（航向 30° 仰 8° 右坡 25°；航向 200° 俯 12° 左坡 40°；水平）下，机头落在玻璃中心，画的地平线转过的角度等于坡度（右坡右端抬起），离瞄准线的距离等于 距离 × tan(俯仰)，滚转指针与地平线垂直；视线背后的方向不上玻璃；线段裁到玻璃边。变异：`Through` 竖直方向取反 → 7 项红。
- `tools/hud_view.cpp` `cockpit_hud_glass`（ctest `hud_layout`）：经生产 `HudDraw` 画一架仰 3°、右坡 20° 的直升机的玻璃 HUD（相机水平），画出的地平线在画面上是水平的、落在中行 540（真实地平线），空速 151、高度 85 在玻璃上。变异：地平线改成固定在玻璃上 → 红（找不到第 540 行的水平线段）。
- `tests/cockpit_retrofit_test.py`：驾驶席都有视线上的 HUD 玻璃。变异：去掉 UH-60M 的头盔显示 / C-130 的 HUD → 分别红。

### 实机（后台 autopilot，测试场，`EDF6VC_JET` / bomber501，本分支 DLL sha256 `44139aaa…41c141b`，已含 origin/main）

- 自动登机：两次都第一次请求就上去（`TESTBOARD ... (request 1)` → `BOARDING ... seat 0: the player boarded it`）。
- `docs/evidence/cockpit-hud-glass/f16-ground-horizon.jpg`（及放大图 `-crop.jpg`）：停在跑道上、俯 2.1°，HUD 地平线落在远处真实地平线上（1280×720 画面里两者相差约 3 px），其下 -5° 虚线梯级、中央水线、航向 174、空速 / 高度框、底部滚转刻度。
- `docs/evidence/cockpit-hud-glass/f16-air-banked.jpg`：空中压坡，俯仰梯与 15° 刻度随坡度倾斜，滚转指针偏转。
- `docs/evidence/cockpit-hud-glass/offline-heli-banked-20.png`：`hud_view` 离线图（相机水平、玻璃右坡 20°）。
- 每次实机都经 gamequeue 排队；测前测后 `Mods` 目录 sha256 全量对比一致（RESTORED_IDENTICAL）。
- 未实机：直升机的头盔 HUD、C-130 的 HUD。直升机（V506 等）已装模型里本来就有 `hud` 标记，这版 DLL 会画，但没在游戏里看过；UH-60M（410 运输直升机）和 C-130 要重新生成座舱模型才有 `hud` 标记。
