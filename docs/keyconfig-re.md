# 原版按键设置（HUiHQKeyConfig）逆向

EDF.dll TimeDateStamp `0x678CCB46`，只读静态反汇编（地址均为 RVA；2026-10-25 起草 §1～§4，2026-10-26 补 §5～§7）。
关键事实由 `tests/keyconfig_native_audit.py`（ctest `keyconfig_native`，需 `EDF6_NATIVE_DLL`）钉住，DLL 变了就会红；
插件挂钩的每一处字节在 `src/keyconfig_signatures.inc`（`tools/keyconfig_sigs.py` 生成），插件加载时逐条核对，不符就写一行
`KEYCONFIG REFUSED` 日志、整个功能不挂。

问题：能不能把插件键位追加进游戏自己的「按键设置」菜单？

**结论**：能，但不是「只加数据」。菜单的分页、行数、行标签、行→动作都编译在代码里，要改 6 处页分派（§7）、页表的 6 个读取点、
模型的「四兵种同写」行范围，再改 3 个调用（恢复默认、对话框的键检查、顶层列表）。键值存储本身有空槽（`0x4F..0xFE`），
**且整块原样存进 COMMON.CFG**（§6），空槽里的插件键随原版存档持久化，不需要插件另存。实现见 `src/keyconfig.cpp`、
方案见 `plan-2026-10-25-keybinds.md` §4。

## 1. 菜单：`ui::HUiHQKeyConfig`

- 布局 `app:/ui/lyt_HUiHQKeyConfig.sgo`（`0x1802BD8`），对话框 `lyt_HUiHQKeyConfigDialogBox.sgo`（改键时的「按下新键」框，
  含 `CallUsedBaseConfigKey` 冲突检查、`OptionPlayer_KeyAlreadyChosen` / `OptionPlayer_KeyCantChange` 提示）。
- 构建 / 刷新函数 `0x896870`（`0x896870..0x8989B8`）：
  - 当前页 `this+0x84C`，键鼠页 0..10（`0x89736F cmp eax, 0xa`）。手柄模式（`this+0x849`，来自 `0x1183D70`）另走固定 11 / 10 行。
  - **行数是立即数**：页号经跳转表 `0x898894` 落到 `mov dword [rsi+0x844], imm`（`0x89738C..0x89740B`）。
  - **行标签是编译的 switch**：`0x897B9D` 按页分派，再按行号 `r9d` 分派到 `lea rdx, "OptionPlayer_Keyboard…"`，经文本表 `0x7A3500`
    （`[0x20B29A8]`）取显示文字；行号超出就是空。动作号 → 标签另有 `0x7CD0A0`，只认 `0x16..0x44`（`add eax,-0x16; cmp eax,0x2e`）。
  - 每行的控件是布局里按名字取的元素（`ItemName`、`ItemValue`、`Cusror01`、`CusroButon`），行数上限受布局限制；
    最长的「通用」页 12 行，说明同一布局至少有 12 行。
  - 编辑缓冲在菜单对象里：`this+0x7B0..0x804` 共 22 个 dword（`0x8969FB..0x896AD2` 初始化为 0..0x15），键鼠 / 手柄由 `this+0x808` 选。

| 页 | 标签前缀 | 行数 | 行（标签键 `OptionPlayer_Keyboard…`） |
|---|---|---|---|
| 0 通用 | `BaseXxx` | 12 | Up、Down、Left、Right、RideRescue（上车 / 救援）、Spot（标记）、Chat、Shotcut、WeaponShotcut1~3、VoiceChat |
| 1 游骑兵 | `Nomal/Normal…` | 8 | Attack1、Attack2、Jump、WeaponSelect、Reload、Dash、UseSub_Ranger、CallVehicle |
| 2 翼人 | 同上 | 7 | Attack1、Attack2、Jump、WeaponSelect、Reload、Boost、UseSub_WingDiver |
| 3 空降兵 | 同上 | 7 | Attack1、Attack2、Jump、WeaponSelect、Reload、UseSub_AirRaider、CallVehicle |
| 4 重装兵 | `Fencer…` | 8 | Attack1_1、Attack2_1、Attack1_2、Attack2_2、Jump、WeaponSelect、Reload、SpecialAction |
| 5 驾驶 | `Drive… / Tanks…` | 6 | DriveAcccel、DriveBrake、DriveHandBrake、TanksAttack1、DriveHorn、TanksAttack2 |
| 6 坦克 | `Tanks…` | 3 | TanksAttack1、TanksAttack2、TanksHorn |
| 7 直升机 | `Heli…` | 3 | HeliAttack1、HeliAttack2、HeliFlight |
| 8 机甲 | `Combat…` | 5 | Attack1_1、Attack2_1、Attack1_2、Attack2_2、Jump |
| 9 巴尔加 | `Bulga…` | 6 | Attack1_1、Attack2_1、Attack1_2、Attack2_2、SpecialPose、SpecialAttack |
| 10 深度履带 | `Depth…` | 5 | Attack1_1、Attack2_1、Gatling、Avoid、Jump |

