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

## 7. 需实机验证

- OnUpdate 中调 0x8BDE80 / 广播是否安全；
- n601 模型配菜单 CAS 动画；
- 哨兵 outfit 对 msg 5/6 判定的影响；
- `change_color` 网格选择 0 是否覆盖所有网格；
- Fencer 选中槽 0 时左右手实际武器；
- 镜头偏移方向；msg 10 的取值含义；
- 关闭画面时工作线程 0x93F910 与析构竞争（关闭前解除 override 并等 Async+0x9C）；
- 离线双人分屏只改 index 0。
