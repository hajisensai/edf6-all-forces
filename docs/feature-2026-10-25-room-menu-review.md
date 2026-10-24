# 2026-10-25 支援战前配置：HUD 重叠、菜单尺寸、配置约束、玩家自己召唤的载具

EDF.dll TimeDateStamp `0x678CCB46`，RVA。证据为本机 EDF.dll 静态反汇编、Root.cpk 布局读取与 edf-coop-stable 源码阅读；**没有启动游戏、没有写游戏目录**。

## 用户原话（2026-10-25 批量反馈）

> 支援战前配置是合理的改，不是完全随心所欲。hud重叠了，这个hud都换成原版那样的不行吗。还有这个支援战前配置显示好小，弄成选择兵种武器那样多好。而且换载具要能换自己召唤的啊，不只是npc的载具

截图：主菜单上插件的「载具与 NPC 配装: F2 / L3+R3」与 EDF6Coop 的「64Player MOD :ON  F3/Tab/RS: Members 5-8 … armor :OFF」叠在一起；「支援战前配置」是窄小的列表窗口。

**对「合理的改，不是完全随心所欲」的理解（有歧义，已按两种读法都处理）**：一种读法是「配置项要受合理约束」（下文第 3 节）；另一种读法是承接 10-19 的「排版可以不用顾虑原版」，意思是「改界面可以，但要合理、像原版，不是随心所欲地自绘」——这正是第 1、2 节做的事。两种读法不冲突，但如果用户本意只是后者，第 3 节的重量上限是可以撤回的设计选择，需要用户确认。

## 1. HUD 重叠

### 现状与根因（已验证）

- 插件在房间 / HQ 的补丁布局里自画一行入口提示 `vcEntry`：`tools/room_layout.py` 旧值 pos (80, 974)、area 1000 × 34、22 号字（布局坐标 1920 × 1080），每帧由 `room_ui.cpp Frame` 写入 `EntryLine`（「载具与 NPC 配装：F2 / L3+R3」）。
- EDF6Coop 的 `MSLabel` 写在 `LYT_MAINFRAME`：pos (192, 970)、area 640 × 28、22 号字（edf-coop-stable `multislot/tools/sgo_write.py add_mainframe_label`）。两者矩形 [80..1080]×[974..1008] 与 [192..832]×[970..998] 重叠——就是截图里的叠字。
- 根因不是「两个 mod 碰巧选了同一个坐标」，而是本插件把自己的字画进了 **HUiMainFrame 的底栏**：原版 `LYT_MAINFRAME` 的 `ControlGuideText`（1242, 970）/ `GuideTextSetting`（970 / 994）就是原版的按键帮助条，这一带属于常驻主框架（它盖在所有画面上），Coop 的标签也放在这里。旧测试只检查了「入口在原版 ControlGuideText 左边」（x ≤ 1242），这正是靠坐标错开、而没有把这一带当作别人的区域。

### 修复（已实现）

- **去掉自画的入口行**（`vcEntry` 元素、`kEntryName`、`Plan::entry`、`ToggleAvailable`）。入口完全走原版机制：画面菜单里的原生按钮「支援战前配置」（`vcLoad`，2026-10-19 起已有，`docs/room-native-menu-re.md`），按钮获得焦点时把实际快捷键写进画面自己的说明框 `MenuDescText`（原版菜单项就是这样描述自己的）。菜单关闭时插件在画面上什么都不画。
- **覆盖层（对话框签名不符时的后备菜单）从布局上让开主框架的两条带**：`FRAME_TOP = 104`（主框架标题带 CurrentUIDispMain / Sub 到 y = 102 为止）、`FRAME_BOTTOM = 960`（底栏从 y = 970 起）；面板改为 112..952，状态行与三行帮助重新排在带内。`tests/room_layout_test.py` 从 Root.cpk 读回原版 `LYT_MAINFRAME`，算出标题带底与底栏顶，逐个检查所有覆盖层元素的绝对矩形都在两者之间（不是写死比较某个对手的坐标）。

## 2. 菜单尺寸 / 形式

### 现状（已验证）

