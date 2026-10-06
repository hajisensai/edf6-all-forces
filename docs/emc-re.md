# EMC 蓄力光束（src/emc.cpp）逆向与设计笔记

EDF.dll TimeDateStamp `0x678CCB46`，地址均为 RVA。纯静态分析（`tools/edfre.py` + capstone、Root.cpk / Chunk02.cpk / SOUND 只读），
**没有起游戏**。置信度：**H** = 指令 / 数据直接可见；**M** = 证据一致的强推断；**L** = 需要实机核对。

用户要求（2026-10-06）：「改成'蓄力 → 一道粗光束持续 2–3 秒'的节奏，总伤害集中在这一发里，而不是细水长流的 1000 连击。
光束要贯穿，沿途建筑和地形一起摧毁，以呼应'瞬间毁灭一座山'的设定。同时要有几百米范围符合设定的爆炸。」

## 1. EMC 是什么（H，Root.cpk）

- 载具 `OBJECT/V510_MASER.SGO`：`xgs_scene_object_class = Vehicle510_Maser`（vtable `0x17DB9D8`，crew.cpp `kClasses` 的 `510_Maser`），
  耐久 1000，一个座位，一把武器 `vehicle_setup[2][0] = app:/weapon/v_510_maser_thunder01.sgo` 挂在 `cannon_top`。
- 空降兵的请求：`AWEAPON348`（EMC）/ `AWEAPON358`（EMCS）/ `AWEAPON363`（EMCX），`Weapon_Sub` + `SmokeCandleBullet01`，
  `Ammo_CustomParameter[4]` 里是运输机、集装箱、`v510_maser.sgo` 和这次请求的 `vehicle_setup`：`[0]` = `[耐久倍率, 伤害倍率]`
  = `[3.9, 1.3]` / `[36, 7.5]` / `[75, 12.5]`（`docs/aircraft-re.md`、`pylib/vcobjects.py` JET_TIER 同一含义）。
- 武器 `WEAPON/V_510_MASER_THUNDER01.SGO`（DSGO）：`Weapon_VehicleMaser`，名字「原子光線砲 / Atomic Ray Cannon」，
  `AmmoClass SolidBullet01`、`AmmoDamage 5`、`AmmoCount 7000`、`ReloadTime -1`（不装填）、`FireInterval 120`、
  **`FireBurstCount 1000`、`FireBurstInterval 1`**（扣一次扳机连射 1000 发、每帧一发，16.7 秒）、`AmmoSpeed 8`、`AmmoAlive 75`（射程 600 米）、
  `AmmoSize 6`、`AmmoExplosion 0`、`AmmoIsPenetration 0`（每发只打第一个碰到的）、颜色 `(0.14, 0.3, 2.5)`。
  AI 用的 `V_510_MASER_AI_THUNDER01`（180 发连射、伤害 10）和任务用的 `_MISSION` 不受影响。

## 2. 扳机与武器字段

- 510 的输入（第 55 槽 `0x61DDF0`）：`0x61DE34 movss xmm1,[rbx+0x2E4]`（rbx = 座位 0）→ `0x62DE50`（≥ 0.8）→ `0x62C000(veh+0x638)`
  拉 0 号武器槽；其余只有右摇杆写 `veh+0x2AE0/0x2AE4`。字节与 Blacker 的 `0x61AD14` 完全相同（`drill.cpp kTriggerSig`）。(H)
  所以和钻头战车一样：在原版输入**之前**读 `seat+0x2E4` 并清零，原版连射永远不会开始。
- `Weapon_VehicleMaser` vtable `0x17E5E40`（ctor `0x6B08A0`，`0x6B08D7 lea rax,[rip→0x17E5E40]`）。(H)
- 武器字段（SGO 读取函数 `0x68A920` 一带）：`AmmoDamage → +0x89C`（`0x68D6EC`）、`FireBurstCount → +0x370`（`0x68CE85`）、
  `AmmoCount → +0x248`（`0x68C567`）、剩余弹数 `+0xBE8`（`0x696819`，HUD 读的就是它）。(H)
- 伤害倍率：`0x62F800` 读请求的 `[耐久倍率, 伤害倍率]`，`veh+0x678 = 伤害倍率 × xmm6 × xmm7`（xmm6 = 1 或 `0xD7870` 的难度项，
  xmm7 = `veh+0x67C` 或 `veh+0x49C/常数`），`veh+0x2F4 = 耐久 × 耐久倍率 × …`。`0x633842` 建武器时把 `veh+0x678` 存进槽 `+0x40`；
  每帧 `0x630446`：`weapon+0x788 = veh+0x398 × 槽+0x40`；开火 `0x697489` 把 `weapon+0x788` 交给发出的子弹（ctor 里同一值写进
  InitParam 头 `+0x830`，伤害在 InitParam+0x6C = `+0x89C`）。(H 写入；M「`+0x788` 就是每发伤害的乘数」)