1~3 页共用一个 switch（`0x897BC8`），Dash / Boost、UseSub、CallVehicle 按页号再分；2、3 页的行序是按这段 switch 推断的，未逐行核对。

原版没有「固定翼」「舰船」「炮手座」「地图」分页，也没有任何插件动作；分页名 `OptionPlayer_KeyConfig*`（Common / Vehicle / Drive /
Tanks / Combat / Bulga / Depth / Heli / 各兵种）同样编译在代码里。

## 2. 每页的动作清单：`0x17F26B0`

`0x7CD2EA imul rbx, rax, 0xd` + `lea rax, 0x17F26B0`：11 组 × 13 字节的动作号，`0x4E` 结束一组，其后补 0。
原版用到的最大动作号是 `0x4D`（驾驶组里）。最长一组（`0x16..0x21` 的 12 个动作 + 结束符）正好占满 13 字节；短组（如直升机组
`4A 4B 4C 4E` + 9 个 0）在**原地**还有空位。冲突检查（`0x7CD2D0`）就按这张表逐个比对同组已绑定的键。

表行不等于菜单页号（`0x896CF9` 跳转表 `0x89886C`）：行 0 / 1 是手柄页（游骑兵·翼人·空降兵共用 / 重装兵），行 2 通用（页 0），
行 3 步兵（页 1..3 共用，按套号区分兵种），行 4 重装兵（页 4），行 5 驾驶（页 5），行 6 坦克（页 6），行 10 直升机（页 7），
行 7 / 8 / 9 机甲 / 巴尔加 / 深度履带（页 8 / 9 / 10）。插件页 11..16 用行 11..16（§7）。

## 3. 键值存储：输入管理器

- `0x7CD2B0`：`rax = [0x20B2890]`，`rcx = rax + 0x318`，跳 `0xD4290`（取键名，`"OptionPlayer_Key%s"`，键码有效范围 `0x12..0x78`，
  是游戏自己的键码枚举，不是 Windows VK）。
- `0xD4260`（读一个绑定）：`dword [mgr + 0x28 + (player*0xA00 + class*0xFF + action)*4]`。即每个本地玩家 0xA00 个 int32，
  按兵种（`[item+0x14]`：0 游骑兵 / 1 翼人 / 2 空降兵 / 3 重装兵……）每兵种 0xFF 个动作槽，动作号是一个字节。
- `0xD41F0`：两个玩家各 0x2800 字节（`lea rcx,[rbx+rbx*4]; shl rcx,0xb`），`mgr+0x5488` 是已初始化标志。
- 所以**每个兵种 255 个槽里原版只用到 0x4D**，`0x4F..0xFE` 在内存里是空的。

§3 当时没追到的三件事（存档、键码→VK、默认键位）已在 §5、§6 补齐。

## 4. 追加条目需要改的地方（2026-10-25 的估计；实际做法见 §7）

1. 动作清单 `0x17F26B0`：短组原地写入新动作号（只读段，需 `VirtualProtect`）；或把 `0x7CD2EE` 的 `lea` 改指插件自己的表。
2. 行数立即数（`0x8973xx` 各页）。
3. 行标签：`0x896870` 内每页的 switch 与 `0x7CD0A0` 都只认原版行 / 动作，需挂钩返回插件的标签键。
4. 文本：标签键要在文本表里有字（挂 `0x7A3500` 或往文本资源里加条目）。
5. 布局行数：新增行不能超过 `lyt_HUiHQKeyConfig.sgo` 的行数（≥12，未读布局确认上限与是否可滚动）。
6. 编辑缓冲 22 个 dword 的索引含义未读清，可能限制每页行数。
7. 持久化：若原版存档不写新槽，插件要自己存（ini），并在菜单打开前写回内存表。

