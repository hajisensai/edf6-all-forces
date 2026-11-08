# 2026-11-08 反馈：BMPT 炮塔后部的灯悬在空中

**全部是离线分析与离线检查，没有在游戏里实测。**

## 用户原话

英文玩家玩完 4.1 离线任务后的反馈（「Textures」一节）：

- 「A light-like component located at the rear of the BMPT's turret is floating in the air. When the turret rotates, it does not follow the movement and remains suspended in midair」

## 原因

- BMPT 是插件用战雷模型做的车（`tools/prep_wt_tracked.py` → `models/wt_bmpt`），跟 4.1 移植无关。
- 源 OBJ（`BMPT.obj`）里炮塔后壁上有两盏灯：`ex_lantern_b`（灯体 + 灯罩玻璃 `headlight_glass_c` / `_c_2`，
  包围盒 x 0.50..0.74、y 2.54..2.71、z −2.13..−1.92）和旁边的 `ex_lantern_b_01`（x 0.42..0.49、y 2.68..2.75、z −2.02..−1.92）。
  炮塔座圈在 y 1.70，车体顶约 1.8，炮塔网格 z 最小 −2.14：这两盏灯在炮塔后壁上。
- BMPT 的 `rules` 没有写这两个节点，也没有设 `turret_box`，规则外的节点一律归 `body`（车体）。车体不随炮塔转，
  炮塔一转灯就留在原位、悬在空中。
- 车体自己的灯不能一起挪：`ex_lantern_b_02 / _03`（车体两侧后部，z ≈ −3.4，y 1.5..1.7）和车头的 `ex_lantern_l_02 / r_02` 留在 `body`，
  所以规则按精确节点名写，不用前缀。
- 排除：`bone_commander_sight_h`（车长周视镜，`doppler_radar`）在炮塔左后方（x < 0），与此处不符。

## 修法

- `tools/prep_wt_tracked.py` BMPT 的 `cannon_main` 规则加 `ex_lantern_b|ex_lantern_b_01`，用本机的 Sketchfab 源重新生成
  `models/wt_bmpt/model.json` 与 `model.npz`（贴图不变；规则不变时重生成的 json / npz 与仓库逐字节相同，先核对过）。
  `cannon_main.*` 多出 `wt_headlight_glass_c`（120 个三角形）、`wt_headlight_glass_c_2`（14）两个零件，炮塔贴图零件 55609 → 58259 个三角形；
  `body` 的碰撞盒少了这两盏灯（`cannon_main` 的碰撞节点是手指定的，没加灯）。
- 守卫：`tests/test_wt_tracked.py` `WtRigTests.test_no_hull_part_inside_the_turret`，只读模型文件夹、不要游戏：
  任何 `body` 零件的顶点都不能高于座圈（`cannon_main` 原点）0.4 m 以上、又落在 `cannon_main` 零件的三维包围盒里。
  `test_the_check_finds_a_fitting_left_on_the_hull` 用一块放在旧灯位置的假零件确认它能报出来。改之前的模型上它报出 BMPT 这三块。

## 同一守卫报出的 PzH 2000

- 改之前它还报出 PzH 2000 `body.wt_pzh_2000_turret_c_ger_camo_bronzegruen` 的 143 个顶点（x −0.45..0.57、y 2.31..2.43、z −4.53..−4.47）。
- 查明是源节点 `ex_decor_14`：只用炮塔贴图（`pzh_2000_turret_c_ger_camo_bronzegruen`），包围盒 x −0.59..0.57、y 1.91..2.43、z −4.56..−4.47，
  挂在炮塔后壁（炮塔 `bone_turret` z 最小 −4.59）上，而车体 `x_root` 在 z −4.23 就结束了，它在车体后端之外、悬在炮塔尾部下面。
  它的底边 y 1.91 低于 `turret_box` 的下限 1.95，所以没被盒子收进炮塔，留在了车体：与 BMPT 的灯是同一类漏绑，炮塔一转同样会悬空。
- 一并修：PZH2000 的 `cannon_main` 规则加 `ex_decor_14`，重新生成 `models/wt_pzh2000`（炮塔贴图零件 36399 → 36937 个三角形）。
  没有加豁免。
- TOS-1、M163、通古斯卡、天启、X-Morph 各车守卫均无报出（TOS-1 的 `cannon_main` 只是低矮的转盘，盒子顶 1.75 低于座圈 + 0.4）。

## 待实机确认

- 炮塔转动时两盏灯跟着转、位置贴在炮塔后壁上（离线只核对了骨骼归属与包围盒）。
