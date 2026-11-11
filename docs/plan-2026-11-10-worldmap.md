# 2026-11-10 方案：世界地图选任务 + 载具 3D 预览（测试站 #46）

用户原话与反馈原文：[feedback-2026-11-10-worldmap.md](feedback-2026-11-10-worldmap.md)。分支 `feat/1110-worldmap`。
EDF.dll TimeDateStamp 0x678CCB46，地址为 RVA；证据级别 H（反汇编 / 数据直接证实）/ M（推断）/ L（猜测或设计选择）。
全部是**离线**工作：没有启动游戏、没有写游戏目录（Root.cpk / EDF.dll 只读）。

## 0. 完成度（如实）

| 部分 | 状态 |
|---|---|
| (a) 载具 3D 预览 | 代码完成、离线测试过；**未实机**。动画 / 骨骼缺失时渲染器的反应未核实（风险见 `room-3d-preview-re.md` §6.1） |
| (b1) 底图素材（2026-11-11 改为全球 JPEG 瓦片，§2） | NASA Blue Marble 已下载到外部素材目录 `D:\APP\edf6-models\worldmap\`，校验哈希，派生出世界图与日本细节图（PNG）；2026-11-10 维护者定「随模型包发」后已转成游戏贴图（DXT1 + mip）放进模型包文件夹 `worldmap_tex`，安装器复制到 `Mods/UI/`（§2）。**游戏是否正确显示没有实机验证**（界面未做） |
| (b2) 任务地点表 | 完成：9 个任务包、483 关，每关 → 所用地图 → 真实地点（45 处），生成 `src/worldmap.inc`，CI `--check` |
| (b3) 纯逻辑 | 完成：投影、视图（拖动 / 以光标为中心缩放 / 框选）、到下一关的大圆航线（跨日界线断开）、同一地点多关的标记散开、点选、该画哪些航线 |
| (b4) 原版选关界面逆向 | 关键对象与入口已定位（§4），够设计用；接管所需的绘制路径（§5）只做了方案 |
| (b5) 游戏内界面（画地图、点选开局、单机开始即进地图、联机选任务进地图） | **没做**：需要实机迭代，见 §6 待办 |

## 1. 载具 3D 预览（已实现）

光标在载具单位的机炮 / 挂点行，或停在该载具的单位 / 挂点 / 机炮 / 挂载 / 弹数页面时，房间 / HQ 右侧的 3D 预览换成该机体的模型，
放远（坦克 ×3、战机 ×6，按尺寸估的，待实机调），每帧原地转 0.01 rad 让各挂点都转到正面；乘员按钮仍显示乘员士兵。
挂载不在模型上：游戏里挂架本来就不画挂载模型（只有射出的弹体有模型，`stores-re.md` §10），菜单渲染器也只挂 WeaponTable 行的玩家武器，
所以挂载改动由菜单行（挂点、重量、弹数）实时显示。实现与风险：`room-3d-preview-re.md` §6.1；
菜单 SGO 生成：`tools/menu_preview.py build_vehicles`（安装时生成，台账 owner 同士兵预览）。
离线：`room_preview`（vehicle 段）、`room_ui`（Preview / Focus 的载具段）、`menu_preview`（有游戏时含载具生成）。

## 2. 底图：NASA Blue Marble 全球 500 m，JPEG 瓦片金字塔（2026-11-11 改）

维护者原话：「真实的世界地图，天空中拍摄的那种」（11-10）；「放境外 要全球高清图」「换成 JPEG 全球240试试」「接进仓库」（11-11）。
地点不再限定日本（境外地图配真实境外地点，地点表另改），所以底图从「全球 4096 + 日本细节」两张 DXT1 改成**全球原始精度**。

- 来源：NASA Earth Observatory「Blue Marble Next Generation」地形 + 海底地形，2004 年 12 月，等经纬度投影。500 m 瓦片 A1..D2 共 8 块，
  每块 21600 × 21600、90° × 90°，合起来 86400 × 43200（每度 240 px）。哈希写死在 `tools/worldmap_basemap.py` `SOURCES`。
  许可：NASA 影像不受版权保护（注明 NASA Earth Observatory）。原图放外部素材目录 `D:\APP\edf6-models\worldmap\`，不进仓库。
- 为什么 JPEG（11-11 实测）：同样的金字塔做成 DXT1 约 2.5 GB；JPEG 质量 85（4:2:0）共 **344 MB**，与原图 PSNR 东京湾 38.2 dB、
  阿尔卑斯 35.3 dB、撒哈拉 40.5 dB，目视无差；单块 1350² 解码中位 3.1 ms（PIL / libjpeg-turbo，WIC 估计慢 2–3 倍）。
  代价：游戏的贴图加载（Skin_Texture）读不了 JPEG，**地图改由插件自己画**：WIC 解码 + D3D11 绘制（`thermal_gpu.cpp` 已拿到设备），
  UI 层次（对话框遮挡、淡入淡出）要自己对齐。原 §5「布局 Texture 元素 + Skin_Texture」的底图方案作废（标记、文字仍可走布局元素，待定）。
- 布局（`pylib/worldmap.py` 唯一定义，`src/worldmap.inc` 的 `kTile*` 由 `gen_worldmap.py` 生成）：每块 1350 px（整除 21600，瓦片不跨原图），
  第 z 级 2^z 列 × max(1, 2^(z−1)) 行，z = 0..6；第 0 级一块 1350 × 675 是整个地球，第 6 级就是原图精度（不放大任何像素），共 2731 块。
  最大缩放 = 第 6 级像素与屏幕 1:1（1920 宽时 45 倍，`MostZoom`），不放出糊图。选级 `LevelFor`：屏幕上地图宽度不超过该级像素宽的最粗一级；
  可见范围 `Visible`（列跨日界线时不取模，由调用方取模）。离线测试 `worldmap`（`Tiles`）。
- 文件：`Mods/Plugins/EDF6VehicleCrew.worldmap.bin`，一个文件：`'EDWM'`、u32 格式 1、u32 1350、u32 7，然后每块（按级、行自上而下、
  列自 180°W 向东）u64 偏移 + u32 大小，然后各块 JPEG。插件按偏移读取需要的块。做成一个文件是为了安装只校验、复制一次、台账一条。
- 生成：`python -B tools/worldmap_basemap.py fetch`（下载并校验 8 块原图）→ `tiles`（约 1 分钟，内存峰值约一块解码后的原图 1.4 GB）。
  模型包文件夹仍是 `worldmap_tex`（`RELEASE_MODELS`），版本 `TEXTURE_VERSION` 2；模型包版本随内容哈希变一次。
- 安装：`tools/worldmap_pack.py`（台账 owner `worldmap`）只校验 `worldmap_tex.json` 的版本、大小与 SHA-256 后复制，已装且相同的不重写；
  缺文件夹 / 版本不符 / 内容不对 / `Mods` 里有别的 MOD 的同名文件时不装、说一行原因并释放以前装的（含版本 1 的两张 DDS），不做替代。
- 测试：`worldmap_pack`（ctest：8 块合成小原图生成金字塔，逐块核对尺寸、JPEG、所在原图；安装 / 重装不重写 / 卸载 / 释放旧 DDS、四种拒绝；
  有开发者模型目录时核对真实文件的表）、`worldmap_data`（来源与布局）、selftest `worldmap_textures_in_model_pack`。
- **未实机**：插件侧的瓦片读取、解码与绘制都还没写（§6）。

## 3. 任务地点表（维护者：「地图弄成实际地点，到下一关有指引路线」）

- 游戏文本不给真实地名：EDF6 的简报只说「city」「town」「mountains」「Base 251」（英文简报关键词统计：city 79、town 25、cave 13……
  只有 1 处「Europe」）。所以按**任务所用的地图**定地点：每张地图按它的名字表示的地形（罗马字日语：haikyo 废墟、danti 团地、
  rinkai 临海、kitaguni 北国、sangaku 山岳、seiyu 炼油、soukogai 仓库街……）配一个同类的真实日本地点（系列设定在日本）。
- 数据流：
  1. `tools/make_worldmap.py`（开发者工具，读 EDF6 Root.cpk，只读）→ `worldmap/missions.json`：每个包按列表顺序的 `{key, map}`。
     EDF6 本篇 / 任务包 1 / 2 读 `MISSION.AC`（脚本源码）的第一个 `Map("app:/Map/<名>.mac", …)`；EDF5 三个包读
     `EDF5_OLD_SCRIPT/<path>/MISSION.BVM` 的第一个 `app:/Map/` 引用（`make_edf5_campaign.first_map`，UTF-16 字符串表两种对齐都要试）；
     EDF4.1 三个包读仓库里已提交的 BVM。只写键和地图名，不写任务文本、不写脚本。
     结果：EDF6 147 / 19 / 40，EDF5 110 / 15 / 14，EDF4.1 89 / 26 / 23，共 483 个不重复的键，63 张地图。
  2. `pylib/worldmap.py`：`PLACES`（45 处：五种语言名称、经纬度、地区）与 `MAPS`（地图 → 地点 + 理由，理由里标 H/M/L：
     名字直说的 H，按罗马字读出的 M，任务包地图 `nw_dlcmap601/602` 内容未读 L）。地点选择本身都是设计选择（L）。
  3. `tools/gen_worldmap.py [--check]` → `src/worldmap.inc`（`kPlaces`、`kMissions` 键 → 地点下标、测试场前缀、细节图边界）。
     CI 在生成文件检查步骤里跑 `--check`。
- 插件按**键**找地点（列表第 2 列，`0xE0D80` 读的同一列），不按行号：列表重排、追加、占位行都不影响。键比较不分大小写、`\` 与 `/` 等价。
  插件自己的测试场任务包（键前缀 `EDF6/EDF6TR_`）统一放在东富士演习场。查不到的键（别的 MOD 的列表）：该行没有标记，日志说一次。
- 改地点：只改 `pylib/worldmap.py`，跑 `python tools/gen_worldmap.py`；`worldmap_data` 测试保证每张用到的地图都有地点、
  没有用不到的地点、坐标都在日本与细节图范围内、生成文件最新；有游戏时还核对 `missions.json` 与 Root.cpk 一致。
- 关东集中：城市图大多配在东京都市圈（23 处在关东），全国视图里会挤在一起；靠缩放 + 同地点标记螺旋散开（§5）解决。

## 4. 原版选关界面（静态逆向，`tools/edfre.py` + RTTI）

| 项 | 内容 | 级别 |
|---|---|---|
| 类 | `ui::HUiHQMissionSelect`，RTTI 名 0x206A530，vtable **0x18052E8**（COL 0x1CAA308），布局 `app:/UI/lyt_HUiHQMissionSelect.sgo`（字符串 0x180B260） | H |
| 工厂 | `Factory@HUiHQMissionSelect`：名字 0x89FA90 返回 L"HUiHQMissionSelect"，创建 0x89FAA0 → 构造 **0x8A1530**（`mission-list-re.md` 里的「选关构建」就在这个构造函数里） | H |
| 槽 1 | 0x8A4F60（与 HUiModeSelect 0x8C3ED0、HUiTitle 0x8D1150 同位，按 UI 基类惯例是 OnUpdate） | H 槽位 / M 含义 |
| 显示列表 | `this+0x798/+0x7A0/+0x7A8` 的 `std::vector`，每项 0x14 字节 `{+0 行号, +4 显示编号, +8 状态, +0xC 字段5, +0x10 已下载字节}`，构造末尾（0x8A23DE..0x8A23FD）把状态 0..3 的行逐项 push | H |
| 光标 | `this+0x778` = 显示列表下标（0x8A85E8） | H |
| 确认 | **0x8A85C0**（构造里 0x8A1B8A 绑定的回调）：`+0x7B0==0` 且该项 `+0x10==0` → 提示 `PlayGoDownload_MissionSelect`；状态 > 2 → `MissionSelectNotOpen`（0x8A8760）；否则取该项行号，经 0x8A2DD0 组 `MissionResult`（`this+0x6B0` 是请求方回调）交回打开它的界面 | H |
| 联机房间「选择任务」 | `HUiRoom` 构造 0x8F2290（经 Factory@HUiRoom 0x8F14B0 调用）里 `Button_MissionSelect`（0x8F381A / 0x8F424E）；按钮回调 0x8FC2C0（0x8F2EB8 处取地址）用 0x845250 以布局路径打开选关，并写房间 `+0xAC0 = 2` | H 地址 / M 0x845250 = 按布局打开子界面 |
| 房间搜索的任务筛选 | `RoomSearchDialog`（Factory 0x90B600 → 0x90C3F0）→ 0x90F490 打开同一界面（`OnRequest_MissionSelect` lambda） | H |
| 建房的任务项 | `HUiRoomCreate`（Factory 0x915720 → 0x91DD90，`Request_Mission`）→ 0x91C260 打开同一界面 | H |
| 单机本部 | 由 MAINSCRIPT（AngelScript）按工厂名创建；脚本函数 `void RestoreMissionSelect()`（声明字符串 0x17EAAC0，在脚本 API 注册函数 0x719690 内 0x71C82F 注册）用于任务结束回来时恢复选关 | H 注册 / M 用法 |

结论：单机本部、联机房间、房间搜索、建房四处都打开**同一个** `HUiHQMissionSelect`，选中结果都经它的确认函数 0x8A85C0 交回请求方。
所以世界地图不必重做「选中以后」的任何东西：保留这个对象（解锁、状态、在线请求、BGM / 背景配色这些原版逻辑全在），
只把它的列表画面换成世界地图，点中一个标记 = 把 `+0x778` 设成该行在显示列表里的下标，再调 0x8A85C0。

## 5. 世界地图界面设计（未实现）

- 接管点：Hook vtable 0x18052E8 槽 1（每帧）。对象活着期间：隐藏原版列表的布局元素（同房间菜单 `room_ui.cpp` 的做法，按名字取元素写 `+0x1F8`），
  画世界地图；原版输入（方向键、确认、返回）照常送给对象，地图只额外处理鼠标 / 右摇杆的平移缩放与点选。
- 绘制：复用房间菜单已在用的原生路径——布局里的 `Texture` 元素 + `Skin_Texture` 换贴图 0x7F63E0（`feature-2026-10-12-room-loadout-menu.md`
  「没有插件时面板不显示」一节，H，已在发布版里用着）。
  - 底图：两个全屏级 Texture 元素（世界、日本细节），平移 / 缩放 = 改元素位置与缩放（`worldmap_logic.h` 的 `View`/`ToScreen` 给出位置）；
    缩放超过细节图覆盖的倍数时只画细节图。元素的缩放 / 裁切是否可写在运行时生效 **M，需实机**。
  - 标记：小 Texture 元素（`EDF6VC_WHITE.DDS` 染色：已通关灰、可进入黄、分支橙、锁定暗红），同地点多关按 `Fan` 螺旋散开；
    悬停显示原版的任务名 / 编号（TextField），点中走 §4 的确认。
  - 航线（「到下一关的指引路线」）：从当前任务（存档 ModeSave+0，`mission-list-re.md`）到它的后继（列表第 3 列）里已开放的行（`RouteTargets`），
    沿大圆取点（`GreatCircle`，跨日界线处 `SeamBreaks` 断开），用一串小点元素画虚线；当前任务的标记加光圈。
  - 备选：插件自己的 D3D11 pass（`thermal_gpu.cpp` 已拿到设备）。不用它为首选，因为 UI 层次（对话框、淡入淡出）要自己对齐。
- 「开始游戏就打开地图」（单机）：本部的选关本来就由 MAINSCRIPT 打开同一对象，所以「进本部即自动打开选关」需要再逆向 MAINSCRIPT 的 `HQMain` 流程
  （任务结束回本部走 `RestoreMissionSelect`，说明脚本有「直接回到选关」的路径，M）。原版列表不保留（维护者：在不在无所谓）：地图即选关画面。
- 「联机选择任务打开地图」：房间 `Button_MissionSelect` → 0x8FC2C0 打开的就是这个对象，接管后自动是地图，不需要额外改动。
  地图只改本机画面，选中结果仍是原版的 `MissionResult`，不经网络、**不需要升房间协议号**（本分支没有升）。
- 默认视图：`Frame` 框住当前任务包所有已显示的行（日本全境），没有已显示行时框住全部地点。

## 6. 待办（需要实机或维护者决定）

1. ~~底图打包~~（2026-11-11 改为全球 JPEG 瓦片，§2）。剩：分享包 `run_export.py` 的 `EXTERNAL_MODELS` 加 `worldmap_tex`；插件侧瓦片读取（WIC 解码、后台线程、显存缓存上限）与 D3D11 绘制，先实机验证拖动时补块的速度。
2. 逆向 `lyt_HUiHQMissionSelect.sgo` 的元素名（哪些是列表、哪些要保留：标题、难度、兵种），写隐藏表。
3. 实现 §5 的绘制与输入（新文件 `src/worldmap.cpp`，签名进 `room_ui_sigs` 同类的审计测试），先在测试场任务包上实机迭代。
4. MAINSCRIPT `HQMain` 的流程逆向，做「单机进本部直接开地图」。
5. 载具预览：实机确认载具菜单 SGO 不崩、镜头倍数与转速合适（`room-3d-preview-re.md` §6.1）。
6. 维护者审一遍地点配对（`pylib/worldmap.py MAPS`），尤其 `nw_europe01` → 神户北野异人馆街（「欧洲风街区」的日本对应）、
   任务包地图 `nw_dlcmap601/602`（内容没读，暂放京都 / 仙台）。
