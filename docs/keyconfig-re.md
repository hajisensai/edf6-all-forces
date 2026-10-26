# 原版按键设置（HUiHQKeyConfig）逆向

EDF.dll TimeDateStamp `0x678CCB46`，2026-10-25 只读静态反汇编（地址均为 RVA）。关键事实由 `tests/keyconfig_native_audit.py`
（ctest `keyconfig_native`，需 `EDF6_NATIVE_DLL`）钉住，DLL 变了就会红。

问题：能不能把插件键位追加进游戏自己的「按键设置」菜单？

**结论**：不能「只加数据」。菜单的分页数、每页行数、行标签都编译在代码里；要追加条目得改 6～7 处原版代码 / 只读数据并实机核对
存档与布局，属于大改，需用户确认后单独做（方案见 `plan-2026-10-25-keybinds.md` §4 阶段 3）。但**键值存储本身有空槽、且可读**：
插件读取玩家在原版里设置的键（只读，低风险）是可行的，这是方案的阶段 1。

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

## 3. 键值存储：输入管理器

- `0x7CD2B0`：`rax = [0x20B2890]`，`rcx = rax + 0x318`，跳 `0xD4290`（取键名，`"OptionPlayer_Key%s"`，键码有效范围 `0x12..0x78`，
  是游戏自己的键码枚举，不是 Windows VK）。
- `0xD4260`（读一个绑定）：`dword [mgr + 0x28 + (player*0xA00 + class*0xFF + action)*4]`。即每个本地玩家 0xA00 个 int32，
  按兵种（`[item+0x14]`：0 游骑兵 / 1 翼人 / 2 空降兵 / 3 重装兵……）每兵种 0xFF 个动作槽，动作号是一个字节。
- `0xD41F0`：两个玩家各 0x2800 字节（`lea rcx,[rbx+rbx*4]; shl rcx,0xb`），`mgr+0x5488` 是已初始化标志。
- 所以**每个兵种 255 个槽里原版只用到 0x4D**，`0x4F..0xFE` 在内存里是空的。

未追到（阶段 1 / 3 开工前要补）：

- 存档怎样序列化这张表（整块写还是只写已知动作）——决定追加的动作能否靠原版存档持久化。
- 游戏键码枚举 → Windows VK 的对照表（`0x11856F0` 给出键名，可由名字反推；或在 `0x1183880` 读 `GetKeyboardState` 后的翻译处找）。
- 原版默认键位表（「恢复默认」`OptionPlayer_ConfirmDefault` 的数据源）。`0xD5560` 是每个玩家的初始化，开头处理的是武器快捷键，未读完。

## 4. 追加条目需要改的地方（阶段 3 的工作量依据）

1. 动作清单 `0x17F26B0`：短组原地写入新动作号（只读段，需 `VirtualProtect`）；或把 `0x7CD2EE` 的 `lea` 改指插件自己的表。
2. 行数立即数（`0x8973xx` 各页）。
3. 行标签：`0x896870` 内每页的 switch 与 `0x7CD0A0` 都只认原版行 / 动作，需挂钩返回插件的标签键。
4. 文本：标签键要在文本表里有字（挂 `0x7A3500` 或往文本资源里加条目）。
5. 布局行数：新增行不能超过 `lyt_HUiHQKeyConfig.sgo` 的行数（≥12，未读布局确认上限与是否可滚动）。
6. 编辑缓冲 22 个 dword 的索引含义未读清，可能限制每页行数。
7. 持久化：若原版存档不写新槽，插件要自己存（ini），并在菜单打开前写回内存表。

另外原版动作是按**兵种**存的：同一个插件动作在四个兵种页里各有一份，菜单里要放在「通用」类页里才不会让玩家设四遍。
