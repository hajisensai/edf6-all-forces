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
## 阶段 4：画面布局补丁 + 两个画面的 OnUpdate + 色块 / 图标 / 文字驱动（2026-10-12，分支 `feat/rl-screen`，只做了离线验证）

没有启动 / 关闭游戏，没有往游戏目录写任何文件；只读了 Root.cpk、EDF.dll 与游戏目录里的日志。补的逆向事实在
`docs/room-ui-re.md` §7。

### 布局补丁（`tools/room_layout.py`）

- 读 Root.cpk 的 `UI/LYT_HUIONLINEROOM.SGO`（HUiRoom）与 `UI/LYT_HUIHQMAIN.SGO`（HUiHQMain），加一棵子树，写 `Mods/UI/` 同名文件。
  `LYT_MAINFRAME` 不碰（断言拒绝）。
- 新增成员名一律 `vc` 开头、≤ 7 个字符（游戏的 `std::wstring` 就地存放）；原版有同前缀成员就拒绝：
  `vcRoot`（透明、整屏，插件写 +0x1F8 显隐）→ `vcBack`（`Window_Test01_skin`，GLOBAL 资源组）、`vcHead`、`vcU00..13`（单位列）、
  `vcR00..13`（行框，内含 `T` 文字 / `A` `B` 色块 `ColorChipBox_skin` / `I` 图标 `HUDIconTexture_skin` / `N` 备注）、`vcS0..3`（状态）、
  `vcHint`。全部 `Layout` / `TextField`，**没有 `Button`**（不进画面的焦点链）。面板右边界 x = 1128（1920×1080 布局坐标），右侧留给 3D 预览。
- 子树追加为 BaseFrame 的最后一个子节点（最后绘制，盖在画面上）。用 `pylib/sgo.py write_depth_first` 写：它对两个原版文件逐字节还原；
  `patch()` 自检「去掉新增部分 == 原版文件」，不符就抛异常、不写。
- 安装器：`install()` 在「全部生成完毕」之前构建（`build_room_layouts`，Root.cpk 读不了时说明原因、不写布局），写入时
  `room_layout.install`（台账 owner `roomui`）；已有一份台账不认的布局（别的 mod 的）→ 跳过并说明；卸载（选项 1、2 都）`room_layout.remove`。
  `loadout-generate`：两份布局缺哪份就 `put_new` 哪份（游戏运行中也安全：从不覆盖已有文件），出错计入状态文件。
  `tools/build_release.py` 的 hidden imports 加了 `room_layout`。

### 插件（`src/room_ui.{h,cpp}`，纯逻辑 `src/room_ui_logic.h`）

- 安装（`InstallRoomUi`，plugin.cpp 在 `InstallStockGauges` 之后）：先核对全部代码字节——要调的函数（0x8DAD00、0x839600、0x839560、
  0x8859C0、0x863690、0x839FD0、0x824D40、0x953D80、0x7F63E0 的开头）、取偏移的指令（色块 0x88014D 的 +0x98/+0xA0、字色 0x8BC68C /
  0x7F890C 的 +0x2B0、显隐 0x7E999C 的 +0x1F8）、两个原版 OnUpdate（0x8FDFE0、0x89F740）；任一不符 `HOOK roomui REFUSED`，两个画面都不动。
  手柄表的签名（0x1182A00、0x1188315、0x1188A4D）单独核，不符只关手柄（键盘照常）。然后**读两个槽的当前值**，`PatchVtableSlot`（CAS）
  转调——房间槽此时通常已是 EDF6Coop 的钩子。`roompreview::InstallPreviewHooks()` 也在这里调（阶段 7 前是桩，返回 0）。
- 每帧（UI 线程）：
  1. 键盘（游戏在前台、地图没扣住按键时才读）+ 游戏自己的手柄表（两个槽，+0x4C 按住、+0x4D 抑制），`KeysDown` 映射成
     `room_menu.h Keys`；手柄开关是组合键（默认 L3+R3），组合按住时它的按钮不当别的键。
  2. `0x8DAD00(this, 真实 ctx)` 非空才 `Step`（含开关）；为空（子画面、对话框、过场）菜单隐藏并冻结，编辑保留。
  3. 打开（`Act::opened`）：从运行时目录 `SupportCallKey / SupportCallSeats / SupportCallName` 列出有座位或载具键的单位（标签走 hudtext），
     `Load` 读 ini（`GetPrivateProfileStringW`，`[VehicleCrew]`）；目录为空时退回 `kDefaultUnits`。
  4. 输入挡板（`TakeInput`）：打开期间、以及关闭后直到所有菜单键松开，拷贝 ctx（0x18 字节）、副本 +8 置空，用副本调下一个 OnUpdate；
     否则原样转调。
  5. 保存（`Act::save`）：`CollectSave` → `SaveItems`（与改过的单位逐个配对、核对键名）→ 逐个 `SaveSupportLoadout`；某个失败就停
     （之前的已写入并标记为已保存），状态行写原因；全部成功后 `MarkSaved`，菜单**留在单位列表**（看生成进度），`StartVariantGeneration`；
     状态每 0.5 秒读一次 `VariantGenerationStatus`。
  6. 房间：`roomsync::TickRoomSync` 每帧，`RoomSyncLines` 进状态行；客机（`!OnlineHostOnly()`）加「本局以房主配置为准」。
     画面对象换了（换房间 / 回 HQ）→ `LeaveRoomSync`、丢弃未保存的编辑；任务开始 `ResetRoomUi`（mission.cpp）只置标志，
     下一个画面帧（UI 线程）再做，遵守 room_sync.h / room_preview.h「只在 UI 线程调用」的约定。
  7. 预览：游标在小队的某个士兵行（行 / 字段层）→ `ShowPreview`（规格变了才再调），否则 `HidePreview`；每帧 `TickPreview(显示中)`。
  8. 绘制：只在值变了时写（缓存每个元素上次写的内容；换画面对象、每次打开菜单时作废重写）。根节点：画面第一帧就藏（布局文件里它是显示的），
     之后只在显隐变化时写。文字 = 0x839600 取下标 → 0x8859C0 取 TextField → SetText + 字色 +0x2B0（游标行黄、ini 值被拒的单位橙、警告红），
     显隐写 0x839560 取到的组件；色块 = 组件皮肤 weak_ptr 副本（弱引用 +1）→ `0x824D40(.., L"ColorChipBox", "color", float4)`；
     图标 = 组件副本（uses +1）→ `0x953D80` → `0x7F63E0(tex, wstring 路径)`，加载失败该图标保持隐藏并记日志（不每帧重试）。
     找不到 `vcRoot`（没装布局 / 别的 mod 的布局）只记一次日志，画面原样。
