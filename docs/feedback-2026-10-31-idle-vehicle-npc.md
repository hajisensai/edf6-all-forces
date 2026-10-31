# 2026-10-31 反馈：多辆载具只有一个 NPC 干活

用户原话（2026-10-31，第 3 条）：「3.场上只有一个载具NPC会干活 其他的只会站在原地挨打 且这种BUG对登上玩家载具的NPC也有效 第一个上车的NPC才会动 之后上车的NPC都会发呆 需要手动用指挥面板指挥移动 并且疑似不会攻击」

## 实机复现（测试场，后台驱动，2026-10-31）

`tests/autopilot/drive.py run range`，测试场方案：三辆空的 403 坦克（`vehicle403_tank_mission`）、两个六人步兵班、
`NpcCrewTest=1`（真实士兵自己走过去上车，不用玩家先开）、`Debug=1`，蚂蚁波次。插件日志的按车诊断（`NPCAI vehicle`，
`src/npcai.cpp` NpcVehicleAi）节选：

```
NPCAI vehicle v=…F3CB14B0 (every 5 s) driver …F4239080: ran its AI pass; 5 s: pass 298, action 298; active=0 sight=0 e04/e08/e09=1/0/1 list=0x17 target …DA3AB010 at 86 m (active 1); gunners [1:aimed, no trigger (trigger 0/298) …]; rounds seat:n [0:25 1:416 2:500]
NPCAI vehicle v=…F3BA6DF0 (every 5 s) driver …F4428680: ran its AI pass; 5 s: pass 298, action 298; active=0 sight=0 e04/e08/e09=1/0/1 list=0x17 target …F3C0D520 at 106 m (active 1); gunners [… 2:trigger (trigger 123/298)]; rounds seat:n [0:25 1:417 2:275]
NPCAI vehicle v=…F40218B0 (every 5 s) driver …F2E467D0: ran its AI pass; 5 s: pass 298, action 298; active=1 sight=0 e04/e08/e09=1/0/1 list=0x17 target …DA3AB010 at 66 m (active 1); gunners […]; rounds seat:n [0:20 1:500 2:500]
```

- 三辆车的驾驶员 Think 每帧都跑了 AI 遍和 AI 动作（pass = action = 帧数），原版 AI 也给三辆车都选了目标（`+0x518`，80–150 m）。
- 只有 `+0x4A0`（对象的激活位）为 1 的那辆开了主炮（25 → 18 发）；另外两辆 `+0x4A0 = 0`，100 秒里主炮一发没打（25 发），
  炮手座（`NpcGunnersInput`，不看激活位）照常开火。
- 为 1 的那辆是它的驾驶员坐上去之前就已经被激活的（坐上那一刻 `active=1`），它在蚁群到达时还空着停在原地；
  是什么激活了它没有读到（推测：空车时被蚂蚁打中，原版受击路径 0x5483E3 激活）。

## 根因（反汇编确认）

- CarBase 的 AI 动作 `0x661440` 在转向目标（`0x661020`）、瞄准与开火（第 70 槽）之前读载具自己的 `+0x4A0`
  （`0x6616C0 cmp byte [rbx+0x4A0],0; je 0x661788`）：为 0 就整段跳过——不转向、不瞄准、不开火，「站在原地挨打」。
  机甲 `0x63C1C0`（`0x63C381`）、普罗透斯 `0x648F70`（`0x649067`）、巴尔加待机 `0x6049B0`（`0x604A1A`）同一个门。
- `+0x4A0` 只由 `0x548D50`（GameObjectBase 的激活）置 1：脚本 CreateFriend / CreateEnemy 的 `active` 参数（`0x1D8AAB`）、
  视距 `+0x470` 内看到目标（`0x54C26A`，要 `+0x470 > 0`）、部分受击（`0x5483E3`）。
- 原版 AI 载具都是创建时激活的；真实士兵走上去开的载具（玩家呼叫、测试场 CreateVehicle2、空投）创建时不激活、视距为 0，
  插件替它跑 AI 遍（`DriveVehicleAi`，`docs/ground-ai-re.md`「已实机确认」）却没有补上激活，于是只有碰巧被激活过的那辆会打。
  哪辆「会干活」取决于它有没有被激活过，与上车顺序无关；用户看到的是「第一个」。

## 修法

`src/npcai.cpp` `ActivateDrivenVehicle`：插件为真实 NPC 驾驶员接管载具 AI 时，在它第一次 AI 遍之前按原版激活 AI 载具的方式调用一次
`0x548D50`（已激活就什么都不做；`+0x380` 第 21 位禁止激活时记为 `not active`，不跑 AI 遍）。签名不符（`0x548D50` 序言）就记一行日志、
不跑 AI 遍。地址由 `tests/npc_vehicle_ai_native_audit.py` 钉住。

同时加了按车诊断（Debug=1，每辆车每 5 秒一行，驾驶员的步骤变了立刻一行）：驾驶员 Think 停在哪一步、AI 遍 / AI 动作次数、
`+0x4A0` / `+0x470` / `+0xE04/E08/E09`、原版目标与距离、各炮手座这一帧为什么开 / 没开火、各座弹药；
驾驶员 Think 1 秒没来时由载具帧报出。原来「一帧只跑一次」的去重是全局一个 `{vehicle, frame}` 静态变量，改为按车记录。

离线：`tests/real_npc_crew_test.cpp` 两辆假车两名 NPC 驾驶员同帧 / 交替顺序 / 同帧二次 Think / 下车再上车、玩家驾驶 + 两名 NPC 炮手，
以及负对照（AI 表上的车、炮手的 Think、拒绝激活、玩家驾驶）；去掉激活调用时其中 3 条变红。