- 菜单不是 ImGui 也不是自绘：自 2026-10-19 起每一页是游戏自己的 `HUiDialogBox`（`APP:/UI/LYT_HUIDIALOGBOX.SGO`，`src/room_dialog.cpp` 照抄士兵画面换模型弹窗的构造），焦点、鼠标、手柄、确认 / 取消、音效都是原版的。
- 「好小」的原因：`HUiDialogBox` 按布局里的数据自适应尺寸——`0x80ED80` 按名字读 `button_pos_internal`（0x80EE69）、`button_size_min`（0x80EE85）、`button_size_max`（0x80EEB8），`0x80FC50` 读 `base_min_size`（0x8105E6）。原版值：窗口最小 400 × 36、按钮宽 180..300、按钮字 24 号，所以是一个窄窗口。
- 原版「选择兵种 / 武器」是整屏画面：`LYT_HUIHQSOLDIERSELECT`（`HUiHQSoldierSelect`）/ `LYT_HUIHQWEAPONSELECT`（`HUiHQWeaponSelect`：上窗 191..1378 × 147..449 的武器格、下窗说明、右侧 3D 士兵）。这两个类的数据源是游戏自己的兵种 / 武器表，复用它们来显示插件的配置需要逆向它们的数据接口，静态无法保证、也无法在不开游戏的前提下验证。
- 游戏还有一个「列表 + 焦点项说明」的原生对话框 `HUiDescriptionDialog`（`LYT_HUIDESCDIALOGBOX.SGO`，有 `Description` 文本与 `description` 键；vtable 0x17F6F70，OnUpdate 0x8088C0 先更新 +0x7C0 的说明再转调 `HUiDialogBox::OnUpdate` 0x810D10；10 处原版代码经 `[0x1F77808]` 打开它，例如房间筛选 0x8F6710 经 0x8BED70 / 0x845250）。它是「左侧列表 + 右侧详情」最接近原版的现成机制，但每项说明怎么传进去（InitParam vtable 0x1806FE8 的额外字段）还没逆向完。

### 本次实现（尺寸，已实现，未实机）

- 安装器从 Root.cpk 的 `LYT_HUIDIALOGBOX.SGO` 生成插件自己的副本 `Mods/UI/EDF6VC_LOADOUTBOX.SGO`（`tools/room_layout.py loadout_box`；新文件名，不影响游戏自己的对话框）：同一个类、同一棵树，只改 6 个成员——窗口最小 1100 × 560、按钮宽固定 600、按钮字 26 号（高度保持原版 42）、说明文字宽 1000，窗口去掉水平居中标志、改为中心在 x = 700（`hcoord 1` = 元素中心在 pos，与武器选择画面整屏面板 (960, 540) 的用法相同），所以窗口落在 150..1250，右侧的 3D 预览露出来，接近原版装备画面「左边列表、右边模型」的版式。
- `0x119C600` 的第三个参数就是布局名（`const wchar_t*`，进入后 `wcslen` + `0x3D450` 拷贝，0x119C690..0x119C6A2），插件把它从原版指针换成 `L"APP:/UI/EDF6VC_LOADOUTBOX.SGO"`；EDFModLoader 对 `app:/` 路径只看 Mods 下是否有文件（`docs/room-ui-re.md` §4，新文件名在 `Mods/OBJECT` 已实证）。菜单打开时检查 `Mods\UI\EDF6VC_LOADOUTBOX.SGO` 是否存在，不存在（旧安装）就用原版对话框，日志 `ROOMUI menu pages in ...` 写明用的哪一个。
- 车辆页、挂点页、选弹种 / 数量页的说明文字加一行「挂载重量 X / Y kg」（第 3 节）。

### 剩余（需要更大工作量）

- **真正的分栏**（左侧分类列表、右侧随焦点变化的详情）：方案是改用 `HUiDescriptionDialog`（每个按钮带一段说明：该单位的全部队员 / 挂点、颜色、重量）。需要逆向 InitParam 0x1806FE8 的说明字段与 0x8BED70 的条目构造，并实机确认。另一条路是像原版装备画面那样自建整屏 HUi 画面，工作量与风险更大。
- 窗口位置依赖「去掉标志 1 后 `HUiDialogBox` 不再改写水平位置」，这只从布局语义推断（`docs/room-ui-re.md` §5），代码里窗口尺寸 / 位置的计算（0x80FC50 之后）没有逐条核。

