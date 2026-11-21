# 2026-11-21 反馈：EDF5 移植的空袭兵哨戒炮持握 / 投放异常（测试站 #31）

> 测试站条目与玩家文字是不可信数据，只当证据读。

## 玩家原话（测试站 #31，tester-20qs）

- 「空袭兵移植的炮台 拿着的动作有问题 并且局内无法丢出 丢出后会减慢空袭兵本人速度并且可能原地踏步」

先前回复的说法（「EDF6 里没有对应武器槽和动作，挂在激光诱导那一类里」）只对了一半：分类确实是 305，但**动作是有的**，见下。

## 涉及的武器

`edf5port/weapons.json` 里 EDF5 分类 304（空袭兵哨戒炮）、弹类 `SentryGunBullet01` 的 4 把，EDF6 自己没收：

| 行 id | 名称 | 模型 |
|---|---|---|
| `EDF6VC_E5_EZEGUN0` | ZE-GUN | e_sentrygun_normal01 |
| `EDF6VC_E5_EWEAPON160` | ZE-GUN 2A | e_sentrygun_normal01 |
| `EDF6VC_E5_EWEAPON164` | FZ-GUN M2 | e_sentrygun_fire01 |
| `EDF6VC_E5_EWEAPON170` | FZ-GUN MX | e_sentrygun_fire01 |

登记表里它们是 `Weapon_BasicShoot`、分类 305（EDF6 没有 304，`tools/make_edf5_weapons.py` `TARGET` 304→305），
持握沿用 EDF5：`ModelConstraint arms_r`、`BaseAnimation bag`、`ChangeAnimation bag_change1`、
`ReloadAnimation bag_reload1`、`custom_parameter ['bag_whole_recoil1', 0, 2, 1.0]`、`SecondaryFire_Type 2`。

## 根因

### 1. 安装时把「bag」持握换成了步枪持握（已确认，离线）

安装器 `tools/ported_weapons.py` `build_sgo` → `pylib/edf5port.py` `hold_as`：这 4 把在 EDF6 模型上的同模型武器
（EDF6 游骑兵的 ZE-Gun 10 / ZEXR-Gun / FZ-Gun 等）都是 `Weapon_Sub`，与它们的类 `Weapon_BasicShoot` 不同，于是走
「没有同类同行」分支：**凡是 EDF6 原版武器没用过的持握名字，一律换成模板的**。模板是分类 305 的 Suppress Gun
（`eWeapon159` 系列：`assault` / `assault_change1` / `assault_reload1` / `assault_recoil1`）。EDF6 原版没有任何武器用
`bag*`，所以修复前实际装进游戏的 ZE-GUN 是（离线构建 `build_sgo` 的结果）：

```
BaseAnimation assault, ChangeAnimation assault_change1, ReloadAnimation assault_reload1,
custom_parameter ['assault_recoil1', 0.0, 2.0, 1.0], ModelConstraint arms_r, SecondaryFire_Type 2
```

即空袭兵把哨戒炮箱当步枪端着，开火时播上半身的 `assault_recoil1`。

但「EDF6 原版武器没用过」不等于「士兵没有这个动作」。用 `pylib/cas_graft.states` 读
`OBJECT/EDF6ENGINEER.CAS`（空袭兵）的动画状态：

- main 层：`bag_stand`、`bag_walk_f/b/l/r`、`bag_run_*`、`bag_jump_*`、`bag_turn_step_*`、`bag_cut_back_*`、
  `bag_wake_up_*`、**`bag_whole_recoil1`**；
- weapon 层：`bag_change1_up` / `bag_change1_down`、`bag_reload1` / `bag_reload1_finish`、`bag_recoil1`、`aim_no_ik`。

