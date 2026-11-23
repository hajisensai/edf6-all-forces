# 房间 / HQ 3D 预览：UiHQ_3DModelView 加载链（静态逆向，2026-10-12）

EDF.dll TimeDateStamp 0x678CCB46，地址为 RVA。全部静态分析，**未实测**。置信度 H（反汇编 / 数据直接证实）/ M（推断）/ L（猜测）。

## 0. 结论

- `HUiHQ3DModel` 构造 0x8782C0 按本机玩家数（GS+0x14FF4）各建一个视图（new 0x450，构造 0x938760）；房间与单人 HQ 只有 index 0。H
- 控制全走 UI 广播 `0x119D1B0(System*, id, payload*)`，System = `0x881FE0()`（`[EDF+0x20B2960]-0x88`），调各对象 vtable 槽 3 OnMessage；视图的是 0x93B320。H

## 1. 视图加载什么

OnMessage 0x93B320 跳转表：

| id | payload | 作用 | 置信 |
|---|---|---|---|
| 3 | `{player, class, outfit}` | 整体重建：view+0x318 填 Desc，异步建模 → 0x93F910 | H |
| 4 | `{player, slot, weaponId, ?}` | 换某槽武器 → 0x940100 | H |
| 5 | `{player, class, outfit, ch, float4 rgba}` | 实时改色；仅当 class/outfit == 当前显示（view+0x180/+0x178）；写 view+0x33C+ch*16 与 view+0x158+ch*16 | H |
| 6 | `{u8, player, outfit}` | outfit 变才重建，否则从 GS 重读颜色；view+0x310≠0 时忽略 | H |
| 7 | `{0}` / `{1, player}` | view+0x304（缩放 / 镜头状态） | M |
| 8 / 9 | 无 | view+0x310 加一 / 减一（锁 msg 6） | H |
| 10 | `{0..3}` | view+0x3EC；1 时 Draw 跳过 | M |

- 模型路径 `0xE3420(GS+0x130, class, outfit)` = CONFIG.SGO `SoldierInit[class][5][1][outfit]`，如 `APP:/MENUOBJECT/P505_RANGER.SGO`。H
- 颜色：`GS + player*0x3E60 + (outfit+class*4)*0x188`，下标 +0x7070，第二色 +0xC4；色值表 +0x6FB0 float4。H
- 武器：`GS+0x6E98+class*0x18+i*4`，数量 `0xE3680`（4/4/5/6）。H

Desc（0x70，view+0x318 为玩家本人那份；异步 lambda 中是拷贝）：+0 wstring 路径；+0x20 兵种号（3 = Fencer 双持）；+0x24 / +0x34 color0 / color1 float4；+0x44 outfit；+0x48 武器数；+0x4C class；+0x50 选中槽；+0x54 武器 int[6]。

触发：HQ `0x8BDE80(player, class, outfit)` 发 msg 3（0x8AD970 = 显示玩家本人：class = GS+0x6E90，outfit = `0xD8700(GS, player)`；0x881F40 发 9 → 3 → 8）；改色界面发 msg 5（0x882070 / 0x8820E0 / 0x87D973）；房间 0x8FDFE0 每帧比较 GS outfit 与缓存 +0xAC4，不同才发 msg 6（0x8FE1F5）。H

异步链：`0x7B5660` 投递作业队列 `[0x20B29B0]` → 工作线程 `0x93F910(ModelSlot*, Desc*)`（ModelSlot = Async+0x28，Async 指针在 view+0xD0）→ 按路径 FNV 缓存 `Resource_Charactor`，加载 0x93AA00 → `0x84E80(0x20B28D0, path)`（按需加载，MENUOBJECT 不在资源组预载）→ 颜色抄到槽 +0x78/+0x88，0x940100 挂武器，Async+0x9C=1。每帧 0x93CE30：就绪时槽 → view+0xE0，再 +0xE0 → 显示槽 +0x1A0。H/M

## 2. 颜色

Draw 0x93C370 每帧对 i=0,1 读菜单 SGO `change_color[i]`，`0x6C3160(model, sel, name, &rgb)`，RGB 取 view+0x218+i*16，alpha 固定 1.0。任意 RGB 可行；实时改色发 msg 5 或写 view+0x158/+0x168。H

## 3. 任意 SGO

渲染器只认菜单 SGO（`animation_model` 菜单 CAS、`change_color`、`random_animation`、`soldier_weapon_slot`）。