- ini：`RoomLoadout=1`、`RoomLoadoutKey=0x71`（F2；EDF6Coop 在房间用 F3 / F4 / F6 / F7 / F8 / Tab，调试召唤 F5~F9，本插件 ini 里没有别的 F 键）、
  `RoomLoadoutButton=192`（L3+R3），`plugin.cpp` 读取，日志 `CONFIG roomLoadout=...`。`Enabled=0` 或 `RoomLoadout=0`：不读按键、菜单关闭，
  根节点照样藏着。
- 文字：hudtext 新增 `room*` 条目（18 个单位、14 种兵、10 种挂载、各行、状态、提示），四种语言；纯数字 / 符号（`%d. %ls`、`x%d`、
  `< HE / AP >`）放在 `room_ui_logic.h`，不进翻译表（`hudtext_check` 的「未翻译」规则）。
- 调色板仍是 `room_menu.h kDefaultPalette`（16 色），**没有**换成游戏 HQ 调色板（0x880010 的色值在 layout+0x780，未读出）。

### EDF6Coop 加载顺序（发现的问题）

设计里「两个 mod 的加载顺序不影响对方」**不成立**：Coop 安装时要求房间槽指向 0x8FDFE0，否则整个 Coop 拒绝（`REFUSED ... nothing was
changed`）。实际 EDFModLoader 按文件名加载（ModLoader.log：EDF6Coop 先于 EDF6VehicleCrew），Coop 先挂、本插件转调它，没有问题。
防护：房间槽仍是原版、而 `EDF6Coop.dll` 就在同目录却还没加载时，本插件**不挂房间槽**（日志 REFUSED，HQ 照常），不让 Coop 整个失效。
根治要 Coop 改成「读当前槽值转调」（edf-coop-stable 的改动，不在本分支）。

### 测试（2026-10-12）

| 测试 | 结果 |
|---|---|
| `room_ui`（C++，ctest，`tests/room_ui_test.cpp`） | 599 项全过：元素名（≤7 字符、唯一、与布局一致）、键盘 / 手柄映射与组合键、游戏手柄表读法（两槽、抑制、缺通道）、输入挡板（含关闭键未松开）、被遮挡时冻结、行外观与图标表、字色、四种语言的各行文字、状态行顺序与上限、目录取单位、预览选人、保存配对 |
| `room_layout`（Python，ctest） | 307 项全过：真实 Root.cpk 两个布局补丁后解析回来、原版成员全部不变、去掉新增部分逐字节等于原版、两次补丁被拒、没有 LYT_MAINFRAME、面板在 x≤1128、头文件里的名字 / 行数 / 图标与生成器一致、图标都在 Root.cpk；台账：覆盖自己的、跳过别人的、`install_missing` 只建缺的、卸载 |
| `room_ui_sigs`（Python，ctest，EDF.dll 当文件读） | 28 项全过：`room_ui.cpp` 的 18 组签名逐字节等于 EDF.dll，两个槽在文件里指向原版，偏移常量与签名里的指令一致，色块调用点调的是 0x824D40、参数是 L"ColorChipBox" |
| `loadout_generate`（Python，改） | 47 项全过：新增「缺布局时作为新文件写入、已有的不替换、台账 owner roomui」 |
| `room_menu` / `loadout_writer` / `support_loadout` / `support_config` / `support_*_ini` | 全过（ctest 10/10） |
| `hudtext_check` | all ok |
| `tools/selftest.py` | 172 项：修改前整跑 4 项失败（`release_imports` 缺 hidden import、`map_wired` 新按键读取者、两个安装测试的假 Root.cpk 抛 struct.error），修好后这 4 项单独重跑全过 |
| 变异 | 去掉挡板的「关闭后锁存」→ room_ui 4 项失败；去掉组合键屏蔽 → 1 项失败；改坏一个签名字节 → room_ui_sigs 失败。均已还原 |

DLL（RelWithDebInfo，Ninja）编译通过，无警告。

### 没在游戏里验证的（全部是静态推断或接口约定）

1. 补丁布局能被游戏加载：`vc*` 元素在两个画面实例化、子树挂 BaseFrame 末尾不影响原有元素的排布与焦点、`Window_Test01_skin` /
   `ColorChipBox_skin` / `HUDIconTexture_skin` 在两个画面都已加载（房间资源组是否含 GLOBAL 组没逐条核）。
