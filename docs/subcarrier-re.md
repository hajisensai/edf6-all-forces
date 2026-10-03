# 潜水母艦（航空潜舰）逆向与实现记录

EDF.dll TimeDateStamp `0x678CCB46`，下文地址都是 RVA。
可信度：**H** = 反汇编或数据直接确认；**M** = 有旁证的推断；**L** = 猜测，必须进游戏验证。

实现文件：
- 插件：`src/subcarrier.cpp`
- 生成脚本：`tools/make_sub.py`
- 测试场：`testrange/gen.py` 中的 `edf6tr_sub_carrier_mission`
- 模型配方：`tools/jet_models.py` 中的 `SUB_MODELS`

---

## 1. 游戏里的潜水母艦是什么

### 1.1 对象与模型（Root.cpk，H）

**`OBJECT/EV603_MARINE.SGO`**（DSGO 格式，要用 `testrange/lib/sgo.py` 读）

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
| **炮塔射击** | 模型骨骼 `gunA/B/C_pan_*`、`gunA/B/C_tilt_*`（三对炮塔）；特效 `gun_fire_loop.esb`；动画 `gun_active` / `gun_end`；BE151_152 日「エピメテウス、武装展開。」英 "Epimetheus, deploying weapons." 中「厄庇墨透斯，展开武装。」 | **部分做到**。两门炮（喷气机机炮 `EDF6VC_JET_GUN_L/R`，射程 600 m）挂在 `gunA_tilt_l` 和 `gunB_tilt_l` 上（这两根骨骼的 +z 朝艏）。炮塔骨骼不会转，插件改为转动整艘艇：艏部对准目标，俯仰不超过 ±8°，偏差小于 4° 才开火。右舷炮塔骨骼朝后（绑定矩阵为 diag(1,-1,-1)），所以没用。炮塔本身不转动，是因为 V506 的动画只驱动 `body`，插件也没有逐骨骼写姿态。 |
| **发射导弹 / チラン爆雷** | BE104_022 日「パンドラに搭載されているチラン爆雷で、グラウコスを撃滅する。」中「用潘多拉搭载的暴君深水炸弹消灭格劳科斯。」；BE104_078 日「チラン爆雷。射出。」英 "Fire Chiren Charges!"；BE104_093「チラン爆雷。全射出。」；BE151_157「潜水母艦、３艦同時攻撃。」；AEX04_150 日「エピメテウスに支援を要請した。ミサイルが来るぞ。」中「已请求厄庇墨透斯支援。飞弹要来了。」；骨骼 `hatch`、`missle_l`、`missle_r`；特效 `missile_fire.esb`、`missile_single.esb`；动画 `missile_fire`。 | **做到（替代弹种）**。导弹舱 `missle_l` 上挂 506 的追踪导弹 `v_506heli_missile01`。插件把它的锁定距离放宽到 1150 m、锁定角放宽到 1.2 rad、锁定速度加倍。游戏锁住目标后，每 2.5 s 发射一次。原作的チラン爆雷（`DemoMissile_vsRadon`，`DemoIndirectFire`）是脚本直接生成的演出弹，不是载具武器；它的投放器 IndirectFireControl 只在 jet.cpp 的轰炸舱里接过线。为了不改 jet.cpp，这里没有复用它。 |
| **舱内装填** | WEAPONTEXT [1009] ライオニックＵ２０：「…ミサイルの装填は潜水母艦内でおこなわれる」（同类还有 [1011]、[1013]–[1015]、[1018]、[1019]、[1433]）。 | **做到**。武器打空 12 s 后，插件把弹量恢复到它第一次被看到时的数值。 |
| **耐久 / 被击伤** | BE103_002：受损，修好前不能动，要保护潜水母艦；BE103_047：パンドラ因机关异常不能动；AE202_033：パンドラ被マザーシップ8号的砲撃击沉。 | **做到**。HP 30000（数值是自定的：原物件没有 HP；空中航母是 8000）。归零后走 506 的坠毁、爆炸、残骸流程。 |
| **血条** | 无文案。游戏自带的跟随者血条见 §4。 | **做到（推断，M）**。复用跟随者血条的绘制函数，在指挥塔上方画一条。 |
| **可以站人** | 无文案。按模型甲板推断。 | **做到（推断，M）**。碰撞箱就是艇身，从艇底到主甲板（y≈193×0.12=23.2 m）：半尺寸 14.5 × 21.6 × 99.8 m。指挥塔和炮塔区高出甲板的部分没有碰撞。 |
| **移动** | M082 注释「潜水母艦の移動はアニメーションで行う」；BE103_086「機関始動。潜航する。」 | **部分做到**。艇身守在自己的位置；玩家离开超过 300 m 时，以 10 m/s 跟过去，跟到 150 m 以内停下。它直接写速度，所以会推开或压过地面上的物体。站在甲板上的人会不会被带着走，要进游戏验证（L）。 |
| **放舰载机 / 无人机** | 文案和语音表里搜过 艦載、発艦、搭載 等词，都没有潜水母艦放飞机或无人机的描述。模型上也没有 catapult、deck、hangar 一类骨骼。英文 "aircraft carrier" 和中文「航空潜舰」只是译名，日文原文是「潜水母艦」。 | **不做**。没有设定依据，所以不复用 `JetLaunch`、`Role::carrier`、无人机或 `SpawnJet`。 |

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