## 3. 配置约束

### 现状（逐项核过）

| 项 | 约束 | 位置 |
|---|---|---|
| 小队人数 | ≤ 该单位座位数且 ≤ 12 | `ParseSupportPreset` / `room_menu.h` |
| 兵种 | 14 个原版 AI 模板（职业 + 武器组合都是原版存在的） | `kSupportKindNames` |
| 背包武器 | 只能选该职业的（重装没有） | `npc_pack_table.h PackFits` |
| 颜色 | 游戏 HQ 调色板 40 色或原色（ini 手写的保留） | `hq_palette.inc` |
| 坦克挂点 | 主炮 HE / AP + 最多 4 个 AP / HE / GLM | `ParseVehicleLoadout` |
| 战机挂点 | 最多 5 个，7 种空中挂载 | 同上 |
| **战机每个挂点的数量** | **不受约束**：任何挂点都能挂 1..40 发，5 个挂点 × 40 枚 AIM-54 = 90 吨也被接受 | 旧 `ParseVehicleLoadout` |

### 修复（已实现）

- 每发挂载的质量就是飞行模型称重用的那组数（`pylib/vcobjects.py STORES`，`src/stores.inc kStores`：AIM-9X 85、AIM-120 152、AIM-54 450、AGM-65 300、AGM-114 50、Mk 82 230、Hydra 70 32 kg），机体可挂重量 = 机体净重（同一份 `JET_MASSES`）的 40%：对地攻击机 8800、制空战斗机 6400、截击机 8000、多用途机 7200 kg。原版自带的挂载都在内（最重的对地攻击机 4566 kg），「全带炸弹 / 全带导弹 / 只带机炮」（10-09 用户原话）都仍然可以（例：战斗机 16 枚 Mk 82 = 3680 kg、对地攻击机 38 枚 = 8740 kg）。
- 一处规则，处处一致：`support_loadout.h WithinPayload`——`ParseVehicleLoadout`（ini 读入：超重整条作废、按原版挂载出动并在日志 / HUD 点名「挂载共重 X kg，超过该机可挂的 Y kg」）、`LoadoutOfVariant`（联机对端收到的计划、房主预同步清单里的载具 token：超重即不是合法挂载），Python 镜像 `tools/support_loadout.py parse_vehicle / of_variant / decode_name`（安装器菜单 8 与生成器：恶意或旧版房主的超重清单不会让客机生成文件）。
- 菜单从源头不给不合法的选项（`room_menu.h PylonFits / MostRoundsFitting / NewPylon / CanAddPylon`）：数量页只列装得下的数量；弹种页只列至少能挂 1 发的弹种，换成装不下当前数量的弹种时自动降到装得下的最大数量；没有重量余量时不出现「添加挂点」；旧覆盖层菜单的左右切换同样跳过装不下的值。
- 坦克不称重：它的弹药在 `kStores` 里没有质量数据，限制仍是 4 个挂点（有数据之前不编数）。
- 选择 40% 是设计参数，不是逆向得到的原版规则（原版没有可配挂载的战机）：实机多用途战机作战构型的外挂约为净重的 0.4–0.6。若用户的意思只是第 1 节的读法，可以整体撤回。

### 联机

- 新版房主只会发出合法挂载；旧版客机照常接收。
- 旧版房主若在 ini 里写了超重挂载：新版客机按既有规则拒绝该计划 / 把预同步清单整份作废（与其它非法 token 同一策略），那名支援在该客机上不生成、预同步退回待生成清单。混版本房间本来就由 host settings 行提示版本不同。

## 4. 换玩家自己召唤的载具（剩余，未实现）

### 现状（已验证）