2. 坐标：元素 pos 相对父元素、1920×1080 布局坐标在别的分辨率按比例缩放；面板不挡住 3D 预览（预览挪到右侧是阶段 7 的事）；
   字号 18~28 在中文 / 日文下放得下（`font_flag [4,10]` 的含义没解码）。
3. 显隐：写根节点 +0x1F8=0 能连子节点一起藏、每帧绘制都读它；画面第一帧之前（打开时的滑入过场）面板会不会闪一下。
4. **没有「初始隐藏」属性**：插件拒绝安装（签名不符，如游戏更新）或没加载时，补丁布局里的面板背景与 28 个色块（模型默认色）会一直显示在
   房间 / HQ 上。缓解：卸载同时删布局；安装器每次从当前 Root.cpk 重新生成。可选改法（未做）：背景与色块改用贴图皮肤、由插件设贴图，
   没有插件时什么也不画。
5. 输入：副本 ctx +8 置空后房间 / HQ 确实不响应键鼠与手柄、网络与成员刷新照常；键盘文字聊天是否另走一路；关闭键松开前一直置空足以防止
   被当成 cancel；手柄开关组合（L3+R3）在房间里本身没有功能；F2 不是游戏自己的默认键（游戏键位默认值没查到）。
6. `0x8DAD00` 为空的各种情况（消息框、换装备子画面、开打过场、Coop 的对话框）菜单都会藏起来；房间状态 +0xAC0 没用。
7. 文字：SetText 在 [tf+0x228]==0（字体未就绪）时直接返回；插件先看 +0x228，为空就不记缓存、下一帧再写（+0x228 的含义是字体是推断）。
8. 色块：`0x824D40` 的 float4 是 RGBA 0..1（推断）；ColorChipBox 模型的 "color" 参数按 16 字节拷贝；弱引用计数的加减与游戏一致（逐条核了调用点）。
9. 图标：`0x7F63E0` 同步绑定（宽高立刻可读），首帧显示是否需要等待 GPU 上传；重装 `canon.dds` 等借用图标是否好看；图标元素 80×40 的比例。
10. 手柄：表里 +0x4C 是按住、+0x4D 抑制（反汇编证实），游戏失去前台时是否置抑制没核；两个槽都读（分屏第二个手柄也能操作菜单）。
11. 房间同步 / 预览：现在是桩（阶段 6 / 7），调用时机按接口约定；`LeaveRoomSync` 在任务开始后的下一个画面帧才调（UI 线程约定），
    任务期间不会有画面帧。
12. 客机判定：`OnlineHostOnly()` 在房间阶段（未进任务）是否已知道主客，没核。
13. 保存后后台生成器的状态显示（生成中 / 就绪 / 失败）只在菜单打开时看得到。
## 阶段 4b：乘员行上屏、没有插件时什么都不画、游戏 HQ 调色板（2026-10-12，分支 `feat/rl-polish`，只做了离线验证）

没有启动 / 关闭游戏，没有往游戏目录写任何文件；只读了 Root.cpk、DX11.cpk、Chunk01/02.cpk 与 EDF.dll。

### 1. 乘员行（阶段 5 的模型接到画面上）

- `room_ui_logic.h CatalogUnits`：目录里有座位、有载具键、或 `HasCrew` 的条目都列出（`UnitSpec.crew = HasCrew(key)`），末尾补
  默认（键 `""`）与任务乘员（`kCrewMissionKey`），与 `kDefaultUnits` 相同的结构。真实目录 37 项：小队 8 + 载具 10（各带乘员行）+
  乘员单位 25 + 默认 / 任务 2 = 45 个单位，≤ `kMostUnits` 48。没有运行时目录时用 `DefaultUnits`（`kDefaultUnits` 按 HUD 语言重贴标签）。
- 乘员行的兵种：`CrewKindOf`——地面载具（`IsGroundCrewKey`）用 `SupportVehicleCrewWeapon`，其余用 `SupportAircraftCrewWeapon`，
  任务乘员固定步枪（`N601_COMMON_RANGER_AF`），默认那一行显示地面乘员的兵种（取值来自 `SupportCfg()`）。
- 绘制（`DressCrewRows` + `LookOf` / `RowLine` / `NoteLine`）：一行「乘员颜色（兵种）」，主 / 副色块，兵种图标；没单独设置的乘员
  色块显示默认乘员的颜色、备注「同默认」；设成 X 显示「原色」、不画色块；编辑时备注 `< 主色 >` / `< 副色 >`。
  单位列里目录条目的乘员单位显示「<名字>·乘员」，默认与任务乘员用自己的标签。hudtext 四语新增 5 条（`roomUnitCrewDefault`、
  `roomUnitCrewMission`、`roomUnitCrewOf`、`roomCrewRow`、`roomCrewDefault`）。`ViewRow` 加了 `key`（单位行与打开单位的每一行）。
- 预览：光标在乘员行（行 / 字段层）→ 该乘员兵种的**队员**（不是队长），颜色 = 自己的，没设置时默认的（`CrewLookOf`）。
- 保存：`SaveItems` 按 `CollectSave` 的行逐条配对（每个单位先挂载 / 预设、再乘员；乘员行的键必须是 `LoadoutKey(crew, key)`），
  每条带 `unitIndex`；`room_ui.cpp Save` 逐条 `SaveSupportLoadout`，每写成一条 `MarkItemSaved` 只标记那一部分（中途失败时，
  已写的部分不再算改动，没写的仍是改动）。**修了阶段 4 的一个缺陷**：以前按「每个改动单位一条」配对，乘员只改了的单位会被当成
  `SupportVehicle_` 配对失败，载具 + 乘员都改的单位第二行永远不写。状态行「已保存 N 个单位」按单位数计。

