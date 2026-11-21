# EDF4.1 赫克托：武器模型与本体模型分开储存、分开读取

2026-11-21。范围：21 种 HECTOR301* / HECTOR401* 变体（`tools/make_edf41_objects.py` 的 `HECTOR_VARIANTS`）。
置信度：**H** = 反汇编 / 数据逐字节核对过；**M** = 读过代码、但没有在实机上验证；**U** = 推断，未核对。
EDF.dll 地址是 Steam 版（TimeDateStamp `0x678CCB46`）的 RVA；EDF41.exe 地址来自 `docs/edf41-hector-shield-re.md` 与生成器注释。

## 改之前

- 每个变体一份「武装本体」：生成器把两只手臂的武器 mdb 嫁接进本体 mdb（`mdb.graft_rigid` / `graft_skinned`，骨骼名 `gun_arms_l_*` / `gun_arms_r_*`），
  21 份武装本体放进同一个 `OBJECT/HECTOR_VARIANTS.MRAB`。
- 武器发光：把武器的 MTAB 块合并进本体的 material_animation，材质改名（`ufosurface.hector301cannon` 等）避开 EDF6 按名字绑定第一个材质的问题（EDF+0x110D570）。
- 武器动画（401 加农 / 发射器开合、加特林转管）：`edf41_hector_anim` 写在本体 SGO 里，插件 `PoseWeapon` 改本体上 `gun_*` 骨骼的 local。
- 盾：已经是独立对象（`Hector401_Shield`，`docs/edf41-hector-shield-re.md`），但模型是无网格的，盾的外形由本体上的嫁接网格画。

## 4.1 的原始结构（数据核对）

- `Hector.mrab` 成员：`Hector301.mdb`、`Hector401.mdb`，以及 7 个武器 mdb：`Hector301cannon` / `Hector301Gatling` / `Hector301Launcher` /
  `Hector401Cannon` / `Hector401Gatling` / `Hector401Launcher` / `Hector401shield`。**H**（直接读档案）
- 4.1 的每把武器本身就是一个对象（`hector_weapon` 每行 `[手臂, 肩骨, 序号, 武器 SGO]`），有自己的 SGO、CAS、ragdoll（301：`Ragdoll_Hector301Cannon.shkt` 等；
  401：`Ragdoll_Hector401*_has_hka.shkt`）和 material_animation（301 加特林没有）。**H**
- 所有武器模型的根骨（mdl）和刚体骨的 local 都是单位矩阵，所以「武器对象的矩阵 = 手臂骨骼的世界矩阵」与旧嫁接的位置完全一致。**H**（逐模型读骨骼）
- 401 的武器是蒙皮的（根 `gun_arms`，子骨 `gun_top` / `gun_bottom` / `gun_rolling` / `gun_bottom_l` / `gun_bottom_r` / `gun_shield`）。**H**
- 4.1 本体死亡时每把武器各自播自己的死亡材质块（EDF41.exe 0x2255D4 .. 0x22A289 的武器状态机）。**M**（沿用盾的逆向笔记）

## EDF6 侧用到的引擎行为

| 项 | 地址 | 结论 | 置信度 |
|---|---|---|---|
| EventAnimationObject（EAO）构造 | ctor 0x5B5FB0，size 0xC90，vtable 0x17D4D20 | 武器对象的类；模型 +0x5C0，骨架 +0x660，ragdoll +0xAA0；构造时播 CAS `main` / `default` | H（`docs/edf41-classes.md`） |
| EAO Update | 0x5B6D20 → GOB::Update → ragdoll 0x6EDCA0 → 骨架 0x1100B90(obj+0x660, obj+0x60) → CAS step | 骨架只按 +0x60 矩阵和各骨 local 重算世界矩阵，local 不被覆盖，所以插件写的 local 会保留到下一帧 | H |
| EAO 构造读 `ragdoll` | 经变体访问器取路径，0x6EB4C0 → 0x6EB760 → 0x6E8560 | 原版 5 个 EAO SGO 和移植的所有 EAO SGO 都有 ragdoll；缺 ragdoll 的行为未知，所以每把武器都带 4.1 自己的武器 ragdoll | H（有 ragdoll 的路径）/ U（缺失时） |
| Ragdoll::Disable | 0x6E97C0（指纹 `48 89 5C 24 08 57 48 83 EC 20 48 8B 59 78 48 8B`） | 遍历刚体（数据 +0x78，数量 +0x88，步长 0xC0），启用字节 +0xA4 为真的调用 0x6ED480(body, 0) 移出物理世界。原版唯一调用点 0x38B321（函数 0x38B2F0） | 函数本身 H；用在 EAO 上 **M**（没有实机验证） |
| SGO `resource` | 原版 Humanoid_Basic、EDF6_Berserker_TypeBommer 等 | 通用的预载列表（列 SGO、mrab、cas、shkt），本体出现前把武器对象的文件读进来 | H（数据）/ M（预载时机） |
| MaterialAnims | ctor 0x6DBE90，绑在 EAO +0x720 | 种类（kind）与播放模式 0 一次 / 1 停在末尾 / 2 循环；模板 Berserker 四槽的 kind 是 0, 0, 2, 2（发光 0，死亡 2） | H |
| 伤害接受 | vtable 槽 16 Accept（ApplyDamage 0x547C30 询问） | 插件让两个武器类一律返回 false，不吃任何伤害 | H |

