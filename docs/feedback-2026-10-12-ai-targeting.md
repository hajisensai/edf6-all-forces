# 2026-10-12 AI 打金色飞船浪费子弹、战斗机对空不如直升机（ai-targeting）

## 用户原话

> ai还会无脑攻击金色飞船浪费子弹，应该只有能打到弱点的时候才开火
>
> 战斗机对空能力甚至不如直升机

## 一、金色飞船（传送舰 UfoCarrier）：只在打得动的时候开火

### 游戏怎么判定一发打没打疼（静态逆向，EDF.dll 0x678CCB46，H）

- 伤害结算 `0x547C30` 在扣血前调目标的 slot 16（`0x54808A`，目标 `+0x380` 有 `0x800` 时跳过）。返回假且 GameDamageInfo `+0x60` 没有 `0x40` 时，`0x548092 → 0x5480C7 → 0x548625` 直接退出，**HP 不变**。
- UfoCarrier（508 vtable `0x17C9DC0`、基类 `0x17C9930`）的 slot 16 = `0x4F7190`：
  - `+0xD85 == 0` → 一律返回假（整艘打不动）；
  - 否则取离命中点最近的那条命中记录（GDI `+0x78` 数组、`+0x88` 条数、每条 0x30 字节，`+0x20` 是部件），经 `0x6EA430` 换成部件号，**只有等于核心部件 `+0xD8C`（SGO `UfoCarrier_CorePartsName`）才返回真**。
- `+0xD85` 由传送舰 CAS 动画事件写：「ダメージ許可」→ `0x4F6590`（置 1）、「ダメージ禁止」→ `0x4F6580`（置 0），注册点 `0x4F68DF` / `0x4F69F7`；构造时 `0x4F61C8` 置 0。即**舱门开合动画决定能不能打疼**，开着时也只有打进核心才算。
- 打在船壳上的子弹照样进结算、照样走到 `0x547C30`（目标就是传送舰），只是 HP 不变——所以插件已有的伤害钩子能看到「这一发打中了但没打疼」。

### 根因

插件所有 AI（步兵 `npcai.cpp`、炮手 `NpcGunnersInput`、驾驶位挂载 `DriverPayloadAim`、直升机 `heli.cpp ForEachTarget`、战机 `jet_combat.cpp VisitTarget`、地面机器人 `ground.cpp Consider`）选目标只看距离 / 射程 / 视线，从不问这一枪能不能造成伤害；原版 AI 也一样（插件只在 `Veto` 里替它挡友伤）。于是舱门关着时所有单位对着船壳倾泻弹药。

### 修法

新增 `src/wasted_fire.h`（纯逻辑）+ `src/wastedfire.cpp`（接线），对外 `WastedFire(shooter,target)`：

1. **确定的门**：目标是 UfoCarrier、`+0x380` 无 `0x800`、`+0xD85 == 0` → 谁都不打（不选为目标、扳机拿掉）。这与游戏自己的判定同一字段，不是猜。
2. **按射手的反馈**：伤害钩子（`damagestats.cpp DamageHook`，原本只给统计页用，现在无论统计开关都喂这一份）把每次结算的「攻击者、目标、HP 是否下降」交给 `WastedFireHit`。同一射手对同一目标**连续 4 发命中都没掉血** → 这个射手对这个目标停火 6 s，到期再试（舱门可能开了、它可能走到了船底）；期间任何一发打疼立刻解除；超过 15 s 没再打中则重新计数。这覆盖舱门开着但站位打不到核心的射手，也适用于其它任何「打中不掉血」的情况，不按敌人种类写特例。
3. **接到所有 AI 选目标 / 开火处**：`npc_logic.h PickTarget` 加 `usable` 谓词；`npcai.cpp` 的步兵选目标、标记目标、`Veto`（含原版 AI 自己的目标）、炮手 `GunnerVisit` 与标记、`DriverPayloadAim`；`heli.cpp ForEachTarget`；`jet_combat.cpp VisitTarget`（地图集火的明确命令仍保留）；`ground.cpp Consider`。
4. 日志：开始停火时一行 `WASTEDFIRE <射手>: 4 hits on <目标> hurt nothing: its fire at it held 6 s`；关舱的船第一次被跳过时一行 `WASTEDFIRE ship <p>: its hatch's damage window shut`。

### 关于「金色飞船」是哪一种

