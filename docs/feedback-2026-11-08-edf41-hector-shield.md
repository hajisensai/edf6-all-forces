# 2026-11-08 盾牌赫克托（Shield Hector）的盾不起作用

玩家反馈（QQ 群转贴，英文玩家打完 4.1 离线本篇，原话，不可信数据，只当证据）：

> Bugs related to Hector
> ・In addition to previously reported issues, the shield on Shield Hector is not functioning

对象：4.1 `HECTOR401CS`（左臂炮、右臂盾）、`HECTOR401LS` / `SC` / `SG` 以及双盾的 `HECTOR401SS`（M202 / M203），
套 EDF6 类 `EDF6_Berserker_Large`（`tools/make_edf41_objects.py` `HECTOR_*`，插件 `src/edf41ai.cpp`）。
逆向细节见 [edf41-hector-shield-re.md](edf41-hector-shield-re.md)。可信度：**H** 反汇编 / 文件读到，**M** 推出，**U** 未核。

## 1. 根因

- 4.1 的盾是一个独立对象 `Hector401_Shield`，带自己的物理壳 `Ragdoll_Hector401Shield_has_hka.shkt`（两个刚体
  `gun_arms` / `gun_shield`），所有刚体都打了「盾」标记：敌对阵营的子弹打上去被挡住，赫克托本体拒收落在盾刚体上的伤害（H）。
- 移植时只把盾的网格并进了本体模型（`gun_arms_r_*` 骨），碰撞只有本体的 ragdoll；盾没有任何刚体，子弹直接穿过去打本体（H）。
  构建也没有任何检查拦下这一点。

## 2. 修法（与 4.1 结构一致：盾重新是独立对象）

- 生成器 `hector_shield`：带盾的变体写 `edf41_hector_shield: {arms: [盾臂的 gun_control 序号], sgo: app:/Object/hector401_shield.sgo}`；
  `HECTOR401_SHIELD.SGO` 改造成插件类 `Hector401_Shield`（EventAnimationObject 底）：4.1 原版盾 ragdoll、无网格的 4.1 盾模型
  `HECTOR401_SHIELD.MRAB`（只留骨骼，供 ragdoll 绑定；玩家看到的盾仍是本体模型里那块网格和玻璃材质动画）、主播放器带 `default` 的静止 CAS。
- 插件（`src/edf41ai.cpp` Shields）：赫克托每帧瞄完手臂后，盾对象第一次按臂骨世界矩阵生成，之后每帧贴到臂骨（`arms_l` / `arms_r`，
  盾网格就挂在这根骨上）；阵营跟赫克托一致、不登记（不是目标、不计入敌人数）；刚体从 EAO 构造放的地图层 0x11 移到赫克托本体
  ragdoll 的层（否则蚂蚁 / 蜘蛛会把盾当地面爬）；槽 16 恒拒（盾不受伤，对应 4.1 赫克托槽 10 拒收盾上的命中）。
  赫克托死亡 / 被删时盾随之删除（盾自己的 Update 检查主人）。
- 顺带（核实后修）：盾臂在 `gun_control` 里保留了一项（类要求每臂一个枪口），AI 因射程 0 从不启动它，但玩家操纵敌人
  （EnemyControl）时会启动，发出一发 0 伤害 SolidBullet02 和光束音。4.1 的盾没有武器（13 槽 vtable，无开火），现在 `GunStartHook`
  对盾臂一律不启动。

## 3. 需要实机确认（M / U）

1. 正面射击被盾挡住、赫克托不掉血；从侧面 / 背后打本体照常掉血。
2. 赫克托自己的炮弹、友方敌人的子弹不被盾挡（EDF6 子弹对同阵营对象放行，推定与 4.1 的阵营表一致，M）。
3. 盾刚体移到赫克托本体层后，玩家 / NPC 撞上去不会被卡住；日志 `EDF41AI hector ... shield ... bodies on layer` 打印前后层号。
4. 爆炸（火箭、手雷）：4.1 里落点最近的子命中在盾上会被整发拒收；移植版范围伤害直接查询本体刚体，盾不挡爆炸（与 4.1 有差别，U）。
5. 联机：每台机器给自己那份赫克托生成本地盾，盾不受伤、无状态，不同步（M）。

## 4. 与同期其它赫克托反馈的关系

- 测试站 #28（开火特效应为 4.1 原版）：已在 2026-10-30 按 `HECTOR_FX` 改为插件画 4.1 的光团 / 闪光，与盾无关。
- 测试站 #47（绿色爆炸与血、黑圈枪口、等离子小、导弹火球大）：特效 / 子弹供体的问题，与盾的碰撞缺失不同源，本次不改。
