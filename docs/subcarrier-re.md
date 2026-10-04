# 潜水母艦（航空潜舰）逆向与实现记录

EDF.dll TimeDateStamp `0x678CCB46`，下文地址都是 RVA。
可信度：**H** = 反汇编或数据直接确认；**M** = 有旁证的推断；**L** = 猜测，必须进游戏验证。

实现文件：
- 插件：`src/subcarrier.cpp`
- 生成脚本：`tools/make_sub.py`
- 测试场：`testrange/gen.py` 中的 `edf6tr_sub_carrier_mission`
- 模型配方：`pylib/jet_models.py` 中的 `SUB_MODELS`

---

## 1. 游戏里的潜水母艦是什么

### 1.1 对象与模型（Root.cpk，H）

**`OBJECT/EV603_MARINE.SGO`**（DSGO 格式，要用 `pylib/dsgo.py` 读，`python -B pylib/sgo.py` 可直接打印）

| 字段 | 值 |
|---|---|
| `xgs_scene_object_class` | `'FarEventObject'` |
| `animation_model` | `['app:/Object/ev603_marine.mrab','ev603_marine.mdb']`，外加 `ev603_marine.cas`、`616`、包围盒 `[-177.70,-166.50,-839.38]..[177.70,366.32,824.44]`、特效 `ev603_marine.efarc` |
| `game_sound` | `tikyuu6_ev_submarine.acb`、`tikyuu6_en_Radon.acb` |
| `setting` | `{scale:[1,1,1], default_animation:'idle'}` |
| 耐久与武器 | 没有 `game_object_durability`，也没有任何武器字段 |

结论：任务里的潜舰只是一个**远景演出物件**。它没有 HP、没有可站立的碰撞体、不能开火。所有攻击都是脚本另外生成的。

**`ev603_marine.mdb`** 有 19 根骨骼和 1 个蒙皮 object（polymesh，12808 个顶点）。

| # | 骨骼 | 父骨骼 | 绑定位置（模型单位，米） | 说明 |
|---|---|---|---|---|
| 0 | ev603_marine | – | 原点 | 根骨骼；生成时改名为 `mdl` |
| 1 | globalSRT | 0 | 原点 | 单位矩阵 |
| 2 | body | 1 | 原点 | 绑定姿态有转轴（x→y、y→z、z→x）；生成时放平（`level_bone`） |
| 3/4 | gunA_pan_l / gunA_tilt_l | body | (17.7, 229.5, -16.3) | 左舷前炮塔；tilt 骨的 +z 朝向艏方向（+z） |
| 5/6 | gunA_pan_r / gunA_tilt_r | body | (-17.7, 229.5, -16.3) | 右舷；绑定矩阵为 diag(1,-1,-1)，+z 朝后 |
| 7–10 | gunB_* | body | (±17.7, 245.9, -52.2) | 第二对炮塔 |
| 11–14 | gunC_* | body | (±17.7, 261.9, -88.0) | 第三对炮塔 |
| 15 | hatch | body | (0, 123.9, 107.2) | 导弹舱盖 |
| 16/17 | missle_l / missle_r | hatch | (±56.5, 172.0, 592.0) | 两个导弹发射点（原文拼写就是 missle） |
| 18 | polymesh | 0 | – | 网格 |

几何尺寸（顶点统计，H）：
- 全长 1664 m，全宽 355 m，高度 -166.5 到 366.3。
- 主甲板在 y≈193。z>280 的艏部宽约 ±69，顶面平整，在 y=193。
- 指挥塔在 z≈-280 一带，最高到 y=366。
- 炮塔区在 z -16 到 -96、y 229–262。

**`EV603_MARINE.EFARC`**（H）
- 弹体：`bullet_syringe01.mdb`，带 col/nor/occ/rml 贴图，即チラン爆雷的弹体。
- 特效：`dive.esb`、`gun_fire_loop.esb`、`missile_fire.esb`、`missile_single.esb`、`surfaced.esb`。
- 贴图：`missile_flare.dds`、`煙_01`、`飛沫_01`。

**动画名**（取自任务脚本里的 `FarObject_PlayAnimation`，H）
- main 层：`surfaced`、`dive`、`idle`
- additive 层：`gun_active`、`gun_end`、`missile_fire`

### 1.2 任务里怎么用（MISSION/*/MISSION.AC，UTF-8 AngelScript，H）

**M082**（列表 #103「潜水母艦 防衛作戦」）
- 生成：`pandora = CreateNeutral("潜水母艦近５", "app:/object/ev603_marine.sgo", 1)`，然后 `pandora.FarObject_PlayAnimation("additive","gun_active")`。
- 结束时播 `gun_end`，再播 `dive`。
- 注释原文：「潜水母艦の移動はアニメーションで行う」。`SetAiRoute` 被注释掉了，说明它的移动只靠动画，不走 AI 路线。

