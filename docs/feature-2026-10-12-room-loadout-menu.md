# 2026-10-12 新功能：房间 / HQ 内的支援战前配置菜单（游戏内编辑，带预览）

分支 `feat/room-loadout-menu`（all-forces）+ `feat/room-member-attributes`（edf-coop-stable）。

## 用户原话

- 「调整npc、载具挂载外观等要在游戏里面，不是在游戏外安装包里面调整。」
- 「不是房rts里面，直接放游戏房间内，没进地图的时候，那里面」
- 绘制选 A（游戏原生 UI + 布局补丁），「但是要求有类似预览的那种展示，比如展示这一队npc，然后旁边是色块和武器之类的」
- 「线上房间和线下都一样有对应的界面吧」 → 在线房间与离线 HQ 都做。
- Coop：改（房间阶段预同步）。
- 范围：「所有支援的吧」。
- 「模型加载链 做」「全部做完吧」 → 3D 士兵预览也做。

## 为什么以前只能在游戏外调

- ini（`SupportPreset_*` / `SupportVehicle_*`）插件已热重读，但颜色和挂点写在安装器从 Root.cpk 生成的变体 SGO
  （`EDF6VC_NPC_*.SGO` / `EDF6VC_LO_*.SGO`）里，只在关卡开始时预载（`mission.cpp` → `support_variants.cpp`）。
- 安装器在游戏运行时拒绝生成这些文件（`tools/installer.py` `manage_support`）。
- 插件从不回写 ini。

在房间 / HQ 里改：进地图前文件已经生成，关卡开始照常预载，不需要关卡中途预载。

## 静态逆向事实（EDF.dll TimeDateStamp 0x678CCB46，RVA）

| 项 | 事实 | 置信 |
|---|---|---|
| 在线房间 | `UI/LYT_HUIONLINEROOM.SGO`，`HUiRoom`，OnUpdate 0x8FDFE0，虚表槽 EDF+0x180AC48（Coop 已挂） | H |
| 离线对应画面 | `UI/LYT_HUIHQMAIN.SGO`，`HUiHQMain`，vtable 0x1804DD8，OnUpdate 槽 EDF+0x1804DE0 = 0x89F740 | H |
| 常驻框架 | `HUiMainFrame::OnUpdate` 0x8C12B0（Coop 已挂，改的是 `LYT_MAINFRAME.SGO`） | H |
| 布局元素类型 | `Layout` / `Button` / `TextField` / `TexButtonTextField`（工厂 0x841800） | H |
| 皮肤 | `Transparent` / `Solid` / `Texture` / `Model` / `Window` / `Button` | H |
| 按名取组件 | 0x839FD0(layout, out shared_ptr, const wstring*)；TextField 0x8859C0 + SetText 0x863690 | H |
| 贴图皮肤 | 0x953D80 取 `Skin_Texture`；0x7F63E0 换贴图；实例 +0xF0..+0xFC RGBA 染色 | H |
| 色块 | 0x824D40(skin, L"ColorChipBox", L"color", float4*)，游戏 HQ 调色板同款（0x880010）；资源在 HQ 与房间资源组都预载 | H/M |
| 显隐 | 组件 +0x1F8（`docs/hud-re.md` §9.1） | H |
| 武器图标 | `WEAPON/ICON/*.dds` 270 个，武器 SGO `WeaponIcon`；运行时武器 +0x1C0 路径 / +0x1B0 名字 | H |
| 3D 预览 | `HUiHQ3DModel` / `UiHQ_3DModelView`（vtable 0x180FA60），单例，只显示本地玩家 | M |

## 设计

### 界面

- 左栏：支援单位列表（小队、坦克、战机……ini 里有键的单位）。
- 中栏：小队逐人一行（兵种、队长标记、主色块、副色块、武器图标、武器名）；载具一行一个挂点（主炮 / 机炮 + 挂点武器图标 + 弹数）。
- 右侧：3D 预览（选中士兵按其颜色与兵种模板显示；见「3D 预览」）。
- 颜色从调色板选，色值取游戏 HQ 调色画面的颜色。
- 底部：生成状态（生成中 / 就绪 / 失败原因）、联机就绪（全员就绪 / 某某缺文件生成中），客机标「本局以房主配置为准」。
- 键盘 + 手柄；菜单打开时房间 / HQ 自己的输入被挡住。

### 不与 Coop 冲突

- Coop 改 `LYT_MAINFRAME`；本功能只补丁 `LYT_HUIONLINEROOM` / `LYT_HUIHQMAIN`，安装器从 Root.cpk 原版布局生成补丁版。
- OnUpdate 一律读当前槽值转调（不写死原函数地址），两个 mod 的加载顺序不影响对方。

### 范围：所有支援

小队、坦克 / 载具乘员、战机驾驶员、任务自带载具的支援乘员都吃颜色变体。不含调试召唤、自动上车（场上已有士兵）。

### 文件生成与 ini

- 安装器新增无交互子命令生成变体；插件保存后后台启动它（隐藏窗口），写入临时文件 + 原子改名。
  游戏运行中只放开这类内容寻址的新文件。
- 插件按行回写 `SupportPreset_*` / `SupportVehicle_*`（保留 BOM 与注释，原子写），写完立即重读。
- 安装器菜单 7 / 8 原样保留，同一份 ini。

### Coop 房间阶段预同步