### 2. 没有插件时面板不显示

阶段 4 的布局里背景用 `Window_Test01_skin`、色块用 `ColorChipBox_skin`（模型默认色）、文字默认一个空格；插件没加载或签名不符拒绝时，
面板背景与 28 个色块会一直盖在房间 / HQ 上。现在：

- 布局（`tools/room_layout.py`）：子树里每个元素的皮肤只有 `Transparent_skin`（什么都不画）和 `HUDIconTexture_skin`；
  所有 TextField `text_dr ''`（原版布局有 60 个这样的空文本）。背景 `vcBack`、色块 `vcRxxA/B`、图标 `vcRxxI` 都是贴图皮肤。
- 证据（静态，`tests/room_ui_sig_audit.py` 逐字节核 EDF.dll）：
  - `HUDIconTexture_skin.sgo` = `{base: '', default: [['', 0]], object_class: Texture}`（Root.cpk 读出，`room_layout_test` 核对）。
  - `Skin_Texture`（vtable 0x17F6228）槽 3 = 绘制 0x7F7040：`rax=[rdi+0A0h]`（0x7F7110），`test rax,rax / je`、`cmp qword [rax],0 / je`
    （0x7F7190）都跳到收尾 0x7F725A——没有绑定贴图就不画。【证实】
  - 槽 4 = 切换状态 0x7F7270：状态路径为空时 `cmp qword [rsp+30h],0 / je` 跳过 0x7F63E0（0x7F73E3），不绑定任何贴图。【证实】
  - 构造函数 0x7F5C20 把 +0xF0..+0xFC 与 +0x100..+0x10C 都置 1.0（0x7F5CD2）；绘制颜色 = lerp(+0x100, +0xF0, +0xE4) × 组件颜色
    （0x7F707F / 0x7F70FC）。【证实】+0xA0 所在的 +0x90 对象由 0x1120520 构造，插件调 0x7F63E0 之前为空【推断，与原版 HUD 图标同一用法】。
- 白贴图：Root.cpk、DX11.cpk、Chunk01/02.cpk 里**没有**纯色的独立 DDS（最小的几张：`DEBUG/GRIDTILE.DDS` 网格、
  `UI/CHATSHORTCUT_CURSOR.DDS` 光标形状，逐像素读过）。所以安装器新写一个 `Mods/UI/EDF6VC_WHITE.DDS`（8×8，全 FF；头部与
  `UI/CHATSHORTCUT_CURSOR.DDS` 相同的 32 位无压缩格式：flags、mip、像素格式、caps 逐字节一致，`room_layout_test` 核对），台账 owner
  `roomui`，安装 / `loadout-generate`（缺了就 `put_new`，不需要 Root.cpk）/ 卸载同布局。插件路径 `app:/UI/EDF6VC_WHITE.dds`
  （`kWhiteTexture`，测试核对与安装器写的是同一个文件）。
- 插件（`room_ui.cpp SetTextured`）：按名取组件 → 0x953D80 取 Skin_Texture → 路径变了才 0x7F63E0（失败记日志、元素保持隐藏、
  不每帧重试）→ 两组染色都写 → +0x1F8。背景染 `kPanelColour`（0x0C1622 85 %），色块染颜色，图标染白色不透明。
  去掉了 0x824D40（ColorChipBox 模型参数）与 0x88014D 调用点签名；新增 3 段签名（0x7F707F、0x7F70FC、0x7F5CD2）。
- 收回：菜单不显示的每一帧（关闭、被子画面 / 对话框 / 过场遮住、`RoomLoadout=0`）`ClearMenu` 把插件写过的都收回——文字写空
  （SetText 对空指针本来就用 L""，0x86369F）、所有贴图元素染色 alpha 0 并隐藏、根节点隐藏；经写入缓存，只有变化时才真正调用。
  画面对象换了（换房间 / 回 HQ / 开打）时旧画面由游戏销毁，插件不再碰它；新画面从第一帧起就是上面的收回状态。
  布局里没有 `vcRoot`（没装 / 别的 mod 的布局）记一次日志，之后这个画面对象不再查找任何元素。
- 文字不显示时写 L""（阶段 4 写的是一个空格）。

### 3. 游戏 HQ 调色板

- 来源（静态，H）：`DEFAULTPACKAGE/CONFIG.SGO` `SoldierInit[class][5][0]` = `App:/DefaultPackage/CustomColor<Ranger|WingDiver|AirRaider|HeavyArmor>.sgo`
  （0xE07D0 取路径）；键 `CustomColor` = 12 项 × 2 通道（主 / 副）× RGBA 浮点，alpha 全 1。加载器 0xDF1B0（`lea rdx, L"CustomColor"`
  0xDF3D5）拷进 GameStatus +0x6FB0（每玩家 × 兵种 × 外观 × 通道 0xC4，12 个 float4）；`HUiHQColorSelect`（vtable 0x1800C40）槽 1
  0x881060 拷到 this+0x790、槽 2 0x880F50 每通道画 12 个色块（`call 0x880010` 0x880FFE，`cmp rbx,0Ch` 0x88100A）。
  第 4..11 项四个兵种相同，0..3 项各兵种不同。EDF.dll 里没有静态色表。自由调色（`HUiHQColorMake`）改写的是这 12 个槽之一，不是额外条目。
- 映射：菜单两个颜色字段共用一个循环，所以调色板 = 共有 8 对（每对主色、副色）+ 游骑兵、翼人、重装各自 4 对，去重后 40 色，
  「原色」仍在循环最前（`CycleColour`）。空降兵（Air Raider）的 4 对不收：支援士兵没有这个兵种。`kPaletteMost` 32 → 48。
