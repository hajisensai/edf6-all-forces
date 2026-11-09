# 压测 `stress:N`：ai::World 的 1024 个寻路 id 与各规模实测

2026-11-07。对象是 Steam 版 EDF.dll（`0x678CCB46`），地址都是 RVA。压测命令是 `python tools/perfrun.py stress:N`（swarm 的怪与士兵小队乘 N 倍，炮击照 blast），报告由 `tools/perf_report.py` 生成。

## ai::World 只有 1024 个 agent id

`stress:100`（实际 1082 名士兵）在 AI 线程上崩溃，位置 `EDF.dll+0xF3E9D`。

- 初始化 `0xEF613`：`mov edi,0x401`，循环把 id 1..1024 压进空闲表。表就这么大，不会再长。
- 空闲表取空后，新 agent 拿到的 id 是 `0xFFFF`，没有任何地方拒绝它。
- `0xF3E60`（ai::World vtable `0x1768A48` 的槽 5）只检查 `id != 0`，`0xFFFF` 照样往下走。
- `0xF3AA0` 的回收循环按 `[world + id*8 + 0x80]` 读写（`0xF3B3D`、`0xF3B76`），不检查 `0xFFFF`，于是越过表尾读写，崩在 `0xF3E9D`。

谁占 id：士兵（人类）每人一个是推断出来的；蚂蚁看来不占：`stress:30` 场上同时有 1123 只蚂蚁和 662 名友军，合计远超 1024 也没崩，崩溃只在士兵过千时出现。

这个上限由插件的 ai::World 扩容补丁解除（`src/aiworld_capacity.cpp`，`docs/aiworld-re.md`：槽表挪到 World 分配末尾、id 放到 1..0xFFFE），所以 `stress:N` 不再限制士兵小队数（3*N 队）。

## 各规模实测

| 场景 | 敌 / 友（PERF OBJECTS） | 平均帧时 | GPU 3D |
|---|---|---|---|
| `stress:1` | 180 / 53 | 16.7 ms | 17% |
| `stress:10` | 583 / 242 | 32.9 ms | 4% |
| `stress:30` | 1123 / 662 | 74.9 ms | 3% |

`stress:10` 与 `stress:30` 都是游戏线程跑满一个核（98%），GPU 反而空闲：瓶颈全在游戏线程的 CPU 上。

游戏线程的构成（`perf_report` 的「by phase」与「Plugin functions」）：

- 插件士兵 AI 的 `ThinkHook`（装在 ai_update 里 vtable 槽 7 上）占游戏线程 63%（stress:10）/ 60%（stress:30），其中 `WastedFire` 58% / 52%，`Feedback` 自身 46% / 40%。`PickTarget`（`src/npc_logic.h`）对每个敌人先算 `WastedFire` 再比距离；载具炮手的 `GunnerVisit` 也一样。
- Havok 7–14%。
- 特效与 ModelDebris 不到游戏线程的 2%。
- 尸体：蚂蚁的死亡状态 15 s 后自删。RagdollManager（单例 `[0x20B30E0]`，登记 `0x6EED10`，求和 `0x6EF090`）只限量**已登记**的布娃娃，是一个池、两种容量（2026-11-08 静态核对，H）：
  - 计量是各条目 `+0x28`（取自 ragdoll `+0x88`，推测为刚体数）之和；超出时从最旧的删起，至少留 1 个（`0x6EEF21` / `0x6EEF30`）。
  - 容量看 `[[0x20B2890]+0x40]`：1 或 2 时 256（`0x6EEF0B`），否则 512（`0x6EEF13`）。脚本 `SetLightWeightMode()`（`0x1BA160`，写 1）/ `SetFullLightWeightMode()`（`0x1BA150`，写 2）设它，每关在 `0x215044` 重置为 0。同一字段还把别处的上限 400→200（`0x2F9AC6` 等）、50→10（`0x4888FE`），对象未追。
  - 带 `obj+0x380` 0x8000000 位的对象（唯一写入 `0x6E1162`，推测是生成器 / 传送出来的敌人，M）默认**不登记**（`0x6EEDBD` / `0x6EEDC9`），除非本关脚本调 `SetRagdollManagerAcceptGeneratorObjectMode(true)`（`0x1BAA20`，置 `mgr+0x18` bit0，`0x6EF0D0` 每关清零）。
  - 所以 testrange 用 `CreateEnemyGroup` 的 stress / swarm 是 512、进池；用 `SetEnemyGenerator` 出兵的场景（如 `testrange/sub_vs_mothership.py`）尸体不受这个池约束。「规模变大尸体不会无限堆积」只对已登记的布娃娃成立。来源：QQ 群反馈（及开，2026-11-08）。
- 音频与渲染不在关键路径上（各自线程，等游戏线程）。
