# 2026-10-04 设计审查修复计划

审查对象：main 063bf99（炮舰机 0.7.0）。原则：根因修复，不加延迟/重试/吞异常/特判。

## 第 0 步：公共契约（本提交）
- 时钟：`GameMs()` 是唯一的逻辑时钟（只在游戏线程用），`PlayerFix.at` 也用它；墙钟只给日志限流和别的线程。
  `GameFrame()` 帧号取代「墙钟 10 ms 当一帧」的去重。
- 配置：`Cfg()` 读不可变快照，热重载整份替换（原子指针），`cfg.bump` 临时改写换成 `SuppressBump`。
  `Enabled=0` 之后能热改回来（重载在 Enabled 判断之前）。
- 对象身份：`ObjRef`（地址 + weak-this 控制块）。
- 任务生命周期：`mission.cpp` `MissionStart()`，各模块 `Reset*()` 在这里清空上一关的逐对象状态。
- 506 机体：`body506.cpp` 唯一挂槽 57，按标记表（唯一一份）分派给 jet / sub / playerjet 的 `*BodyStep`。
- 撞击伤害：`ImpactDamage()` 接口（jet.cpp 实现，playerjet 撞击时调用）。
- 版本：CMake `project(VERSION)` 生成 `version.h`，DLL 自报版本与发布一致。

## 分包（按文件归属，互不交叉）
- A 喷气机：jet.cpp / airstrike.cpp / booster.cpp / jetprops.cpp / tools/make_jets.py（炮舰机机体与标记）
- B 核心：crew.cpp / heli.cpp / ground.cpp / plugin.cpp / hud.cpp / overlay.cpp / loadout.cpp / mission.cpp
- C 母舰与玩家喷气机：subcarrier.cpp / carrierlaser.cpp / playerjet.cpp（含撞击伤害敌人）
- D AutoTurret：autoturret/**，两插件共用的 memory 层
- E 安装链与仓库：tools/（除 make_jets.py）/ testrange / CI / README / ini / dist 卫生