- `tools/hq_palette.py`（`--write`）从 Root.cpk 生成 `src/hq_palette.inc`；`room_menu.h kDefaultPalette = HqPalette()`。
  浮点 ×255 四舍五入成 RRGGBB（与生成士兵文件的颜色写法相同）。原来 16 个占位色（含 1E3A8A / FFFFFF）不再在循环里；
  ini 里手写的这类颜色照旧保留，改它时从「原色」的位置开始循环（阶段 3 的既有规则）。
- `tests/hq_palette_audit.py`（ctest `hq_palette`，没有游戏时 SKIP）：四个文件是 CONFIG 点名的、每兵种 12 对、4..11 共有、
  `.inc` 与现在从游戏生成的逐字相同、顺序与去重规则、支援兵种的每个颜色都在；EDF.dll 0x678CCB46 时再核 `L"CustomColor"`、
  0xE07D0 调用、色块循环 12 次。

### 测试（`cmake --build build` + `offline_checks` 0 警告；`ctest -j 3`：237 项 100% 通过，其中 36 项需要游戏原生环境按设计 SKIP）

| 测试 | 结果 |
|---|---|
| `room_ui`（C++） | 665 项（原 599）：目录含乘员 / 默认 / 任务（12 个规格、13 个单位）、`DefaultUnits` 重贴标签、乘员兵种（地面 / 空中 / 任务 / 默认）、继承默认色的色块与备注、X 不画色块、默认行本身是原色、四语行文字、乘员单位「名字·乘员」、预览（坦克乘员是队员、用默认色、设置后用自己的、X 是原色、战机乘员空中兵种、任务乘员步枪）、**一次保存同一单位写载具 + 乘员**（直升机乘员、坦克挂载、坦克乘员、默认 `SupportCrewColour`、`SupportCrewColour_MISSION` 五行，逐条 `MarkItemSaved` 后只剩未写部分算改动）、乘员改回默认写空值、乘员行配到载具键被拒、每条值都过 `ValidLoadoutValue` |
| `room_menu`（C++） | 211 项（原 205）：调色板 = 表、40 色、每色一次、顺序、原色在循环首尾、一圈 41 步、每色经 ini 往返；两条硬编码旧色的用例改为新调色板的首 / 末色 |
| `room_layout`（Python） | 578 项（原 307）：每个新增元素「默认什么都不画」（皮肤只在 Transparent / HUDIconTexture、文字全空）、判据本身能抓出 Window / ColorChipBox 皮肤与非空文字；Root.cpk 里两种皮肤的定义；白贴图像素与头部格式、不遮蔽原版文件名、插件路径一致；安装 3 个文件、`install_missing` 单独补白贴图 |
| `room_ui_sigs`（Python） | 35 项（原 28）：新签名逐字节、染色偏移、Skin_Texture 槽 3 / 4、无贴图不画、空路径不绑定、模型皮肤调用已删除 |
| `hq_palette`（Python，新） | 17 项 |
| `loadout_generate` | 48 项：新文件里多了白贴图（owner roomui） |
| `hudtext_check` | all ok |
| `tools/selftest.py` | 172/172：整合分支上原有 2 项失败（不是本阶段引入，顺手修了）：`release_imports`——`build_release.py` 的 hidden import 缺 `menu_preview`（阶段 7）；`heli_sight_after_aim_lines`——插件读 `SupportCrewColour` / `_MISSION` / `_<单位>` 而发布的 ini 没有这些键、selftest 不认这个前缀（阶段 5）。改：ini 加 `SupportCrewColour=`、`SupportCrewColour_MISSION=`（空 = 原色，与不写相同），selftest 的后缀表加 `SupportCrewColour_`（键 = `CREW_KEYS` + `MISSION`，示例匹配注释里的 `SupportCrewColour_TANK_CREWED=X:FFFFFF`） |

**变异测试**（先提交 6b78a76，再改、跑、还原）：

| 变异 | 结果 |
|---|---|
| 布局文字默认改回一个空格 | `room_layout` 变红（「vcHead draws nothing by itself: text ' '」） |
| 色块皮肤改回 `ColorChipBox_skin` | `room_layout` 变红（「vcR00A draws nothing by itself: skin …ColorChipBox_skin.sgo」） |
| `SaveItems` 不配乘员行 | `room_ui` 11 项变红（载具 + 乘员两行保存、逐条标记、乘员改回默认等） |
| `hq_palette.inc` 改一个色值 | `hq_palette` 变红 |

### 没在游戏里验证的（4b 新增；阶段 4 的清单其余各条仍然有效，第 4 条已由本阶段处理）

1. `Mods/UI/EDF6VC_WHITE.DDS` 经 EDFModLoader 重定向后能被 0x7F63E0 加载（新文件名在 `Mods/OBJECT` 已实证，`Mods/UI` 的贴图没有）；
   8×8 无 mip 的 32 位 DDS 拉伸到 1080×912 的背景是否均匀（单色，理论上没有可见差异）。加载失败时面板与色块不显示、日志 `ROOMUI texture … not loaded`。
