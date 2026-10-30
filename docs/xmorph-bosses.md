# X-Morph: Defense 的首领（boss）移植：底盘映射与现状（2026-10-30）

用户原话：「boss 也移植」。立场（2026-10-27 定，见 `docs/xmorph-port.md`）：X-Morph 里玩家是外星人，首领是**人类一方**的单位，
所以在 EDF 里做成**友军**：可呼叫的超大型支援单位，能驾驶的给玩家 / NPC 驾驶；没有可行底盘的不做替代品，写证据。

## 首领是「部件拼起来的」（`pylib/xm_model.py assemble`）

每个首领的实体蓝图只带一个躯干网格，炮塔、腿、履带、机翼、旋翼都是 `ChildDescComp` 里的**子蓝图**（写在同一个 `.ent` 文件里，
各有自己的网格、骨架、`HealthComp`、武器，原作里逐个被打掉）。`assemble()` 把它们拼成一个模型：

- 子件写了 `bone`（或挂点）：网格在那根骨骼的绑定坐标系里（JAGUAR 的炮管挂 `be_main_turret_turret_vert`），根骨骼接到那根骨骼下；
- 没写：网格就在首领自己的模型空间里（实测：JAGUAR 的履带、侧炮塔、火箭巢不加偏移就在原位），根骨骼接到首领的根骨骼下；
- 子件的骨骼 / 挂点和已有的重名时改名 `<子件>/<名字>`（静态子件的唯一骨骼、各自的 `att_smoke_1`），它们的武器炮口一起改；
- 子件的炮塔与武器并入 `UnitModel.part_turrets / part_weapons`，`turrets()` / `weapons()` 照常读；子件可以再套子件。
- 两处原作数据的写法也一并处理：`RenderableDescComp.SubMeshes` 给网格换材质（TARANTULA Mk VI / VII 就是四足机甲的网格换
  `four_leg_mech_skin1 / _skin2`）；`ChildDesc` 的蓝图名后面可能跟设计者的注释（warcar：`middle_grenade_launcher -- obecny laser projectile`），只取第一个词。

只对首领拼装（`tools/prep_xmorph.py model()`：实体在 `bosses/` 下）；先进坦克和护盾车也有一个子件，那是护盾罩，不拼。

| 首领（原作名） | 部件 | 三角形 | 尺寸 宽×高×长（m，绑定姿态） | 原作武器 | 运动 |
|---|---|---|---|---|---|
| `highway_tank`（JAGUAR Type S） | 18 | 54,222 | 27.8 × 21.2 × 46.3 | 主炮（两种弹）、巡航火箭巢 ×2、防空导弹巢 ×2、侧炮塔 ×4、迫击炮、前激光炮塔、哨戒无人机发射口 ×2 | 履带 |
| `four_leg_mech`（TARANTULA） | 7 | 37,840 | 40.0 × 28.2 × 38.8 | 左炮塔、右火箭塔、上半身等 8 件 | 四足步行（Lua 驱动） |
| `tarantula_mk_vi` / `_mk_vii` | 7 | 37,840 | 同上 | 同上（`mkvi_` / `mkvii_` 子件，换皮） | 同上 |
| `giant_mech_minotaur`（MECHATAUR） | 11 | 54,141 | 93.1 × 51.2 × 26.5（绑定姿态双臂平伸） | 激光炮、导弹发射器、双手等 10 件 | 双足步行 |
| `gunship`（REDWING KM-2） | 28 | 46,519 | 47.4 × 21.6 × 58.4 | 炸弹、巡航导弹（前 / 后）、机翼机炮 / 激光 / 导弹、机腹机炮等 23 件 | 飞行（`MovementComp` 15） |
| `warcar` | 18 | 64,303 | 39.9 × 27.6 × 72.3 | 核炮、三联机枪 ×2、榴弹 / 激光 / 等离子 / 蜂群导弹、照明弹炮等 20 件 | 轮式（6 组轮），随从直升机挂点 `att_escort_1..6` |
| `gunship_drone` | — | 726 | 4.5 × 2.9 × 4.5 | 无武器：`AmmoComp` + `LifeTimeComp` | 弹药（自杀无人机） |
| `highway_tank_drone` | 1（护盾罩） | 1,304 | — | 一门炮塔 | 弹药：JAGUAR 的 `left_ / right_sentries` 以它为 `ammo_blueprint` 发射，有寿命 |
| `warcar_escort_helicopter` | — | 11,298 | 13.6 × 6.6 × 19.1 | 机炮、护盾光束 | 直升机：`large_helicopter_core` 的蓝图继承 |