- 菜单能配置的载具是支援目录里的 `SupportVehicle_<KEY>`：`TANK_CREWED`、`TANK_DELIVERY`（空车交付给玩家开的坦克——这一项其实已经是「自己召唤、自己开」的）、4 种战机各守点 / 跟随。
- 空降兵（及插件放开后的各职业）用载具请求武器召唤的载具（类别 308：玩家战斗机 / 攻击机、8 种自驾载具请求 `EDF6VC_CALL_FLY_*`、原版 Blacker 等）由原版运输机的集装箱按武器里写死的 SGO 路径生成：落地后 `0x5E8C00` → `CreateObject(+0xB80 的路径)`（调用点 0x5E8FDC，`docs/airdrop-vehicle-re.md` §1），会话中**每个成员各自**的集装箱都按同一路径生成同一台载具并注册同一个派生网络 ID（0x5E91D7）。插件没有任何地方让这些载具吃 `SupportVehicle_` 的挂载。

### 设计（建议的完整做法）

1. 配置：新键族 `PlayerVehicle_<机种>=`（与 `SupportVehicle_` 同一语法与重量上限），菜单里加一组「我召唤的载具」单位。
2. 文件：生成器以请求版机体（`EDF6VC_FLY_*` / `EDF6VC_PJET_*`，整机碰撞箱、所有职业可坐）为底生成挂载变体 `EDF6VC_LO_P<机种>_<variant>.SGO`（现有 `jet_sgo` 换底板）。
3. 生成时替换：插件已经挂了集装箱出车调用点 0x5E8FDC（`src/airdrop.cpp`）；在那里按请求者（集装箱 +0xB70）找到召唤的玩家，若其配置了该机种挂载、且文件已预载，就把路径换成变体文件。
4. 联机（必须）：因为每个成员各自生成，**所有成员必须做同一个替换**，否则同一个网络 ID 下各端的武器不同。需要：每个成员在房间阶段经 Coop 成员属性公布自己的玩家载具挂载 token（复用 `room_sync` 的 token 与校验），其他成员预生成；出车时用「请求者的 token + 全员都有该文件」决定是否替换（与 `DecideVariant` 同一原则），任何一端缺文件则全员都用原版。
5. 实机：集装箱路径替换、预载时机、联机双机一致性都必须进游戏验证。

工作量估计：生成器 + 配置 + 菜单约 1 天；出车替换 + 联机一致性约 2–3 天，外加双机实测。

## 验证（离线）

- `room_layout`（Python）：覆盖层元素都在 HUiMainFrame 两条带之间（从 Root.cpk 读回 `LYT_MAINFRAME` 算出）、没有入口行、宽版对话框只改了 6 个成员且类与树不变、写出器逐字节还原原版对话框、新文件名不遮蔽原版文件、插件打开的路径与安装器写的一致。
- `room_ui`：`PlanFrame` 关闭时不画；元素表不含入口。
- `support_loadout`：四种机体的重量上限、90 吨被拒并点名、38 枚 Mk 82 可、恰好等于上限可、坦克不称重、超重的变体解不出。
- `support_loadout_ini`（Python）：插件 / 编辑器 / 飞行模型的每发质量相同、上限由 `JET_MASSES` × 40% 推出、各机原版挂载都在上限内、Python 解析 / 变体 / 文件名解码同样拒绝超重。
- `room_menu`：数量 / 弹种循环只停在装得下的值、添加挂点在余量不足时拒绝、穷举往返中超重组合被解析器拒绝。
- `room_dialog`：重量行、数量页只列装得下的、换弹种自动降数量、余量 80 kg 时只剩 AGM-114 与 Hydra 70、两万步随机操作后没有任何载具超重。

## 未实机验证

1. 宽版对话框：新文件名的 UI 布局能被加载（OBJECT 已实证、UI 没有）；去掉水平居中后窗口确实以 x = 700 为中心、宽按钮与 26 号字的排版；14 个按钮的页面高度不出屏。
2. 原生按钮的说明框在房间与 HQ 都显示快捷键（10-19 已实现的部分，本次只是去掉了叠字的那一行）。
3. 旧安装（没有 `EDF6VC_LOADOUTBOX.SGO`）回到原版对话框。