2. 染色 alpha < 1 的背景是否按透明混合（`kPanelColour` 85 %），以及组件颜色（rsi+0xB0）是否为白色。
3. `HUDIconTexture_skin` 在房间画面的资源组里已加载（阶段 4 只核了 GLOBAL 组的推断）；背景与色块现在都依赖它。
4. 没有插件 / 插件拒绝时，两个画面与原版完全一样（静态：皮肤不画、文字为空、Transparent 根节点）。要在装了布局、移走 DLL 的情况下各看一次房间与 HQ。
5. 收回：关菜单、被子画面遮住、`RoomLoadout=0` 时文字与色块立刻消失；再打开时全部重画（写入缓存作废）。
6. 乘员行：兵种图标、「乘员颜色（兵种）」文字在四种语言下放得下（340 px）；预览显示乘员队员（队长模板不用）。
7. 一次保存写多行后生成器只启动一次、状态行「已保存 N 个单位」。
8. 调色板颜色是 `CustomColor` 浮点按 sRGB 直接换算；游戏若按线性色处理，实机颜色会与 HQ 色块略有差别（HQ 绘制还乘了 `[ctx+8]` 的染色，0x7F9D50，未追）。
## 阶段 R（审查修复）（2026-10-13，分支 `feat/rl-review-fix`，基于 `feat/room-loadout-menu` 72e2b45，只做了离线验证）

没有启动 / 关闭游戏，没有往游戏目录写任何文件；只读了 EDF.dll（析构函数签名）与 Root.cpk（既有测试）。提交：d872936（审查 1–7）、
fbc9f0b（EDF6Coop 成员属性接口 acda838 的改动），文档另一提交。

### 1.（高）`RoomLoadout=0` / `Enabled=0` 时房间预同步照跑

- 根因：`room_ui.cpp Frame` 无论 `on` 与否都 `TickRoomSync`；`InstallRoomUi` 无论配置都挂钩。
- 修复：`InstallRoomUi` 在 `RoomLoadout=0` 时**什么都不核、不挂**直接返回（重启生效；ini 注释写明）。运行中 `Enabled` / `RoomLoadout`
  改成 0：`Off()` 原样转调画面自己的 OnUpdate（原 ctx），不读按键、不调 0x8DAD00、不同步、不预览，只收回本画面画过的东西（`st.drew`）。
  新增纯逻辑 `room_ui_logic.h PlanSwitches / LeaveOnForget`：只有「在线房间 + 两个开关都开」才 `TickRoomSync`；不再满足时
  `LeaveRoomSync` 恰好一次（并 `ForgetRoomHostList`，见第 2 条）。新 ini 键 `RoomPreSync=1`（`crew.h roomPreSync`，`plugin.cpp` 读取并记日志）：
  0 = 不公布、不读房主清单、不自动生成。旧 ini 由安装器既有的 `merge_ini`（`ADDED_HEADER` 段）补入，连同上方注释。
- 测试：`room_ui` `SwitchesPlan`（12 项）；新 `room_ui_wiring`（`tests/room_ui_wiring_guard.py`，源码守卫：0 时 Install 在任何签名 / 挂钩前返回、
  Off 帧不调 ReadRaw / GetInput / TickRoomSync / Step / TakeInput / DrawMenu、Tick 只在 `sw.tick` 下、Leave 只两处）；
  `tools/selftest.py ini_merge_adds_room_presync`。

### 2.（高）生成文件只增不减、关卡开始按目录扫描且 64 个封顶

- 根因：`support_variants.cpp Find` 扫 `Mods/OBJECT/EDF6VC_NPC_*` / `EDF6VC_LO_*`，按名字顺序取前 64 个；预同步只往里加文件，
  玩家自己的预设会被别人的文件挤出去静默变原版；恶意房主可以让客机无限生成。
- 数据模型改为「本关能被要的文件」（`src/support_variants_logic.h MissionVariants`，`support_variants.h` 注释）：
  本机配置（`ForEachConfiguredVariant`，上限 `kConfiguredVariantsMost` = 64×(12+2)+2 = 898）∪ 上一个房间校验过的房主清单
  （`room_sync` 新增 `HostList` / `ForEachRoomHostVariant`，客机才有；离开房间（开局）仍保留，下个房间的 tick 替换、清单变坏 / 没有清单 /
  自己成为房主 / 开关关闭时丢弃；另一线程读时走 SRWLOCK 副本；上限 `kHostVariantsMost` 512 = `kSetMost`，static_assert）∪ 待生成清单
  最新 128 个合法名字（`PendingNames`）。三部分各自封顶，所以并集永远放得下：`kSupportVariantsMost` 64 → 1538。只预载其中磁盘上有的。
  不再有 `FindFirstFileW`。名字一律过 `ParseVariantFile`（路径、别的文件进不来）。
- Hello 的 Bloom 过滤器线格式固定 32 字节（256 位、3 个探针，不能加长）。改为：有房主清单时只放「清单 ∩ 本机已预载」（`HelloBloom`），
  没有时放全部已预载。房主仍只在「所有对端都报有」时应用变体（`DecideVariant` 不变）。误报率（实测 / 理论 (1-e^{-3n/256})^3）：
  n=10 0.1%、30 约 3%、64 约 15%、128 约 54%。误报的后果与以前相同：士兵颜色对端按原色生成；载具挂载对端 `Validate` 拒绝该计划。
  有房主清单时 n = 清单里本机有的文件数（通常几十个），比以前「目录里所有文件」小得多。
- 客机生成上限：`room_sync_logic.h kNotesPerRoom = 128`，每个房间最多往待生成清单记 128 个名字（换清单不重置，离开房间才重置）；
  超出的计数为 `ownCapped`，不启动生成器，状态 `f`，界面新行 `roomSyncSelfCapped`（四语），日志一次。只记校验过的清单里的名字（原有）。
