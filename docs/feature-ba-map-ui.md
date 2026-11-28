# M 地图照抄断箭（Broken Arrow）的操作与显示：对照清单

用户原话（2026-11-27，`docs/feedback-*` 体例，只当证据）：「还有这个操作方式和显示都抄抄断箭吧，m里面的，包括m里面装配东西，之类的？按v上下高度一点反馈没有」

资料来源：Broken Arrow 官方手册（Matrix Games，`BrokenArrowManualEBOOK.pdf`）与 Basics 小册（`BrokenArrow_Basics.pdf`）。
手册里查不到的（命令确认音、路径配色、空中单位高度图标）在下表标「未查到」，那几项按 EDF6 现有配色和合成音自定。

## 1. 断箭的界面骨架

| 断箭 | 说明 |
| --- | --- |
| 左下 Selection 面板 | 选中单位的小卡片，同类叠成「×N」；点卡片或按 Tab 在选中里只挑一类下令 |
| 中下 Status 面板 | 所选单位的图像、名称、血条、每件武器的弹药 n/m |
| 右下 Action 面板 | 约 4×3 的图标格，每格一个命令；数量型（烟雾 ×2）在格内显示剩余 |
| 单位头顶标签 | 名称、掩体、停火图标、补给、血条、瞄准 / 装填条、雷达、受损图标 |
| 左上点数 | 黄色储备 + 白色每分钟收入（「1044 / +300」） |
| 增援菜单 | 左上，分页（含飞机页）；飞机下令是「拖一下定位置与航向」 |
| 热键 | Q 攻击移动、E 取消、R 倒车、F 快速、U 下车（UU 原地全下）、H 停火、T 火力任务、Z 冲刺 / 加力、X 烟雾、C 诱饵、V 高度切换、B 返航、Shift 排队、Ctrl+1-9 编组、Tab 选中内挑一类、F1-F4 信号 |
| 高度 | V 降低，再按 V 恢复；战机默认高空。低空更隐蔽、更慢；轰炸与俯冲需高空。高度的界面图标 / 语音：未查到 |
| 部署前配装 | Army Builder 分 REC/INF/VEH/SUP/HEL/AIR；单位卡显示可用数「4/4」与黄色价格；Customization 每项列出选项与差价（「Loadout · High Explosive rounds · +0」）。战斗中改配装：未查到（手册只写部署前） |
| 路径 | 炮兵弹道无效时变红、激光锁定红线；普通移动路径配色：未查到 |

## 2. 抄了什么、对应到 EDF6 哪里

| 断箭 | EDF6 M 地图（本分支） | 代码 |
| --- | --- | --- |
| 右下 Action 图标格 | 命令卡从「整行居中的长按钮」改成右下固定格子（宽屏 2 行、窄屏 3 行）：图标 + 短词，**格子右上角印快捷键**（原来只在提示框里） | `map_buttons.h` `Grid`，`hud_map.cpp` `MapButtons` |
| 左下 Selection 卡片 | 命令卡左边一排单位卡（只显示，不可点）：同类同归属叠成「xN」，卡上写当前命令、空中单位的高 / 低与离地高度、停火、排队点数；Tab 子选的那一类高亮；放不下时缩成紧凑卡，最后一张写「+N」 | `map_buttons.h` `GroupCards` / `Cards`，`hud_map.cpp` `MapSelectionCards` |
| V 高度切换 | 原有命令（command protocol v11）不变；**补反馈**：① 提示行「高度 → 高空：N 架」；② 提示音（升高上行三音、降低下行三音）；③ 地图上每架飞机旁「▲高 / ▼低 + 离地米数」标签，刚切换时闪烁；④ 选中的飞机画一根落地垂线（高度指示）；⑤ 命令格里的高度按钮写当前状态（高 / 低 / 混合 / ?），提示框列出高 / 低 / 别人机器上各几架 | `mapcmd.cpp` `Publish` / 下令处，`mapcmd_cue.h`，`hud_map.cpp` |
| 命令确认音 | 每个命令下达后一声短确认，全部被拒时低音两声（断箭是单位语音，EDF6 用合成提示音代替）。共 4 段：ack / up / down / refuse | `mapcmd_cue.h`，`vsynth.h` `CmdAck/CmdUp/CmdDown/CmdRefuse`，`jetaudio` `kClipCmd*`；ini `MapCueVolume`（0~2，0 静音，默认 0.8） |
| 路径样式 | 命令点处画箭头（方向 = 单位 → 点），Shift 排队的后续段改成同色虚线 + 段末箭头（原来是统一暗色实线） | `hud_map.cpp` `MapCommands` |
| 部署前配装 → 战斗中切换 | M 里在支援栏选中（武装）一项有载具挂载的支援时，支援栏右侧出「配装」面板（若同时开着编组面板，则在编组面板右侧）：原版、当前值、**本机已安装的变体**逐行列出，当前行加粗 + ▶，右侧写状态；点一行即切换，提示行写结果并响确认音，失败写原因并响拒绝音 | `map_loadout.h` `VariantOfFile` / `BuildRows`，`map_loadout_row.h`，`mapcmd.cpp` `LoadoutRows` / `PickLoadout`，`hud_map.cpp` `MapLoadoutPanel` |
| 单位卡价格 | 已有（支援栏每行价格，买不起变红），未改 | — |

