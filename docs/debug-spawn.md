# 调试召唤：生成路径的静态依据

`src/debug_spawn.cpp` / `src/debug_spawn.h`，ini `DebugSpawn`（默认 0）。EDF.dll TimeDateStamp `0x678CCB46`，地址均为 RVA。
本页只记静态逆向（`tools/edfre.py` 反汇编本机 EDF.dll），未启动游戏；实机行为见末尾「待实机验证」。

## 没有自己的钩子

- 预载：`mission.cpp MissionStart` → `PreloadDebugSpawn()`（和 `PreloadSupportVehicles` / `PreloadSupportSoldiers` 同一时机，任务 `WaitPreload` 之前）。`DebugSpawn=0` 时第一行就返回。
- 每帧：`map.cpp MapHumanFrame`（玩家士兵的 pre-update）和 `crew.cpp FrameTick`（坐在载具里时）都调 `DebugSpawnFrame`，按 `GameFrame()` 每帧只跑一次。关闭时在读任何按键之前返回。
- 绘制：`hud.cpp HudDraw` → `DebugSpawnHud`，读 `DebugSpawnReadout`（关闭时返回 false）。

## 原版敌人：脚本 `CreateEnemy`

脚本声明 `::Object CreateEnemy(const string & in, const string & in, float , bool )`（字符串 `0x17962C0`）在注册函数里的 native 是 `0x1AD220`（`0x1EA27E lea rcx,[rip-0x3D065]`；同法核对 `CreateFriend` = 已知的 `0x1B0310`）。

`0x1AD220`：解析点名（`0x6F83B0`）→ 填创建描述符 → `0x1D8900(ctx+8, &out, &desc)` → `0x1BEA30` 包成脚本 Object。描述符：

| 偏移 | CreateEnemy 写入 | 地址 |
|---|---|---|
| +0x80 | level（脚本参数） | `0x1AD370` |
| +0x84 | bool 参数（测试场传 true） | `0x1AD37C` |
| +0x85 | 1（初始化 `word 0x100`） | `0x1AD2F2` |
| +0x86 | 0（不贴地） | `0x1AD2F8` |
| +0x88 | **1 = TEAM_ID_ENEMY** | `0x1AD37F` |

`0x1D8900`：`0x1D9C20` 里 `CreateObject 0x11945E0(objectMgr, &matrix, sgo, &InitParam{vt 0x1762068})`（`0x1D9D7E`，与插件其它生成同一个 InitParamBase）→ `__RTDynamicCast(obj, 0, SceneObject 0x2006450, GameObjectBase 0x2006400, 0)`（`0x1D893E`；thunk `0x12DA7AA`）→ `SetTeam 0x54EE70(obj, team, 1)`（`0x1D8A60`）→ `+0x85` 时 `SetLevel 0x54E740(obj, ctx[0x280+team*4]*level)` → `+0x84` 时 `0x548D50(obj)`（`0x1D8A9E`）→ `0x1DB1E0` 加入任务对象组。

`0x548D50`：`+0x4A0` 已置位则返回；否则按 `+0x380` 的状态位处理（bit 24 时经 vfunc `+0xB8` 更新 bit 22，并按 team 通知 `0x5E0B70` / `0x5E1C60`），置 `+0x4A0 = 1`，调 vfunc `+0xD0`，尾调 `0x54D180`。即 CreateEnemy 的 bool = 生成后立即激活。

插件做法（`SpawnEnemy`）：`CreateObject` → 原生 `__RTDynamicCast` 到 GameObjectBase（失败或指针偏移则删除并报原因）→ `SetTeam(1, true)` → `SetLevel(1.0)` → `0x548D50`。与脚本的差别：

- level 的队伍系数 `ctx[0x280+4]` 只有脚本对象 ctx 有，插件拿不到；取 1.0（团队系数初始为 1，`jet_spawn.cpp` 同样取 1）。`0x54E740` 自己按任务难度和 SGO 的 `game_object_level_adjust` 缩放。
- 不调 `0x1DB1E0`：脚本不按名字等待它；但 team 计数（`GetTeamObjectCount(TEAM_ID_ENEMY)`）会算上它。

## 原版载具：空车