- edf-coop-stable 导出发布 / 订阅成员属性的小接口。
- 房主在房间公布本局需要的变体文件名；客机缺的在房间里后台生成；进地图时全员都有。
- 旧版 Coop / 旧对端：沿用待生成清单（`EDF6VehicleCrew.variants_pending.txt`），行为不变。

## 实施阶段

1. 逆向补缺：房间 / HQ 输入阻断、3D 模型视图加载链、AI 武器图标、新增 SKIN SGO 的重定向、布局 flag、HQ 子画面隐藏判据。
2. 后台生成 + 插件回写 ini（离线可测）。
3. 菜单纯逻辑状态机（离线可测，照 `debug_spawn.h` 结构）。
4. 布局补丁生成 + 两个画面的 OnUpdate + 色块 / 图标 / 文字驱动。
5. 乘员 / 驾驶员颜色变体。
6. Coop 导出接口 + 房间阶段预同步。
7. 3D 预览。
8. 真机验证（只在游戏未运行时安装并核 sha，不关用户的游戏）。

## 状态

见文末各阶段记录（实现时逐段补充：做了什么、证据、未验证项）。

## 阶段 3：菜单纯逻辑状态机（离线，已完成）

- `src/room_menu.h`（header-only，只依赖 `support_loadout.h`，无游戏内存、无 Windows API），命名空间 `crew::roommenu`：
  - 模型：`Load(menu, specs, n, read, ctx)` 按单位表（`UnitSpec{key,label,seats}`；有座位 = 小队，`kVehicleKeys` 里的 = 载具）读 ini，
    用真解析器解析；被解析器拒绝的值显示为原版、标 `refused`，玩家不改就不回写。`kDefaultUnits` 与安装器菜单 8 的 18 个单位一致，
    阶段 4 改用运行时目录（`SupportCallKey` / `SupportCallSeats`）。
  - 序列化：`SerialisePreset` / `SerialiseVehicle` 输出与 `format_preset` / `format_vehicle` 相同的最短写法（相邻相同士兵合成 `*n`；
    原版 = 空值；只带机炮 = `GUNS`）。
  - 颜色：`Palette`（默认 16 色常量 `kDefaultPalette`，含 1E3A8A / FFFFFF），循环顺序「原色 → 调色板」；`Menu::palette` 可整体换成游戏
    HQ 调色板（0x880010）。ini 里手写的非调色板颜色原样保留，只有改它时才落到调色板上。
  - 状态机：`Presses`（边沿触发，同 `debug_spawn.h Press`）+ `Step`。三层：单位列表（末行「保存」）→ 单位的行（士兵；载具为主炮 / 机炮行
    + 挂点）→ 字段（兵种 / 主色 / 副色；挂载 / 弹数；坦克主炮 HE↔AP）。上下循环，左右改值，确认 / 右进入，取消 / 左退出；
    单位列表上取消或再按开关 = 放弃全部修改并关闭。Add 在光标后插入（士兵复制光标处那名；挂点追加：坦克 AP×20、战机 AAM_S×2），
    Remove 删光标处，Reset 恢复原版。限制：小队不超过该单位座位数且 ≤12，坦克 4 挂点只能 AP/HE/GLM，战机 5 挂点只能七种空挂。
  - 保存：在「保存」行确认返回 `Act::save`（没改动则直接关闭）；`CollectSave` 只给改过的键，每个值先用 `ParseSupportPreset` /
    `ParseVehicleLoadout` 读回并与编辑结果逐字段比对，任何一个不符就整批不写；调用方写完后 `MarkSaved`。
  - 视图：`Snapshot` 给出两列（单位 / 行）的窗口（`pageRows` ≤16，光标出窗才最少量滚动），每行带文字类型、标签、兵种与兵种大类、
    队长标记、主副色 RGBA（原色 0）、挂载码、弹数、光标 / 编辑中字段、已修改、解析被拒标记。
- 测试 `tests/room_menu_test.cpp`（ctest `room_menu`，159 项）：2026-10-09 文档全部写法示例往返（规范写法逐字相同）、
  全兵种 × 颜色 × 挂载 × 弹数往返、边沿触发、关闭时只认开关、载入（被拒值不回写）、导航与循环、座位 / 挂点上限、坦克与战机可挂种类、
  恢复原版写空值、取消与开关放弃修改、被拒值改后按合法值重写、2 万次随机按键后每次收集的值都能被真解析器读回、滚动窗口、快照字段。
- 未做：绘制、输入挡板、回写 ini 文件（阶段 2 / 4）；没有在游戏里运行。
## 阶段 2：后台生成 + 插件回写 ini（2026-10-12，分支 `feat/rl-generate`，只做了离线验证）

没有启动游戏，没有往游戏目录写任何文件；只读了 Root.cpk（`loadout_generate` 测试里用真实构建器在内存里生成一个彩色士兵）。

### 安装器：无交互子命令 `loadout-generate <游戏目录>`

- `tools/installer.py` `loadout_generate` / `generate_main`；`run()` 在一切横幅、提问、「按回车关闭」之前分派，冻结的 exe 也一样。
- 只生成 ini 与待生成清单点名、`Mods/OBJECT` 里还**没有**的文件（`support_loadout.missing_names` → `build_named`）。
  文件名即内容，已有的就是对的，不重建、不覆盖。
- 每个文件先写 `<名>.tmp-<pid>`，再 `os.rename` 到空闲的名字（Windows 上 rename 不覆盖；中途被别人抢先写了，留别人的，删临时文件）：
  `pylib/ledger.py` `Ledger.put_new`，记入台账（owner `loadout`），卸载照常删除。不释放、不删除任何文件。
