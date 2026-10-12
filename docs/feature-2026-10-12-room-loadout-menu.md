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