脚本 `CreateVehicle2`（`0x1B28C0`）：同一 `0x1D8900`（team 5）→ cast 到 VehicleBase `0x2006480`（`0x1B2AFF`）→ `0x633280`。`0x633280` = 读 `mission_setup`（`0x62D6E0`）→ slot 46 → 写 `+0xE30 = 2`（空投状态）→ 按变体表 `0x1765220` 析构临时 setup。

插件用地面支援已在用的独立 setup 路径（`support_spawn.cpp`，见 `docs/support-ground-spawn.md`），把其中的 setup 步骤导出为 `ApplyMissionSetup`：`CreateObject` → 原生 cast 到 VehicleBase → `ApplyMissionSetup` → `SetTeam(2, true)` → `SetLevel(1.0)` → `NoteLocalCopy`。不写空投状态，不上任何 Dummy / RideAi。每个 SGO 都确认带 `mission_setup`（`tests/debug_spawn_resource_audit.py`；呼叫型 SGO 没有它会崩，`testrange/gen.py`）。

## 插件飞机

- **有人驾驶的机种**（8 种喷气机、3 种直升机）：最初版本用 `JetLaunch` / `HeliLaunch` + `HeliCalled` 直接在空中生成，这在 main 上不成立——真实机组（#88）之后空中生成的有人机没有驾驶员，`heli.cpp HeliFrame` 判 `!NpcDriver` 直接返回，飞机自由落体（2026-10-09 日志）；#103 起 `Launch` 拒绝有人机型，`HeliLaunch` 也将被删除。现在改为以「调试请求」向准星点提交一次地图空中支援呼叫：`SupportCallAt(catalog, target, note)`（`support_dispatch.cpp`，与 M 地图的支援呼叫同一入口）。目录项按键名查（`kSupportCalls`：`STRIKE_F` `FIGHTER_F` `INTERCEPTOR_F` `MULTIROLE_F` `CARRIER_F` `BLAST_CARRIER_F` `DOLL_CARRIER_F` `GUNSHIP_F` `HELI_F` `HELI` `MEDIC_HELI_F`，均为 `src/calls.inc` 的 jets / helis 呼叫）。支援系统自己规划从地图边缘入场的航线，`BoardAirborne` 在机内同一矩阵创建真实机组并当场入座；冷却、`SupportDisabled`、`CustomNpcAi` / `NpcBoarding` / `JetPilot` / `HeliPilot`、任务的外部航空支援限制都由它判定。调试召唤只看它是否受理，并把它的答复（`note`）显示在菜单上。单机限制保留：联机时调试召唤在提交前就拒绝（支援系统本身允许联机客户端向房主请求，调试工具不走那条）。
- **机炮无人机**：无人驾驶（#103：`Unmanned` 机型由插件空座位直飞），仍用 `JetLaunchDrone` 在准星点上空生成。
- 守卫 `tests/debug_spawn_launch_guard.py`：`debug_spawn.cpp` 里不得调用 `JetLaunch` / `HeliLaunch` / `HeliCalled` / `SpawnJet` / `PrepareSupportAircraft`，有人机行必须是 `supportCall`、键名必须是 calls.inc 里的 jets / helis 呼叫，唯一直接生成的飞机是无人机，联机拒绝在任何生成路径之前。

## 友军士兵

`ApplySupportSoldierResource(local=true)` + `HoldSupportSoldier(false)`（离线调用方自行放开）。

## 签名

运行时 `CheckProfile` 比对 12 处字节（函数开头和上面列出的调用点），任何一处不符整个工具不启用；`tests/debug_spawn_resource_audit.py` 对照本机 EDF.dll 逐条核对，并核对调用点里的类型描述符名字、`0x548D50` 调用目标、thunk 指向 `__RTDynamicCast` 导入。

## 待实机验证

- 敌人生成后的 AI（寻路、攻击玩家）、飞行敌人在半空出生后的行为；是否需要 `0x1DB1E0` 才能被任务的结束条件正确处理。
- 原版载具空车：上车、武器、己方阵营（team 2）而非原版空车的 team 5 时是否有差异（地面支援交付已用 team 2）。
- 巨型机甲 Balam 515 / 巴尔加炮 605（`Vehicle501_FortressRobo`）这类大体积载具放在准星点时与地形 / 建筑的重叠。
- 预载 29 种额外资源对进关时间和内存的影响。
- 支援呼叫：从菜单发出的呼叫与地图呼叫表现一致（入场、真实机组、跟随 / 守点）；冷却中菜单显示「现在不可用」。