> 2026-11-24 起改为**统一模型文件**（`docs/unified-model-re.md`）：每个支援兵种 / 机体只有一个文件，战斗键原样、另加菜单键，菜单读 `menu_animation_model`（0x93AD30 的钩子）。下面几条是当时的背景，单独的菜单 SGO 已删除。

- N601_* / EDF6VC_NPC_* 是 DSGO（`soldier_color`），不能直接用。M
- 翼人 / 重装 NPC 身体就是玩家模型 p606/p607：用原版 `MENUOBJECT/P606_WINGDIVER.SGO` / `P607_FENCER.SGO`。H
- 游骑兵 NPC 身体是 `n601_common_ranger`：复制 `MENUOBJECT/P505_RANGER.SGO`，只改 `animation_model[0]` 为 n601 的 mrab/mdb（同 EDF6ArmySoldier 骨架）。M
- 游骑兵队长只给 head（AF 队长 head+body）上色：可再生成按网格名写 `change_color` 的版本。

## 4. 方案 A（采用）：临时换单例显示内容

1. Hook 0x93F910：`slot-0x28 == armedAsync` 时改写 Desc 拷贝（路径、+0x20/+0x4C 兵种、两色、+0x44 哨兵 outfit、武器数、+0x50=0、武器）再调原函数。工作线程执行，override 加锁。
2. Hook OnMessage 0x93B320（或 vtable 槽 0x180FA78）记录 index 0 的 view，`armedAsync = view+0xD0`。
3. 打开 / 切换：设 override，主线程调 `0x8BDE80(0, cls, outfit)`；改色发 msg 5。打开时发 msg 8 锁住房间的 msg 6。
4. 关闭：解除 override，调 `0x8BDE80(0, GS+0x6E90, 0xD8700(GS,0))`，发 msg 9，恢复 view+0x3EC。

存档零写入（msg 3/5/6 只读 GS）。方案 B（第二个视图实例）不采用：构造时读 GS 第 index 槽（单人 slot 1 是脏数据）、两个全屏 3D 空间叠加、生命周期风险。

## 5. 武器

0x940100 经 `0xE1CF0(GS+0x130, id)` 取 WeaponTable 行加载，只认行号；运行时用 `0xE1BA0` 按名字查行号。AI 武器按模型对应：

| AI 武器 | WeaponTable 行 |
|---|---|
| AiSoldierRifle01 | aWeapon003 (3) |
| FlameThrower01 | aWeapon197 (201) |
| RocketLauncher01 | RocketLauncher01 (109) |
| Shotgun01 | aWeapon046 (46) |
| SniperRifle02 | aWeapon077 (77) |
| LaserLance01 | pWeapon001 (385) |
| LAZR01/02 | LAZR01 (430) |
| MNS01 | pWeapon131 (517) |
| IZNR01/02 | pWeapon100 (486) |
| TB01/02 | pWeapon102 (488) |
| HeavyCannon01 | hCannon01 (803) |
| MiddleCannon01、HeavyShotGUN01 | hWeapon077 (772) |
| PileBanker01 | hPileBanker01 (715) |
| Shield01 | hShield01 (747) |
| Shield01Elite | 无对应行，用 hShield01 |

Fencer 挂点 `[[1,3]→weapon_r]`、`[[0,2]→weapon_l]`，推测 weapons[0] 左手（盾）。L

## 6. 镜头

全屏 Ui3DSpace。`MENUMODELSETTING.camera_pos`：单人 [0]=(1.1,−0.9,−3.5)，双人 index 0 用 [1]、index 1 用 [2]（0x93F619）。视点 view+0x2B0 = +0x2C0→+0x2D0 与 +0x2E0→+0x2F0 按 +0x300/+0x308 插值；fov +0x260。菜单打开时把模型挪到右侧：每帧 Update 前给这四个点的 x 加偏移，关闭还原。M/L

## 6.1 载具预览（2026-11-10，测试站 #46，只做了离线验证）

> 2026-11-24：本节的「单独菜单 SGO `EDF6VC_MENU_V_<BODY>.SGO`」已换成统一文件 `OBJECT/EDF6VC_FLY_*.SGO`（战斗键 + 菜单键），战机 CAS 的小写 `main` 副本改名为 `MENUOBJECT/EDF6VC_MENU_V506_HELI.CAS`；坦克 / 机甲基础文件有别的工具分层写入，暂不统一、不预览。见 `docs/unified-model-re.md`。

