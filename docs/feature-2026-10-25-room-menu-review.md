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
- 游戏还有一个「列表 + 焦点项说明」的原生对话框 `HUiDescriptionDialog`（`LYT_HUIDESCDIALOGBOX.SGO`）：按钮列表加一块随焦点变化的说明文字，正是原版选择画面「左边列表、右边详情」的机制，原版 10 处代码经 `[0x1F77808]` 打开它（如房间筛选 0x8F6710、0x8BEE40 构造条目）。

### 说明传参（逆向，H；`tests/room_dialog_native_audit.py` 逐条核 EDF.dll）

| 项 | 事实 |
|---|---|
| 类 | `HUiDescriptionDialog`（RTTI `.?AVHUiDescriptionDialog@ui@@`，vtable 0x17F6F70），派生自 `HUiDialogBox`；OnUpdate（槽 1）0x8088C0 先跑焦点跟踪（+0x7C0，0x812170），再直接 `jmp 0x810D10`（基类 OnUpdate，不经基类虚表槽） |
| InitParam | `.?AUInitParam@HUiDescriptionDialog@ui@@`，vtable 0x1806FD8（槽 0 = 0x8C3620，释放 0x1C0 字节；析构体 0x8C33E0：先 +0x170 的函数，再条目 0xA9460、两段文字、基类 0x8E540） |
| 说明函数 | InitParam +0x170 是 `std::function<std::wstring(int)>`（存储 0x38 字节，impl 指针在 +0x1A8）；对话框构造时拷到 this+0x7E0（0x80AF6A → 0x80E190） |
| 调用 | 焦点移动时：`index = 焦点按钮 +0x110`，`[this+0x818]->_Do_call(out wstring, &index)`（vtable +0x10，0x80E700..0x80E72A），返回的文字原样 `SetText 0x863690` 到说明框（0x80E787..0x80E799）——是**字面文字**，不是文本键 |
| 原版用法 | 0x8BEE40：条目照常进 +0x150，另把每项说明收进 `vector<wstring>`，包成 lambda（vtable 0x1806B20，`_Do_call` 0x8BF830 = 拷 vector[index]）放进 +0x170，+0x1B4 = 220.0；之后 0x845250 → 0x119C600 打开（与插件同一入口） |

### 本次实现（分栏 + 尺寸，已实现，未实机）

- 每一页改为 `HUiDescriptionDialog`：InitParam 用 0x1806FD8 的 vtable，+0x170 放插件自己的函数实现（`{vtable, serial}` 16 字节就地存放，vtable 克隆自原版按钮 lambda 0x1805CF0，`_Do_call` 换成插件的：按 serial 找到这一页的说明表、记下焦点按钮下标、用游戏自己的 `0x3D450` 构造返回的 wstring），拆参数时调游戏的 0x8C33E0。说明表在打开前登记（对话框打开时会立刻问第一个焦点项）。焦点下标由游戏报告（`FocusedButton`）。
- 右侧详情（`room_dialog_logic.h Describe`，每个按钮一段）：单位页的单位 = 该单位全部队员（兵种、队长）或每个挂点及其重量、总重 / 上限、玩家自己召唤的载具注明套用条件；士兵行 = 兵种、主副色；挂点 / 弹种 / 数量 = 每发重量、这个挂点最多能装几发、这个挂点的重量、全机重量；保存 / 返回 / 翻页 / 恢复原版各有说明。
- 3D 预览跟随光标（`room_ui_logic.h FocusPreview`）：单位页焦点在哪名士兵就显示哪名；兵种 / 主色 / 副色 / 背包页显示该士兵换上焦点值后的样子（不改数据），与原版装备画面「模型跟着光标变」一致。
- 宽版布局 `Mods/UI/EDF6VC_LOADOUTBOX.SGO` 改为从 `LYT_HUIDESCDIALOGBOX.SGO` 派生（同一个类、同一棵树、`description` 键不变），只改 6 个成员：窗口最小 1100 × 560、按钮宽固定 600、按钮字 26 号、说明文字宽 1000、窗口中心 x = 700（去掉水平居中标志，`hcoord 1` = 元素中心在 pos），右侧露出 3D 预览。没装宽版布局时用原版 `LYT_HUIDESCDIALOGBOX`（同样是分栏，只是小一些）；说明对话框的签名或帧钩子不成立时退回原来的普通对话框。
- 关闭时机：说明对话框的 OnUpdate 槽（0x17F6F70 槽 1）单独链上（它不经基类槽），任务开始时照样由它自己的帧关闭。

### 宽版布局的加载路径（为什么可靠）

