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
