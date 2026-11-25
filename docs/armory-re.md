# 整备画面（armory）逆向笔记

EDF.dll TimeDateStamp `0x678CCB46`，地址均为 RVA。证据等级：**H** = 反汇编 / 已有实测直接支持；**M** = 由调用关系推断、未实机看；**L** = 推测，需要实机确认。

本文件对应 `docs/plan-2026-11-24-ui-rewrite.md` 阶段 2（界面框架）与阶段 1 的运行时部分（整备方案的读写与切换）。代码：`src/armory.cpp`（游戏侧）、`src/armory_logic.h`（状态机）、`src/armory_skin.h`（几何）、`src/armory_draw.cpp`（绘制）、`src/armory_presets.cpp`（方案）、`src/player_slots.h`（玩家槽位规则）。

## 1. 画在哪里：两个画面的 Draw 槽

| 画面 | vtable | slot 2（Draw）槽地址 | 原版函数 | 等级 |
|---|---|---|---|---|
| `ui::HUiHQMain`（离线 HQ） | `0x1804DD8` | `0x1804DE8` | `HUiBase::Draw 0x7FA970` | H |
| `ui::HUiRoom`（联机房间） | `0x180AC40` | `0x180AC50` | `HUiBase::Draw 0x7FA970` | H |

- vtable 由 RTTI 确认（`vtable[-1]` 的 CompleteObjectLocator → `.?AVHUiHQMain@ui@@` / `.?AVHUiRoom@ui@@`），离线审计 `tests/armory_native_audit.py`（ctest `armory_native`，`EDF6_NATIVE_DLL`）逐项核对本节与 §2、§4 的地址。H
- 两个画面都没有重写 Draw，槽里都是基类的 `0x7FA970`（入口字节 `48 89 5C 24 18 48 89 74 24 20 55 57 41 56 48 8D` 作为签名检查）。插件读槽里**当前**的值并链在它后面（别的插件可能先挂了），先调原函数、再画整备。H
- OnUpdate 槽（`0x1804DE0` / `0x180AC48`）与析构槽沿用 `docs/room-ui-re.md`，由 `room_ui.cpp` 挂；整备的生命周期跟随它们（§3）。H

## 2. 绘制上下文（与世界地图共用的约定）

- Draw 的第二个参数（ctx）`+8` 是 `ID3D11DeviceContext*`（`0x113E928: mov rcx,[rcx+8]`），`+0x398` 是当前视口 `int[4]` `{x, y, w, h}`（`0x113E90B: movups [rcx+398h],xmm0`，后面 `+0x3A8` 是 `float minZ, maxZ`）。两处在同一个视口设置函数 `0x113E8F0` 里：存好视口后 `0x113E984 call [rax+160h]` 即 `ID3D11DeviceContext::RSSetViewports`（vtable 第 44 项）。两处都在安装时按字节核对，不符则整备不安装（日志 `REFUSED`，房间 / HQ 不再能打开整备，不退回旧对话框）。H（`worldmap.cpp` 已在实机用同一约定画选关地图）
- 整备在视口里按设计分辨率（`ui_gpu` 的 Painter，1920×1080 等比）铺满整个视口；鼠标坐标经 `::worldmap::ClientToDesign` 换算到设计坐标。H（同世界地图）
- **整备画在画面自己的 Draw 之后**；在它之后绘制的东西（`HUiMainFrame` 的顶栏 / 底栏、EDF6Coop 的叠加文字、游戏的弹窗）仍会画在整备上面。整备打开时这些是否遮挡、遮挡多少，没有实机看过。**L**
- 绘制线程：Draw 在渲染时调用，与 UI 线程（OnUpdate）不一定是同一线程。UI 线程每帧把 `View` 发布到 `published`（`viewLock`），Draw 侧拷贝成 `painting` 后在 `gpuLock` 下画。M

## 3. 输入所有权与生命周期

- 打开：F2（`RoomLoadoutKey`）/ L3+R3（`RoomLoadoutButton`，经游戏的手柄表读取）或画面菜单里的原生按钮。原生按钮的文字是 `Tx::roomTitle`，改为「整备」（英文 ARMORY）。H（按钮机制见 `docs/room-native-menu-re.md`）
- 打开期间，以及关闭后关闭键仍按住时，`room_ui.cpp` 用把 `ctx+8`（有效的 `UiInput*`）置空的 ctx 副本调用画面的 OnUpdate：游戏自己的「不是输入所有者」状态，网络、成员、聊天照常运行。H（与旧菜单相同的机制，已实机用过）
- 整备自己读输入：键盘（`edf::AsyncKey`，游戏在前台且地图没占用按键时）、鼠标（窗口子类化拿滚轮和字符，只在需要输入文字时）、第一个 XInput 手柄。M（未实机）
- 关闭时机：
  - 自己的键（Esc / B / 关闭键）；
  - 画面失去输入（`GetInput 0x8DAD00` 用真实 ctx 返回空：游戏弹窗、子画面、过场）——此时各页 `Commit` 后关闭；M
  - 画面对象换了或其析构函数跑了（`RoomGoneHook` / `HqGoneHook` 调 `armoryui::ScreenGone`，Draw 侧立刻不再画在它上面，并释放 GPU 资源）；H（析构钩子本身见 `docs/room-ui-re.md`）
  - 任务开始（`ResetRoomUi` → 下一帧 `ForgetMenu` → `armoryui::Forget`，不提交）；`Enabled` / `RoomLoadout` 运行中改成 0（`Off`）。H（代码路径）