另外原版动作是按**兵种**存的：同一个插件动作在四个兵种页里各有一份，菜单里要放在「通用」类页里才不会让玩家设四遍。

## 5. 键码、默认键位、按兵种存键（2026-10-26）

### 5.1 游戏键码

槽里存的是游戏自己的键码（`int32`），不是 VK：

| 键码 | 含义 | 出处 |
|---|---|---|
| `0x00` | 未设定（`OptionPlayer_NoAssign`） | 名字表 `0x1F56050[0]` |
| `0x01..0x0B` | 手柄 A B X Y LT RT LB RB L3 R3 Back | 同上（另有 PS4 / Switch 两套名字表 `0x1F560E0` / `0x1F56170`） |
| `0x0C..0x11` | 鼠标 左 中 右 滚轮 侧键1 侧键2 | 同上 |
| `0x12..0x78` | 键盘（A..Z、0..9、Num0..9、F1..F12、JIS 的 `- ^ ¥ @ ; : [ ] , .`、左右 Shift/Ctrl/Alt/Win、方向、Enter…Kana） | 名字 `0x11856F0`，VK `0x11852C0` |

- `0x11852C0(i)`：键盘码 `0x12+i` → Windows VK（跳转表 `0x118554C`，每项 `mov eax, vk; ret`）；共 `0x67` 个，完整表在
  `common/edf/keybind.h kKeyVk`，审计测试逐项比对。轮询 `0x1188270` 用 `GetKeyboardState`（`0x1183880` 存到 `0x21360E0`）的
  `byte[vk] & 0x80` 喂每个键对象。
- 鼠标：`0x1188270` 直接读 VK 1（左）/ 4（中）/ 2（右）/ 5 / 6（侧键）；滚轮是一个码（`0x0F`），没有「按住」状态。
- `0xD4290`（取键名）对 `0x12..0x78` 以外的码**不检查范围**就去索引名字表（`0xD4360`），所以插件显示自己的行时先查码，
  非法值显示「未设定」（`keyconfig.cpp KcValue`）。

### 5.2 原版默认键位（键盘）

`0xD3F90` 构造存储时调 `0xD4530(0xFFF)`：先把 10 套全部清 0，再按标志写默认值。键盘部分：

| 页 | 动作 → 默认键 | 写入函数 |
|---|---|---|
| 通用（行 2） | `0x16` 上 W、`0x17` 下 S、`0x18` 左 A、`0x19` 右 D、`0x1A` 上车/救援 E、`0x1B` 标记 Q、`0x1C` Shotcut T、`0x1D` Chat 左 Ctrl、`0x1E..0x20` 武器快捷 1/2/3、`0x21` 语音 V | `0xD48B0`（四兵种） |
| 步兵（行 3，游骑兵 / 翼人 / 空降兵） | `0x22` 攻击1 鼠标左、`0x23` 攻击2 鼠标右、`0x24` 跳 Space、`0x25` 换武器 滚轮、`0x26` 装填 R、`0x27` 冲刺/推进 左 Shift、`0x28` 副装备 Z、`0x29` 呼叫载具 F | `0xD4530` 标志 0x10 |
| 重装兵（行 4） | `0x2A..0x31`：鼠标右、左、Space、左 Shift、鼠标中、滚轮、R、Z | 标志 0x20 |
| 驾驶（行 5） | `0x32` 油门 W、`0x33` 刹车 S、`0x34` 手刹 Space、`0x35` 主炮 鼠标左、`0x4D` 喇叭 鼠标右、`0x36` 副炮 鼠标中 | `0xD52F0` |
| 坦克（行 6） | `0x37..0x39`：鼠标左、右、中 | 标志 0x80 |
| 机甲 / 巴尔加 / 深度履带（行 7/8/9） | 鼠标右、左、Space、左 Shift、鼠标中（巴尔加多一个 F） | `0xD50D0` / `0xD4FD0` / `0xD51A0` |
| 直升机（行 10） | `0x4A` 攻击1 鼠标左、`0x4B` 攻击2 鼠标右、`0x4C` 飞行 Space | 标志 bit 11 |