- 安装器清理：`support_loadout.stale / remove_stale`：台账里 owner 为 `loadout` 的生成文件，ini 与待生成清单都不再点名的（连同只有它们挂的
  挂点武器）→ `ledger.release`（别的工具也拥有的只去掉本工具的认领；不在台账里的文件不碰）。安装（1）在 `write_loadout_files` 里、
  清空待生成清单**之前**执行；菜单 8（有无修改都）提示数量、输入 y 才删；游戏在运行时都不做；都在生成器锁里（第 4 条）。
- 测试：新 `support_variants`（C++，26 项：名字校验、待生成清单最新 128 个 / BOM / LF / 重复、三部分各在上限时并集 1538 正好放下、
  某部分超限挤不掉别的、Bloom 限定、误报率表）；`room_sync`（132 项，原 103：每房间 128 上限、换清单仍受限、换房间重新计、
  `MissionList` 保留 / 丢弃规则）；`installer_lock` 的 stale 一段。

### 3.（高）`IsGroundCrewKey` 前缀匹配把 `*_AIRDROP` 当地面乘员

- 根因：前缀 `TANK_` / `TRANSPORT_` / `TRUCK_` 命中 `TANK_AIRDROP` 等，而集装箱投送的乘员是运输直升机飞行员（`SupportCfg().aircraftCrew`）。
- 修复：`support_loadout.h kGroundCrewKeys` 显式 6 个键，与 `tools/support_loadout.py VEHICLE_CREW_KEYS` 相同；`room_ui_logic.h CrewKindOf`、
  `support_variants.cpp GiveCrew` 随之正确。
- 测试：`support_loadout` 负例（3 个 `*_AIRDROP`、`TANK_CREWED_X`、`TANK_`、空）；`room_ui` `CrewKindOf(*_AIRDROP)` = 空中兵种；
  `support_loadout_ini`（104 项，原 101）从源码解析 C++ 列表 == Python 列表（同序）、函数体不再有 `_wcsnicmp`、`*_AIRDROP` 不在地面表。

### 4.（中）台账并发写

- 根因：`Ledger` 构造时读一次，之后整文件写回旧快照；安装器流程不拿生成器锁；生成器可能比游戏活得久。
- 修复：`pylib/ledger.py`：每次修改前 `_refresh`，保存时重读并把本对象自上次读后的改动（加 / 减 owner、改 sha）合并进当前文件
  （`merge_files` / `_merge_entry`，别人新加的 owner 保留；直接改 `.files` 再 `_save` 的 make_optics / make_proteus 同样合并）。
  `loadout_generator.wait_generate_lock`（等待最多 `LOCK_WAIT_S` = 120 秒，提示一次，超时拒绝且什么都不写）；`installer.write_game`
  （安装 / 卸载 / 战役）、`manage_support`（菜单 7 / 8）都在锁里；卸载不再删除自己持有的锁文件（`remove_generator(lock=False)`，
  放锁后在 `write_game` 里删）。`generate_lock` 先建 `Mods/Plugins`。
- 测试：新 `installer_lock`（Python，35 项）：旧快照保存不丢别人的条目、别人新加 owner 时 release 不删文件、直接改 `.files` 合并、全释放后删台账、
  `merge_files` 单元；生成器持锁时安装 / 卸载 / 菜单 8 拒绝且 ini 不变；等待中生成器结束则继续；持锁期间另一个生成器拿不到锁。
  `loadout_generate`、`slim_generator`（导入白名单加 `time`）随之更新。

### 5.（中）保存逐键读写 + 重读配置（最多约 96 次）

- 修复：`loadout_ini.h EditLoadoutIniAll`：先校验全部（键名、`ValidLoadoutValue`），全部通过才在内存里依次改；`loadout_store.h SaveSupportLoadouts`：
  读一次、写一次（原子）、`ReloadConfigNow` 一次；`SaveSupportLoadout` = 一条的批量。`room_ui.cpp Save` 一次调用，失败什么都不写、所有行仍是改动，
  成功后才逐条 `MarkItemSaved`。
- 测试：`loadout_writer`（66 项，原 50）：批量结果与逐条保存逐字节相同（含同键两次后者生效）、任一条非法（6 个位置）整批不写并点名、
  坏键、UTF-16、空批；`room_ui_wiring` 源码守卫：一次读 / 写 / 重读，校验在写之前。

### 6.（中）布局生成 / 写入异常中断整个安装

- 修复：`installer.build_room_layouts` 捕获 `Exception`（`TypeError`、`NotImplementedError` 等）打印类型与原因返回 None；
  新 `write_room_layouts` 包住 `room_layout.install`，失败说明后返回 False，安装继续。去掉不再用的 `import struct`。
- 测试：`installer_lock`：注入 `TypeError` / `NotImplementedError` / `RuntimeError` / `MemoryError` 到构建与写入，都只提示、不抛；`install()` 只经守卫写布局。

### 7.（低）画面身份靠裸指针

- 根因：新画面可能复用旧画面地址，`Follow` 认为没变，不会 `Forget` / `LeaveRoomSync`。
- 修复：挂两个画面的标量删除析构（虚表槽 5：HUiRoom 0x180AC68 → 0x8F7510，调 ~HUiRoom 0x8F5950 再 delete 0xCA0；HUiHQMain 0x1804E00 → 0x89F480，
  内联析构后 delete 0x810），读当前槽值 CAS 转调（与 OnUpdate 同法）；析构里只在「是正在跟随的那个」时置原子标志，下一帧 `Follow`
  先看标志再比较指针。析构槽挂不上则该画面不挂菜单。新增 3 段签名（含 ~HUiRoom 第一条写的是 HUiRoom 虚表）。