- 游戏运行中也允许（只新增内容寻址的文件）；交互菜单 7 / 8、安装的「游戏运行时不生成」原样不变。
- 待生成清单不动：插件运行中会往里追加，改写会丢行；由菜单 7 / 8 / 安装清理（行为不变）。
- 状态：
  - 退出码 0 成功（含无事可做）/ 1 有文件没生成或出错 / 2 不是装了插件的游戏目录、用法错 / 3 另一个生成器在跑；
  - 最后一行 `EDF6VC_GENERATE state=ok|failed|busy made=N failed=M`；
  - `Mods/Plugins/EDF6VehicleCrew.generate_status.txt`：`state=running|ok|failed`、`made`、`failed`、`message`（UTF-8，CRLF，原子替换）。
- 同一时间一个：`EDF6VehicleCrew.generate.lock` 的字节锁（进程死了 Windows 自动释放，不会残留）。
- 安装（菜单 1）把安装器 exe 自身复制成 `Mods/Plugins/EDF6VehicleCrew.generator.exe`（`install_generator`，内容不变不重写；从源码运行时没有 exe，不复制，插件显示「找不到生成器」）。
  卸载时 `remove_generator` 删除它、状态文件和锁文件（生成器还在跑删不掉时说明并继续卸载）。

### 插件

- `src/loadout_ini.h`（纯逻辑）：`EditLoadoutIni` 先用插件自己的 `ParseSupportPreset` / `ParseVehicleLoadout` 校验（只收可见 ASCII，杜绝换行注入），
  再 `RewriteIniKey` 改 `[VehicleCrew]` 里的这一行：第一处原位替换、后面的重复行（读取器本来就读不到）删除、没有就加在本节最后一个非空行之后、
  没有节就追加；空值 = 删除键（恢复原版）。BOM、CRLF / LF、注释、其它键及顺序逐字节保留；UTF-16 的 ini 拒绝改写。
- `src/loadout_file.cpp`：`ReplaceFileAtomic`（临时文件 + `MoveFileExW(REPLACE_EXISTING|WRITE_THROUGH)`）、`ReadFileBytes`（共享读写删，
  不挡生成器替换状态文件）、`ParseGenerateStatus`、`VariantGenerator`（`CreateProcessW`，`CREATE_NO_WINDOW`、不继承句柄、低于正常优先级，
  同时只一个；退出后没写最终状态 = 失败并报退出码；没人在写的陈旧 `running` = 失败，不会永远「生成中」）。
- `src/loadout_store.cpp`：菜单调用的 `SaveSupportLoadout` / `StartVariantGeneration` / `VariantGenerationStatus`。
  保存后 `ReloadConfigNow()`（`plugin.cpp`，新增，`crew.h` 导出；不再等载具输入钩子里的 `ReloadConfigIfChanged` 一秒轮询）。

### 测试

| 测试 | 内容 |
|---|---|
| `loadout_writer`（C++，44 项） | 原位替换 / 新增 / 删除 / 无节追加 / LF 文件 / 大小写与空格匹配 / 重复键 / 末行无换行 / 空文件 / UTF-16 拒绝；9 种非法值不写；真实文件经原子写后由 `GetPrivateProfileStringW` + 插件解析器读回；不留临时文件；状态文件解析；`cmd.exe` 代替生成器验证隐藏启动、单实例、读状态、崩溃与陈旧 running 判失败、生成器缺失 |
| `loadout_generate`（Python，43 项） | 不提问（`input` 被替换成抛异常）；游戏「运行中」照常生成；只建缺的、已有文件字节不变；临时名 + rename；无残留；待生成清单不动；台账记 owner；状态文件与最后一行；部分失败点名原因、其它照常生成；异常写 failed；锁占用退出 3；非游戏目录退出 2 且不写；`run()` 冻结模式不等回车；生成器复制与卸载删除；插件侧文件名、子命令、状态字段与这里一致；有游戏时用真实构建器在内存生成一个彩色士兵 |

### 没在游戏里验证的

1. 菜单还没有（阶段 3、4），`SaveSupportLoadout` / `StartVariantGeneration` 目前没有调用者，只编译进了 DLL。
2. 冻结 exe 的 `loadout-generate` 没有实际打包运行过（`build_release.py` 未跑）；安装器 exe 约百 MB，复制一份进 `Mods/Plugins`。
3. 游戏运行中生成新文件、再进关卡预载它们，没有实机跑过（设计上关卡开始才扫描，与阶段 1 结论一致）。
4. 已知小缺口：生成器运行时若同时在游戏外用菜单 7 / 8 保存（只在游戏关闭时生成），两边都改台账 `.edf6vc_files.json`，后写的会盖掉先写的记录，
   被盖掉的文件卸载时不会删（不影响游戏）。

## 阶段 5：乘员 / 驾驶员颜色变体（2026-10-12，分支 `feat/rl-crew-colour`，只做了离线验证）

用户要求：NPC 颜色预设对**所有**支援单位生效，不只是呼叫的小队。没有启动游戏、没有写游戏目录；只读了 Root.cpk（`support_loadout_ini` 用真实模板在内存里生成乘员文件并逐字段核对）。

### 覆盖面（逐条核过生成士兵的全部调用点）