- HQ 自动打开选关（`HqAutoMissionSelect`）在整备打开或其关闭键仍按住时不触发。H（代码路径，`tests/room_ui_wiring_guard.py`）

## 4. 整备方案：写玩家装备与支援配置

- 方案文件：`EDF6VehicleCrew.presets.ini`（与插件 ini 同目录），每次修改整份原子替换（`ReplaceFileAtomic`）。文件读不出或格式坏：整本视为空且拒绝一切修改，不覆盖原文件。H（代码）
- 切换方案：
  1. 支援键：`loadout_preset.h SwitchEdits` 算出要改的 `SupportPreset_*` / `SupportVehicle_*` 等键，一次 `SaveSupportLoadouts`（全成或全不成，逐值用插件自己的解析器校验，然后重载配置），再启动变体生成器。H（代码，与旧菜单保存同一路径）
  2. 联机：`room_sync.cpp` 每次 tick 编码 `SupportCfg()` 发布本机成员配置，重载后下一次 tick 自然带上新配置，不需要额外调用。**M**（代码路径，未实机看房间里其他成员收到）
  3. 玩家兵种与武器：`player_slots.h PlanWrites` 先按名字筛（武器表里**按名字找得到**：`FindRow 0xE1BA0`，表 = `GS+0x130`，`+0x188` 非零表示已载入；**已拥有**：武器记录 flags bit 0；**该槽接受该类别**），再逐兵种交给 `wequip::SetClassSlots`（`weapon_equip.cpp`，GS+0x6E98 的唯一写入者，按原版规则再判等级上限与重复，重复按写完以后判）写 `GS+0x6E98 + 兵种*0x18 + 槽*4`，最后 `wequip::SetClass` 写 `GS(0x20B2890)+0x6E90`；被拒的槽保持原值并记日志。然后对新兵种的槽 0..3 调 `roompreview::ShowPlayerWeapon`（消息 4 的唯一发送者）。行号写入位置与原版武器选择 `0x7DD8AB` 写的是同一处。H（地址 / 代码）；写入后 HQ / 房间里的玩家模型是否立即反映：**M**（消息 4 只在视图正显示该兵种时发；兵种变了视图是否重建未知，L）；存档脏标记：原版确认路径上没有（`weapon-select-re.md` §3.6），装备随游戏自己的下一次存档写盘。M
- 全部在 UI 线程（OnUpdate 帧内）执行，与原版武器选择写这些字段的线程相同。M

## 5. 未验证 / 待办