**M092**
- `CreateNeutral("潜水艦", ev603_marine.sgo, 1)`，然后播 `surfaced`。
- 接着播三次 `missile_fire`。
- 之后 `AirStrike2(radon, 0, "app:/object/DemoMissile_vsRadon.sgo", 3.14f)`，这就是チラン爆雷。

**M123**（BE151_150–161）
- 三艘潜水母艦同时攻击：`PointAirStrike(..., "app:/object/DemoMissile_vsRingMissile.sgo", ...)`。

导弹本体（H）：

| 文件 | 类 | `indirect_fire_param` | `indirect_fire_damage` |
|---|---|---|---|
| `DemoMissile_vsRadon.SGO` | `DemoIndirectFire` | `[1.47,0.8],[1500,0],12,20,'EfsBullet',...`，带 homing_data | 3000 |
| `DemoMissile_vsRingMissile.SGO` | – | `[0,0.1],[4000,0],12,80,...` | 3000 |

---

## 2. 设定与能力对照

文案分别取自：
- 语音表：`MISSION/EDF6_VOICETABLE.JA.SGO`、`.EN.SGO`、`.SC.SGO`
- 任务说明：`MISSIONLIST.OFFLINE.TXT.*`
- 武器说明：`WEAPON/WEAPONTEXT.JA/EN/CN`

中文译名：简体是「航空潜舰」，繁体是「航空潛艦」。日文原文是「潜水母艦」，英文是 "submarine aircraft carrier"。

| 能力 | 设定出处（文件 / 原文）或模型挂点 | 实现 / 做不到的原因 |
|---|---|---|
| **浮上，停在海岸** | BE104_025 日「潜水母艦が浮上するぞ。」中「潜艇母舰要上浮了。」；BE151_150「エピメテウス、浮上。」；任务说明 table[103]：パンドラ遭スキュラ攻击后紧急浮上，停在海岸附近；动画 `surfaced`。 | **做到（形式改变）**。地图上没有海，艇身就坐在陆地上：每帧在艇身投影的 5 个点向下打地图射线（艏、艉、中、左、右），把艇底保持在最高地面上方 0.6 m。由任务放置（落在地面点）时，艇身会从地下「浮」上来。 |
| **潜航** | BE103_086 日「機関始動。潜航する。」英 "Engine started. Time to dive." 中「启动机械，进行潜航。」；BE103_088「潜航。」；动画 `dive`。 | **不做**。陆地地图没有水，潜下去就等于钻进地里。另外插件不知道什么时候该让它离场。 |
| **炮塔射击** | 模型骨骼 `gunA/B/C_pan_*`、`gunA/B/C_tilt_*`（三对炮塔）；特效 `gun_fire_loop.esb`；动画 `gun_active` / `gun_end`；BE151_152 日「エピメテウス、武装展開。」英 "Epimetheus, deploying weapons." 中「厄庇墨透斯，展开武装。」 | **部分做到（2026-10-04 改为炮塔独立瞄准，需实机验证）**。两门炮（喷气机机炮 `EDF6VC_JET_GUN_L/R`，射程 600 m）挂在 `gunA_tilt_l` 和 `gunB_tilt_l` 上（这两根骨骼的 +z 朝艏）。V506 的动画只驱动 `body`，插件每帧改写这两根 tilt 骨骼的局部矩阵（与 jet.cpp 舵面同一种写法），让骨骼 +z 转向目标（每秒最多约 57°，射界：甲板平面下 20° 到上 80°，炮口到目标的连线不穿过船体箱）。开火判定只看**游戏实际算出的炮口**（heli.cpp `MuzzleFrame` 同款：炮口骨骼世界矩阵 × 炮口局部矩阵）：炮口到目标距离 < 580 m、炮管方向偏差 < 4°、不穿船体、炮口沿线没有友军。所以即便骨骼转动没有带动弹道（炮口模式若取武器自身矩阵），也只会「不开火」，不会乱开火。水上母舰不再转艏（甲板稳定），靠炮塔自己转。右舷炮塔骨骼朝后（绑定矩阵为 diag(1,-1,-1)），没用。 |
| **发射导弹 / チラン爆雷** | BE104_022 日「パンドラに搭載されているチラン爆雷で、グラウコスを撃滅する。」中「用潘多拉搭载的暴君深水炸弹消灭格劳科斯。」；BE104_078 日「チラン爆雷。射出。」英 "Fire Chiren Charges!"；BE104_093「チラン爆雷。全射出。」；BE151_157「潜水母艦、３艦同時攻撃。」；AEX04_150 日「エピメテウスに支援を要請した。ミサイルが来るぞ。」中「已请求厄庇墨透斯支援。飞弹要来了。」；骨骼 `hatch`、`missle_l`、`missle_r`；特效 `missile_fire.esb`、`missile_single.esb`；动画 `missile_fire`。 | **做到（替代弹种）**。导弹舱 `missle_l` 上挂 506 的追踪导弹 `v_506heli_missile01`。插件把它的锁定距离放宽到 1150 m、锁定角放宽到 1.2 rad、锁定速度加倍。游戏锁住目标后，每 2.5 s 发射一次。原作的チラン爆雷（`DemoMissile_vsRadon`，`DemoIndirectFire`）是脚本直接生成的演出弹，不是载具武器；它的投放器 IndirectFireControl 只在 jet.cpp 的轰炸舱里接过线。为了不改 jet.cpp，这里没有复用它。 |
| **舱内装填** | WEAPONTEXT [1009] ライオニックＵ２０：「…ミサイルの装填は潜水母艦内でおこなわれる」（同类还有 [1011]、[1013]–[1015]、[1018]、[1019]、[1433]）。 | **做到**。武器打空 12 s 后，插件把弹量恢复到它第一次被看到时的数值。 |
| **耐久 / 被击伤** | BE103_002：受损，修好前不能动，要保护潜水母艦；BE103_047：パンドラ因机关异常不能动；AE202_033：パンドラ被マザーシップ8号的砲撃击沉。 | **做到**。SGO 里 HP 30000（数值是自定的：原物件没有 HP；空中航母是 8000），插件出生时按比例抬到 `SubHullHp`（默认 100000）。HP 拆成船体 + 4 个甲板子系统，船体只吃重型攻击，见 §8。归零后走 506 的坠毁、爆炸、残骸流程。 |
| **血条** | 无文案。游戏自带的跟随者血条见 §4。 | **做到（推断，M）**。复用跟随者血条的绘制函数，在指挥塔上方画船体一条，每个子系统在自己位置上方再画一条（§4、§8）。 |
| **可以站人** | 无文案。按模型甲板推断。 | **做到（推断，M）**。碰撞箱就是艇身，从艇底到主甲板（y≈193×0.12=23.2 m）：半尺寸 14.5 × 21.6 × 99.8 m。指挥塔和炮塔区高出甲板的部分没有碰撞。 |
| **移动** | M082 注释「潜水母艦の移動はアニメーションで行う」；BE103_086「機関始動。潜航する。」 | **部分做到**。艇身守在自己的位置；玩家离开超过 300 m 时，以 10 m/s 跟过去，跟到 150 m 以内停下。它直接写速度，所以会推开或压过地面上的物体。站在甲板上的人会不会被带着走，要进游戏验证（L）。 |
| **放舰载机 / 无人机** | 文案和语音表里搜过 艦載、発艦、搭載 等词，都没有潜水母艦放飞机或无人机的描述。模型上也没有 catapult、deck、hangar 一类骨骼。英文 "aircraft carrier" 和中文「航空潜舰」只是译名，日文原文是「潜水母艦」。 | **做到（用户要求，2026-10-04）**。原作没有设定依据；按用户要求加一个规模很小的无人机舱：位置是插件自选的艉部甲板（§8.3，L），舱完好且有目标时每 10 s 放 1 架 jet.cpp 的机炮无人机，最多同时 4 架。复用 jet.cpp 的无人机（`JetLaunchDrone`，内部就是 `Launch(Role::drone)`），没有第二套无人机系统。 |