## 新结构（生成器 ↔ 插件的契约）

文件（`edf41port/objects/OBJECT/`）：

- `HECTOR_BODY.MRAB`：4.1 的 `Hector301.mdb`、`Hector401.mdb`（**不带任何武器**），加两个碎片 mdb 和它们绑定的贴图。每代一份本体，21 个变体共用。
- 每种武器一组（7 种，不论多少变体用它，只生成一次）：
  - `EDF41_<4.1 武器 SGO 名>.SGO`，例如 `EDF41_HECTOR301_CANNON.SGO`：经典 SGO，键
    `xgs_scene_object_class`（`Hector41_Weapon`，盾为 `Hector401_Shield`）、
    `animation_model [[[本武器 MRAB, 4.1 的 mdb 名]], EDF41_HECTOR_WEAPON_STILL.CAS, MAB]`、
    `ragdoll`（4.1 自己的武器 ragdoll）、`game_object_durability 1.0`、
    `material_animation [[发光块, 0], [死亡块, 2]]`（有才写）、`edf41_weapon_anim`（401 加农 / 发射器 / 加特林才有）。
  - `EDF41_<…>.MRAB`：只有这把武器的 mdb 和它绑定的贴图。
  - `RAGDOLL_HECTOR301CANNON.SHKT` 等：4.1 武器 ragdoll 的转换结果。
- `EDF41_HECTOR_WEAPON_STILL.CAS`：所有武器共用，`main` 只有一个不动的 `default`（EAO 构造会播它）。
- 删掉：`HECTOR_VARIANTS.MRAB`、`HECTOR401_SHIELD.MRAB`、`HECTOR401_SHIELD_STILL.CAS`、`HECTOR401_SHIELD_COLLISION.SGO`（盾改由带网格的 `EDF41_HECTOR401_SHIELD.*` 承担）。

变体 SGO（`HECTOR301CC.SGO` 等）：

- `animation_model` 指向 `app:/object/hector_body.mrab` 的 `Hector301.mdb` / `Hector401.mdb`。
- material_animation 只动本体的 `ufosurface`（四槽，死亡块在槽 3），不再合并武器材质、不再改名。
- `edf41_hector_weapons {sgo: [左臂武器 SGO, 右臂武器 SGO], shield: [盾所在的 gun_control 序号...]}`。
- `resource`：每把武器的 SGO、MRAB、静止 CAS、ragdoll（去重），随本体一起预载。
- 枪口和子弹仍是本体的 `gun_control`（`muzzle` 骨在本体手臂上），枪口偏移按武器模型的枪管末端（`weapon_barrel`，最大 z）算，和武器对象的位置一致。
- 没有 `edf41_hector_anim`、`edf41_hector_shield` 了。

插件（`src/edf41ai.cpp`）：