- 以上运行时行为**全部未实机测试**（本阶段只做了离线构建与离线测试）。
- 页签顺序：武器 / 支援编成 / 载具 / 任务。支援编成 / 载具页可编辑（阶段 3，见 §6）；任务页有「世界地图」和房间里的「对战设置」（打开原生对战对话框页）。
- 武器页（阶段 5，`weapon_page_logic.h`，离线测试 `weapon_page_logic_test`）：列表列 = 兵种分页（Z / C、LT / RT）下每槽一行；中间列头两行是类别标签（A / D、右摇杆左右，或点）与来源开关 EDF6 / EDF5 / EDF4.1 + 搜索框；下面是系列折叠的武器列表（「▸ 系列 持有 n/m Lv a–b」，超上限 / 未持有 / 重复的灰显）；详情列上方 7 行（设计坐标 {1328,130,1792,530}）留洞给 3D 预览（`Page::Hole` → 背景菱形与面板绕开它画，`roompreview::SetViewPlacement` 把玩家模型放进去，镜头「正面」），下面是装备 / 展开、收藏、来源、显示未持有、只看收藏与属性对比（目前只有等级，▲▼）。确认即写 GS（同原版，无「保存」步骤）。光标所指的武器经 `ShowPlayerWeapon` 让玩家试拿，换槽 / 离开本页时换回已装备的。收藏存 ini `[VehicleCrew] WeaponFavourites`（逗号分隔的武器 id）。
- 搜索**没有屏幕键盘**：键盘按 Tab（或点搜索框）直接打字（窗口子类化拿 WM_CHAR，与方案改名同一路径），Enter 保留、Esc 清空、Backspace 删字；手柄用首字跳转（PgUp / PgDn，右摇杆上下）在列表里按名字首字循环跳。匹配不分大小写与全半角，片假名 = 平假名。
- 新增按键（`armory_logic.h Key`）：`kAltPrev / kAltNext`（A / D，右摇杆左右）、`kJumpPrev / kJumpNext`（PgUp / PgDn，右摇杆上下，阈值 16000）、`kSearch`（Tab）。这些只给页面（`Page::Key`）用，框架本身不处理；页面拿走的键当帧不再给框架。
- 3D 预览留洞未实机：HQ 画面在整备之后画的东西（顶栏 / 底栏、弹窗）可能盖住洞；洞只在看**当前兵种**时留（视图只能显示当前兵种的玩家模型）。L
- 只处理 1P（GS 玩家 0）；分屏 2P 的装备整备不改。
- 3D 预览：武器页（玩家本人模型拿着光标所指的武器，放在详情列的洞里）与支援编成 / 载具页（中栏视口，§6）。三页共用原版那一个视图，放置只由 `room_ui.cpp DrivePreview` 按 `armoryui::PreviewWanted` 设 / 清（`own` = 玩家本人模型、不挂 override；此时 `TickPreview(false)`，与武器页原先的行为一致），各页不再自己调 `SetViewPlacement` / `ClearViewPlacement`，避免切页那一帧互相清掉。H（代码）；实机效果 L（未实机）。
- 武器页的洞在预览钩子没装上（`InstallPreviewHooks` 失败）时仍然留着，洞里没有模型、露出 HQ 画面。L（代码推断，未处理）
- 旧菜单单位页标题里的状态行（保存结果、生成器进度、房间同步、房主设置、PvP 等待）整备里还没有显示位置。

## 6. 支援编成 / 载具页（阶段 3，`src/support_pages.h`）

- 数据：打开整备时拷一份 `room_ui` 刚从 ini 读的 `roommenu::Menu`，两页共用（`sp::Model`）；编辑一律走 `room_menu.h`（`detail::Change`、`AddSoldier`、`Materialise`、挂载增删），与旧菜单同一套解析 / 校验。H（代码）
- 保存：每页 `Commit` 只写**自己那页**改过的单位（`CollectSave` → `roomui::SaveItems` → 按页过滤），一次 `SaveSupportLoadouts`（全成或全不成），成功后 `StartVariantGeneration`，与预设切换同一路径（`armory.cpp SaveSupport`）。离开页 / 关闭时有改动就弹「保存并关闭 / 放弃 / 返回」。预设切换后两页从 ini 重读（`sp::Reload`），未保存的编辑丢弃。H（代码；离线测试 `support_pages`）
- 开火键列：喷气机 / 直升机等的挂载显示「随主武器循环」，坦克的显示「随主炮」，依据 `docs/multi-weapon-keys.md` 的默认 `key=0`（挂载跟主武器循环）。**M**：那份表里把部分挂载映射到额外键 3～6 / 副扳机的车型（派生种类 `<KIND>_K<key>`）在本页**没有区分**，仍显示默认文字。
- 3D 视口：`room_ui.cpp DrivePreview` 每帧问 `armoryui::PreviewWanted`，变了才 `ShowPreview` / `SetViewPlacement`（相机为页面当前视角预设），否则 `HidePreview` + `ClearViewPlacement`。页面在视口处给 `Build` 留洞（`Hole`），模型在 3D 阶段画在所有 UI 之下。H（代码）；模型是否确实出现在洞里、位置 / 大小是否对：**L**（未实机）
- 视口的限制（代码事实，H）：
  - 原版视图一次只显示**一个模型**：小队页显示选中的那名士兵，不能 4 人同框；视口左上写明「只显示选中的士兵」。
  - 每个支援载具机体都有统一模型文件（`docs/unified-model-re.md` §3.1）：预览战斗会刷出的那个文件（有配置用 `EDF6VC_LO_*` 变体，否则基础文件）。配置刚改、还没生成文件时视口显示「这套配置的模型还没生成」。乘员行显示乘员士兵。
  - 挂载**不画在模型上**（菜单渲染器只挂 WeaponTable 行），视口里用文字列出挂载。
  - 地面橙环与机甲 6 个槽位标注是 2D 叠加，位置由 `rp::Place` 投影原点算出：与模型是否对齐 **L**（未实机）。