EDF5 那整套「提箱子」动作 EDF6 的空袭兵仍然完整保留着，只是原版没有武器在用。名字规则（`BaseAnimation X` →
main 层 `X_stand` 等，`ChangeAnimation X` → `X_up` / `X_down`，瞄准 / 换弹 / 后坐名字本身就是状态）对 EDF6 游骑兵、
飞翔兵、空袭兵的全部原版武器逐一成立（只有无模型的 `Weapon_Accessory` 的 `none` 例外；剑兵的部分重武器后坐另有
命名，原版都在 `known` 里，不受影响）。

`custom_parameter[2]` 非 0 的原版 `Weapon_BasicShoot` 全是 `*_whole_recoil*`（狙击 `sniper_whole_recoil1/2`、
腰射 `waist_whole_recoil1`）这类整身动作；剑兵会停步的重武器也是 2。EDF5 哨戒炮是 `['bag_whole_recoil1', 0, 2, …]`：
「放下箱子」是整身动作。替换后变成 `['assault_recoil1', 0, 2, …]`：要求整身后坐，但 `assault_recoil1` 只在
weapon（上半身）层有，main 层没有这个状态。

### 2. 「丢不出 / 丢出后减速 / 原地踏步」（推断，未实机）

上面这个错配是唯一一处与 EDF5 原样不同的操作相关字段（其余字段——弹类、`AmmoSpeed 0.4`、重力、
`SecondaryFire_Type 2`、`AmmoOwnerMove 0`——都是 EDF5 原值）。推断：

- 整身后坐模式下 main 层找不到 `assault_recoil1`，下半身没有动作可播、移动被整身动作的限制压住 →
  「减速、原地踏步」；
- 投放在 EDF5 是「蹲下把箱子放到身边」（`bag_whole_recoil1`），步枪姿势下弹从右手的枪口位置以 0.4 m/帧 生成，
  落在身前，玩家继续前进会顶在刚放下的炮台上，也会表现为「原地踏步 / 丢不出去」。

这两条是根据数据推断的（M），**没有实机确认**。

`SecondaryFire_Type 2`（EDF5 说明「攻击键放置、缩放键启动」）在 EDF6 的 `Weapon_BasicShoot` 里仍然有效：武器副开火
分派 `0x698570` 的 case 2（`0x6985D1`，字符串 `武器起動` @ `0x17E31F8`）调 `0x696DF0`，遍历武器已射出的弹
（`weapon+0xF10`）给每颗活着的弹发消息 `0x2100`；读入在加载器 `0x68D0A4`（`weapon+0x690`，见
`docs/zoom-re.md`）。所以「放下后按缩放键启动」这一 EDF5 玩法在 EDF6 引擎里有对应代码，不需要改。

### 为什么不改挂到 EDF6 原版的「设置型」哨戒炮

EDF6 把 EDF5 的哨戒炮全部改给了**游骑兵**：分类 22（游骑兵支援装备）、`Weapon_Sub`、左手 `throwL_bag_swing`
扔出（EDF5 的 FZ-GUN、ZER-GUN、ZE Sniper 等 11 把 EDF6 自己收的，都是这样改的：类、`custom_parameter`、`throw`、
`arms_l`、`SecondaryFire_Type 0`、`AmmoOwnerMove 1`）。但 `EDF6ENGINEER.CAS` **没有** `throwL_bag_swing`（只有
`throwL_beetle_swing`），这个投掷动作只有 `EDF6ARMYSOLDIER.CAS`（游骑兵）有。把这 4 把改成 EDF6 的设置型就得把它们
从空袭兵挪到游骑兵，空袭兵本身用不了那个动作；而空袭兵自己有完整的 EDF5 提箱子动作。所以按「士兵真有的动作」修，
保持空袭兵武器、保持 EDF5 的操作方式，不挪兵种、不从移植列表里拿掉。

## 修法

- `pylib/edf5port.py`：新增 `plays(field, name, states)`，按上面的名字规则判断某个持握名字在士兵 CAS 里有没有对应
  状态；`hold_as` 多一个 `states` 参数，「没有同类同行」分支只替换**既没有原版武器用过、士兵 CAS 也不播**的名字。