插件里创建支援士兵只有三处：`support_dispatch.cpp SpawnDeployment`（所有呼叫）、`mission_crew_support.cpp Apply`（任务自带载具的支援乘员）、`debug_spawn.cpp`（调试，不在范围）。
战机 / 直升机 / 运输机的乘员都在 `SpawnDeployment` 里经 `CreateSupportSoldierUnregistered` 生成（`jet_spawn.cpp PrepareSupportAircraft` 只造机体），所以只要计划里每个乘员座位带上变体，生成路径原样接住。

| 路径 | 乘员 | 兵种模板 | 现在 |
|---|---|---|---|
| 空中支援（战机、直升机、炮艇、医疗机、救援直升机） | 飞行员 + 门炮手（`AirCrew`） | `SupportAircraftCrewWeapon` 的队员模板 | 每人 `SupportCrewVariant` |
| 运输机 / 机降（`SQUAD_HELI` 等 4 项） | 飞行员 | 同上 | 飞行员带颜色；乘客仍是小队（`SupportPreset_` 的颜色，原逻辑） |
| 集装箱投送（`*_AIRDROP` 3 项） | 运输直升机飞行员 | 同上 | 带颜色 |
| 地面载具（坦克 / 装甲车 / 卡车，有人 / 空车交付） | 驾驶员 + 该单位自带的车组 | `SupportVehicleCrewWeapon` 的队员模板 | 每个车组座位带颜色；组合编组时只有驾驶员是乘员，乘客按预设 |
| 任务自带载具的支援乘员 | 座位 0 队长、其余队员 | 步枪 `N601_COMMON_RANGER_AF[_LEADER]` | `Build` 给每座位写变体，`Apply` 改走 `ApplySupportSoldierResource(..., look)` |

10-09 文档里「坦克计划的车体带挂点变体、机组不带」的结论随本阶段改为「机组带乘员颜色变体」（没配置乘员颜色时仍然不带，计划与以前逐字节相同）。

### ini

```ini
SupportCrewColour=1E3A8A                  ; 所有没单独设置的乘员的默认
SupportCrewColour_TANK_CREWED=X:FFFFFF    ; 该单位的乘员：主色 X（原色）、副色白
SupportCrewColour_HELI=X                  ; 只写 X：原色，不用默认
SupportCrewColour_MISSION=FFFFFF          ; 任务自带载具的支援乘员
```

- 写法 `RRGGBB[:RRGGBB]`，`X` = 原色；空或不写 = 用默认（默认本身空 = 原色）。
- `support_loadout.h`：`CrewColour`、`ParseCrewColour`、`CrewLookOf`、`HasCrew`（`SQUAD` / `PLATOON` 没有乘员）、`kCrewMissionKey`。
- `support_config.cpp`：读默认、`_MISSION` 和每个目录键；写错或写在没有乘员的单位上整条作废，HUD / 日志点名（`CONFIG support ... INVALID`），按默认出动。日志多一项 `crew colours=N`。
- 发布的 `EDF6VehicleCrew.ini` 注释里加了写法说明。

### 联机线格式（选择与理由）

- **每个乘员座位用自己的 `Unit::variant`**，编码就是小队士兵的颜色变体（`LookVariant`），不另占位。载具单位自己的 64 位留给挂点（`kVariantVehicle`），本来就装不下第二份颜色；乘员是独立的计划单位，每人都有一个空着的 64 位。
- 房主：`SupportCrewVariant` → 生成文件名 → 与小队同一个 `DecideVariant`（本机有文件、全员宣告 `kExtVariants`、每个对端的 Bloom 过滤器都有，才置 `kVariantApplied`；否则原版 + 待生成清单 + 状态提示）。
- 对端：`Validate` 早就把士兵身上的颜色变体当「可缺省」——缺文件只把那名乘员按原色生成，**从不因乘员颜色拒绝计划**；解不开的变体、乘员身上的挂点变体照旧拒绝。任务乘员计划（`ValidateMissionCrewPlan`）同样只拒绝解不开的。
- 旧对端：
  - 没有 `kExtVariants` 的：房主不置 applied，对端读不到 `challenge`，行为与现在一致；
  - 已有变体支持的（10-09 版）：它的 `VariantFile` / 生成路径对任何士兵资源都解颜色，所以会显示同样的颜色，缺文件进待生成清单；
  - 任务乘员计划：旧版 `Apply` 不看 `variant`，按原版生成。
- 没配置乘员颜色：所有变体为 0，计划与以前完全相同（`support_dispatch_test` 钉住）。

### 菜单与编辑器（保持两边一致）

- `src/room_menu.h`：`UnitSpec` 加 `crew`；载具单位末尾一行「乘员颜色」（主色：默认 → X → 调色板；副色：原色 → 调色板），没有载具的乘员（运输类、默认、任务）各成一个乘员单位（`UnitKind::crew`）。一次保存可写两行（挂点 + 乘员），`SaveSet` 扩到 `2*kMostUnits`；解析器拒绝的乘员值单独标 `crewRefused`，不改不回写。快照加 `RowText::crewUnit / crew` 和 `inherit`。
- `src/loadout_ini.h` / `loadout_store.cpp`：`LoadoutField::crew`，空单位名写 `SupportCrewColour`，`MISSION` 允许；写之前用 `ParseCrewColour` + `HasCrew` 校验。
- `tools/support_loadout.py`：`parse_crew` / `format_crew` / `crews` / `crew_files`（按单位分 `SupportVehicleCrewWeapon` / `SupportAircraftCrewWeapon` 的兵种，坏武器名回落步枪，与插件同）；`wanted_files` 加入乘员文件，所以安装、菜单 7 / 8 保存、无交互 `loadout-generate`（`missing_names`）都会生成；菜单 8 新增 `c1`…`c18`，顺序与游戏内菜单的 `kDefaultUnits` 相同（测试按源码比对）。