EDF6 里会出现「只有弱点能打」的飞船是传送舰（`E508_CARRIER*`，类 UfoCarrier508），本修复的确定门针对它。金色 UFO（`E507_GOLDUFO`，耐久 75）和帝国 UFO（`E515_IMPERIALUFO`）的 SGO 没有部件判定参数；若用户说的是别的金色单位，第 2 条反馈机制同样生效（只要它确实「打中不掉血」）。

## 二、战斗机对空不如直升机

### 证据（本机游戏日志 `Mods\Plugins\EDF6VehicleCrew.log.1`，17:09–17:10）

三架 multirole 追飞行目标（目标根部 y≈700 m，`JET target … flies`），距离 22–760 m，`msl=2`，模式在 `missile` / `crank` 间来回，**每一行 `fire=0/0`**；全日志没有一行 `missiles:`。

### 根因

`jet_combat.cpp Attack`：只要还有导弹，对任何飞行目标都走 `Missile`（视距外对峙）：机头对准 2.5 s 有锁定才发；目标近于导弹射程 45%（≈500 m）就 `kTurnAwayAngle` 转开、`crank` 4.5 s；这一分支里 `gunsOk` 恒为假。EDF 的飞行怪都在几百米内成群，于是战机永远在转开—再转回之间，机炮从不放开，导弹也锁不满。直升机悬停直接开枪，所以反而强。

### 修法

飞行目标在机炮射程（`GunReach` = 武器射程与 `gunOpen` 取小）内就是**狗斗**：走 `Chase`（机炮，导弹锁上了也照发），离开到 1.5 倍射程才回到对峙（防止边界来回切）；机炮射程外仍用导弹对峙。`Fire` 的机炮射程同用 `GunReach`。

### 试过又撤回的

低空飞行怪（离地 < pullAlt+60 m）改走对地扫射航线（`Strike`）：离线仿真里比直接狗斗更差（多数用例 0 帧），已撤回。

## 测试

- `tools/wasted_fire_check.cpp`（新，CTest `wasted_fire_check`）：15 例——连续 3 发不停火、第 4 发停火、停火期间继续命中不新开停火、换射手 / 换目标不受影响、6 s 到期再试、一发打疼立即解除、间歇打疼永不停火、超 15 s 重计、表满淘汰最久未命中的。变异（`>=` 改 `>`）实测被杀（3 例失败）。
- `tools/npc_ai_check.cpp`：`PickTarget` 的 `usable` 两例（跳过即使是当前目标、全部浪费则无目标）。
- `tools/npc_core_check.cpp`（走生产 `Drive`）：对浪费目标不选、原版 AI 对它扣的扳机被拿掉；解除后重新选中。
- `tools/jet_obstacle_sim --selftest`：`limits` 组 +2（战机跳过浪费目标、手里的浪费目标被放掉）；新 `air` 组：从巡逻圈（1400 m+）接近低空 / 高空飞行目标，修复前机炮 **0 帧**（导弹 2.5–7 s），修复后 2.1–7.3 s 开火、212–570 帧；600 m 正上方 / 300–400 m 正下方开局与「1400 m 外 40 m 高」作 info 打印（首个通过机头压不下去，绕一圈后 22–29 s 才开火，拦截机 600 m/80 m 40 s 内未开火）。
- 结果：`cmake --build build`、`--target offline_checks` 退出码 0；`ctest` 226 项退出码 0（原生 DLL 测试按 CI 同样 Skipped）；`tools/selftest.py` 见 PR。

## 未验证 / 缺口

- **未真机验证**：没有在游戏里对传送舰实测（需要舱门开合的任务或测试场生成传送舰 + NPC 齐射）。实机看日志：`WASTEDFIRE ship …` 出现在舱门关时、`WASTEDFIRE <射手>: 4 hits …` 出现在船壳上的射击；舱门开后船底下的射手应继续开火并打掉 HP。
- `+0xD85` 语义来自静态逆向（CAS 事件名「ダメージ許可 / ダメージ禁止」），未在运行时读数核对。
- 攻击者归属：NPC 士兵的弹头 owner 是士兵、载具武器是载具（`docs/damage-stats-re.md` §2.1）；炮手座位的弹按载具记，同车多座位共用一份记录。
- 反馈机制每次再试仍要打出 4 发命中（每 6 s 一次）才会再停火；舱门关时的确定门不费弹，开着时站位打不到核心的射手每轮最多浪费 4 发命中。
- 战机：战斗机从正上方 / 正下方近距离开局、或 1400 m 外对 40 m 高目标时，第一个通过受地面防撞（`Guard` 的拉起预测）限制压不下机头，要绕一圈；对地扫射从同样距离开局也一样（既有限制，本次未改）。战机修复未真机验证。