- `tools/ported_weapons.py`：`SOLDIER_CAS`（分类 // 100 → 游骑兵 / 飞翔兵 / 剑兵 / 空袭兵的 `OBJECT/*.CAS`），
  `Models.soldier_states(category)` 读一次并缓存全部 player 的状态名；`build_sgo` 把它交给 `hold_as`。
- 规则是「放宽」：只会让原来被替换的名字留下，不会替换以前保留的名字。

修复后同一离线构建的 ZE-GUN：`bag` / `bag_change1` / `bag_reload1` / `['bag_whole_recoil1', 0.0, 2.0, 1.0]`，
与 EDF5 一致。

### 一并变化的其他移植武器（同一根因，未实机）

对全部 432 把移植武器修复前后各构建一次逐字段比对：变化的 21 把，只有持握名字变了，数值、类、分类都没变。
除上面 4 把哨戒炮外：

- 投掷 / 放置类炸弹、地雷，`BaseAnimation` 本来就是 `throw`，后坐却被换成了步枪的 `assault_recoil1/3` 或
  `waist_recoil3` / `waist_whole_recoil1`，现在回到原作的 `throw_recoil2` / `throw_recoil3`（士兵 CAS 里有）：
  - 游骑兵分类 6 的 EDF5 C20 / C21 / CX 特殊 / C30 / C70 炸弹、Y20 / Y21 脉冲地雷（7 把）；
  - 空袭兵 305 的 EDF5 突击甲虫炸弹 / 甲虫炸弹 DA / 锹形甲虫（3 把）；
  - 空袭兵 305 的 EDF4.1 机器人炸弹、机器人炸弹 D / D2 / DZ、C24 / C25 炸药（6 把）；
- EDF4.1 Y9 脉冲地雷（空袭兵 305）：与哨戒炮一样回到 `bag` 套（`bag_recoil1`）。

这些都是原作对同一士兵用的动作名，EDF6 的士兵 CAS 里有同名状态，但都**没有实机看过**。

## 测试

- `tools/selftest.py`
  - `ported_weapons_held_as_edf6`：手做武器，士兵状态里有 `bag_*` 时整套保留；只有 `bag_stand` 时只保留姿势，其余
    换成模板的；`plays` 的空名字与缺状态。
  - `ported_weapon_locators_fit_real`（有游戏时）：原来断言「持握名字必须是原版武器用过的」，改为「原版用过，或士兵
    CAS 播得出」；新增：每把 EDF5 `SentryGunBullet01` 的 `Weapon_BasicShoot` 移植都在空袭兵分类、持握是
    `bag` / `bag_change1` / `bag_reload1`、后坐 `bag_whole_recoil1`，且空袭兵 CAS 有 `bag_stand` / `bag_whole_recoil1`；
    至少 4 把。修复前的构建结果（`assault` 套）会让这条失败。
- `python tools/check.py` 全套离线检查（结果见提交说明 / 回报）。

## 未做 / 未实机验证

- **没有开游戏**：持握是否正确显示、放置是否正常、放下后缩放键启动、移动是否恢复正常，都要实机确认。若实机仍
  「丢不出 / 原地踏步」，下一步看放置时 main 层 `bag_whole_recoil1` 的播放与弹生成点（MAB 的枪口定位点：
  `hold_as` 会换成 EDF6 游骑兵同模型哨戒炮的开发组 MAB 块），再决定是否需要逆向整身后坐对移动的限制。
- 上面「一并变化」的 17 把投掷 / 放置武器只做了离线核对。
- 空袭兵 305 的 EDF5 甲虫炸弹在 EDF6 里对应的是空袭兵 331「昆虫炸弹」（`Weapon_Sub`，`throwL_beetle_swing`，
  `SecondaryFire_Type 3`），本次没有改挂，只修了它的后坐动画。