- 同一条路（方案 A）：Desc 路径换成安装器生成的 `app:/MENUOBJECT/EDF6VC_MENU_V_<BODY>.SGO`（`tools/menu_preview.py build_vehicles`）：原版 `P505_RANGER.SGO` 其余键不动，`animation_model` 整个换成该机体基础 SGO（`support_loadout.BODIES`，Mods 里别的工具写的那份优先，否则 Root.cpk）的 `[模型, CAS, MAB]`。兵种号写游骑兵（只决定武器握持，载具没有武器），武器数 0。机体没装（基础 SGO 不在）就不生成，也不预览（日志一行）。
- 挂载不上模型：游戏里战机的挂载武器本来就没有挂架模型（`docs/stores-re.md` §1、§10：挂点上的武器是独立场景对象，只有发射出去的弹体有模型），菜单渲染器也只按 WeaponTable 行号挂玩家武器（§5）。所以挂载改动由菜单的行（挂点、重量、弹数）实时显示，模型不变。H（静态）。
- 镜头：载具比士兵大，四个机位点在加右移之后整体乘一个倍数（`VehicleScale`：坦克 3、战机 6，按尺寸估的 L，待实机调）；离开菜单逐字节还原。模型在原地每帧转 0.01 rad（`kVehicleTurn`），菜单键不再转它；转台开始时记下原来的朝向（`Turntable`），光标回到士兵或菜单关闭时还原，士兵和关菜单后玩家自己的模型仍朝原来的方向（菜单本来从不转模型）。
- **风险（L，需实机）**：菜单 SGO 的 `random_animation` 播 `_menu1/2/3`，`soldier_weapon_slot` 指 `weapon` 骨骼；载具 CAS 里没有这些动画、MAB 里没有这根骨骼。渲染器找不到动画 / 骨骼时是跳过还是出错没有静态核实；HQ / 房间里第一次把光标移到载具行时要盯日志与画面，出问题先把 `tools/menu_preview.py` 的载具文件撤掉（插件见文件缺失就不预览）。
- **已确认的崩溃（2026-11-21，测试站 #50，dump 核对）**：菜单渲染器播放 CAS 里**名字正好是 `main`** 的 anmgroup，查找时区分大小写。异步构建 `0x93F910` 在 `0x93FB85` 查 `L"main"`，在 `0x93FB95` 把下标存进 `model+0xE0`；视图更新 `0x93D640` 在 `0x93EBAC` 直接拿它去调 `0x93D2F0` → `0x11664F0`，不检查 -1。战机的 `v506_heli.cas` 只有 `Roter` / `Main`，所以下标是 -1，在 `EDF.dll+0x116650D` 读野指针（打开 F2 菜单约 20 ms 后崩溃，edx=0xFFFFFFFF）。修法：`build_vehicles` 给 CAS 里没有 `main` 的机体另生成 `MENUOBJECT/EDF6VC_MENU_V_<BODY>.CAS`（把那个 anmgroup 的名字原位改成小写，长度不变，并核对该名字字符串没被别的字段共用），菜单 SGO 的 `animation_model[1]` 指向它。机体自己的 CAS 不动，因为直升机 / 战机代码照原样查 `Main`。`tests/menu_preview_test.py` 断言每个菜单 SGO 引用的 CAS 都有 `main`。坦克的 `v505_tank.cas` 本来就有 `main`，照用原 CAS。如果状态名不存在，`0x93D2F0` 在 `0x93D31E` 会安全返回；改名之后 `_menu1/2/3` 的行为未实机验证。

## 6.2 摆放与机位（2026-11-24，静态逆向）