## 每个首领的底盘

| 首领 | EDF 底盘 | 状态 | 依据 / 缺口 |
|---|---|---|---|
| JAGUAR Type S | V603（`tracked` 家族，`pylib/wt_tracked.py`） | **已移植**（第 94 行） | 履带车，和 7 辆履带车同一条管线；见下节 |
| TARANTULA / Mk VI / Mk VII | 步行机：`mech` 家族的底盘 | 等 `mech` 家族 | 见「步行机首领」 |
| MECHATAUR | 同上 | 等 `mech` 家族 | 同上，外加绑定姿态双臂平伸 |
| REDWING KM-2 | V506 机体（`heli` 家族；同无畏舰、沙扎比的插件机体） | 等 `heli` 家族 | 见「飞行首领」 |
| warcar | 轮式（`wheeled` 家族） | 等 `wheeled` 家族 | 见「warcar」 |
| warcar 随从直升机 | `heli` 家族（`large_helicopter_core` 的一行） | 等 `heli` 家族 | 继承重型直升机的网格，自带炮塔、武器、血量 |
| 两种无人机 | 不是单位，是首领武器的**弹药** | 随本体的武器移植 | 见「随从无人机」 |

`tracked` 以外的家族在本文写作时都还在各自的会话里建（`xm_units.py` 的 `wheeled / mech / heli` 当时都是 planned，没有生成器）。
首领要的只是「拼装好的模型 + 那个家族的准备步骤」：`prep_xmorph.model()` 已经对首领返回拼装结果，那个家族的准备函数用它即可，
登记仍是 `xm_units.py` 一行（`family` 填底盘家族，不再是 `boss`）。

### JAGUAR Type S（`xm_highway_tank`，已移植）

- 骨骼映射走 `rig()` 不用改：主武器 = 第一个由炮塔瞄准、yaw / pitch 都是骨骼的武器 `main_turret_artillery`（`be_main_turret_turret` /
  `_vert`）→ 主炮塔 `cannon_main`（含炮塔顶的导弹巢子件 `main_turret_turret_wreck`）、炮管子件 `main_turret_barrel` → `cannon_l`，
  其余 149 根骨骼（履带、负重轮、侧炮塔、火箭巢、迫击炮、灯、发动机）→ `body`。炮口 `att_main_turret_muzzle_1`（z 23.1）在炮管末端。
- 合成负重轮：履带是独立的闭合带（8 m 高），`running_gear` 原来取「接地段上方最低的朝上面」当带顶，会取到带的下回程内侧（0.44 m），
  轮半径只剩 0.18 m。加了唯一的例外 `BELT_FLOOR = {'xm_highway_tank': 1.0}`：朝上面从 1 m 起算，取到负重轮罩（2.88 m），轮半径 1.15 m。
- 武器：主炮 `main_turret_projectile`（每 10 秒，500 → 60000，冲击波 16 m → 24 m）；副武器 5 组（`xm_units.Arm`，第二扳机轮换）：
  巡航火箭、防空导弹（主炮塔顶）、四座侧炮塔高射炮、迫击炮、前炮塔激光，数值按原作换算（`xm_units.py` 该行注释）。
- 耐久 100000：原作各部件血量合计 17,850（主体没有 `HealthComp`，原作靠打掉部件），按 EMP 坦克 2000 → 12000 的 6 倍。
- 生成：`python tools/prep_xmorph.py tracked xm_highway_tank` → 模型包 `xm_highway_tank/`（12 MB）；
  `make_xmorph.py --kind xm_highway_tank --check` 用 Root.cpk 全量生成 + 检查通过（11 个文件，含座舱适配、碰撞、布娃娃检查）。