### 测试（`cmake --build build` + `offline_checks` 0 警告；`ctest -j 3`：228 项 100% 通过，其中 36 项需要 EDF.dll / 游戏原生文件，按设计 SKIP）

| 测试 | 项数 | 新增内容 |
|---|---|---|
| `support_loadout` | 97（原 78） | 乘员颜色语法、默认与 X 的优先级、`HasCrew`、乘员文件名、座位变体往返 |
| `support_dispatch` | 249（原 222） | 读 ini（默认 / 单位 / 任务 / X / 写错与写在小队上被点名）；战机飞行员、直升机 X、机降飞行员（乘客不带）、投送直升机飞行员都从文件生成；坦克每个车组座位带色；装甲车组合编组：驾驶员乘员色、乘客预设色；缺文件原版 + 清单 + 状态；旧对端 / 对端缺文件不置 applied；对端缺乘员文件照收计划、那名乘员原色并记清单；乘员身上的挂点被拒；任务乘员队长用 `_L` 文件；没配置时计划无任何变体 |
| `mission_crew_support` | 27（原 18） | 每座位带变体；离线队长 / 队员从文件生成；缺文件原色不拒绝；房主未 applied 时原色；旧房主计划无变体；挂点变体被拒 |
| `room_menu` | 205（原 159） | 乘员行 / 乘员单位、字段循环、两行保存、拒绝值保留、重置与取消；原有用例按新单位数（26）更新 |
| `loadout_writer` | 50（原 44） | 乘员键写入、默认键无后缀、`MISSION`、小队拒绝、坏值拒绝 |
| `support_loadout_ini`（Python） | 101（原 72） | 乘员键表与 `room_menu.h` / 插件比对、同值同结论、乘员文件推导、`missing_names`、菜单 8 脚本化编辑；有游戏时用真实模板生成乘员队员与任务队长，只有 `soldier_color` 不同 |
| `loadout_generate` | 43 | 未改，照常通过 |

**变异测试**（先提交 85feffa，再改、编译通过、跑、还原）：

| 变异 | 结果 |
|---|---|
| 空中支援的乘员不写 `SupportCrewVariant` | `support_dispatch` 变红（「an aircraft's pilot …」） |
| 任务乘员 `Apply` 不传颜色路径 | `mission_crew_support` 变红（「offline: the leader and the member …」） |
| 地面载具车组不写乘员变体 | `support_dispatch` 变红（「the tank's crew …」） |

### 没在游戏里验证的

1. 乘员穿彩色模板在载具 / 飞机座位里是否正常显示（与小队彩色模板同一套文件，只是坐进载具）。
2. 任务自带载具的支援乘员改走 `ApplySupportSoldierResource` 后的实机生成与上车（逻辑上与原 `ApplySupportSoldierSpawn` 相同：同资源、`local=false`、同 ID）。
3. 联机双机：乘员变体只在协议替身和计划校验上验证过。
4. 游戏内菜单的绘制（阶段 4）还没有接乘员行；`room_menu.h` 的快照已经给出 `RowText::crew` / `crewUnit`。
## 阶段 7：3D 士兵预览（2026-10-12，分支 `feat/rl-preview`，只做了离线验证）

没有启动 / 关闭游戏，没有往游戏目录写任何文件；只读了 EDF.dll 与 Root.cpk。方案 A（`docs/room-3d-preview-re.md` §4），实现时按反汇编修正了三处（见「与方案 A 的差异」）。

### 菜单模型（`tools/menu_preview.py`）

- 读 Root.cpk 核实：10 个游骑兵模板（含 `_LEADER`）都是 `n601_common_ranger.mrab/.mdb`；翼人 10 个都是 p606、重装 8 个都是 p607，与原版菜单 SGO 相同。
- 生成 `Mods/MENUOBJECT/EDF6VC_MENU_RANGER.SGO`（P505 菜单 SGO，只换 `animation_model[0]` 为 n601）与 `EDF6VC_MENU_RANGER_L.SGO`
  （再把 `change_color` 的选择器写成网格名 `p609_proto_ranger_common_head`）。n601 只有 body / head 两个可上色材质：
  火焰 / 火箭 / 霰弹 / 狙击队长只染头 → `_L`；步枪队长头身都染 = 全部材质 → 普通文件。翼人 / 重装用原版 `p606_WingDiver.SGO` / `p607_fencer.SGO`。
- 渲染器 0x93C370 对 `change_color[i]` 的第一项：字符串 → 按材质名 0x110D570 查找只染该材质；其它 → 全部材质（静态读码）。
- 安装（菜单 1）`write_menu_preview`：内容不变不重写，失败只提示、不中断安装；`loadout-generate` 缺文件时以新文件名 `put_new` 生成，失败写入状态；卸载删除（台账 owner `menu_preview`）。

### 插件（`src/room_preview.cpp` + 纯逻辑 `src/room_preview_logic.h`）

- 安装：先核 25 段代码签名（`src/room_preview_signatures.inc`，`tools/room_preview_sigs.py` 从 EDF.dll 生成），不符则 `ROOM preview REFUSED` 且不打补丁；
  再链接 5 个虚表槽：两个 `std::function` 作业（msg 3 的 lambda_1 0x180FDB0、msg 6 的 lambda_3 0x180FE20，槽 2 `_Do_call` = 0x9424A0 / 0x942590，
  它们调 `0x93F910(Async+0x28, this+8)` 并置 `Async+0x9C=1`），视图虚表 0x180FA60 的槽 5（析构）与槽 1（每帧更新）。
