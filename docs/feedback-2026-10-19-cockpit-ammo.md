# 2026-10-19 反馈：载具第一人称座舱的弹药显示

用户原话（`docs/feedback-2026-10-19.md` 第 17 条）：

> 载具第一人称的驾驶舱里面的弹药显示太怪了，还有样式，你看看怎么改改那些显示合适

同批总体要求（第 11 条）：「排版ui之类的你可以不用顾虑原版，原版不方便直接把代码改到方便为止」。

## 现状（改前）

第一人称进入载具时（`FirstPersonHideStockHud=1`，默认），原版 HUD 关掉；插件自己的第三人称挂载条 / 武器块（`StockDockHud`、`HeliStrip`、`CockpitStrip`）也让位（`hud.cpp` `screensOn`）。弹药信息只剩座舱里的**武器屏**一处：

- 新生成的座舱（有 `vc_i_` 骨骼，`snap.scr.physical`）：武器屏是模型几何（`pylib/cockpit_instruments.py` `instrument`，`kind == 'weapons'`），由 `fp_vehicle` 绘制时按 `cockpit_instruments.h` `Pose` 摆骨骼。坦克、BMPT、格雷普、卡车 / 喀秋莎、机甲、原版直升机、插件战机 / 旋翼机、C-130 系列都是这条路径，区别只在屏幕的尺寸与比例（0.10×0.08 m 到 C-130 的 0.19×0.26 m 竖屏）。
- 旧模型（没有重新生成座舱）：`cockpit_screens.inc` `WeaponsScreen` 用单应映射把 HUD 文字画到屏幕四边形上。本次不动这条兼容路径。
- 遥控武器站（`sensor` 屏）：`RwsDisplay` 在整屏监视器上写武器名与弹数，不在本次范围。

改前的物理武器屏只有两样：`AMMO` 五位七段数字 + `SLOT` 一位七段数字。

## 根因

1. **数字的形状随屏幕和位数变形**（「太怪了」的直接来源）。`digits()` 把字段宽度按位数平分成格子宽度（屏幕宽度的比例），格子高度却是屏幕高度的固定比例（0.16）：一位的 `SLOT` 横跨屏幕宽度 70%，七段被拉成两块扁平的大方块 / 三条横线（「2」在直升机屏上横跨整个下半屏，还压住了「武器」二字）；五位的 `AMMO` 每格宽于高，数字矮胖；屏幕越宽越畸形。离线预览见 `docs/evidence/cockpit-weapons-screen/before-after-*.png` 左列。
2. **信息不全**。屏上没有武器是什么（无名称、无类别、无弹种）、没有弹匣 / 装填进度（坦克主炮两发之间、弹匣打空换弹时只显示一个 0）、没有弹尽、没有备用武器；`SLOT 1` 是索引不是信息。第三人称的挂载条这些都有，而第一人称把它关掉之后没有对应物。
3. 模型几何没有游戏字体，武器的本地化名称是运行时字符串（`weapon+0x1B0`），不能逐字做成几何；改前的做法是干脆只显示数字。

## 改动

### 武器屏重新设计（`pylib/cockpit_instruments.py` `weapons()`）

自上而下四层，宽屏左右布局、竖屏（宽 < 1.1×高）上下布局，文字和格子按 `min(高, 宽/1.45)` 缩放：

| 层 | 内容 | 控件 |
| --- | --- | --- |
| 顶行 | 左：弹种标签 AP / HE / BEAM / HEAT / CAN（`reticle.h` Shell）；右：**装填中**（琥珀）或 **弹尽**（红），四种语言 | `e_SHELL_k`、`l_<lang>_<LOAD+1>`、`l_<lang>_<EMPTY+1>` |
| 中部 | 左：当前武器图标（与第三人称挂载条 `StoreGlyph` 同一套形状：机枪弹、炮弹、火箭巢、空空弹、空地弹、炸弹、无人机、能量、爆破）；右：四位大号弹数 | `e_ARM_g`、`D_AMMO_*` |
| 进度条 | 弹匣余量（绿）；装填时改为装填进度（琥珀）取代余量条 | `v_MAG`、`B_MAG`、`B_LOAD` |
| 底栏 | 席位有两件以上武器时，每件一格（序号 + 余量 / 装填条），当前武器加框；顺序与第三人称挂载条一致（`hudcue::SideOrder`） | `v_ROW+i`、`B_ROW+i`、`B_ROWLOAD+i`、`e_PICK_i` |