---

## 3. 承载对象类的选择

### 3.1 为什么不用 `FarEventObject`

它是任务演出用的远景物件（M，依据是类名、SGO 字段和任务用法）。原因：
- SGO 里没有 HP。
- 没有阵营；只能通过 `CreateNeutral` 生成。
- 没有可站立的刚体。
- 只能播动画，没有武器接口。

要让它成为能打、能挨打、能站人的友军，需要另外实现太多东西。

### 3.2 采用 Vehicle506_Helicopter 壳

做法和空中航母、喷气机一样（`docs/jet-plan.md`、`docs/jet-model-re.md`）。

| 项 | 值 | 可信度 |
|---|---|---|
| 类 vtable | `0x17DB238` | H |
| HP 当前值 / 最大值 | `+0x2F8` / `+0x2F4` | H |
| 死亡标志 | `+0x2E8` | H |
| 刚体 | `+0x1650`，`heli_rigid_body` 是单个箱体 | H |
| 速度增益 k | `+0x162C`，用作身份标记 | H |
| 开火字节 | `+0x2020`（武器 0、1），`+0x2021`（武器 2） | H |
| 物理 | slot 57 `0x61B710`。写线速度 `0x11B18F0`，写角速度 `0x11B1760` | H |

身份标记 k = **7101**（喷气机用的是 7001–7099）。标记区间表只有一份（body506.cpp `kMarks` / `BodyOf`），`IsSub()` 就是 `BodyOf(v)==PluginBody::sub`（同时检查 vtable 和 k）。

