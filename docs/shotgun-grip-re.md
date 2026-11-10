# 游骑兵霰弹枪的握持（测试站 #32）

用户原话与理解见 `docs/feedback-2026-11-10-shotgun-grip.md`。证据级别：H = 直接读 Root.cpk / 登记表数据得到；M = 由数据推断、未实机看；L = 猜测。

## 1. 握持由什么决定

- **姿势（手臂怎么摆）**：武器 SGO 里 5 个名字（`pylib/ranger_hold.py` `FIELDS`，与 `edf5port.HOLD_NAMES` 同一组）：`BaseAnimation`（常态姿势）、`AimAnimation`、`ChangeAnimation`、`ReloadAnimation`、`custom_parameter[0]`（后坐动画；`Weapon_BasicShoot` / `Weapon_BasicSemiAuto` 都在这一位）。[H：字段名与取值来自 Root.cpk 的武器 SGO；作用为 M，名字即动画片段名]
- **左手握在哪**：武器 SGO 的 `animation_model[2]`（MAB 块）里的 `IK` 定位点，挂在武器模型的骨骼上。[H]
- 插件自己的第一人称手臂（`FirstPersonArmIk`）只在第一人称模式下重摆手臂，不决定第三人称姿势，不改。[H：`src/` 里没有按武器类别区分霰弹枪握持的代码]

## 2. 原版数据（Root.cpk，`WEAPON/WEAPONTABLE.SGO` 的 `table`，第 3 列是类别）[H]

类别 0（突击步枪，54 把有完整 5 名）全部 `assault` 姿势：

| 把数 | 握持 5 名 |
|---|---|
| 38 | assault, aim, assault_change1, assault_reload1, assault_recoil1 |
| 12 | assault, aim, assault_change1, assault_reload2, assault_recoil1 |
| 2 | assault, aim, assault_change1, assault_reload2, assault_recoil2 |
| 2 | assault, aim, assault_change1, assault_reload1, assault_recoil2 |

类别 1（霰弹枪，38 把）：

| 把数 | 握持 5 名 |
|---|---|
| 14 | waist, aim, waist_change1, waist_reload1, waist_recoil2 |
| 8 | waist, aim, waist_change1, waist_reload3, waist_recoil1 |
| 7 | waist, aim, waist_change1, waist_reload1, waist_recoil1 |
| 6 | waist, aim, waist_change1, waist_reload2, waist_recoil2 |
| 3 | **assault, aim, assault_change1, assault_reload2, waist_recoil1** |

`waist` 是腰际持枪、左前臂上折托护木，即用户说的「左手臂 90 度弯曲」[M]。最后 3 把（Destruction Blazer / Destruction Blazer G / Burst Blazer）是开发组自己按步枪姿势握的霰弹枪：步枪的常态 / 换枪 / 换弹动作，后坐仍用霰弹枪的 `waist_recoil1`。[H]

## 3. 做法

- 取「开发组按步枪握的霰弹枪」那一组 5 名（`ranger_hold.rifle_like_set`：步枪最多的姿势 → 该姿势下霰弹枪最多的一组，安装时从游戏的表算，不写死），给所有游骑兵霰弹枪；只换这 5 个名字（`custom_parameter` 只换第 0 位），数值、MAB（握把位置）、其余参数一概不动。
- **原版霰弹枪**：新工具 `tools/make_stock_holds.py`（归属账本 owner `stockholds`），安装器写 `Mods/WEAPON/<原版霰弹枪>.SGO`（35 个文件：38 把减去已是该组的 3 把），武器表不动；卸载 / `--remove` 释放后原版文件回来。别的 mod 已经放了同名文件时跳过并提示。
- **移植霰弹枪**（EDF5 / EDF4.1 登记表里类别 1 的 24 把）：`tools/ported_weapons.py` `build_sgo` 在 `hold_as` / `fit_locators` 之后同样换成这一组。
- **红色总督（`s_shotgun_governor02`）**：EDF6 没有武器用这个模型，以前 `hold_as` 找不到同模型的 EDF6 武器，握把定位点只按骨骼名挪。模型节点是 `mdl / s_shotgun_governor02 / body / grip / s_shotgun_governor01`，即 governor01 的网格换贴图，body / grip 骨骼与 governor01 同位 [H：模型骨骼；同位为 M，按局部矩阵相同推断]。`Models.kin` 认出这层关系，`peers` 改用 governor01 上的 EDF6 武器（暴雨等），于是 4 把红色总督的 MAB 块与暴雨（`aWeapon057`）逐字节相同，握持 5 名也与改后的暴雨相同。[H：selftest `ported_weapon_locators_fit_real` 断言]
- 联机：只改动画名，不改数值与武器表行号，房间协议不变。没装这一版的成员看到的别人的兵仍是原版姿势，只是外观差别。

## 4. 离线覆盖（`tools/selftest.py`）

- `ranger_hold_rifle_like`：造的表上，donor 选择、只改 5 名、具名 `custom_parameter` 不动、无 donor 报错。
- `stock_holds_build`（有游戏时）：donor 就是 Blazer 那组；35 个原版霰弹枪文件只改 5 名；暴雨在内；已是该组的 3 把不写。
- `stock_holds_wired`：账本 owner、安装 / 卸载、打包、移植路径、文档。
- `ported_weapon_locators_fit_real`（有游戏时）：每把移植霰弹枪是这一组；红色总督 MAB == 暴雨 MAB。
- `pack_install_upgrade_uninstall`：安装写入、卸载删掉。

## 5. 待实机确认

- 第三人称：游骑兵和 NPC 拿霰弹枪是否为步枪姿势、左手是否握在护木上（特别是 MAB 与步枪动作搭配时左手位置，原 `waist` 系霰弹枪的 IK 点是按腰际姿势放的）[M]。
- `reload3`（逐发装填一类）换成 `assault_reload2` 后，换弹动作时长与武器换弹时间的匹配。
- 红色总督与暴雨实际看起来握法一致。