- **相机不动**（H）：Draw `0x93C370 / 0x93C3A0` 在 `+0x214 == 0`、`+0x3EC == 1` 或 `+0x30C == 0` 时跳过；否则每帧把相机对象 `+0xC0` 写成 fov（view `+0x260`，`MENUMODELSETTING.camera_fov` 0.6，竖直），`+0x80..+0xB0` 写成单位矩阵。Ui3DSpace 全屏、没有视口矩形。所以「把视图放进某个矩形」只能靠模型的世界矩阵。
- **更新 `0x93CEC0`**（H）：yaw `+0x2A4 += 输入 × 0.025`；缩放 `+0x308 += 输入 × 0.01`，夹 [0,1]；`+0x304` 是 msg 7 的选中态：0 时 `+0x300 -= 4·dt`，1 时 `+= 4·dt`，2 时 `+0x30C = 0`（`0x93CF1F` 每帧先写 1 再判断，所以 `+0x30C` 只由 `+0x304` 决定）。`+0x2B0 = lerp(lerp(+0x2C0,+0x2D0,+0x300), lerp(+0x2E0,+0x2F0,+0x300), +0x308)`；然后 `+0x3EC` 2→0、3→1。
- **msg 7**（`0x93C1C7`，H）：`{0}` → `+0x304 = 0`；`{1, player}` → 是本视图的玩家（`+0x2AC`）则 1，否则 2（隐藏）。
- **模型矩阵**（H）：`0x93D073` 用 `cosf`（IAT `0x12DA8B2`）/ `sinf`（`0x12DA8B8`）按 `+0x2A4` 造行向量矩阵 row0 (c,0,−s,0)、row1 (0,1,0,0)、row2 (s,0,c,0)、row3 = `+0x2B0`，在 **`0x93D1A1` `call 0x1100B90`** 交给 SetWorldMatrix（rcx = `[[view+0x120]+0x88]+0xA0`，rdx = 矩阵）。`0x1100B90` 用 `movaps` 拷四行（矩阵须 16 字节对齐）、置 `+0xB0` 脏标记，再跳 `0x1100010`。
- **相机初始化 `0x93F4F0`**（M）：`+0x2B0 / +0x2C0 = camera_pos`（单人 [0]；双人按 `+0x2AC` 取 [1] / [2]），`+0x2E0 = pos + normalize(pos.xz) × camera_move_length(2.0)`。投影核对：(1.1, −0.9, −3.5) 在 16:9、fov 0.6 下 NDC x ≈ 0.57，与 HQ 里士兵在右侧一致（`tests/preview_camera_test.cpp` 断言）。
- **实现**（`src/preview_camera.h` 纯计算 + `room_preview.cpp`）：`SetViewPlacement(rect, camera)` 后，`0x93D1A1` 的调用被改到 `WorldMatrix`：对象是本视图的才换成 `Place()` 的矩阵（模型绕中心先 yaw 后 pitch，中心投影到矩形中心，距离使模型尺寸填满矩形高度 / `distance`），否则原样转发。预设：自由（跟随原版转台 yaw，玩家输入照常转）/ 正面 / 侧面 / 俯视（pitch 1.2）/ 特写（距离 0.5，瞄准点抬高 0.3 倍身高）。屏幕宽高比取本进程可见顶层窗口的客户区（M：假定交换链填满客户区）。
- **绘制顺序**（M）：模型在 3D 场景里画，UI 总在本帧 3D 之后（`docs/worldmap-select-re.md`，H）。整备界面盖在 HQ 上时，皮肤要给矩形**留洞**；视图要继续画，需 `+0x304 ≠ 2` 且 `+0x3EC ≠ 1`：摆放期间每帧把 2 改 0、1 改 0，清除摆放时还原进入时的值（锁住期间 `+0x3EC` 由 msg 8/9 那套负责）。
- **正面朝向**（L）：yaw 0 时模型朝 +Z（朝相机）是按原版默认画面推的，未实机核对；不对就改 `kCameraPresets` 的 yaw。
- **msg 4**（`0x93B895`，H）：`{player, slot, row, 0}`，player 须等于 `+0x2AC`；`+0x368 = slot`、`+0x36C + slot*4 = row`，再按 `+0x318` 的 Desc 重建。`ShowPlayerWeapon(cls, slot, row)` 只在视图显示的是玩家自己的模型、且显示兵种（`+0x180`）等于 `cls` 时，于下一次视图更新里发送；slot 限 0..3（M：数组到 `+0x3EC` 前最多 32 项，实际槽数未逐兵种核对）。

## 7. 需实机验证

- OnUpdate 中调 0x8BDE80 / 广播是否安全；
- n601 模型配菜单 CAS 动画；
- 哨兵 outfit 对 msg 5/6 判定的影响；
- `change_color` 网格选择 0 是否覆盖所有网格；
- Fencer 选中槽 0 时左右手实际武器；
- 镜头偏移方向；msg 10 的取值含义；
- 6.2 的摆放：矩阵钩子画面、正面朝向、宽高比、整备界面盖住时视图是否继续画、msg 4 换武器；
- 关闭画面时工作线程 0x93F910 与析构竞争（关闭前解除 override 并等 Async+0x9C）；
- 离线双人分屏只改 index 0。