派生 SGO 由 `gen.jet_sgo('edf6tr_sub_carrier_mission')` 生成：
- 模型：`edf6vc_sub.mrab`，即 EV603 原尺寸（全长 1664 m，任务里用的就是 1 倍）。根骨骼改名为 `mdl`，以挂上 V506 MAB 的定位点（`JET_MAB_ROOT`）；`body` 放平。
- `animation_model_bone_mapping = ['mdl','body']`。
- `vehicle_weapon_setting = [gunA_tilt_l, gunB_tilt_l, missle_l, body(燃料)]`。为此 `gen.Jet` 新增了 `weapon_bones` 字段，默认为空；为空时行为和以前一样，全部挂在 `anchor` 上。
- `heli_rigid_body = [[0,1.59,-0.91],[14.52,21.58,99.84],0.305]`。
- `game_object_durability = 30000`。
- 保留 V506 的 `.cas`。它驱动 `body` 骨骼，缺了会崩（`docs/jet-model-re.md`）。

已检查：其余 6 种喷气机的 SGO 字节和改动前完全一致。

### 3.3 生成与预载（H，和 jet.cpp 相同的已验证路径）

预载：
```
0x7A3780(*(img+0x20B29A8), L"app:/object/edf6vc_sub_carrier.sgo", 2, -1)
```

生成：
1. `CreateObject 0x11945E0(*(img+0x20B2958), &matrix, path, &InitParam{vtable 0x1762068})`
2. 修 body part（`0x6EA4B0` 查 `"body"`，写入 `+0x1530`；`docs/jet-model-re.md`）
3. `SetTeam 0x54EE70(v, 2, true)`
4. `RideAi`（slot 50）`(v, true)`：这一步读取 `mission_setup`，写入 k 和武器

删除用 `0x118A1B0`。`SubLaunch` 在生成出来的不是潜舰、或分不到驱动条目时删除（不留下没人驱动的潜舰）。

### 3.4 身份、驱动与每帧分发（2026-10-04 审查后）

- **谁驱动由插件自己的条目决定**：`subs[]` 每艘一条，按 `ObjRef`（地址 + weak-this 控制块）认对象。`SubLaunch` 生成时登记；任务放置的潜舰第一次被看到（输入阶段或物理步）时登记（`Adopt`）。条目只在新任务（`ResetSubs`）或对象确认死亡/不存在（控制块计数为 0、对象换了、删除标志、死亡字节）时作废（`Sweep`，有日志）；不再按「1.5 s 没更新」重建条目，所以磨损、损毁、修复计时、`launched` 不会被悄悄清零。
- 超过 3 艘时，多出来的那艘在物理步里被按住不动（速度 0），日志说明一次；不会交回原版 506 物理（7101 的速度增益会让它乱飞）。
- 每帧一次 `Tick`：
  1. 正常来自输入阶段：crew.cpp `InputHook` → heli.cpp `HeliFrame`（座位 0 是 NPC 驾驶员时）→ `SubFrame`，之后写开火字节。
  2. 驾驶员丢失（连续两帧没有输入阶段）时，由物理步（body506.cpp 唯一的 slot 57 钩子 → `SubBodyStep`）接着驱动和写开火字节，日志说明一次；输入阶段回来也记一次。
- 物理：body506.cpp 唯一挂 506 slot 57，按标记分发到 `SubBodyStep`，只对潜舰写速度和角速度。
- 消息：body506.cpp 唯一挂 506 slot 9，按标记分发到 `SubMessage`（水消息整条吞掉、伤害消息走 §8 的分流）。
- 时间步：`Tick` 用 GameMs 差（最多 1/60 s，body506.cpp `GameStep`），不再用 QPC 墙钟。
- 座位武器按「炮口在哪根骨骼旁」解析到部件（`Resolve`，炮口离部件枢轴 60 m 内），并校验它就是开火字节需要的那个 holder（0x2020 = holder 0/1 机炮，0x2021 = holder 2 追踪导弹）和追踪类型；对不上的部件明确报错并关闭（不开火），不再按 `i==3 || (!homing && i>=2)` 硬猜。`tools/make_sub.py` 也断言同样的顺序（`PLUGIN_WEAPONS`）。

---

## 4. 血条

| 项 | 内容 | 可信度 |
|---|---|---|
| `HudPlayer_FollowerDurability` | vtable `0x17F6C08`，slot 3 = `0x8040E0`（绘制） | H |
| 绘制入口条件 | HUD 对象 `+0x78`/`+0x80` 的 weak_ptr（玩家）存活，且 `0x56FD00(玩家)` 为真 | H |
| 唯一调用点 | `0x8042AD`：`call 0x804300(hud, viewProj, owner=[hud+0x80], r9, 第5参数)` | H |