- 测试：`room_ui_sigs`（49 项，原 35）：两个析构槽是各虚表槽 5、文件里指向原版、~HUiRoom 写入 HUiRoom 虚表、HUiHQMain 析构写入自己的虚表、
  delete 大小、钩子转调；`room_ui_wiring`：`Follow` 先查析构标志、两个析构都挂、先挂析构再挂 OnUpdate。

### 8. EDF6Coop 成员属性接口变更（edf-coop-stable acda838）

- `src/coop_member_attribute_api.h` 按原样重新拷贝（逐字节相同）。
- 4096 字节总额：主机清单所有分段合计 `kListBudget = 3800` 字节（含头部）封顶，放不下的照旧计入「没能公布」；就绪值 ≤ `kHaveMost` 40；
  static_assert 两者之和 ≤ 4096。换短清单时先撤回多余分段再发布（腾出额度）。实测 502 个文件的清单列出 200 多个、总字节 ≤ 3800。
- 发布失败不再静默：`Publish` 返回 0 时取 `lastError`（`EDF6COOP_MA_HAS(api,lastError)` 才调；旧表为 0），界面行 `roomSyncPublishRefused`
  （四语，带错误码；5 = 总额满），日志按「键 + 字节数 + 错误码」每种原因一次，下个 tick 重试。
- 函数表：只要求到 `snapshot`（v1 基础），`lastError` 可选；`room()` 的 size 用本头文件 `sizeof`（≥ 160）。
- 2 秒合并发送 / 先发布后挂接：逻辑本就以缓存比较、每成员的就绪值带清单 hash；测试加「节流」模式（其他成员只在 `Send()` 后看到值），
  验证清单 / 就绪值晚到时显示「等待」/「核对中」，到达后才就绪。
- 测试：`room_sync` `Budget`：最长清单与就绪值在额度内、其他插件占满额度时清单被拒并显示「错误 5」且记日志、腾出额度后重试成功、
  旧表（无 lastError）可用且显示错误 0、节流下的完整流程。

### 变异测试（先提交 d872936，再改、编译、跑、`git checkout` 还原）

| 变异 | 结果 |
|---|---|
| 1a 预同步改回 `if(screen.room)` 每帧 tick | `room_ui_wiring` 红 |
| 1b `PlanSwitches` 的 tick 改成 `(on \|\| preSync) && room` | `room_ui` 6 项红 |
| 1c `RoomLoadout=0` 仍安装 | `room_ui_wiring` 红 |
| 2a 去掉每房间 128 上限 | `room_sync` 6 项红 |
| 2b 预载集合不含房主清单 | `support_variants` 3 项红 |
| 2c Hello 过滤器不限定房主清单 | `support_variants` 红 |
| 3 `IsGroundCrewKey` 改回前缀匹配 | `support_loadout`、`room_ui`、`support_loadout_ini` 红 |

### 结果

`cmake --build build` + `offline_checks` 0 警告；`ctest -j 3`：240 项 100% 通过（新增 `support_variants`、`installer_lock`、`room_ui_wiring`；
需要游戏原生环境的按设计 SKIP）；`tools/selftest.py` 173/173（新增 `ini_merge_adds_room_presync`）。
中途发现：`pack_install_upgrade_uninstall` 的替身构建器总是返回坦克文件、而测试的 ini 没有点名它；游戏没在运行时安装里的清理（第 2 条）
会把它当成不再引用的文件删掉（第一次全跑时游戏恰好在运行，清理跳过，没暴露）。替身同时给出 `wanted_files`，与真构建器一致；
游戏运行 / 未运行两种状态单独跑都通过，第三次全跑 173/173。

### 没在游戏里验证的

1. 两个析构槽的钩子在实机上是否每次画面销毁都被调用（静态：两个虚表槽 5 是各自类的标量删除析构）；析构所在线程未核（只写原子标志，任何线程都安全）。
2. `RoomLoadout=0` 时游戏里两个画面完全原样（静态：什么都不挂）。
3. 开局预载改为点名集合后，房间预同步生成的文件在下一关被预载并被房主应用（逻辑与测试覆盖，未实机）。
4. EDF6Coop 新接口的额度 / 节流只在假表上验证。
5. 安装器的锁等待与清理只在假游戏目录里跑过。

### 2026-10-18 背包资源兼容补充

带颜色的背包预设现在同时生成并预载完整背包资源及去掉背包的颜色资源；`wanted_files`、配置枚举与清理使用同一需求范围，
pending 清空后仍保留两者。配置来源上限增为 1666，另加房主清单 512、pending 128，任务预载集合当前总上限为 2306；上述阶段 R
记录的 898 / 1538 是当时的上限。

房间成员用 `EDF6X_VCPACKS=1` 声明可读背包 token。房主每次 tick 重新确认全员标记；有旧版或标记尚未到齐时公布只含颜色与载具的
兼容清单，全员支持后才加入背包 token。清单、就绪值及标记合计最多 3841 字节、6 个键，支援 wire 仍为 v2。任务中 applied
背包文件必须在每端实际预载；Bloom 假阳性不能绕过 prepare 的精确检查，缺文件时记录 pending 并拒绝计划。

生成 / 清理、无 pending 时的配置预载、旧成员加入 / 离开的清单切换、旧 peer 首次计划保留颜色及真实 Bloom 假阳性拒绝均有通过的
定向离线回归。详见 [NPC 背包位武器 §5](npc-backpack-re.md#5-联机协议)；实际 EOS 节流、房主迁移及双机投掷效果仍需实测。
