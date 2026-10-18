# 2026-10-18 EDF4.1 第 16 关「要塞围攻」（M117）必崩

## 用户原话（测试站 report #19 / #20，planner_c85876，build `EDF6VehicleCrew-0.8.0-build.b4faa8e`）

> EDF4.1 16号任务"要塞围攻"   游戏推进到一定进程（4足步行要塞投放小无人机被全部打爆后大概2分钟），游戏会直接崩溃。换了兵种、难度，不使用M召唤友军试过，都会崩溃

用户本人：「4.1的16号任务过不去，游戏进展到一定程度的时候100%崩溃」。

## 证据

证据分级：**H** = 读 EDF.dll / EDF41.exe 机器码或实机日志直接确认；**M** = 多条间接证据一致；**U** = 推测、未核。

### 1. 崩溃点（H）

report #20 的 `EDF6Coop.log` 记了三次（16:50:34、16:59:42、17:05:02），每次完全相同：

```
EXCEPTION C0000005 at EDF+4C3A30 reading 000000000000000C
#00 EDF+4C3A30  #01 EDF+4C6339  #02 EDF+1DA4B8  #03 EDF+212A6C  #04 EDF+10AC259 ...
```

逐帧对应（EDF.dll TimeDateStamp 0x678CCB46）：

| 帧 | 函数 | 是什么 |
|---|---|---|
| `EDF+212A6C` | `0x20FF70` | BVM 原生分派器，`0x212A36` 是原生 3200 `ObjectAction(句柄, int)`：取对象后 `SendMessage(obj, 0x1000000D, &int)`（docs/edf41-ufomother301.md 已核） |
| `EDF+1DA4B8` | `0x1DA480` | 三步消息分派，调对象虚表槽 9 |
| `EDF+4C6339` | `0x4C6240` | `MovingFortress` 槽 9（插件 `edf41fortress.cpp` 钩的就是它，钩子原样转给它） |
| `EDF+4C3A30` | `0x4C37E0` | 0x1000000D 动作 **1** 的处理：撒瓦砾 |

`0x4C6240` 对 0x1000000D 的分派（H）：0 → 目标模式 = 1（站起），**1 → `0x4C37E0`**，2 → `0x4C43D0`（立刻站起），3 / 4 → 对 6 个组各调 `0x4CA7E0(this, 1<<i, 1/0)`（全部部件开 / 关），其余交基类 `0x54A530`。

`0x4C37E0`（H）：`0x528A0(&this+0xA0, L"debris")` 取 SGO 的 debris 节点，按节点里的定位点名调 `0x6BADD0(&this+0x5E0, &out, 0, 名)` 在模型里找定位点，**不判空**就读 `[out+0xC]`（`0x4C3A2C: mov rax,[rbp-10h]` / `0x4C3A30: movsxd rcx,[rax+0Ch]`）；之后在定位点周围用 `0x11945E0` 造瓦砾对象。寄存器 `rax=0` 与「读 0xC」吻合：定位点没找到。

debris 节点来自 `tools/make_edf41_objects.py` 的 `FORTRESS_ALIAS`：4.1 要塞套 EDF5 移植的 `E510_FORTRESS.SGO` 键，debris 指向 E510 的定位点 `瓦礫基準点`，4.1 的 MAB 没有这个定位点（生成器里原注释已写明，H）。原注释的判断「类只在 0x1000000d / 1 时读它，4.1 的任务从不发」是错的。

### 2. 4.1 的任务确实发 ObjectAction(要塞, 1)（H）

`edf41port/missions/M117/MISSION.BVM`（pylib/bvm41 解析）：全局 38 由 `Main` 用 `CreateEnemy(…, app:/object/Alien4Legtank301.sgo)`（原生 1020）建出，脚本对它发的 ObjectAction：

| 函数 | 值 | 怎么到这里 |
|---|---|---|
| `Func_23` | 0 | `Func_13`（要塞的生成器投放 Ufo301）→ 原生 9300 等这批全灭 → … → `Func_17` → `Func_21` → `Func_23` |
| `Func_24` | **1** | `Func_23` 之后 35 秒的计时器 |
| `Func_25` / `Func_26` | 2、4 / 3、5 | `Func_19` 之后 |
| `Func_1` | 6 | 最后一波后：轰炸机演出、`MissionClear` |

「无人机全灭后约 2 分钟崩」= `Func_13` 的 Ufo301 全灭 → 若干计时 → `Func_24` 的 ObjectAction(1)。和日志的分派链一致（M：计时总长未逐个加，只按结构对上）。

其它带 4.1 要塞的任务：M133 发 0、4、2；mp01 M416 对一台要塞发 6；M134 / M152 / M992 不发（selftest 扫的结果，H）。

### 3. 4.1 的 ObjectAction 是另一套编号（H）

