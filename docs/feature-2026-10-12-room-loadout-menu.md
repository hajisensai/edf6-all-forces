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
