# EDF4.1 赫克托的盾（Hector401_Shield）逆向

EDF41.exe（Steam 版 4.1，x64，ImageBase 0x140000000）的 RVA，静态反汇编（capstone），没开游戏。对照 EDF6 的 EDF.dll
（TimeDateStamp 0x678CCB46）RVA。可信度：**H** 反汇编 / 文件读到，**M** 推出，**U** 未核。
用途：`tools/make_edf41_objects.py` `hector_shield`、`src/edf41ai.cpp` Shields；反馈见 [feedback-2026-11-08-edf41-hector-shield.md](feedback-2026-11-08-edf41-hector-shield.md)。

## 1. 对象与类（H）

- RTTI：`Hector`（vtable 0xA987E0，28 槽）、`Hector_WeaponBase`（0xA98C80）、`Hector301_Cannon / _Gatling / _Launcher`、
  `Hector401_Cannon`（0xA98A78）/ `_Gatling` / `_Launcher` / `Hector401_Shield`（0xA98C10）。
- 盾：工厂 0x3CB310，分配 0x6C0，构造 0x22A080：先调武器公共基类构造 0x22A400（`+0x10C = 0`、`+0x110 = -1`），再写 vtable，
  `+0x10C = 1`（只有盾这样设），0x22A1E0 建自己的 CAS 播放器。
- 盾 vtable 只有 13 槽，与加农炮比：槽 2（Update 0x22A2F0 → 公共 0x22A9F0：ragdoll 跟随 0x334410 / 0x334580、模型矩阵取 `+0x60`）、
  槽 9 / 12（析构 0x22A160 / 状态复位 0x22A010）不同，**没有开火槽**（加农炮槽 10 0x227980，盾为空函数 0x8E1E0），也没有伤害处理。
- 槽 1 0x22A900：`+0x110 = edx`（SetTeam：盾的阵营）。

## 2. 挂接与物理壳（H）

- 赫克托读 `hector_weapon`（0x21DE06），每项 `[父骨, 挂点骨, 序号, 武器 SGO]`（HECTOR401CS：`['arms_r', 'shoulder_r1', 1,
  'app:/object/Hector401_Shield.sgo']`），逐项创建武器，0x22A740 挂接：`+0x670` / `+0x38` = 父对象，`+0xB8` 父骨架，`+0xC0` 序号。
- 0x22A740 读武器 SGO 的 `ragdoll`（宽字符串 0x9F99A0 / 0x9F9990 / 0x9F9980：存在性、路径、映射），在 `+0x578` 建 ragdoll：
  0x3327E0(ragdoll, model +0x130, 路径, 映射, 模式 2, &计数, **owner = 父对象（赫克托）**)。模式 2 不走 0x333E50（那是模式 1）。
  0x332AF0 → 0x332DBD `0xEF240(body, 序号, owner)`：每个刚体加 hkp property 0 = owner、property 4 = 刚体序号。
  所以子弹打到盾刚体，命中对象是赫克托本体。
- 之后若 `+0x10C != 0`（只有盾）：0x3342B0(ragdoll, key 2, &(this+0x110))：每个刚体加 **property 2 = 指向盾阵营的指针**。
  全文件只有这一处给刚体加 property 2（0x3342B0 的另一调用 0x22BD38 用 key 5）。
- 4.1 的盾 SGO 没有耐久键；盾 ragdoll 两个刚体 `RagDollProxys.gun_arms`（5000）/ `RagDollProxys.gun_shield`（20000），全部带 property 2。

## 3. property 2 的两个读者（H）

- 子弹射线过滤 0x169550（经 0x168DF0 的表调用）：先取 property 0 的 owner，dynamic_cast 成游戏对象，owner 与子弹（`+0x240` 阵营）
  在阵营表里是 1（友方）则不碰撞；再找 property 2：有则查 `table[*prop2][子弹阵营]`，**不是 2（敌对）就不碰撞**。
  即：盾只挡敌对阵营的子弹，赫克托自己和友方的子弹穿过去。0x1A1F80 是同样逻辑的另一种过滤（`+0x310` 阵营）。
- 赫克托槽 10 0x2210E0（伤害是否受理）：在命中信息的子命中表（`+0x58`，0x30 一项，`+0x20` 刚体）里找离命中中心（`+0x20..+0x28`）
  最近的一项，该刚体带 property 2 → 返回 0（**不受理，不掉血**），否则返回 1。

结论：4.1 的盾 = 挡敌对子弹、自己不受伤、也不让赫克托受伤的物理壳，随臂骨移动，不能被打坏。

## 4. EDF6 侧对应（H，除注明）

- EDF6 同样的伤害受理入口：对象 vtable 槽 16 Accept(object, GDI*)（ApplyDamage 0x547C30 先问它）；子命中最近刚体的取法
  `edf41c::HitBody`（GDI +0x78 / +0x88，0x30 一项，`+0x20` 刚体 id，0x6EA430 映射）与 4.1 槽 10 相同。
- EventAnimationObject（构造 0x5B5FB0）读 `animation_model` 与 `ragdoll`，构造末尾 0x5B6401 把 ragdoll 全部刚体放到层 0x11（地图层）。
- WalkingEnemyBase（构造 0x53AB80）的 ragdoll 组件在 `+0xD80`（0x53BCD9，与 EAO `+0xAA0` 同类：0x6ED0D0 / 0x6ECD80 / 0x6EC0B0 一样调用）。
  插件把盾刚体移到赫克托 `+0xD80` 第一个刚体的层（0x11B1240 读 filter info，0x6E9320 改层）。
- 移植的盾因此是：插件类 `Hector401_Shield`（EAO 克隆，槽 16 恒假、槽 22 放开阵营），4.1 盾 ragdoll + 无网格盾模型，每帧矩阵 = 赫克托
  臂骨世界矩阵（`mdb.graft_skinned` 把盾模型的模型坐标系放在臂骨上，所以与本体里那块盾网格重合，M）。
- 与 4.1 的差别（M / U）：4.1 的盾刚体 owner 是赫克托（命中即命中赫克托，再由槽 10 拒收）；移植版 owner 是盾对象自己（槽 16 拒收），
  效果相同。EDF6 子弹对同阵营对象放行（推定，M），对应 4.1 的阵营表过滤。范围伤害：EDF6 的爆炸按 AABB 查询直接找到本体刚体，
  盾不挡爆炸；4.1 里爆炸的最近子命中落在盾上会被整发拒收（U，未核 4.1 范围伤害是否走槽 10 的同一子命中表）。