`0x804300` 的行为（H）：
- 遍历 `owner+0x550` 链表，节点结构：`[0]` next，`[8]` prev，`[0x10]` 对象。
- 对每个对象：
  - 取 `+0x90` 的位置（vec4，w 参与矩阵乘，必须是 1），加 2.0 m（常量 `0x1C369B8`）；
  - 投影到屏幕，在屏幕外就跳过；
  - 用 `0xC2FB0` 画底框，再按 `+0x2F8 / +0x2F4` 的比例画血量条，颜色档 0–3。
- 然后递归处理每个对象自己的 `+0x550` 链表。

载具不在任何人的跟随者链表上，所以原版不显示载具血条。

做法：把 `0x8042AD` 的 call 改指向 `GaugeHook`（`RedirectCall`）。`GaugeHook` 先原样调用，再用一个替身 owner 调一次：
- 替身 owner 的 `+0x550` 指向栈上的链表，链表里每艘活着的潜舰对应一个替身对象（`proxies[i]`）。
- 替身对象由游戏线程每帧写入暂存区，再整份发布（三缓冲：游戏线程只写自己的那份、发布时原子交换；绘制线程只读最近一次发布的那份），绘制线程从不读潜舰条目：`+0x90` = 指挥塔上方 44 m 处，`+0x2F4`/`+0x2F8` = 艇的 HP，`+0x550` = 空链表（防止递归时读到垃圾）。面板（hud.cpp 的 `CarrierPanel`，含各部件是否损毁、修复剩余秒数）也在发布时一并做好。发布超过 1 s 没更新（暂停、潜舰都没了）就不画。
- 不会把真实载具放进链表，避免读到已释放的内存。
- 子系统（§8）每个也有一个替身对象（`proxies[i][1..4]`），`+0x90` = 该部件 `gauge` 点（机体坐标转世界坐标），`+0x2F4` = 部件 HP 上限，`+0x2F8` = 剩余 HP（损毁时为 0，修好后回满）。一艘潜舰共 5 条，最多 3 艘 = 15 条（M：没验证过绘制函数对条数有没有上限）。

`InstallGauge` 会核对 `0x804300` 读取这几个字段的指令字节（`kGaugeSigs`），不匹配就不挂钩。它与潜舰的其它检查完全分开（由 body506.cpp 的安装先调用，`InstallSub` 再调一次无副作用）：喷气机/直升机的载具 HUD 也画在这次调用里，不能因为潜舰的签名不符一起消失。

风险（M/L）：
- 血条的大小和样式和跟随者一样（小条）。
- 联机时只在本机显示。

---

## 5. 插件行为参数（`src/subcarrier.cpp`，单位：米、秒）

| 参数 | 值 | 说明 |
|---|---|---|
| 最多同时存在 | 3 艘 | M123 三艦同時攻撃 |
| 艇底离地 | 0.6 | `kHullBottom` = -163.08（箱体底在原点上方 163.08 m），与 SGO 箱体一致，`make_sub.py` 会检查；箱体几何只在 `src/subcarrier.h` 写一份，激光也用它（`SubSpot` / `SubDeckUnder`） |
| 水上 | 不打地面射线 | 水上只按水面定高度，每帧那 5 条约 1000 m 的射线算了也不用，跳过 |
| 跟随 | 1500 出发，1000 停下 | 巡航 25 m/s，加速度 3 m/s²（甲板上的玩家离舰体中点最远约 830 m，不会触发跟随） |
| 任务放置的最低高度 | 出生时的原点高度 | 海上地面射线只打到海底，艇身按任务给的高度浮着（M082：原点 -130，甲板 y≈63） |
| 升降速度 | 上升最多 8 m/s，下降最多 4 m/s | |
| 转向 | 最多约 3°/s | 俯仰最多约 ±3°；横滚保持水平 |
| 索敌 | 2000 | 选最近的敌人 |
| 机炮 | 射程 580，炮管偏差 4° | 炮塔独立瞄准（§2），射界甲板下 20° 到上 80°、不穿船体；任一炮管沿线有友军（`FriendInLine`）就不开火 |
| 导弹 | 60–2000，间隔 2.5 s | 游戏锁定目标后才发射 |
| 武器打空后装填 | 12 s | |
| 移动区域 | `+0xE00 = -1e6` | 和喷气机一样关掉区域夹紧，防止把艇身瞬移回区域内 |
| 船体 HP | 100000（ini `SubHullHp`，0 = 用 SGO 的 30000） | 出生时按比例抬高 |
| 船体重型判定 | 攻击者类在 `kHeavy` 表里，或单发伤害 ≥ 1500（ini `SubHeavyHit`，0 = 只认表） | §8.2 |
| 子系统 HP | 炮塔 A/B 各 6000，导弹舱 5000，无人机舱 5000 | `kSystems` |
| 子系统修复 | 损毁后 90 s（游戏时间）回满 | `kRepairMs` |
| 无人机舱 | 每 10 s 1 架，最多 4 架 | 需 `JetPilot=1` 且 `EDF6VC_JET_DRONE.SGO` 已装 |

