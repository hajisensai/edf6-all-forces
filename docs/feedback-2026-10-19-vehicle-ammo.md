# 2026-10-19 载具弹种盘点：按原型补缺失的弹种

用户原话（全文见 PR #212 的 `docs/feedback-2026-10-19.md`）：

> 坦克好像还有串联破甲弹或者破甲弹之类的弹种。你看看各种载具缺什么都补一下。

补充要求：破甲弹 / 串联破甲弹只是举例；**只有载具的现实或设定原型确实配有该弹种才加**，每个新增弹种注明依据。

全部为静态分析 + 离线生成 / 离线测试（读本机 Root.cpk，不启动游戏、不写游戏目录）。游戏内表现未实测。

## 1. 盘点

原型取本仓库已有的认定（`pylib/vcobjects.py` COAX_MOUNTS 注释：布莱克 = M1 艾布拉姆斯级，瓦利斯 = 10 式级，格雷普 = LAV-25 / 布拉德利级；BMPT、M163 按真车建模）。

| 载具 | 原型 | 原有弹种 / 武器 | 新增 | 依据 |
|---|---|---|---|---|
| 布莱克 505（5 个请求） | M1 艾布拉姆斯级 | 原版主炮（榴弹炮 / 滑膛炮 / 热熔枪）；APFSDS 或 HE（原版不是的那种）；LAHAT 炮射导弹；M240C 同轴机枪 | **HEAT-MP**、**M1028 霰弹** | M256 120 mm 炮列装 M830 HEAT-MP-T / M830A1 MPAT 与 M1028 canister |
| 瓦利斯 601 | 10 式级 | 原版高压榴弹炮；APFSDS；LAHAT；74 式同轴机枪 | **HEAT-MP** | 日本 120 mm 炮列装 JM12A1 HEAT-MP；无霰弹，不加 |
| BMPT（插件载具） | BMPT「终结者」 | 双 2A42 30 mm（高爆弹链）；9M120 阿塔卡（串联破甲） | **3UBR8 APDS 弹链**（双管一个选项）、**9M120F 温压导弹** | 2A42 是双路供弹炮（HE-FRAG / APDS 两条弹链）；阿塔卡家族有 9M120F 温压型 |
| 格雷普 401 | LAV-25 / 布拉德利级（M242 25 mm） | 原版炮；AP / HE 弹链（原版不是的那种）；AGM-114；M240C | 无 | M242 的 APDS-T / HEI-T 已有；APFSDS-T（M919）与 AP 弹链同一角色；可编程空爆弹只有 30 / 35 mm 炮有 |
| 艾普西隆 403 | 电磁炮（设定） | 电磁炮；两挺机枪；AGM-114 | 无 | 电磁炮只发弹芯 |
| 泰坦 404 | 设定超重战车 | 安魂曲炮、副炮、加特林、侧面榴弹 / 导弹；AGM-114 | 无 | 设定里没有其他弹种，不凭空加 |
| 奈格林 402 | 设定导弹车 | 原版火箭；AIM-120、AGM-65、Hydra 70 | 无 | |
| 开普勒 / 沃卢斯 603 | 双联高炮 | 原版机炮；AP 弹链；近炸高爆；AIM-9X、AIM-120 | 无 | 已有穿甲 / 近炸；驾驶席选项已到 8 个上限（`kMostPayload`） |
| M163（插件载具） | M163 VADS | M61 高爆 / 穿甲 | 无 | M56 HEI / M53 API 已有 |
| 弗利德 503 / 613 | 摩托 | 机枪；Hydra 70 | 无 | |
| 直升机 506 / 602 / 409 | 设定 | 机炮、导弹；Hydra 70、AGM-114、AIM-9X | 无 | 原型不明确；飞机导弹由别的会话处理，本次不动 |
| 自行榴弹炮、喀秋莎、钻头战车、边三轮、轿车、EMC、萨扎比、普罗透斯、机甲 / 巨型机器人 | 自建 / 设定 | 各自原版或插件武器 | 无 | 原型不明确或设定武器唯一；4.1 移植载具由别的会话处理 |

**串联破甲弹**：上表没有一辆载具的原型配串联破甲**炮弹**（125 mm 3BK31 属于 T-72 / T-90 的 2A46，这里没有这类坦克）。串联破甲战斗部已经有了：LAHAT（坦克）与 9M120 阿塔卡（BMPT）都是串联破甲导弹，所以没有另加。

**没加但原型有的**：BMPT 的两具 AG-17D 30 mm 榴弹发射器。自建模型没有发射器几何和炮口，要加需要先改模型，另开一项。

## 2. 新弹种的行为（与穿甲 / 榴弹区分）

游戏没有装甲模型：伤害对所有目标一视同仁。所以「对装甲 / 大型目标更好」只能落在**单发伤害、是否穿透、爆炸半径、弹速和弹道**上：