EDF41.exe `Alien4LegTank` 虚表 0xA967B8（RTTI `.?AVAlien4LegTank@@`），槽 6 = `0x1CA500`：消息 `0x80000009`（4.1 的 ObjectAction）按 `[data]` 查 7 项跳转表 `0x1CA5EC`（`cmp eax,6; ja` → 返回 1、什么也不做）：

| 动作 | 4.1 做什么 |
|---|---|
| 0 / 1 | `0x1C9B50(this, 0, 1/0)`：第 0 组部件（炮）开 / 关 |
| 2 / 3 | `0x1C9B50(this, 1, 1/0)`：第 1 组（加特林） |
| 4 / 5 | `0x1C9B50(this, 2, 1/0)`：第 2 组（bit） |
| 6 | `0x1C9520`：没死过就 `0x1C7700`（HP 0、死亡标志 `+0x1E8 |= 0x10`）再进毁坏状态 `0x1CBB00`；`0x1CBB00` 也是血量打空时进的状态（`0x1CA6EA` / `0x1CA906` 也引用它） |

`0x1C9B50` 遍历部件表（`+0x920`，每项 0x20）中组号 `[+0x18] == group` 的部件，写部件的启用标志。

所以根因：**同一条消息两边编号不同**，插件只钩了 0x20100 / 0x20103，0x1000000D 原样交给 EDF6 的类，4.1 的「关炮」变成了 EDF6 的「撒瓦砾」，读了 4.1 模型没有的定位点。即使不崩，其它几个值也都跑错（2 = 立刻站起、3 = 全部开、4 = 全部关、5 / 6 交基类不做事：6 本应让要塞倒下）。

### 4. EDF6 这边对应的做法（H）

- 组开关：`MovingFortress` 读 `parts_setting` 时组号存成掩码 `1 << [2]`（`0x4C13F8..0x4C1403`），`0x4CA7E0(this, 掩码, on)` 遍历部件表（`+0xE38`，每项 0x50）中 `[+0x28] & 掩码` 的部件调 `0x4DB8E0(部件, on)`。类自己的 3 / 4 就是用它。4.1 组 g ↔ EDF6 掩码 `1 << g`；`FORTRESS_PARTS_SETTING` 的组号照搬 4.1（0 炮、1 加特林、2 bit）。
- 毁坏：`0x4CB6D0(this)`：`[+0xE69]` 为 0 时（槽 0x108 为假则 `0x54DDB0`）HP = `min(0, 最大 HP)`、`[+0x2E8] = 1`、`[+0x2E9]`= 首次、状态机（`+0xDC0`，`0x4CA110`）切到毁坏状态 `0x4CAE70`（播 `destroy`，生成器把 4.1 的 `down` 改名成它）。受伤后的槽 18 `0x4C60E0` 见 `[+0x2E8]` 已置且 `[+0xE69]` 为 0 时切进同一个 `0x4CAE70`（H；`[+0x2E8]` 在血量打空时由谁置位没有逐条读到，M）；联机客人在 Update `0x4C6A1A..0x4C6A99` 里内联了与 `0x4CB6D0` 一字不差的序列。`0x4CB6D0` 本身在 EDF.dll 里没有直接引用（只有 .pdata 项），是类的完整成员函数。

## 修复

- `src/edf41fortress_action.h`（新）：4.1 的 7 个动作 → `{组开关(掩码, on) | 毁坏 | 不做事}`，纯函数。
- `src/edf41fortress.cpp`：消息钩子对带 `edf41_fortress` 标记的要塞，0x1000000D **全部自己处理、返回 1**（与 4.1 一致），不再交给类的表：0..5 调 `0x4CA7E0`，6 调 `0x4CB6D0`，表外的值只记日志。安装时核对两个函数开头 8 字节，不符则不挂钩。EDF6 自己的要塞（无标记）不受影响。
- `tools/make_edf41_objects.py`：改掉「4.1 任务从不发 1」的错误注释（debris 键保留：每个原版同类 SGO 都有，只有类自己的动作 1 读它，而插件不再让 4.1 要塞走到那里）。

没有用 try / 吞异常 / 判空跳过：问题不是「定位点找不到」本身，而是消息被错当成 EDF6 的动作 1。

## 测试

- `tools/edf41_fortress_check.cpp`（ctest，进 `EDF6_OFFLINE_CHECKS`）：7 个动作逐一对上 4.1 的跳转表；表外值什么也不做；每个组动作只碰一组。
- `tools/selftest.py edf41_fortress_object_actions`：扫 `edf41port` 下全部 4.1 任务脚本，对 4.1 要塞发的 ObjectAction 都是 0..6 的常量（M117 恰好 0..6 各一次，M416 有 6）；钩子在任何 `nextMessage` 之前先接 0x1000000D、`Edf41Action` 不转给类；有游戏时核 `0x4CA7E0` / `0x4CB6D0` 开头字节、`0x4C6334` 调 `0x4C37E0`、`0x4C3A27` 调 `0x6BADD0` 后不判空的那两条指令。