- 布局按名字经 `0x119C600` → 资源缓存 `0x84E80` 读取；游戏的文件读取都经 CRI 文件系统，`0x741C0` 是 `CriFsIoImplemnt`（RTTI `.?AVCriFsIoImplemnt@fs@sgs@@`，vtable 0x175F0F0 槽 1，全文件只有这一处引用）——EDFModLoader 挂的正是它（`docs/room-ui-re.md` §4），按「Mods 下有没有这个文件」重定向，不分目录、不看文件名是否新。
- 实证：`Mods/UI` 的同名覆盖（`LYT_HUIONLINEROOM` / `LYT_HUIHQMAIN` 的补丁布局）在实机上生效过（10-19 用户截图里的自绘配装页就是这份补丁布局画的）；新文件名在 `Mods/OBJECT` 实证过。两者走同一个重定向点。仍未在 `Mods/UI` 实测过**新文件名**，列为实机确认项。
- 插件只在文件确实在 `Mods\UI\` 时才用它（每次打开菜单检查），否则用原版说明对话框；日志 `ROOMUI menu pages in ...` 写明。

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
- **40% 是设计参数**（插件定的，不是逆向得到的原版规则：原版没有可配挂载的战机）：实机多用途战机作战构型的外挂约为净重的 0.4–0.6。用户 10-25 原话「合理的改，不是完全随心所欲」按「配置要受合理约束」执行；数值可调（`support_loadout.h kPayloadSharePercent` 与 `tools/support_loadout.py PAYLOAD_SHARE_PERCENT`，测试把两边与 `JET_MASSES` 钉在一起）。

### 联机

- 新版房主只会发出合法挂载；旧版客机照常接收。
- 旧版房主若在 ini 里写了超重挂载：新版客机按既有规则拒绝该计划 / 把预同步清单整份作废（与其它非法 token 同一策略），那名支援在该客机上不生成、预同步退回待生成清单。混版本房间本来就由 host settings 行提示版本不同。

## 4. 换玩家自己召唤的载具（已实现，未实机）

### 现状（已验证）

- 菜单能配置的载具原来只有支援目录里的 `SupportVehicle_<KEY>`（`TANK_CREWED`、`TANK_DELIVERY`、4 种战机各守点 / 跟随）。
- 玩家用载具请求武器召唤的载具（类别 308）由原版运输机的集装箱按武器里写死的 SGO 路径生成：运输机初始化 0x5E5070 在 **0x5E5687**（configure 0x5E8B40 唯一调用点）把请求者（rdx，shared_ptr）和路径（r8）交给集装箱（存到 +0xB70 / +0xB80）；集装箱落地后在 0x5E8FDC 用 +0xB80 的路径 `CreateObject`，并在会话中按它派生的网络 ID 注册——**房间里每台机器各自**跑这条链（`docs/airdrop-vehicle-re.md` §1）。

### 设计与实现

| 部分 | 实现 |
|---|---|
| 配置 | `SupportVehicle_OWN_PFIGHTER / OWN_PSTRIKE / OWN_FIGHTER / OWN_INTERCEPTOR / OWN_MULTIROLE`（`support_loadout.h kPlayerVehicles`：玩家战斗机 / 攻击机、自驾制空战斗机 / 截击机 / 多用途机），与支援载具同一语法、同一重量规则（各自机体净重的 40%）；`support_config.cpp` 读入 `SupportConfig::playerVehicle`；菜单「我召唤的·…」五个单位（`CatalogUnits` / `kDefaultUnits`），安装器菜单 8 也能编辑（`VEHICLE_KEYS`）。不是支援目录项，永远不进房主的支援清单 |
| 文件 | 新机体 `PFIGHTER / PSTRIKE / RFIGHTER / RINTERCEPTOR / RMULTIROLE`，底板就是请求带来的那架（`EDF6VC_PJET_*.SGO` / `EDF6VC_FLY_*.SGO`），生成器沿用 `jet_sgo`：`EDF6VC_LO_<BODY>_<variant>.SGO`（离线用本机 Root.cpk 与已装文件在内存里生成过 5 种并通过 `check_vehicle`） |
| 房间 | 每个成员发布自己的 `EDF6X_VCPVEH = "1;<token>..."`（`player_vehicle_logic.h OwnValue / ParseValue`，token 与预同步同形：'0'+机体 + 16 位十六进制，机体 6..10）；其他成员读到后逐个校验（只收玩家机体、每机体一条、过重量规则），缺的文件记进待生成清单并启动生成器（与房主清单同一上限 `kNotesPerRoom`），不影响「全员就绪」的计数；离开房间后保留给下一关预载（`room_sync_logic.h MemberPlayerVehicles`，`room_sync.h ForEachRoomPlayerVehicle`）。Coop 额度：清单 3800 + 就绪 40 + 背包标记 1 + 本键 128 ≤ 4096（static_assert） |
| 关卡开始 | `PreloadPlayerVehicles`（mission.cpp，紧跟支援变体）：自己的与房间里其他成员的、`Mods/OBJECT` 里有的文件预载；缺的记待生成清单 |
| 召唤时 | `player_vehicle.cpp` 重定向 0x5E5687：原版 configure 先跑，然后若路径是五架请求机之一：**请求者本机**按 `Engine::Decide` 决定——自己的文件已预载、且房间里每个对端都宣告了 `kExtPlayerVehicle` 并在 `have` 消息里报了有这个文件，才用挂载版，否则原版；把决定连同「这名玩家这种机体的第几次请求」（seq）经 PVEH 频道广播。**其他机器**只为这名请求者计数（每台机器按同一顺序看到同一玩家的请求）|
| 落地时 | `airdrop.cpp VehicleStep`（已有的 0x5E8FDC 重定向）在 `CreateObject` 前问 `PlayerVehicleStepPath`：按集装箱的路径指针找到记录，取（请求者、机体、seq）的决定；有挂载决定且本机预载过就换成 `app:/object/edf6vc_lo_….sgo`，否则原版 |
| 频道 PVEH | `have`（本机预载的玩家挂载版，确切名单，不用 Bloom），`decide`（请求者 native ID、机体、seq、variant），`sync`（新加入的机器收到每个本机玩家的下一个 seq）；决定 / sync 只收发送者自己的玩家（`SenderIsPlayer`）；旧版本没有这个频道 → 请求者听不到它的 have → 全员原版 |

### 一致性与残余风险（离线测试覆盖，`tests/player_vehicle_test.cpp`）

- 覆盖：全员都有 → 全员挂载版（决定先到 / 后到 configure 两种顺序）；任一台缺文件 / 旧版本 / have 还没到 / 请求者自己缺文件 → 全员原版；两名玩家、两种机体交错 300 次随机请求 → 每次全员一致且是请求者自己的配置；中途加入的机器经 sync 对齐计数；离线只看本机是否预载。
- 残余（测试里明确列出、日志会写）：决定在某台机器的集装箱落地之后才到（请求到落地通常十几秒，频道可靠有序），或某台机器在集装箱飞行途中加入且未收到 sync——那台机器不知道决定，造出原版，日志 `PLAYERVEH ... landed with no decision heard`。不是静默不一致。
- 依赖：集装箱落地钩子来自 `airdrop.cpp`（`AirdropWatching`）；它不成立时本机不发 `have`，所以别的玩家不会为本机选挂载版。

## 验证（离线）

- `room_layout`（Python）：覆盖层元素都在 HUiMainFrame 两条带之间（从 Root.cpk 读回 `LYT_MAINFRAME` 算出）、没有入口行；宽版布局是 `HUiDescriptionDialog`、只改 6 个成员、类 / 树 / `description` 不变，写出器逐字节还原原版，新文件名不遮蔽原版文件，插件打开的路径与安装器写的一致。
- `room_dialog_native`（Python，读 EDF.dll）：新增说明对话框的 RTTI、OnUpdate 直接跳基类、InitParam vtable / 0x1C0 字节 / 析构顺序、构造时拷 +0x170、焦点时 `_Do_call(out,&index)` 与 SetText、布局名指针、借用的 `_Delete_this` 就地不释放；签名表重新生成（40 段）。
- `room_dialog`：每页每个按钮都有说明且不越界（两万步随机）、单位说明列出队员 / 挂点重量、弹种说明给出每发重量与最多发数、「我召唤的」单位注明套用条件、重量行与可选值过滤。
- `room_ui`：`FocusPreview`（单位页跟随焦点士兵、兵种 / 颜色页显示焦点值且不改数据）、目录含玩家的 5 个单位。
- `support_loadout` / `support_loadout_ini`：重量上限（含 5 种玩家机体，按各自机型净重推出）、`kPlayerVehicles` 与 Python、`tools/calls.py` 的请求机一致、超重变体 / 文件名不解码。
- `room_sync`：玩家挂载值的发布 / 校验 / 坏值整条忽略、房主也为他人的玩家挂载记待生成并启动生成器、不影响就绪计数、离开房间后保留、撤回。
- `player_vehicle`（C++，1400+ 项）：值与报文编解码、拒绝项、多机模拟一致性（见 §4）。
- `player_vehicle_native`（Python）：接线源码守卫 + EDF.dll 上 0x5E5687 的调用形状与唯一性、configure 存 +0xB70 / +0xB80、落地从 +0xB80 `CreateObject`。

## 未实机验证

1. 说明对话框：插件的说明函数被调用、文字出现在说明框；InitParam 换成说明对话框的类型后打开 / 关闭 / 任务开始时关闭都正常；焦点下标与按钮顺序一致；宽版布局（`Mods/UI` 新文件名）被加载、窗口在左侧、14 个按钮不出屏、说明框位置。
2. 3D 预览跟随焦点：频繁换模型 / 换色的表现。
3. 原生按钮的说明框在房间与 HQ 都显示快捷键（10-19 已实现的部分，本次只是去掉了叠字的那一行）。
4. 玩家召唤：运输机初始化时请求者确实是召唤玩家的士兵对象（离线探针 2026-10-10 已抓到过，`docs/airdrop-vehicle-re.md` §2.1）；挂载版 SGO（以请求机为底）被集装箱生成后玩家能正常上机、挂载能用；联机双机：各端同一架飞机、`have` / `decide` 时序、加入中的机器。
5. 旧安装（没有 `EDF6VC_LOADOUTBOX.SGO`）回到原版说明对话框。