- 工作线程：作业的 Async == 已采纳视图的 `view+0xD0`（构造函数建一次，终生不变）且有 override 时，改写 Desc 拷贝：+0x20 / +0x4C 兵种号（游骑兵 0、翼人 1、重装 3），
  +0x24 / +0x34 两色，+0x44 哨兵 `0x56430000`，+0x48 武器数，+0x50 = 0，+0x54 武器行号；路径用游戏自己的 `wstring::assign` 0x3D450。override 与计数在 SRWLOCK 里。
- 武器行号在 UI 线程用 0xE1BA0 按名字查（查不到就不挂该武器）：游骑兵 / 翼人一把；重装 `[hShield01, 主武器]`（CONFIG `Weapon_Slot1L` 在前 = 左手盾）。
- 颜色：`@RRGGBB` → 各字节 /255、alpha 1（与生成器相同）；原色 = 模板 `soldier_color`（游骑兵队长用头色，步枪队长用身体色）。
- 每帧（视图自己的更新里，视图必然活着）按「想要 / 正在显示」规划（`Step`）：打开时发 msg 8 锁住房间的 msg 6、保存 +0x3EC 并置 0（显示）；
  换模型发一次 msg 3；同模型只改色且已显示（+0x180 == 兵种、+0x178 == 哨兵）时只对变了的通道发 msg 5；关闭时若还有士兵作业没被工作线程取走就等（最多 180 帧），
  然后清 override、发 msg 3 重建玩家本人（`GS+0x6E90+player*0x3E60`、`0xD8700(GS, player)`），最后 msg 9 解锁、还原 +0x3EC。
- 菜单打开时：镜头四个点（+0x2C0 / +0x2D0 / +0x2E0 / +0x2F0）的 x 加 `kCameraShiftX = -0.8`，游戏改写过的点以新值为基准重加，关闭时只还原仍是我们写的值的点；
  视图自己从输入得到的转向 / 缩放（+0x2A4 / +0x308）每帧放回（菜单占用按键）。
- 拆除：视图析构钩子清掉一切（不再碰该视图；之后才完成的作业不匹配任何 Async，原样构建）；`TickPreview` 连续 30 帧没来（画面没关菜单就走了）视为关闭并自行放回玩家。

### 与方案 A 的差异

1. 不调 0x8BDE80、不用 `0x119D1B0` 广播：0x8BDE80 就是把 msg 3 广播给所有 UI 对象；改为直接调 index 0 视图自己的 OnMessage（当前虚表槽 3），
   msg 8 / 9 也只给这个视图（广播的 8 / 9 不判玩家号，会连带分屏的第二个视图）。payload 与 0x8BDE80 相同（outfit ≥ 0 时 0xDE470 原样返回）。
2. 不挂 OnMessage：构造函数直接 `call 0x93B320` 发第一条 msg 3，看不到；改由每帧更新（槽 1）采纳 `+0x2AC == 0` 的视图。
3. 不在 0x93F910 打内联补丁：它的调用点是两个 `_Do_call` 与无人引用（无 call / jmp / 指针）的 0x93FFF0，挂这两个虚表槽等价且不改代码字节。
4. 「等 Async+0x9C」改为不阻塞的计数：发出的士兵作业数 vs 工作线程应用 override 的次数；不在 UI 线程等待。

### 测试

| 测试 | 结果 |
|---|---|
| `room_preview`（C++，`tests/room_preview_test.cpp`） | 137 项：14 兵种 × 队长的菜单路径 / 兵种号 / 武器、颜色换算与原色、Desc 逐字节（路径与计数之后的字节不动、计数封顶 6）、镜头偏移（不重复加、被改写重算、精确还原）、规划器（锁一次、每模型建一次、只重发变了的通道、等待 / 超时、放回玩家后解锁） |
| `room_preview_native`（Python） | 50 项：签名 = EDF.dll 现有字节、5 个虚表槽 = 文档里的函数、OnMessage 跳转表 3/5/6/8/9、两个作业虚表的引用点、GS 全局；表里 14 个武器名都在 WEAPONTABLE.SGO、原版菜单路径与 CONFIG.SGO 拼写一致、生成文件名与 `menu_preview.py` 一致、14 兵种原色与队长头 / 身体色 = 模板 `soldier_color` |
| `menu_preview`（Python） | 36 项：真 Root.cpk 生成、解析回读、`animation_model[0]` 为 n601、其余键与 P505 相同、队长选择器、二次安装不写、卸载删除、安装器接线 |

### 需实机验证（全部未在游戏里跑过）

1. 本分支没有调用者：`InstallPreviewHooks` / `ShowPreview` / `HidePreview` / `TickPreview` 由阶段 4 的菜单画面调用；没接上之前钩子不装。
2. `UiHQ_3DModelView` 的槽 1 是否每帧被调（房间与 HQ 都要）；它的 ctx+8 在房间里是否为空（为空时游戏不重算 +0x2B0，镜头偏移不生效）。
3. 在 UI 帧内（视图更新前）直接调 OnMessage 3 / 5 / 8 / 9 是否安全；不广播 msg 3 是否漏掉了别的对象对它的依赖（如加载中提示）。
4. EDFModLoader 是否从 `Mods/MENUOBJECT/` 加载新文件名；n601 配菜单 CAS `app:/MenuObject/EDF6ArmySoldier.cas` 的动画是否正常；
   `_L` 的材质名选择器是否只染头。