## 实机（autopilot 后台，本机 EDF.dll 0x678CCB46）

推进办法：`python tests/autopilot/drive.py run edf41:15 0 900 <log> --place <build>/EDF6VehicleCrew.dll=Plugins/EDF6VehicleCrew.dll --ini Edf41DevShoot=2 --ini Edf41DevShootDamage=3000 --ini Edf41Mother401Dev=32896`（edf41:15 = M117，难度 0；32896 = 32768「DevShoot 也打普通敌人」+ 128「玩家装甲保持满」，脚本的全灭等待因此能推进）。每次跑前备份、跑后逐字节还原存档目录（`saves restored byte for byte`），插件 DLL / ini 由 drive.py 逐字节还原。

**修复前（origin/main 1fb6931e 构建）**，H：

```
[17:37:38.117] EDF41FORTRESS 000002A0D2AF2010 stands up (mode 0 -> 1)
[17:37:56.134] EDF41FORTRESS 000002A0D2AF2010 message 20100 generator 0 -> 1
[17:41:54.331] EDF41FORTRESS 000002A0D2AF2010 message 20100 generator 0 -> 1
EDF6Coop.log [17:43:21.648] EXCEPTION C0000005 thread 30476 at EDF+4C3A30 reading 000000000000000C
  rsi=000002A0D2AF2010 rdi=000000001000000D ... #00 EDF+4C3A30 #01 EDF+4C6339 #02 EDF6VehicleCrew.dll+1698CC #03 EDF+1DA4B8 #04 EDF+212A6C
```

与测试者的栈相同（多出的一帧是本插件的消息钩子），`rsi` 就是日志里那台要塞。dump `%LOCALAPPDATA%\CrashDumps\EDF6.exe.81820.dmp`：`0x4C6240` 帧的消息数据（`r14 = 000000aa375fec00`）里是 `00000001`，即 ObjectAction(1)。进任务约 5 分 50 秒崩。

**修复后（本分支构建）**，H：同一命令跑满 900 秒，游戏到时由 drive.py 正常退出（`drive exit 0`），无新 dump，`EDF6Coop.log` 无新异常：

```
[17:55:04.592] EDF41FORTRESS 00000263D7005DC0 ObjectAction 0 (cannons on)
[17:55:39.659] EDF41FORTRESS 00000263D7005DC0 ObjectAction 1 (cannons off)
[17:56:13.905] EDF41FORTRESS 00000263D7005DC0 message 20100 generator 0 -> 1
[17:59:40.116] EDF41FORTRESS 00000263D7005DC0 message 20100 generator 0 -> 1
```

崩溃点（ObjectAction 1）之后又跑了约 9 分钟、要塞又投放了两批。

**修复后，跑到任务结束**（H）：同一命令改 `--ini Edf41Mother401Dev=295040`（再加 262144「DevShoot 一次打一批」）、1500 秒。4.1 的七个动作全部按 4.1 语义执行，`Func_1` 的 6 走 `0x4CB6D0` 后约 48 秒脚本进结算（autopilot 记到 `Result_Offline()` 协程，任务开始后 1140.5 秒），游戏到时正常退出（`drive exit 0`），无新 dump、`EDF6Coop.log` 本局无异常：

```
[19:01:30.476] EDF41FORTRESS 00000217CC949870 ObjectAction 0 (cannons on)
[19:02:05.519] EDF41FORTRESS 00000217CC949870 ObjectAction 1 (cannons off)
[19:12:04.944] EDF41FORTRESS 00000217CC949870 ObjectAction 2 (gatling on)
[19:12:04.944] EDF41FORTRESS 00000217CC949870 ObjectAction 4 (bits on)
[19:12:39.986] EDF41FORTRESS 00000217CC949870 ObjectAction 3 (gatling off)
[19:12:39.986] EDF41FORTRESS 00000217CC949870 ObjectAction 5 (bits off)
[19:14:17.755] EDF41FORTRESS 00000217CC949870 ObjectAction 6 (destroyed)
[  1140.5 s] COROUTINE ... "string Result_Offline()"
```

未核（U）：要塞倒下的画面（没开 `--shots`）、结算是「成功」还是别的（`Func_1` 末尾调 `MissionClear`，玩家装甲被 dev 位保持满，推断为成功）、部件开关在画面上的效果、联机（脚本消息只在主机还是各机执行，与类原有的 3 / 4 同路径，未测）。存档每局跑后逐字节还原，任务进度不保留。