- 所以一发原版子弹的伤害 = `weapon+0x89C × weapon+0x788`，插件在开火那一刻从武器本身读，不自己猜档位 / 难度。(M)

## 3. 碰撞层：只打建筑的射线（H）

`CollisionFilter` ctor `0x105510` 用 `0xD92E50(a, b)` 逐对开启 141 对层碰撞（参数是 `[rbx−k]`，按已知「22 层 ↔ {15,16,17,18,19,20,26}」
定出 rbx = 29）。整张表里：

- 22 层（游戏的打地图射线，`docs/raycast-re.md`）↔ {15, 16, 17, 18, 19, 20, 26}；
- **27 层 ↔ {15, 16, 17, 18, 20}**：正好是地图物体 / 建筑的创建代码用的层（`Preload_Structure` 等，`docs/raycast-re.md` §3），
  不含 19、26（地形和单位 / 载具在这两层里：直升机的 22 层射线曾打到僚机，见 heli.cpp CastRay 的注释）。
  27 层的使用者是子弹代码（`0x2C5560`、`0x2F9920` 经 `0x106060`），即子弹查建筑用的就是它。
- 12 层 ↔ {15..20}（含 19）；没有「只碰地形」的层。

插件的 `BuildingRay`（heli.cpp）= filter `0x1B` + 普通最近命中收集器。地形是否真在 19 / 26 层没有直接读到（M）：
若地形也在 27 层可见的层里，光束会被当成穿过地形（日志 `deep in buildings` 或爆炸落在山后），实机一看便知。

## 4. 地形能不能被摧毁：不能（M，接近 H）

- 地形的碰撞是 `IG_*.MAD` 里预先烘焙的静态刚体（`hknpCompoundShape` 套 `hknpCompressedMeshShape`，`docs/map-collision.md`），
  渲染是 FMB 网格（`docs/fmb-format.md`）；两者都是读盘时整块载入。(H)
- EDF.dll 里没有任何 crater / deform / terrain / dig / scorch 一类的游戏代码字符串；`deform` 只出现在 Havok 库自带的类名里
  （`hkpDeformable*`、`hclMeshMeshDeformOperator`、`hkdDeformableBreakableShape` 等，布料 / 破坏库）。(H：字符串扫描)
- 能被「打坏」的只有 MapObject（建筑、树、岩石，`MapObject_Base` 第 13 槽 `0x1267A0` 扣耐久、`0x126EF0` 判倒塌，`docs/drill-re.md` §3.2），
  地形不是 MapObject。(H)
- 结论：游戏没有改变地形的机制，插件也做不到（要在运行时重建压缩网格碰撞和渲染网格，没有引擎路径）。光束照到地形就停在那里、在那里爆炸。

## 5. 插件做法

- 4 个生成的 DemoIndirectFire（`pylib/vcobjects.py` EMC_*，`tools/make_emc.py`，安装器写入 `Mods/OBJECT`，ledger 主人 `emc`）：
  - `EDF6VC_EMC_BEAM.SGO`：卫星激光 `DEMOSATELLITELASER18`（`LaserBullet02`，#11 穿透 = 1）改成粗 12、蓝色、每帧一发、
    100 米/帧 × 6 帧（= 原版射程 600 米，安装时与原版武器的 `AmmoSpeed × AmmoAlive` 核对）、最多 330 发（5.5 秒），开火音效只放一次、
    命中音效音量 0（每帧一发会每秒响 60 次）。
  - `EDF6VC_EMC_SIGHT.SGO`：同上但细 0.6、暗、静音、最多 630 发（10.5 秒），蓄力时的炮口光。
  - `EDF6VC_EMC_BREAK.SGO` / `_BLAST.SGO`：炮舰炮弹 `DEMOGUNSHIPFIRESOLID`（`SolidBullet01`）改成 1 发、无散布、不穿透，
    爆炸半径 12 米 / 300 米（≥ 3 米 → 带「破坏建筑」位，`docs/drill-re.md` §3.1），弹体细 0.5、光束色（藏在光束里）。
- 发射都走 `jet_bay.cpp` 的 `Shell` 一路（CreateObject → IFC owner = EMC → 伤害写 `+0xDC` → 从 `+0x300` 直线射向 `+0x20`），
  新增 `EmcFire / RoundSteer / RoundSize / RoundBlast / RoundDrop`。IFC 的 `+0x100`（AmmoSize）/ `+0xF0`（AmmoExplosion）由 config
  `0x2B5F40` 写入（`0x2B6A15` / `0x2B68C5`，r14 = IFC，H）；子弹在发射时取 IFC 的 InitParam，所以改它们影响之后发出的子弹（M）：
  蓄力光每帧变粗、爆炸半径改成 `EmcBlastRadius`。