1. 安装：一个 EAO 克隆 `weaponClone` 注册两个类名 `Hector41_Weapon` / `Hector401_Shield`（`AddFactory`），槽 16 Accept 返回 false，`AllowAnyTeam`（EAO 槽 22 只收 team 6）。
2. 赫克托第一次更新时读 `edf41_hector_weapons`；缺这个键 = 安装坏了，记一行日志，这台赫克托不出武器（不做替代）。
3. 每帧在 `AimArms` 转完手臂骨之后（`Weapons`）：
   - 每只手臂第一次用 `CreateFromPath(手臂世界矩阵, 武器 SGO)` 建对象，核对是本插件的类，记弱引用，同步阵营；
   - 武器对象 +0x60 = 手臂骨骼世界矩阵；
   - `WeaponPose`：把 4.1 的开合 / 转管片段（`hector::StepOpenClose` / `StepRoll`，攻击状态与该手臂的炮台一致）写进**武器对象自己的骨骼** local；
   - `edf41c::ModelMatrix(obj, m)`（0x1100B90）立刻按新矩阵重算武器骨架，无论本帧谁的 Update 先跑，画出来的都是这一帧的手臂姿态。
4. 发光：武器对象自己的 MaterialAnims，建出来就循环播槽 0；本体死亡时播槽 1（死亡块）一次并停在末尾。
5. ragdoll：活着的盾把刚体移到赫克托自己的 ragdoll 层（WalkingEnemyBase +0xD80，0x53BCD9），挡敌方子弹；其他武器、以及本体死后的盾，每帧 `DisableRagdoll`（与旧嫁接武器一样不参与碰撞）。
6. 生命周期：武器留在尸体的手臂上；赫克托对象消失（弱引用失效，武器自己的 Update 检查）或赫克托的表项被丢弃（`DropWeapons`）时删除武器；游戏关闭时先释放材质动画（`AtGameShutdown`）。
7. 盾的手臂永不开火（`shieldMask`）。

新调用的 EDF.dll 函数只有 Ragdoll::Disable 0x6E97C0：`edf41c::DisableRagdoll` 先比对 16 字节指纹，不符记一行日志并什么都不做。

## 联机一致性

武器对象是每台机器各自生成的纯表现对象，与盾相同：

- 赫克托是任务对象，每台机器都有自己的一份；每台机器的插件为自己那份赫克托建武器。
- 武器对象不吃伤害（槽 16 Accept false、+0x380 不掉血）、不是目标、不计数、不注册给网络同步；它们没有自己的状态：
  位置来自那台机器上赫克托的手臂骨骼，开合 / 转管来自那台赫克托的攻击状态，死亡外观来自那台赫克托的死亡标志。
- 子弹仍由本体的 gun_control 发射，走原有的联机路径。所以各机器显示的武器、位置、动画与死亡效果都由各自已同步的赫克托推导出来，不需要额外传输。
- 盾的 ragdoll 只影响本机的碰撞（与改之前的盾一致），所以「盾挡子弹」的判定和以前一样在发射方本机进行。

## 大小

按 `edf41port` 里的成员字节数（`python -B tools/make_edf41_objects.py` 全量重生成，123 个对象、0 跳过）：

| | 改之前 | 改之后 |
|---|---|---|
| 本体 | `HECTOR_VARIANTS.MRAB`：91 个成员，23 个 mdb（21 份武装本体 + 2 个碎片），约 28.8 MB | `HECTOR_BODY.MRAB`：26 个成员，4 个 mdb（2 份本体 + 2 个碎片），14.16 MB |
| 武器 | 嫁接在每份本体里；盾另有一个 406 字节的无网格 mdb | 7 个 `EDF41_HECTOR*_*.MRAB`，共 4.33 MB（0.51–0.77 MB 一个，各自带自己的贴图） |
| 合计 | 约 28.8 MB | 约 18.5 MB |

只有赫克托相关的文件变了（`edf41port/objects.json` 的差异里没有别的对象）。

## 没做 / 风险

- 没有开游戏实测（任务禁止）：EAO 克隆建武器、`DisableRagdoll` 用在 EAO 上、`resource` 预载、武器材质动画绑定，都只做了离线检查和 fixture 测试。
- 4.1 里打中武器的伤害会转给赫克托；这里武器一律不吃伤害（和改之前的嫁接武器一样），没有转发。
- 301 加农的充能灯（4.1 的 120 帧块）仍不发：曾把 M113 照成全白（2026-10-17 实测），旧的未使用代码已删掉。
- 武器对象没有 FarRender（本体也没有）。
- `HECTOR_BODY.MRAB` 现在每个变体都带两代本体（共用一个档案，加载一次）；如果只出现 301，也会读入 401 的本体（与旧 `HECTOR_VARIANTS.MRAB` 一次读入 21 份武装本体相比仍小得多）。
