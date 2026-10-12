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