- 七段数字改为固定比例（宽:高 0.56、笔画 0.13 高），右对齐，任何屏幕比例、任何位数形状一致；状态屏百分比与飞行屏航向用同一函数，一并修正。
- 新增控件模式 `e_<field>_<value>`：字段等于该值时显示（一组图标 / 标签 / 框中选一个）。属于纯显隐，可跨屏共享骨骼（`cockpits.graft` `reusable` 加 `e`）；邻席时隐藏（`firstperson.cpp` `InstrumentPose` 放行 `e`，未就绪即塌缩），不会九个图标叠在一起。
- 新增字段追加在 `Field` 末尾（`arm, shell, mag, load, empty, pick, row×4, rowLoad×4`），旧模型的字段编号不变；旧 `slot` / `ammo` 继续填写。
- 新标签字形（装填中 / 弹尽及繁中、日文）由 `tools/make_label_glyphs.py` 一次性从微软雅黑粗体 20px 渲染成掩码，追加到 `pylib/cockpit_label_glyphs.py` 的标记块；运行时不依赖字体。

### 读数（`src/cockpit_screens.inc` `PublishInstruments`，`src/cockpit_instruments.h` `ArmReadings`）

- 原版载具 / 原版直升机：每件武器取图标组（`hudcue::ArmIconOf` → `CockpitArmGroup`）、弹数、弹匣比例（`ammo/ammoMax`）、装填（`hudcue::CellLoad`：换弹匣或两发之间的上弹）、弹种、是否弹尽（无弹且不能装填）。当前武器沿用旧规则（`selected`，否则 `sight`）。
- 插件战机 / 旋翼机：各挂载 + 机炮一格；读不到弹匣上限，大进度条隐藏，底栏有弹即满格。
- 装填中的武器用装填进度取代余量条（一根条只表达一件事：「多久能打」）。

### 第一人称换武器提示（`src/hud.cpp`）

几何屏写不出武器的准确名称。第一人称下切换武器时，沿用第三人称挂载条的 `LoadoutBanner`，在视图下方显示 1.5 秒当前武器的本地化全名与弹数；平时不常驻，不与座舱屏重复。原版弹药 HUD 在第一人称仍然关闭。

## 验证

- `py -3.12 tests/cockpit_instruments_test.py`：新增武器屏契约（四种语言的装填中 / 弹尽、28 个弹数段、九个图标、五个弹种、四个格子的控件），以及 7 种实际屏幕尺寸（0.10×0.08 … 0.19×0.26）上每位数字宽高比 0.56±0.02、各区块（标签、图标、弹数、进度条、底栏）互不重叠、四种语言标签不出屏。改前的 `digits()` 在这些屏上五位数字的宽高比随屏幕在约 0.5（C-130 竖屏）到 1.4（直升机宽屏）之间变化，一位的 `SLOT` 在坦克屏上约 5，过不了这一项。
- `fp_view_check`：`ArmReadings` → `Pose` 的真实链路：只显示一个图标、只框当前武器、HE 标签、装填中 / 弹尽互斥、装填取代余量、底栏余量、单武器不列格子、邻席图标隐藏。
- `tests/cockpit_build_test.py`（真实模型，全部已适配席位）：骨骼预算最高 215/256（`EDF6VC_TRANSPORT_PLANE`）；每个带武器屏的席位比改前多约 27 根骨骼。该测试的唯一失败 `V607_ROBOTRUCK seat 0 (truck_cab) … cab1` 在 `origin/main`（51eb03e1）上同样失败，与本改动无关；`selftest` 的 `katyusha_bm13_launcher` 失败是同一个 truck_cab 适配异常。
- 离线预览：`python tools/instrument_preview.py [--pylib 旧版 pylib] [--lang 0..3]`，按 `Pose` 的语义摆每个控件后正面绘制。对比图：`docs/evidence/cockpit-weapons-screen/before-after-zh-CN.png`、`before-after-en.png`。

## 未验证 / 风险

- **未实机**：本次没有开游戏。预览是正面平面绘制，不含透视、遮挡与发光材质的实际观感；需要在坦克、直升机、战机、C-130 竖屏上各看一次。
- **必须同时更新 DLL 与座舱模型**：新 DLL 配旧模型照旧工作（旧字段保留）；新模型配旧 DLL 时，旧 DLL 不认识 `e` 模式和新字段，九个图标、五个弹种、四个框会全部显示叠在一起。安装器一次装两样，单独替换模型才会出现。
- 插件战机的挂载没有弹匣上限读数，底栏格子只表达「有 / 无」。
- 席位武器超过四件时底栏只列前四件，第五件起被选中时不加框（中部图标、弹数、装填照常）。
- 弹种 HEAT / CAN 依赖 PR #216（`reticle::Shell` 新增 heat=4、canister=5）；本 PR 的标签表已按该顺序预留，#216 未合并时只会出现 AP / HE / BEAM。
