# NPC 核心离线审查（2026-10-06）

审查基线：`186ee9365af2414e6ea382dc58e63b1d468179a1`（PR #53–59 整合头）。范围为 `src/npcai.cpp`，以 `npc-ai-design.md` 的友军、脚本、权威和单机边界为契约。

## 已修复

- `PreThink` 在后置 `Think` 的友军门之前运行，敌方同类士兵也会被改写跟随关系。前置补同样的队伍门；继任前检查死队长及每个受影响存活成员的脚本控制，任何一名受控就交还原版。并队候选同时排除固定位置、方向命令和根路线控制。
- 解除招募的小队在队长死亡后可直接并入已招募小队，绕过冷却。冷却中只选继任者并转移剩余冷却；其他小队也不能并入冷却小队。
- `SeeSquad` 原先在联机写不复制的 `+0x540`。脚本释放招募仅在单机且顶层对象为本机权威时写入；指令入口和 NPC 炮手入口也各自执行总开关、单机门。
- 脚本 Fencer 的两个扳机只以主手爆炸半径检查友军。现在以携带武器最大爆炸半径保守否决；未识别原版目标时，枪线延伸至真实最长射程，并按地图首个落点检查爆炸，替代固定 60 米、爆炸半径 0 的回退。
- 换枪先在全部武器中选最高分、再拒绝第四把，导致存在可用第二把时也无法换枪。现在只在三个实际选择键中选择；若第四把已经持有，仍保留分数和防抖比较。
- 上车用水平距离且把原生距离放宽到至少 3 米，还在门点读取失败时使用车身原点。现在要求有效门点并使用原生三维距离与原生距离阈值，避免穿楼层上车。下车以及后续已接受指令会取消尚未完成的登车订单。

## 原生证据

仅只读 EDF.dll 静态反汇编，没有加载或运行游戏。

- `CanRideSeat 0x6346D0`：`0x634748` 从门点减去人物位置；`0x634793..0x6347A9` 对 xyz 平方求和并与门半径（含原版 slack）平方比较。`SeatPoint` 经 `heli.cpp::RidingPoint` 已返回含 slack 的距离，不应再设 3 米下限。
- `RideVehicle 0x57690D..0x57693A` 递减 by-value shared_ptr 的强引用，归零走析构。原有调用前加一行为正确，fixture 记录原生入口释放一次并验证最终引用计数不变。
- `0x59B32B..0x59B35E` 读取 `d82` 起的选择输入并调用 `0x590A90`。插件只写前三个输入，因此候选选择不能被无法下发的第四把占据。

## 验证

`tools/npc_core_check.cpp` 直接包含生产 `npcai.cpp`，使用真实 `edf6common` 的内存/座位/玩家检查，构造原生偏移布局并记录 SetFollow、RideVehicle 调用；不另写一套 AI 模拟算法。首批 9 个断言在基线出现 7 个失败，修复后扩至 **21 个断言全部通过**。原有 `npc_ai_check` **1381 cases, 0 failures**。编译均为 MSVC `/W4 /WX /std:c++17`。

集成目标（由整合负责人添加，本提交不修改 CMake/selftest）：

```cmake
add_executable(npc_core_check EXCLUDE_FROM_ALL tools/npc_core_check.cpp)
target_include_directories(npc_core_check PRIVATE src common third_party/EDFModLoader)
target_link_libraries(npc_core_check PRIVATE edf6common user32)
target_compile_options(npc_core_check PRIVATE /W4 /WX /utf-8 /permissive-)
```

将 `npc_core_check` 加入 `EDF6_OFFLINE_CHECKS`。fixture 只分配进程自身的空白内存；没有操作游戏进程、窗口或安装目录。原生动画、实际脚本时序、联机复制和 NPC 炮手在游戏更新顺序中的行为仍是 **implemented_unverified**，本次没有实机验收。