通用页行标签的实际顺序（`0x899750`）：Up、Down、Left、Right、RideRescue、Spot、**Shotcut（T）、Chat（Ctrl）**、
WeaponShotcut1~3、VoiceChat（§1 表里 Chat / Shotcut 两行的顺序以此为准）。键盘页的行号 = 动作号 − 页首动作（`0x898FF7 add bl,0x16`）。

`0xD5560`（加载后 `0xD41F0` 调）只做一件事：旧存档没有 `0x4D`（喇叭）时，从候选键里挑一个没被占用的填进去。

### 5.3 按兵种存键

存储每个本地玩家 10 套 × 0xFF 槽（`set*0x3FC`）；原版只用 0..3 套（四兵种）。菜单模型 `0x7CDE10`：

- `[+0x14]` 套号：页 1..4（三个步兵 + 重装兵）是 页号−1，其余页 0；
- `[+0x10]` 「四兵种同写」：表行 2（通用）和 5..10（载具）为 1，写入走 `0xD5470`（同时写 0..3 套）；兵种页写单套（`0xD54B0`）。

所以**通用页和载具页本来就四兵种同步**。插件页（行 11..16）把 `0x7CDE40 cmp eax,5` 改成 `cmp eax,0xB`，也成为同写行：
插件只读第 0 套，四兵种不会不一致。

运行时读键：`0xD43C0(store, player, set, action)` 共 51 处调用，动作号都是立即数，最大 `0x4E`（`0x7A649`）；
`0xD4260` / `0xD4290` 只被菜单按页表调用。没有找到按计算出的动作号读槽的代码，`0x4F..0xFE` 原版不读（审计测试钉住调用数和最大值）。

## 6. 持久化：COMMON.CFG

- `0x7C0120`：`[0x20B2890]` 不存在就 `new 0x150A0` 并构造（`0xD62C0`，其中 `+0x318` 是键存储 `0xD3F90`），然后把
  `+0x318` 交给存档任务 `0x7BBC30`，文件名 `"COMMON.CFG"`（`0x17F1498`）。
- `0x7BBC30`（写）/ `0x7BC9E0`（读，`0x7C2AF0` 发起）都是 `r9d = 0x548C`：**整块 0x548C 字节原样**，即两个玩家各 10 套
  × 0xFF 槽 + 尾部设置 + `+0x5488` 已初始化标志。空槽和标准槽一样被保存、读回。
- 「全部恢复默认」`0xDB8C0`：用新构造的存储整块覆盖（插件槽和标记一起清 0）。
- 各页「恢复默认」`0x89A230` → `0xD4BB0(store, player, page)`：页 0..10 各写各的默认，页号 > 10 直接返回。

结论：插件键写进空槽后，跟着原版改键 / 退出设置时的 COMMON.CFG 保存一起持久化，**不需要插件自己存再写回**。
插件只需要一次性「播种」：每个插件在 `0xFE`（EDF6VehicleCrew）/ `0xFD`（EDF6AutoTurret）第 0 套放一个标记值，标记不在
就从 ini 把自己的键写进去（`common/edf/keybind.h Seed`）。旧 COMMON.CFG 读入、全部恢复默认后标记消失，会再播种一次。

## 7. 插件页的实现（`src/keyconfig.cpp`、`src/keyconfig_menu.h`）

### 7.1 进入：顶层列表

HQ → 设置 → 按键设置先弹一个选择列表（`HUiHQGameSetting::cbCallKeyConfigSelect` `0x892FF0`，请求类型 5，在 `rbp+0xA0`）：
键盘模式 6 项（通用、四兵种用 `lambda_3` `0x18030C0`，捕获 `(screen, 页号)`，选中时 `0x88DD00(screen, 页号)` 打开菜单；
载具一项用 `lambda_2` `0x1803088` 打开载具子列表，子列表 `lambda_1` 打开 `5+序号`）。手柄模式的列表是 `lambda_1` `0x1803050`。