身份标记 k = **7101**（喷气机用的是 7001–7006）。`IsSub()` 同时检查 vtable 和 k。

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

删除用 `0x118A1B0`。`SubLaunch` 只有在生成出来的不是潜舰时才删除。

### 3.4 每帧分发

1. crew.cpp 的 `InputHook<I>`（各载具类 slot 55）调用 `HeliFrame`。
2. heli.cpp 的 `HeliFrame` 只处理 NPC 驾驶的载具：
   - 先交给 `JetFrame`；
   - 新增一行：`if(IsSub(v)){SubFrame(v);return;}`。
   - 没有这一行，heli.cpp 会把潜舰当普通直升机来飞（H）。
3. 物理：`InstallSub` 在 `InstallJets` 之后把 506 slot 57 串到 jet.cpp 的 `PhysicsHook` 后面。只对 `IsSub` 的载具改写速度和角速度。

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
- 替身对象由 `SubFrame` 每帧写入：`+0x90` = 指挥塔上方 44 m 处，`+0x2F4`/`+0x2F8` = 艇的 HP，`+0x550` = 空链表（防止递归时读到垃圾）。
- 不会把真实载具放进链表，避免读到已释放的内存。

`InstallSub` 会核对 `0x804300` 读取这几个字段的指令字节（`kGaugeSigs`），不匹配就不挂钩。

风险（M/L）：
- 血条的大小和样式和跟随者一样（小条）。
- 假设绘制线程与游戏线程的数据竞争只会让浮点数撕裂。
- 联机时只在本机显示。

---

## 5. 插件行为参数（`src/subcarrier.cpp`，单位：米、秒）

| 参数 | 值 | 说明 |
|---|---|---|
| 最多同时存在 | 3 艘 | M123 三艦同時攻撃 |
| 艇底离地 | 0.6 | `kHullBottom` = 19.99，与 SGO 箱体一致，`make_sub.py` 会检查 |
| 跟随 | 1500 出发，1000 停下 | 巡航 25 m/s，加速度 3 m/s²（甲板上的玩家离舰体中点最远约 830 m，不会触发跟随） |
| 任务放置的最低高度 | 出生时的原点高度 | 海上地面射线只打到海底，艇身按任务给的高度浮着（M082：原点 -130，甲板 y≈63） |
| 升降速度 | 上升最多 8 m/s，下降最多 4 m/s | |
| 转向 | 最多约 3°/s | 俯仰最多约 ±3°；横滚保持水平 |
| 索敌 | 2000 | 选最近的敌人 |
| 机炮 | 射程 580，开火锥 4° | 若玩家在弹道附近（`FriendInLine`）就不开火 |
| 导弹 | 60–2000，间隔 2.5 s | 游戏锁定目标后才发射 |
| 武器打空后装填 | 12 s | |
| 移动区域 | `+0xE00 = -1e6` | 和喷气机一样关掉区域夹紧，防止把艇身瞬移回区域内 |

---

## 6. 需要进游戏验证的推断

1. **(L) 站人与带动**：200 m 的箱体被直接写速度时，站在甲板上的士兵会不会被带着走、会不会被弹开。
2. **(M) 武器挂在炮塔骨骼上**：`vehicle_weapon_setting` 用非 `body` 的骨骼名能否正确找到部件，炮口方向是否沿骨骼 +z。喷气机只验证过 `body` 和根骨骼。
3. **(M) 艏方向**：按炮塔 tilt_l 骨骼的 +z 推断艏是模型的 +z。如果反了，开火方向会对着艉。
4. **(M) 血条**：替身对象是否画得出来，位置是否在指挥塔上方。
5. **(L) CAS 的 `globalSRT` 节点**：V506 的 cas 有同名节点，可能会写到 EV603 的 `globalSRT` 骨骼上（单位矩阵，预计没有影响）。
6. **(M) 测试场放置**：任务的 `CreateFriend` 把原点放在地面点，艇身会先埋 20 m 再升上来，可能挤开附近其它载具。建议只放这一艘。
7. **(M) 大型刚体的物理表现**：506 的刚体参数（质量 0.305）配上 200 m 的箱体，被敌人撞击或爆炸冲击时会怎样表现。

---

## 7. 文件清单与安装

生成（`python tools/make_sub.py [游戏目录]`，默认是 `gen.DEFAULT_GAME`；Root.cpk 只读）：
- `Mods/OBJECT/EDF6VC_SUB.MRAB`（约 39 MB）：stock 档案只替换 `ev603_marine.mdb`，其它成员逐字节不变，由 `jet_models.check` 校验。
- `Mods/OBJECT/EDF6VC_SUB_CARRIER.SGO`。
- `Mods/WEAPON/EDF6VC_JET_GUN_L/_R.SGO`：和 `make_jets.py` 写出的字节相同。`make_sub.py --remove` 不删它们；如果 `make_jets.py --remove` 删掉了，插件发现文件不全，就不预载潜舰，`SubLaunch` 返回 nullptr。

`--remove` 只删除 `EDF6VC_SUB*` 两个文件。不碰任何共享表。

测试场：在载具列表里选「航空潜舰」。安装时 `gen.jet_models` 会顺带生成 `EDF6VC_SUB.MRAB`。