- 流程（`src/emc_plan.h`，`tools/emc_check.cpp` 离线核对）：按住扳机蓄力 `EmcChargeSec`，蓄满那一帧开火（之前松开 → 取消、
  蓄力以两倍速度退掉）；光束 `EmcBeamSec`；结束后 2 秒（原版 `FireInterval` 120 帧）不能再蓄，且要松开再按。
- 伤害预算（`Plan`）：一发消耗 `min(FireBurstCount, 剩余)` 发，`line = AmmoDamage × weapon+0x788 × 消耗`；光束每帧一发、
  每发 `line / (EmcBeamSec × 60)`，穿透 → 沿线每个敌人吃满 `line`；终点爆炸 `line × EmcBlastShare`。整个弹匣 7 发，总伤害与原版相同。
- 每帧沿炮口方向扫描（`ScanLine`）：22 层射线和 27 层射线比，先碰到建筑 → 记下（相隔 ≥ 9 米各给一个装药）、从建筑里 0.5 米处继续
  （射线从形状内部出发不会碰到这个形状）；先碰到别的（地形）→ 光束终点；什么都没有 → 600 米。光束和蓄力光的终点每帧更新；
  每 0.1 秒给记下的每栋建筑一个破坏装药（`EmcBreak × 0.1` 伤害，从建筑面前 3 米飞到面后 2 米）。
- 光束结束时删掉光束对象，在终点前 4 米朝终点后 2 米发出爆炸装药（终点是地形或建筑才发；600 米处是空中 → 不发）。
- 蓄力音：SEPRESET `ＷＤ武器チャージループ`（`weapon_WD_chargeIn_Bloop`，常驻的 `TIKYUUX_SE.ACB` 里，`SOUND/PC` 逐个 ACB 搜过），
  `0x7B2A80` 按名字播放到句柄，`0x7A8BF0` 每帧把音高从 0.6 升到 1.4，`0x7A8C20` 跟着炮口，开火 / 松开 / 换任务时 `0x7A8CD0` 淡出 6 帧
  再 `0x7A8730` 释放（`docs/sound-re.md` §2，入口字节都做了签名）。开火声是卫星激光自己的发射声（光束 SGO #17，只放一次）。
- 签名：扳机读取、5 处武器字段写入、5 个声音函数、IFC 两处字段写入；扳机或武器不符 → EMC 保持原版；声音不符 → 只是没声音；
  IFC 字段不符 → 蓄力光不变粗、爆炸用 SGO 自带的 300 米。
- 只接管玩家坐驾驶座的 EMC；NPC 的 AI 自己拉武器，不变。

## 6. 离线核对（`tools/emc_check.cpp`）

- 扳机：3 秒蓄力第 180 帧开火、2.5 秒光束第 330 帧结束；松开再按，第二发在 630 帧（2 秒间隔 + 3 秒蓄力）；蓄到一半松开 → 不开火、
  第 91 帧取消、1 秒内退空；没弹不蓄；最短设置 0.5 / 0.5 秒正好 30 / 60 帧。
- 预算：EMC / EMCS / EMCX 一发 6500 / 37500 / 62500（150 发 × 43.33 / 250 / 416.67），7 发，总和 = 原版弹匣（45500 / 262500 / 437500）；
  最后剩 400 发时一发只算 400 发。
- 扫描：替身直线世界（盒子建筑 + 一面山坡，射线从盒子里出发不碰它）逐帧跑：5 栋 1500 耐久 → 第一帧就看到后面的山、0 秒全倒；
  倒塌后碰撞再留 1 秒 → 同样（只是多发了装药）；10 栋 900 耐久的街区 0.1 秒；3 栋 5000 耐久的高楼 0.2 秒；12 栋（超过扫描步数）0.1 秒。
- 转动：破坏装药 12 米半径、每 0.1 秒一个，光束末端（600 米）每秒转 23° 以内不留缺口。

## 7. 未在游戏内验证（L）

1. 卫星激光改粗改色后的外观；每帧一发是否真的连成一道光束；穿透弹是否每发都打到沿线每个敌人（还是按帧多次 / 只第一个）。
2. 伤害是否被游戏再乘一次难度系数（插件直接写 IFC 的绝对值，与原版连射同口径：`+0x89C × +0x788`）；`+0x788` 的含义（M）。
3. 地形是否在 19 / 26 层（27 层射线看不到它，§3）；倒塌中的建筑碰撞何时消失（不影响扫描，只影响装药数）。
4. 300 米爆炸：特效、性能、范围内大量建筑同时倒塌的开销；友军在爆炸里是否被冲击推倒（伤害按阵营免除）。
5. 蓄力音的音量与音高；`0x7B2A80` 在声音预设找不到时是否安静返回（按代码是）。
6. 原版 EMC 的炮管转速下扫过建筑群的手感。