| | APFSDS（原有） | HE（原有，105 mm 榴弹炮） | HEAT-MP（新） | 霰弹 M1028（新） |
|---|---|---|---|---|
| 弹类 | SolidBullet01Rail | RocketBullet01 | RocketBullet01（HE 模板改） | SolidBullet01Rail（APFSDS 模板改） |
| 伤害 | 350 | 350 | **450**（单一目标） | 12 枚 × 40 |
| 爆炸半径 | 0 | 8 m | **3 m**（破片） | 0 |
| 穿透 | 是（穿过一串敌人） | 否 | **否**（射流止于所中之物） | 是（钢珠穿软目标） |
| 弹速 | 18 m/帧（1080 m/s） | 3 m/帧，重力 1 | **13 m/帧（780 m/s），重力 0.15** | 14 m/帧，0.07 散布 |
| 射程 | 1800 m | — | 1820 m（140 帧） | **约 500 m**（36 帧） |
| 接触球 | 0.6 m | 1.6 m | 0.8 m | 0.3 m |

HEAT 弹速取真实比例：M830 1140 m/s 对 M829 1575 m/s，约为 APFSDS 的 0.72。

BMPT 的 APDS 弹链：在原生 30 mm 高爆弹链的数值上改为穿透、无爆炸、18.7 m/帧（3UBR8 约 1120 m/s 对 HE-FRAG 约 960 m/s）、伤害 30（高爆 22）、150 发。9M120F：飞行与 9M120 相同，伤害 600（串联 1200），爆炸 14 m（串联 6 m），弹体换了一个模型（同一个按键循环里的弹不能长得一样，`selftest store_looks` 的约定）。

## 3. 实现

- `pylib/vcobjects.py`：`Shell.kind` 新增 `heat`、`canister`（`ROUND_KINDS`）。`_check_round` 先按模板的弹族检查（破甲弹必须来自有爆炸的原版炮弹，霰弹来自无爆炸的），再在改完数值后检查弹种含义（破甲：一发一弹、不穿透、有爆炸；霰弹：多弹丸、无爆炸）。新 STORES：`HEAT`、`CANISTER`、`BMPT_AP` / `BMPT_AP_R`（group 3，两根炮管一个选项，与高炮的做法相同）、`BMPT_ATGMF`。
- `tools/make_stock_stores.py`：LOADOUTS 改为按原型（`_BY_STEM`）：布莱克 = APFSDS/HE 对 + HEAT + 霰弹 + LAHAT + 同轴；瓦利斯 = APFSDS/HE 对 + HEAT + LAHAT + 同轴。都挂在主炮挂点旁，各自是切换（R / LB / 挂载菜单）的一个选项，炮口是原版主炮的 `(0, 0, 2.8)`。`REQUIRED['mbt']` 加 `HEAT`，`PROTOTYPE` 记布莱克的 `CANISTER`。
- `tools/make_tracked_combat.py` + `tools/call_weapons.py`：BMPT 的挂点从 3 个变 6 个（`BMPT_SLOTS`：左炮、右炮、导弹、APDS 左、APDS 右、温压导弹），APDS 弹链用原生炮的 MAB（炮口、抛壳口与高爆弹链完全相同），请求的 2 → 6 挂点契约同步改。`tools/calls.py` BMPT 说明文字同步。
- 瞄具弹种标签：HEAT 有爆炸半径，`reticle.h ShellOf` 会把它标成 HE。`tools/gen_stores.py` 按弹种在 `kStores` 写 `StoreShell`，`vhud.cpp Arm` 对目录内的挂载弹优先用它（`HEAT` / `CAN`）。

## 4. 测试

- `tests/test_stock_store_loadouts.py`：两辆坦克都有 HEAT，只有布莱克有霰弹；HEAT 与霰弹的数值含义；有游戏时生成的弹与模板只差声明过的字段，HEAT 伤害高于、爆炸小于、弹速快于模板；座席选项上限按各类实际原生武器数计算。
- `tests/test_tracked_combat.py`：BMPT 6 个挂点；APDS 弹链与高爆弹链同骨骼、同 MAB、穿透无爆炸、更快；温压导弹与串联导弹同发射管，爆炸更大、伤害更低。
- `tests/fixed_weapon_sight_test.cpp`：生产 `Arm` 下，带 HEAT 挂载的爆炸弹标 `HEAT`、霰弹标 `CAN`，普通挂载和非挂载武器保持瞄具自己的判断。
- `tests/test_tracked_install.py` / `test_tracked_range.py`：BMPT 文件数按 `GROUND_VEHICLES` 推出（4 + 武器数）；`gen_spawn_catalog.py`（含 `--archives`）重新生成。
- `gen_stores.py --check`、`gen_calls.py --check`、`gen_spawn_catalog.py --check`；插件 `/W4 /WX` 构建。

## 5. 未实测（需进游戏）

- RocketBullet01 以 13 m/帧飞行时 HEAT 的命中和爆炸表现（原版有 20 m/帧的 RocketBullet01：DLC2 动力 GC 炮）。
- SolidBullet01Rail 配 `FireCount 12` 的霰弹是否每发都生成 12 枚弹丸（原版霰弹炮是 SolidBullet01 + FireCount 10；Rail 类没有原版先例）。
- 切换到 HEAT / 霰弹时瞄具显示 `HEAT` / `CAN`，弹道提前量按所选弹的弹速。
- BMPT 的 APDS 弹链切换后两根炮管都换弹、AI 炮手是否会选用。