5. 哨兵 outfit 对 msg 5 / msg 6 以外的影响（静态只找到 0x93C21A 一处比较）。
6. 重装 weapons[0] = 左手盾、选中槽 0 时两手的实际武器；翼人 / 游骑兵只给一把武器（count 1）时的显示。
7. 镜头偏移方向与幅度（`kCameraShiftX = -0.8` 是猜的）；+0x3EC 置 0 是否让房间里原本隐藏的模型显示出来、是否需要。
8. 转向 / 缩放放回造成的单帧抖动；视图在房间里是否收到输入。
9. 画面关闭时工作线程与析构的竞争（析构后才完成的作业：不匹配 override，按原样构建进已无人显示的 Async）。
10. 离线双人分屏：只采纳 index 0；第二个视图不受影响。
11. 游骑兵生成文件缺失时回退到玩家 P505 模型（Desc 仍是士兵的颜色和武器）。
12. 翼人队长的红色脸部染色在预览里没有（菜单只有两个颜色通道，原版 p606 菜单 SGO 染全部材质）。
## 阶段 6：Coop 房间阶段预同步 + 精简生成器（2026-10-12，分支 `feat/rl-sync`，只做了离线验证）

没有启动 / 关闭游戏，没有往游戏目录写任何文件（只读了 Root.cpk 与 `Mods/OBJECT` 里已有的机体 SGO，在内存里生成）。
Coop 侧接口是 edf-coop-stable `feat/room-member-attributes`（`EDF6CoopGetMemberAttributeApi(1)`），头文件原样拷成
`src/coop_member_attribute_api.h`。

### 插件：`src/room_sync.cpp` + `src/room_sync_logic.h`

- `room_sync.cpp` 只做接线：从已加载的 `EDF6Coop.dll` 用 `GetModuleHandleW` / `GetProcAddress` 取 `EDF6CoopGetMemberAttributeApi`，
  要 v1；没有 Coop、旧 Coop 没有这个导出、或返回的表比我们的小 → `SyncState::unavailable`，界面一行「Coop 版本过旧，预同步关闭」，
  其它行为不变（进关时缺文件照旧走待生成清单）。`room_sync.h` 原有声明一个没改。
- 全部逻辑在 `room_sync_logic.h`（无 Windows、无游戏内存）的 `Engine`，UI 线程每帧调 `TickRoomSync`，内部节流到 1 Hz。
- **房主**：`ForEachConfiguredVariant`（新增在 `support_variants.cpp`，命名仍只走 `support_loadout.h` 的 `SupportLookFile` /
  `VehicleVariantFile`）列出当前配置可能用到的全部变体文件：每个目录项 `SupportPreset_` 里每名有颜色的士兵（每 4 人第一位是队长模板，
  同 `SupportLoadoutResource`）、`SupportVehicle_` 的挂载（机体与键名一致，同 `HullVariant`）。挂点武器 `EDF6VC_<STORE>_<n>.SGO`
  不单列：生成器生成载具时一并生成。乘员颜色（阶段 5）加进同一个函数即可。每秒重算，值变了才重新发布（配置热重读、生成完成都会反映）。
- **线上格式**（值只用可见 ASCII，单值 ≤ 999 字节；本插件共用 5 个键，Coop 每进程上限 16）：
  - `EDF6X_VCVARIANTS` = `1;<hash>;<条数>;<分段数>;<token,...>`，放不下时续在 `EDF6X_VCVAR1..3` = `<hash>;<token,...>`；
  - token 是短形式，不是文件名：士兵 = 兵种字母（`a`=rifle … `n`=fshotgun，大写 = 队长模板）+ 主色 + 副色（各 6 位大写十六进制或 `X`），
    例 `a1E3A8AX` = `EDF6VC_NPC_RIFLE_1E3A8A_X.SGO`；载具 = 机体数字（1 坦克 … 5 多用途）+ 64 位变体 16 位十六进制，
    例 `14000000000000C81` = `EDF6VC_LO_TANK_4000000000000C81.SGO`；
  - `<hash>` = 所列 token 以 `,` 连接的 FNV-1a 64，分段之间、成员就绪值与清单之间都靠它对上；
  - 约 270 个 token 封顶（4 段）；更多的列不下，房主界面显示「N 个文件没能公布」，这些照旧走待生成清单。
- **校验（值不可信）**：客机只读 `isHost` 成员的值；每个 token 解析成结构 → 用 `support_loadout.h` 的命名函数生成文件名 →
  文件名再按严格语法解析回来必须一致、token 重新编码必须逐字相同；载具挂载按 `LoadoutOfVariant` 逐字段检查；任何一个不合法、重复、
  条数或 hash 对不上 → 整份清单作废（「房主的清单格式不对：已忽略」），不记任何文件、不启动生成器。文件名从不当路径用，只交给
  `NoteMissingVariant`（照旧的待生成清单），生成器再按名字的语法重新解码（`support_loadout.decode_name`）。