### 配装面板的数据来源（与安装器菜单 8 同一份）

- 读写的就是 `Mods/Plugins/EDF6VehicleCrew.ini` 的 `SupportVehicle_<KEY>`：安装器菜单 8、游戏内整备菜单、M 地图三处改的是同一个键，没有另一份数据。键由 `SupportHullCatalog` → `SupportCallKey` 得出，机体由 `VehicleBodyOfKey` 得出。
- 写入走 `loadout_store.cpp SaveSupportLoadout`（解析校验、写 ini、`ReloadConfigNow`），和整备菜单同一条路。
- 候选行：原版（键留空）、当前值、安装器列出的 `Mods/OBJECT` 文件（`mod_file::dirs[0].files`，插件加载时读入，**不在运行时扫目录**）里的 `EDF6VC_LO_<BODY>_<16hex>.SGO`（文件名就是变体编码，`LoadoutOfVariant` 解出挂载，再用 `VehicleVariantFile` 反算一遍核对）。同一挂载只列一次，最多 8 行。只列已安装的变体；M 里不新建挂载（要退出游戏跑安装器生成 SGO），本局运行中才生成的文件也不会出现在列表里。
- 每行状态（面板每秒刷新一次）：
  - 「本关可用」：本关开场已预载（`SupportVariantReady`），且（房主侧）房间里每人都有这个文件；
  - 「下一关生效」：文件在盘上但本关没预载（换成它后本关仍按原版出动，下一关起生效）；
  - 「有人缺文件」：房主侧查到有队友没有这个文件（`SupportPeersHaveVariantFile`），本关按原版出动（`DecideVariant` 的现有回退）。**只有房主知道**；客人本机看不到别人缺不缺，客人这边只会显示「本关可用 / 下一关生效」，真缺时由房主回退原版。
- 喷气机的出击（DN 轰炸机 / DN 防御者战斗机 / DN 战斗机·出击，2026-11-27 起按机型起名）没有自己的键，飞的是同机种守点呼叫的挂载（`SupportHullCatalog`）；面板里用琥珀色一行写「与『××』共用」，切的就是那一个键。

### 联机

- 高度切换、提示音、标签全是本机显示，命令仍是 command protocol v11 的 `altitude`，协议不变。
- 别人机器上的飞机：本机不知道它的高 / 低（只有它的主机知道），标签写「高度 ?」，按 V 时它照旧被跳过（现有 `notAuthority`）。
- 配装切换只改本机 ini；`room_sync.cpp` 下一拍把新的 `SupportCfg()` 发给房间，主机用该成员发布的配置规划他的呼叫（`PlanCfg`），所以**不用改协议、kVersion 仍是 11**。变体文件缺失按现有规则回退原版并提示。

### 验证

- 离线：`map_buttons_check`（格子、卡片、配装解码与行序）、`map_command_runtime_check`（V 的提示与音、配装点选写键 / 点亮 / 提示 / 再点不写 / 解除武装关面板）、`map_command_identity_test`、`hud_view` 场景 `map_air_altitude*`、`map_nine_squads*`、`map_place_payload`、`map_loadout`、`map_loadout_narrow`（点击框在屏内、不压命令格和支援栏）。
- 实机待确认：任务中切换后 ini 重载是否顺；联机客人切换后房主是否按新配置出动、缺文件时回退；提示音音量；配装面板贴着支援栏的位置在各分辨率下是否挡视野。

## 3. 受游戏限制做不到 / 没做的

| 断箭 | 原因 |
| --- | --- |
| 单位语音确认 | 游戏没有现成的 NPC 应答语音可调；用合成提示音代替 |
| Status 面板里每件武器的弹药 n/m | 插件读不到 NPC 武器的通用弹药表（只有载具挂架有）；单位卡只写命令 / 高度 / 停火 |
| 战斗中生成新挂载 | 新挂载要安装器生成 SGO（游戏运行时写 `Mods/OBJECT` 不会被本关读到）；M 里只能在已安装的变体间切换 |
| 已在空中的飞机换挂载 | 机体模型在出动时按变体文件创建，换挂载只影响之后的呼叫 |
| 直升机 / 炮舰机 / 武库鸟（原天顶）导弹的挂载（这些支援不出配装面板） | 这些机体没有可配置挂点（`support_loadout.h` 注释：NPC 驾驶员只打原版挂架） |
| 返航（B）退点数 | EDF6 支援点数没有退款概念；B 仍是现有的撤离 |
| 高度改变真实飞行（爬升速度、低空隐蔽） | 本分支只做显示与反馈；飞行行为归 `claude/1127-entry-edge` |