- 没移植：哨戒无人机（下节）、炮管的激光指示器（瞄准指示，无伤害）、部件逐个击毁、侧 / 前炮塔各自转动（随主炮瞄准，同重型坦克的机枪塔）。
- **未实机**：46 m 长的车在 V603 上的物理（爬坡、转向、撞建筑）、第三人称镜头距离（45 m）、炮口位置、补给运输机能否吊运这么大的车。

### 步行机首领（TARANTULA ×3、MECHATAUR）

- 原作的腿是独立子件，步态由 Lua（`lua/units/bosses/four_leg_mech.lua` 等）摆动骨骼，网格本身是刚性件；拼装后是绑定姿态
  （MECHATAUR 双臂平伸到 ±46 m，四足的四条腿伸开 40 m 宽）。
- EDF 侧现成的步行类：`Vehicle502_GroundRobo`（深渊爬行者，`docs/ground-ai-re.md`：Havok 角色胶囊 + 自己的骨架与动画）、
  `Vehicle501_FortressRobo`（巴尔姆 / 巴尔加）、`VehicleBigBegaruta`（Proteus）。仓库里**没有**把外来骨架接到这些类上的管线
  （V603 有 `wt_tracked` 这一套，步行类没有）；唯一的外来步行体是沙扎比，走的是 V506 机体 + 插件逐帧摆骨骼（`docs/gundam-plan.md`），
  步态代码是它专用的。
- 所以这四个首领等 `mech` 家族（轻型 / 重型机甲那个会话）定下步行机底盘后再做：那时它们是该家族的四行，需要的是该家族的准备步骤
  支持多条腿（四足：四组腿骨；MECHATAUR：两腿 + 双臂姿态）。在那之前不做替代品（不把它们放到履带底盘上滑行）。

### 飞行首领（REDWING KM-2）

- 28 个部件里有 10 个旋翼 / 发动机子件、6 个机翼子件；原作 `MovementComp.types none`，路径由 Lua 驱动，悬停 / 平飞。
- 对应 V506 机体（直升机，`heli` 家族，同时也是无畏舰、沙扎比用的插件机体：插件直接写速度、HP、开火、逐帧骨骼）。尺寸（58 m 长）
  比无畏舰小，`pylib/aircraft_collision.py` 按模型量碰撞的做法可以照用。等 `heli` 家族的准备步骤落地后是那里的一行（模型用 `assemble`）。

### warcar

- 72 m 长、6 组轮（`front_wheels_*`、`rear_wheels_*` 子件）、`MovementComp.speed 20`，武器 20 件，`att_escort_1..6` 是随从直升机的位置。
- 对应 `wheeled` 家族的底盘（轮式会话在定）。随从直升机是 `heli` 家族的一行（继承 `large_helicopter_core`，自带机炮与护盾光束），
  「随本体出动」= warcar 的呼叫同时派出随从：需要呼叫表支持「一个请求带多个单位」，现在的 `tools/calls.py` 一行只出一辆，
  这部分等两个家族都落地后一起做。

### 随从无人机

- `highway_tank_drone`：原作是 JAGUAR `left_ / right_sentries`（`MissileWeaponDef`，每 60 秒一发）的**弹药**：发射出去会飞、会转炮塔射击、
  有寿命的无人机。EDF 侧最接近的是插件已有的投掷式无人机（自带 AI 的飞行体），但那是步兵武器，不是载具的副武器；
  做成 JAGUAR 的第 6 组副武器需要「载具挂载发射无人机」这一机制，现在的 `vcobjects.STORES` 没有。未移植。
- `gunship_drone`：没有武器，只有 `AmmoComp` + `LifeTimeComp` + 移动：自杀式无人机，即 REDWING 的一种弹药，随 REDWING 的武器一起移植
  （导弹类挂载换外观即可）。

## 复核

- `tests/test_xmorph.py test_boss_assembles`（装有 X-Morph 时）：JAGUAR 18 个部件、炮管挂在俯仰骨骼下、炮口在炮管末端、迫击炮炮口在其炮管上、
  没有部件的单位拼装结果与 `load()` 相同。
- 其余首领只验证了能拼装（三角形都落在骨骼上、尺寸见上表），没有生成 EDF 文件。