- **每个成员（含房主）**发布 `EDF6X_VCHAVE` = `1;<hash>;<缺几个>;<状态>`（`r` 就绪 / `g` 生成中 / `f` 生成失败 / `n` 没有生成器 /
  `w` 还没有清单）。缺的 = 清单里 `Mods/OBJECT/<名>` 不存在的。客机把缺的名字记进待生成清单（每房间每个名字一次），房主不记
  （自己的 ini 已经点名）。然后 `StartVariantGeneration`：同时只一个、两次启动至少隔 5 秒、同一组缺失最多两次（生成器报 ok 但仍缺
  时再试一次；报 failed 不重试，换了一组缺失才重来）。
- **界面行**（`RoomSyncLines`，hudtext 四语）：Coop 过旧 / 房主没有预同步 / 清单格式不对 / 等待房主清单 / N 个没能公布 /
  本机缺 N 个生成中 / 本机生成失败：原因 / 本机找不到生成器 / 成员 X 没有预同步（旧版）/ 成员 X 正在核对清单 / 成员 X 缺 N 个生成中 /
  成员 X 缺 N 个生成失败 / 全员已就绪（N 个文件）。成员编号 = Coop 的 ordinal + 1（Coop 只给 EOS ProductUserId，没有显示名）。
- `LeaveRoomSync` 撤回本机发布过的所有键；房主换人时旧房主自动撤回清单、改读新房主的。

### 精简生成器

- `tools/loadout_generator.py`：`loadout-generate` 的全部代码从 `installer.py` 搬来（installer 原样 re-export，行为与测试不变），
  只依赖 `gamedir` / `modfiles` / `support_loadout`。实测真实构建器（士兵、坦克、四种战机、全部 10 种挂点武器 × 3 种弹数）只导入
  `cpk crilayla dsgo gamedir graft_pure ledger mdb mdb_jet modfiles rootcpk sgo support_loadout vcobjects`，不碰 numpy / Pillow。
- `tools/build_release.py`：先 `build_generator()`（PyInstaller 单独冻结 `loadout_generator.py`，排除 numpy、PIL、tkinter、installer、
  testhub、procmesh、jet_models 等），再把它作为 `plugin/EDF6VehicleCrew.generator.exe` 打进安装器。CI（`.github/workflows/build.yml`）
  调用方式不变。
- 实测（本机 PyInstaller 6.22.3 / Python 3.13）：精简生成器 8,921,987 字节（约 8.5 MiB，打包后只含上面 13 个本仓模块，无 numpy / PIL），
  安装器 152,288,614 字节（发布 zip 168,034,069 字节），安装器内确有 `plugin\EDF6VehicleCrew.generator.exe`。
  在假游戏目录里 `loadout-generate`：精简版启动到结束约 2.2 秒，安装器约 6.2 秒。以前复制进 `Mods/Plugins` 的是整个安装器（约 150 MB）。
- 安装（`install_generator(game, generator_source())`）：安装器里有精简版就复制它；没有（开发时手工冻结的旧安装器）就复制安装器自身；
  从源码运行且 `build/Mods/Plugins` 里没有精简版 → 不复制（菜单显示「找不到生成器」），与阶段 2 相同。

### 测试

| 测试 | 内容 |
|---|---|
| `room_sync`（C++，103 项） | 14 兵种 × 队长 × 24 种颜色写法、7 种载具挂载的名字 / token 往返；文档例子逐字；32 个非法名字（路径、小写、全原色、applied 位、坦克挂空空弹、位数不对、多余后缀等）与 21 个非法 token 全拒；清单编码逐字符合文档、长清单分段且每段可见 ASCII ≤ 999、只列得下的前缀、缺段 / 他清单的段 = 等待、篡改 / 重复 / 空 token / 路径 / 条数或 hash 不符 = 整份作废；就绪值往返与矛盾拒绝；生成器启动策略；假 Coop 表上的整房间：房主发布 3 个（重复去掉、非法名丢弃）、客机只记缺的 2 个并只启动一次、1 秒内不重复干活、房主看到「成员 2 缺 2 个生成中」/「成员 3 没有预同步」（中、日文也核对）、生成完全员就绪、房主改配置后成员「正在核对」→ 客机再生成 → 失败不重试并显示原因、房主看到失败、离开撤回全部键、最多 5 个键、Coop 从未拒绝值；202 个文件分段并撤回多余段；恶意清单 / 非房主的清单不记不生成；房主迁移；无 Coop 表、不在房间、独自在房间 |
| `slim_generator`（Python，49 项） | 生成器导入白名单（含函数内导入）；installer 的 loadout-generate 就是它的；入口只认 `loadout-generate`、不等输入；`generator_source` 三种情况；安装复制精简版；`build_release` 命令单独冻结、排除重模块、先建生成器、打包到 `plugin/` |
| `loadout_writer` 44 / `loadout_generate` 43 / `support_loadout` 78 / `support_loadout_ini` / `room_menu` 159 / `hudtext_check` / `tools/selftest.py` 172 | 原有，全部通过 |

### 没在游戏里验证的

1. 没有真实 EOS 房间、没有两台机器：成员属性的发布 / 读取时序只在假 Coop 表上跑过；Coop 侧值到达有 1–2 拍延迟，逻辑按「等待」处理。
2. `TickRoomSync` / `RoomSyncLines` 目前没有调用者（阶段 4 的房间界面接）。
3. 冻结的精简生成器只在假游戏目录里跑过（无事可做 → ok、缺文件 + 假 Root.cpk → failed 并写原因、用法错 / 非游戏目录 → 退出码 2）；
   用真实 Root.cpk 生成只在 Python 进程内（不写盘）验证过。
4. 成员显示为「成员 N」（Coop 只有 ProductUserId）；要显示名字需要 Coop 再导出昵称。
