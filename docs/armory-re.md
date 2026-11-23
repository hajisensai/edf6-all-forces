# 整备画面（armory）逆向笔记

EDF.dll TimeDateStamp `0x678CCB46`，地址均为 RVA。证据等级：**H** = 反汇编 / 已有实测直接支持；**M** = 由调用关系推断、未实机看；**L** = 推测，需要实机确认。

本文件对应 `docs/plan-2026-11-24-ui-rewrite.md` 阶段 2（界面框架）与阶段 1 的运行时部分（整备方案的读写与切换）。代码：`src/armory.cpp`（游戏侧）、`src/armory_logic.h`（状态机）、`src/armory_skin.h`（几何）、`src/armory_draw.cpp`（绘制）、`src/armory_presets.cpp`（方案）、`src/player_slots.h`（玩家槽位规则）。

## 1. 画在哪里：两个画面的 Draw 槽

| 画面 | vtable | slot 2（Draw）槽地址 | 原版函数 | 等级 |
|---|---|---|---|---|
| `ui::HUiHQMain`（离线 HQ） | `0x1804DD8` | `0x1804DE8` | `HUiBase::Draw 0x7FA970` | H |
| `ui::HUiRoom`（联机房间） | `0x180AC40` | `0x180AC50` | `HUiBase::Draw 0x7FA970` | H |

- 两个画面都没有重写 Draw，槽里都是基类的 `0x7FA970`（入口字节 `48 89 5C 24 18 48 89 74 24 20 55 57 41 56 48 8D` 作为签名检查）。插件读槽里**当前**的值并链在它后面（别的插件可能先挂了），先调原函数、再画整备。H
- OnUpdate 槽（`0x1804DE0` / `0x180AC48`）与析构槽沿用 `docs/room-ui-re.md`，由 `room_ui.cpp` 挂；整备的生命周期跟随它们（§3）。H

## 2. 绘制上下文（与世界地图共用的约定）

- Draw 的第二个参数（ctx）`+8` 是 `ID3D11DeviceContext*`（`0x113E928: mov rcx,[rcx+8]`），`+0x398` 是当前视口 `int[4]`（`0x113E90B: movups [rcx+398h],xmm0`）。两处都在安装时按字节核对，不符则整备不安装（日志 `REFUSED`，房间 / HQ 不再能打开整备，不退回旧对话框）。H（`worldmap.cpp` 已在实机用同一约定画选关地图）
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
  3. 玩家兵种与武器：写 `GS(0x20B2890)+0x6E90`（兵种）与 `GS+0x6E98 + 兵种*0x18 + 槽*4`（武器行号）。只写武器表里**按名字找得到**（`FindRow 0xE1BA0`，表 = `GS+0x130`，`+0x188` 非零表示已载入）、**已拥有**（武器记录 flags bit 0，记录容量内）且**该槽接受该类别**（`player_slots.h`）的行；找不到 / 未拥有 / 不合槽的槽保持原值并记日志。行号写入位置与原版武器选择 `0x7DD8AB` 写的是同一处。H（地址）；写入后 HQ / 房间里的玩家模型与存档是否立即反映：**M**（原版换装后还会发通知 3D 预览的消息 4、可能还有脏标记，插件目前没有调用，见计划 §82）
- 全部在 UI 线程（OnUpdate 帧内）执行，与原版武器选择写这些字段的线程相同。M

## 5. 未验证 / 待办

- 以上运行时行为**全部未实机测试**（本阶段只做了离线构建与离线测试）。
- 各页目前是占位：支援小队 / 载具页只读显示 `room_menu.h` 模型的单位与士兵行；玩家页显示 4 个兵种的槽位武器名；任务页有「世界地图」（灰）和房间里的「对战设置」（打开原生对战对话框页）。编辑在阶段 3 / 5 做。
- 旧菜单单位页标题里的状态行（保存结果、生成器进度、房间同步、房主设置、PvP 等待）整备里还没有显示位置。
- 3D 预览（阶段 4）未接：整备打开时预览一直隐藏。
