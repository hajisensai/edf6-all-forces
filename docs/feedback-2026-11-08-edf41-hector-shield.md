# 2026-11-08 盾牌赫克托（Shield Hector）的盾不起作用

玩家反馈（QQ 群转贴，英文玩家打完 4.1 离线本篇，原话，不可信数据，只当证据）：

> Bugs related to Hector
> ・In addition to previously reported issues, the shield on Shield Hector is not functioning

对象：4.1 `HECTOR401CS`（左臂炮、右臂盾）以及双盾的 M202 / M203 变体，套 EDF6 类 `EDF6_Berserker_Large`（`tools/make_edf41_objects.py`
`HECTOR_*`，插件 `src/edf41ai.cpp`）。地址：EDF41.exe 的 RVA，静态逆向，没开游戏。可信度：**H** 反汇编 / 文件读到，**M** 推出，**U** 未核。

**本次没有修，只记录查到的事实和缺口。**

## 1. 现状（事实）

- 4.1 的赫克托把每件武器做成独立对象：`hector_weapon` 每项 `[父骨, 挂点骨, 序号, 武器 SGO]`，盾是
  `app:/object/Hector401_Shield.sgo`（H，读 4.1 SGO）。盾 SGO 有自己的 `animation_model`（`Hector401Shield.mdb`）、CAS、MAB、材质动画和
  **自己的 ragdoll** `Ragdoll_Hector401Shield_has_hka.shkt`；没有 `durability` 一类的耐久键（H）。4.1 的炮 / 加特林 / 导弹也都各有 ragdoll（H）。
- 移植版把武器网格并进本体模型（`gun_arms_*` 骨），碰撞只有本体的 `Ragdoll_Hector401_has_hka.shkt`；`edf41port/objects` 里没有盾的 SHKT（H）。
  所以盾在移植版里只是外观，子弹直接穿过去打本体或打不到——这就是「盾不起作用」。
- 盾那一侧的 `gun_control` 项保留（EDF6 类每个武器槽要一个枪口），伤害系数 0（`make_edf41_objects.py` `w.bullet == 'none'`）。
  分诊时注意到这一项仍是一发 SolidBullet02、带 `バーサーカーlargeビーム発射` 发射音（U，未核实是否真的会播）。

## 2. 4.1 侧已读到的（逆向）

- 工厂：盾类名取值 `0x3CB300`，创建 `0x3CB310`（分配 0x6C0，构造 `0x22A080`，vtable `0xA98C10`）；炮 `0x3CB210` / `0x3CB220` → `0x227450`（0x8E0）（H）。
- 武器公共基类构造 `0x22A400`：调 `0x56B620`（对象基类），`+0x130` 上建 `animation_model`（`0x320910`），读 `hector_weapon_range`，
  `0x56B930` 传字节 7（含义未明，U），按名字找 `main` 播放器；**构造里没有读 `ragdoll` 键**（H）。
- 赫克托本体读 `hector_weapon`（`0x21DE06`）后逐项创建武器，`0x22A740` 把武器挂到父对象（参数：父对象、父骨架 = 模型 + 0xA0、序号）（H）。
- EDF41.exe 里 `ragdoll` 键的代码引用只有 `0x2E7231` / `0x2E728D`（与 `vehicle_weapon` 相邻的通用读取）；盾的 SHKT 由哪条路径建成刚体、
  在哪个碰撞层、子弹打上去是挡住还是转伤害，**没查到**（U）。

## 3. 没做的（需要继续）

1. 逆向补完：4.1 武器对象的 ragdoll 由谁创建（`0x2E7200` 一带的调用方是否包括 `0x22A400` / `0x22A740` 链）、所在层、命中处理（挡弹不扣血？）。
2. 按逆向结论实现（分诊方案 (a)）：生成器把 `Hector401_Shield` 的 SGO / MDB / SHKT 作为独立对象发货；插件给每个带盾的变体建一个
   EventAnimationObject 子对象（盾模型 + 盾 SHKT），每帧贴到 `gun_arms_r` / 对应臂骨的世界矩阵上，伤害过滤（槽 16）恒假，碰撞层与 4.1 一致；
   先写离线检查（盾 SHKT 已发货、子对象挂点骨存在于本体骨架），再实机验证挡弹。
3. 核实盾那一侧 `gun_control` 是否真的会开火 / 出声，会的话一并去掉（只留类需要的枪口）。
4. 与 #47（赫克托特效 / 数值异常：绿色爆炸与血、黑圈枪口、等离子小、导弹火球大）不同源：那是 `HECTOR_FX` / 子弹供体的问题，盾是碰撞缺失。