插件把 `0x893337` 的 `call 0x119C600` 改到自己（`TopDialogHook`），在 6 项之后追加 6 项：复制第 0 项（与 `outfit41.cpp`
同法，`game_dialog.h PushCopyOf`），改文字，再把复制出的 `lambda_3` 的捕获页号（impl `+0x10`）改成 11..16。原版的
`_Do_call` 就会用 `0x88DD00` 打开第 11..16 页。手柄模式（项是 `lambda_1`）不加。

### 7.2 菜单里的六处页分派

| | 位置 | 原版 | 插件页（11..16） | 接回 |
|---|---|---|---|---|
| A | `0x896CFC` | 页 → 表行（`ja` 默认行 2） | 表行 = 页号 | `0x896D63` 存 `+0x850` |
| B | `0x89736F` | 行数立即数（`ja` 默认 0 行） | 行数取自桩里的小表 | `0x897288` 按 `+0x844` 克隆行控件 |
| C | `0x897B9D` | 每行的标签键（`ja` 不设控件 id） | `KcLabel(page,row)` 返回插件自己的文字 | `0x897E65`（非空才写 id） |
| D | `0x8980CC` | 每行的值 / 标签文字 | r12 = `KcValue`，rax = `KcLabel` | `0x897F25` |
| E | `0x898FBD` | 选中行 → 改键对话框（标签、当前键、动作号 bl） | `KcDialog` 填三项 | `0x8991AC` |
| F | `0x89AD47` | 每帧刷新值 | `KcValue` | `0x89AF3E` |

每处把 `cmp 页, 10; ja 默认`（5～10 字节）换成 `jmp` 到一个桩：页 0..10 回原版跳转表，11..16 走插件，其余回原版默认。
桩的指令只用 `keyconfig_menu.h` 里的一小组，`tests/keybind_test.cpp` 逐条解码，检查三条条件跳转与最终 `jmp` 的目标、
`call` 的目标、栈对齐。原版默认分支对 >10 的页本来就是「空页 / 寄存器未初始化」（D、E、F 的默认会用到未初始化的 r12/r14/r15/rbx），
所以不改分派就不能加页。

### 7.3 其余补丁

- 页表 `0x17F26B0`（11 行 × 13 字节）：6 个 `lea`（`0x7CCE8B`、`0x7CCF13`、`0x7CD2EE`、`0x7CDA96`、`0x7CDB34`、`0x7CDC51`）
  改指一份 17 行的副本（前 11 行原样，后 6 行是插件各页的动作 + `0x4E`），放在 EDF.dll 附近的只读页。`0x7CD9E0` 只取通用行，不动。
- `0x7CDE40`：同写行 5..10 → 5..16（§5.3）。
- `0x89A286` 恢复默认：插件页写回插件默认键（四套），原版页照旧调 `0xD4BB0`。
- `0x89D1F1` 对话框的键检查 `0x7CD940`：插件行只接受插件能「按住读」的键（键盘、鼠标左中右侧键；不收滚轮、手柄码）。

### 7.4 冲突检查

原版的改键（`0x7CDA40`）在**同一页内**查冲突：新键已被同页另一行占用就互换。插件页沿用这一套（每页一张表行，互换在页内）。
`0x7CD9B0`（兵种 / 载具页不许用通用页的键）只管表行 3..10，插件行不受它约束：插件的默认键本来就和通用页的 T（Shotcut）、
V（语音）、Ctrl（Chat）、E（上车/下车）有重叠（`tests/keybind_test.cpp` 的冲突矩阵逐条列出），把插件行也套上这条规则，
会在「恢复默认」时把这些默认键清成未设定。是否调整这些默认键是方案 §3.3 的事，等用户确认。

反方向：改通用页时原版会清掉行 3..10 里撞键的动作（行表 `0x17F35F0` 固定 8 项），插件页不在其中，所以通用页改成插件已用的键
不会清插件键，两边同键并存（日志与冲突矩阵可见）。

### 7.5 安装顺序与失败

全部签名先核对（54 条），桩、页表先在内存里建好；之后依次写：桩 → 6 个 lea → 同写行 → 6 处分派 → 3 个调用 → 最后顶层列表。
任何一步失败就停下、不再挂顶层列表：此前的补丁只影响页 11..16，而这些页在没有顶层列表项时打不开，原版菜单照常。