---

## 6. 需要进游戏验证的推断

1. **(L) 站人与带动**：200 m 的箱体被直接写速度时，站在甲板上的士兵会不会被带着走、会不会被弹开。
2. **(M) 武器挂在炮塔骨骼上**：`vehicle_weapon_setting` 用非 `body` 的骨骼名能否正确找到部件，炮口方向是否沿骨骼 +z。喷气机只验证过 `body` 和根骨骼。
3. **(M) 艏方向**：按炮塔 tilt_l 骨骼的 +z 推断艏是模型的 +z。如果反了，开火方向会对着艉。
4. **(M) 血条**：替身对象是否画得出来，位置是否在指挥塔上方。
5. **(L) CAS 的 `globalSRT` 节点**：V506 的 cas 有同名节点，可能会写到 EV603 的 `globalSRT` 骨骼上（单位矩阵，预计没有影响）。
6. **(M) 测试场放置**：任务的 `CreateFriend` 把原点放在地面点，艇身会先埋 20 m 再升上来，可能挤开附近其它载具。建议只放这一艘。
7. **(M) 大型刚体的物理表现**：506 的刚体参数（质量 0.305）配上 200 m 的箱体，被敌人撞击或爆炸冲击时会怎样表现。
8. **(M) Genocide 炮弹的攻击者**：日志 `SUB hit ... from EDF+XXXX` 打印最后一发的攻击者 vtable RVA。被 Genocide 打中时应是 `17CACC0`（大炮）/`17CB0A0`（小炮）/`17CA278`（母舰本体）之一并带 `heavy`；如果是别的值，把它加进 `kHeavy`。
9. **(M) 传送门激光的攻击者**：只有激光弹以运兵船（`UfoCarrier508`，`17C9DC0`）为 owner 时才按类识别；否则只能靠 `SubHeavyHit` 阈值。看日志里的 `from`。
10. **(L) 子系统几何**：命中点是子弹打到箱体（甲板顶面）的位置，或爆炸中心。炮塔判定用的是炮塔脚下甲板一圈 20 m；具体手感要进游戏看。
11. **(L) 无人机舱**：飞机从舱上方 60 m 以 140 m/s 起飞，围绕起飞点巡逻；起飞瞬间会不会撞上指挥塔没验证。
12. **(L) 炮塔独立瞄准**：改写 `gunA/B_tilt_l` 的局部矩阵后，模型炮管是否转动、炮口（`MuzzleFrame`）是否跟着转。Debug 日志 `SUB v=... turretA: muzzle mode M, barrel X deg off the pose` 记一次：X 接近 0 说明弹道跟着骨骼走；X 很大（或 mode 0）说明炮口取的是武器自身矩阵，炮塔只会在炮管恰好对准时开火。也要看父骨骼（pan）是否就是机体坐标系（插件按此假设）。
13. **(M) 驾驶员丢失后的驱动**：从物理步写开火字节，原版输入在下一帧是否覆盖它没验证；日志 `no input stage ... driven from its physics step` 出现时注意炮塔是否还开火。

---

## 7. 文件清单与安装

生成（`python tools/make_sub.py [游戏目录]`，默认是 `gen.DEFAULT_GAME`；Root.cpk 只读）：
- `Mods/OBJECT/EDF6VC_SUB.MRAB`（约 39 MB）：stock 档案只替换 `ev603_marine.mdb`，其它成员逐字节不变，由 `jet_models.check` 校验。
- `Mods/OBJECT/EDF6VC_SUB_CARRIER.SGO`。
- `Mods/WEAPON/EDF6VC_JET_GUN_L/_R.SGO`：和 `make_jets.py` 写出的字节相同。`make_sub.py --remove` 不删它们；如果 `make_jets.py --remove` 删掉了，插件发现文件不全，就不预载潜舰，`SubLaunch` 返回 nullptr。

`--remove` 只删除 `EDF6VC_SUB*` 两个文件。不碰任何共享表。

测试场：在载具列表里选「航空潜舰」。安装时 `gen.jet_models` 会顺带生成 `EDF6VC_SUB.MRAB`。

---

## 8. 伤害路径与「船体 + 子系统」拆分（2026-10-04）

### 8.1 伤害怎么到达载具 HP（静态反汇编，EDF.dll TimeDateStamp 0x678CCB46）