- 联机规则面板（右上「我的呼叫 / 房主规则」）：**规则取自房主**（2026-11-24 起，`feat/ui3-hostrules`）。H（代码）
  - 为什么是房主的：能不能叫由房主裁决（`support_dispatch.cpp` `Plan` 用房主自己的 `SupportCfg().Enabled` 拒绝，客人的请求都经房主规划）；价格是 pt × 房主的 `SupportPtScale`（`support_points.cpp`：pt 池由房主保管，客人的 `SupportPointsPrice` 用房主经 SPTS 发来的 scale，但那是任务内的通道，房间画面里没有）。
  - 怎么传：房间字 `EDF6X_AF_ROOM`（`host_settings_logic.h`）升到 **v4**，在 v3 后追加 `d=<SupportConfig::disabled 的 64 位掩码，1..16 位大写 hex>` 与 `p=<SupportPtScale 的 float 位模式，8 位大写 hex>`（按位传，客人算出的价格与房主扣的完全一致）。新特性位 `kFeatHostRules=0x800`。解析严格：v3 字、缺字段、scale 不在 0..100 / NaN 一律整字作废。
  - 预算：最长字 `kWordLongest`=113 字节（含 NUL）≤ `kWordMost`=128；`room_sync_logic.h` 的 `static_assert` 把它和 `kConfigBudget`(3800)+`kHaveMost`(40)+`playerveh::kValueMost`(128) 一起算进 Coop 的 4096 字节总预算（正好 4096）。`room_sync_test` Budget 用最长配置行 + 最长房间字实测不被拒。
  - 取值（`DecideSupportRules`）：离线 / 房主 → 本机 ini；客人且已收到房主的字 → 房主的；客人未收到 → `known=false`，面板显示「正在等待房主的规则」、不灰掉任何单位，**不拿本机 ini 顶替**。`armory.cpp` `FillRules` 每帧从 `SupportRulesInEffect()` 刷新（房间帧先跑 `HostSettingsRoomTick`），价格用 `support_points::Price(pt, scale)`。
  - 房间协议 15 → **16**（`mod_room.h`）：v3 读者会整字拒收 v4，旧版客人的招募 / 掩护 / 开镜散布会退回本机 ini，所以旧版不能进房（全员同版本，AF_PROFILE 公告所需协议）。
  - 仍只是**显示**：客人本机的提前拒绝（`support_dispatch.cpp` `Request` 里 `SupportCfg().Enabled(index)`）和打击（strike）的停用检查仍读本机 ini；真正的裁决在房主。要把客人本机的提前拒绝也改成房主规则，需另改 `support_dispatch.cpp` 并补 `support_dispatch_test`。
  - 需实机确认：客人进房后 1–2 秒内面板从「等待」变为房主规则；房主改 ini 保存后客人面板跟着变；房主 `SupportPtScale=0` 时客人显示价格 0。
- 小队页签（第 1/2/3 小队）：鼠标点击，或在士兵列表 / 士兵字段里按 A/D、右摇杆左右（`kAltPrev/kAltNext`，`SquadPage::Key`）切到上 / 下一小队的第一名士兵（循环）；单位列表里不响应。提示栏写明（`spSquadHintCentre/Details`）。Z / C 在单列页面上切换视角预设。H（代码）
- 载具页「开火键」列（`sp::PylonFireKey`）：挂载码 1..10 对应的武器文件 `EDF6VC_<NAME>_<发数>.SGO` 按 `StoreSpecOfPrefix` 取 `StoreSpec::fireKey`；支援坦克上 3..6 显示「开火键 n：键盘键 / 手柄键」（本机 `VehicleFire<n>Key/Button`），其余显示「随主炮（切换键 切换弹种）」；战机（`jet_combat.cpp` 不看 fireKey）总是「随主武器（切换键）」，切换键 = `PlayerJetSwitchKey` / LB。目前 stores.inc 里挂载用的文件 fireKey 全是 0，所以实际都显示切换键。H（代码）
- 游戏强制收走整备画面（失去输入 / 换画面 / 选关对象析构）时**放弃**未保存的修改（`DropEdits`：各页 `Discard`，记日志），下次打开在提示栏说明丢了几页（`armoryEditsDropped`）；不再静默保存。H（代码）
- 状态条（`ar::StatusLines`，提示栏下最多 4 行，警告为红）：房间开始被挂起（`RoomSyncStartHeldLine`）、PvP 等待、上次保存结果、生成器状态（500 ms 读一次状态文件）、房间同步行、房主设置行。H（代码）
- 房间里开始任务的闸门（`room_pvp.cpp StartGate`）：先 `RoomSyncAllowsStart`（`room_sync_logic.h HoldOf`：成员配置在路上 / 本机或成员生成中 / 成员核对清单 → 等待；本机或成员生成失败 / 无生成器 / 成员无预同步 / 房间清单读不出 → 失败，同样挂起，由同步行说明原因；仅超出房间上限的文件「按原版出动」与成员配置读不出「按默认」不挂起），再 PvP 锁。整备或地图切方案后 `RoomSyncNudge` 让下一次 Tick 立即重算。M（未实机：StartGate 与 room_ui 的 TickRoomSync 被假定在同一线程）