| 环节 | 事实 | 可信度 |
|---|---|---|
| 入队 | `0x541FF0(queue=core+0x6E0, weak target, GDI)`：每个条目 0xA0 字节，`+0/+8` 目标 weak_ptr，`+0x10` 一份 GameDamageInfo 拷贝。范围伤害 `0x542860` 同样入队（`0x542FD4`、`0x54301D` 处调用） | H |
| 结算 | `0x543920` 逐条调用目标的 vtable：slot 10（`+0x50`，预处理）→ slot 9（`+0x48`，`0x543ADE`，`edx` = 消息 `0x10000000`（`0x543AB9` 写入），`r8` = GDI 指针）→ slot 11（`+0x58`，后处理） | H |
| 506 的 slot 9 | `0x652E70`。指向它的指针在 506（`0x17DB280`）、409、410 和基类 vtable 里，没有直接调用者。它自己只处理 `0x10000025`（`0x652E8E` 的 cmp），其余转给 `0x62ECB0` → 尾跳 `0x54A530`（GameObjectBase 消息处理，跳转表 `0x54A928`） | H |
| 扣血 | 消息 `0x10000000` → `0x54A579` → `0x54A586: call 0x547C30(obj, GDI)`（`0x547C30` 只有这一个调用点）。`0x548109` 读 `GDI+0x50` 伤害；正伤害 `hp(+0x2F8) += -dmg × [obj+0x394] × 友军系数 × 护甲系数`，夹在 `+0x2F0`..`+0x2F4` 之间（`0x54815F`–`0x54817A`）；负伤害走治疗分支（`0x548284`） | H |
| 友军判定 | `0x547DB7` 读 `GDI+0x24`（攻击方阵营），`0x547DBF` 读 `obj+0x314`（本方阵营），`0x547DDF` 查关系 | H |
| 攻击者 | `GDI+0x10/+0x18` 是攻击者 weak_ptr（`0x547C30` 拿它和 `obj+0x28` 比，判断是不是自伤）；子弹管线里它是子弹 owner（`core+0x9A8`，docs/decoy-blast-re.md） | H |
| 命中位置 | `GDI+0x30`：子弹命中点或爆炸中心（vec4，w=1） | H（decoy-blast-re.md） |
| slot 10/11 | `0x54AA50` 把 HP 存进 `+0x5BC`；`0x54A970` 把 HP 差累计进 `+0x5B4`。两者都看 HP，不看 `+0x50` | H |

### 8.2 挂钩与船体规则

- 506 vtable 的 slot 9（`image+0x17DB238+9*8`）只由 body506.cpp 挂一次，先读出当前值再链接（若不是 `0x652E70` 会记日志并照样链上去），按标记把消息先交给潜舰（`SubMessage`）或玩家机（`PlayerJetMessage`）。挂钩只要求 `0x652E70` 头部和 `0x652E8E` 的 cmp 对得上；伤害分流另外用 `kDamageSigs` 核对上表其余地址，全部一致才分流。
- `MessageHook(obj, msg, data)`：消息不是 `0x10000000`、对象不是潜舰（speed gain ≠ 7101）、潜舰已死、没有对应的 `Sub` 条目、伤害 ≤ 0（治疗）、攻击方不是敌对阵营（TeamManager 关系 ≠ 2；读不到关系时按「不同阵营即敌对」）——这几种情况消息原样放行。所以非潜舰的 506（喷气机、直升机）行为逐字节不变；友军误伤仍按原版规则。
- 其余命中先查子系统表（§8.3），再决定：
  - 命中点在某个**完好**部件的范围内 → 伤害全部记到该部件，`GDI+0x50` 改成 0，船体不掉血；
  - 否则是船体：**重型**来源原样放行（船体吃满伤害），其它一律改成 0。
  - stock 处理完后把 `+0x50` 写回原值（这份 GDI 是队列自己的拷贝，后面没有人再读，写回只是为了不留副作用）。
- **重型来源的识别（用户/协调者要求的首选方案：按来源识别）**：读攻击者 weak_ptr，控制块 use count（`+8`）> 0 才算活着，取对象 vtable 的 RVA，与 `kHeavy` 表比对：

  | vtable | 类（RTTI，安装时逐个核对 TypeDescriptor 名） | 依据 |
  |---|---|---|
  | `0x17CACC0` | `UfoMother511CoreBigCannon` | `E511_MOTHERSHIP_GENOCIDE_L.SGO` 的类；耐久 6000，`bullet` = RocketBullet01，参数里有 6000 / 1500 / 2400，炮口 `Fire0`（Root.cpk 只读解析，H） |
  | `0x17CB0A0` | `UfoMother511CoreSmallCannon` | `E511_MOTHERSHIP_GENOCIDE_S.SGO` 的类（耐久 1200，SGO 里没有 bullet，H） |
  | `0x17CA278` | `UfoMother511` | 母舰本体：如果部件发射的子弹 owner 记的是母舰本身 |
  | `0x17C9DC0` | `UfoCarrier508` | `E508_CARRIER*.SGO` 的类（运兵船）。原版运兵船不开火、只投放敌人，所以算在它头上的伤害就是传送门激光。激光实现（feat/carrier-laser 2ca3856，docs/carrier-laser-re.md，只读参考）：插件生成 stock `DemoIndirectFire`（vtable `0x17D4B20`），其开火单元 `+0x170` 用 `0x2B8390` 把 owner 设成运兵船自己的 weak-this，伤害写在单元 `+0xDC`（默认 2500，ini `CarrierLaserDamage`）。激光弹的 GDI 攻击者是否就是这个 owner 未实测（M）。`DemoIndirectFire` 本身不进表：原版任务脚本的炮击也用它 |

  - **证据缺口（M）**：没能在静态反汇编里找到 CoreBigCannon 的开火点（追过 `0x514xxx`–`0x517xxx`、构造 `0x5145B0`、`this+0x1150` 的动画控制器 `0x6DBE90`），所以「Genocide 炮弹的 owner = 炮部件」没有静态证明。传送门激光的 owner 取决于另一个 agent 的实现，这里同样无法确认。
  - 因此同时保留**阈值兜底**：单发伤害 ≥ `SubHeavyHit`（默认 1500，低于传送门激光的默认 2500）也算重型。设成 0 就只认类表。阈值的代价是：难度很高时普通敌人的单发大伤害也可能超过 1500 而伤到船体。
  - 验证办法：日志 `SUB v=... hit xN: parts .., hull .., held off ..; last D on PART at (x,y,z) from EDF+RVA [heavy]`（每艘每秒最多一行），`from` 就是攻击者 vtable RVA。
- 船体 HP 出生时按比例抬到 `SubHullHp`（默认 100000），原版 HP 字段直接写，血条和坠毁逻辑都照旧。

### 8.3 子系统表（`kSystems`，唯一一张表，所有命中走同一个查找）

每个部件是机体坐标系里的一段胶囊（线段 a→b，加半径 `reach`）。机体坐标：矩阵行 `m[0..2]` 右、`m[4..6]` 上、`m[8..10]` 艏、`m[12..14]` 位置（与 jet.cpp `Launch` 一致）。命中点转到机体坐标后，取「距离 / reach」最小且 ≤ 1 的完好部件；没有则算船体。损毁的部件不参与查找，打到它那里的伤害按船体规则处理。

| 部件 | 线段（机体坐标，米） | reach | HP | 功能 | 来源 |
|---|---|---|---|---|---|
| turretA | (17.7, 193.08, -16.3) → (17.7, 229.5, -16.3) | 20 | 6000 | 座位武器 0（机炮） | `gunA_tilt_l` 骨骼（M） |
| turretB | (17.7, 193.08, -52.2) → (17.7, 245.9, -52.2) | 20 | 6000 | 座位武器 1（机炮） | `gunB_tilt_l` 骨骼（M） |
| missiles | (-56.5, 172, 592) → (56.5, 172, 592) | 30 | 5000 | 座位武器 2（追踪导弹） | `missle_l` / `missle_r` 骨骼（M） |
| dronebay | (0, 193.08, -560) → (0, 193.08, -640) | 35 | 5000 | 放无人机 | **插件自选**：艉部甲板中线，模型上没有对应骨骼（L） |

- 子弹只和箱体碰撞（顶面就是甲板 y = 193.08），甲板以上的炮塔模型没有碰撞，所以炮塔线段从甲板顶面画起：打在炮塔脚下一圈甲板上的子弹算打炮塔。导弹舱在甲板下 21 m，`reach` 30 覆盖它上方的甲板。
- 部件损毁：日志 `SUB v=... part <name> destroyed`。炮塔/导弹舱：`Arm` 每帧把那件座位武器弹量写 0，开火判断就不会再开它（两门炮各自独立，坏一门另一门照常）；无人机舱：不再放无人机（已经在飞的照常飞完）。
- 修复：损毁后 `kRepairMs` = 90 s（游戏时间）回满，日志 `SUB v=... part <name> repaired`。武器在修好后再经过原有的 12 s 舱内装填才恢复弹量。
- 损毁爆炸特效：**没做**。唯一现成的爆炸是 `EDF6VC_BLAST_CHARGE`，它是自爆无人机的武器，没有「在某点生成一次爆炸」的干净入口，硬做就要再逆向一条生成路径，所以跳过。
- 无人机舱：舱完好、潜舰有目标（2000 m 内有敌人）时，每 10 s 从舱上方 60 m（`kBayLaunch`）放 1 架 jet.cpp 的机炮无人机，最多同时 4 架。新增的导出接口只有两个：`JetLaunchDrone`（内部 `Launch(Role::drone, ...)`，没有母机，锚点 = 起飞点，即守在潜舰上空；燃料/受损/没弹时按 jet.cpp 原有逻辑撤离并删除）和 `JetFlying`（数还在飞的数量）。需要 `JetPilot=1` 且本关预载了 `EDF6VC_JET_DRONE.SGO`（`PreloadJets` 只要文件在就预载）；放不出来时只记一次日志，舱仍然有血条、仍能被打坏。
