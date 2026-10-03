# EDF6VehicleCrew：NPC 开载具 / 开直升机，玩家随时顶替

EDFModLoader 插件，只支持 EDF.dll TimeDateStamp `0x678CCB46`（当前 Steam 版）。版本不符或代码签名对不上时，
插件只写一行 `REFUSED` 日志后自行退出，不改动游戏的任何内容。

## 功能

1. **NPC 自动上车**：己方空载具空置 `CrewDelayMs`（默认 3 秒）后，会来一名 NPC 驾驶。用的是任务脚本让 NPC 坦克队出动的同一个原生调用
   `Vehicle_RideAi`（VehicleBase 第 50 槽）。只给玩家同队、离玩家 `CrewRange` 米以内的载具派人。
2. **玩家顶替 NPC**：原版不允许玩家坐已有人的座位。插件放开了这一限制，所以 NPC 坐着的载具也会照常出现上车提示，按上车键就能坐进去：
   - 被顶下的 NPC 优先挪到空的副座/炮手位；
   - 没有空位时它下车（NPC 会直接消失，这是原版对 AI 乘员下车的处理）；
   - 玩家下车后，过 `CrewDelayMs` 又会有 NPC 来开这台车。
3. **NPC 开直升机**：游戏本身没有直升机驾驶 AI（静态分析结论见 `docs/heli-input-re.md`），NPC 坐上直升机后由插件来飞：
   - **跟随**：保持在玩家旁 `HeliFollow` 米、头顶 `HeliHeight` 米处；
   - **护卫**：`HeliRange` 米内有敌人时，飞到玩家与敌人之间，机头对准敌人，开机枪、发导弹。不会隔着玩家开火；
   - **降落**：玩家原地站着不动 `HeliLandMs`（默认 6 秒）后，直升机降落在玩家身边约 20 米处。玩家在 25 米内时它不起飞，方便走过去顶掉 NPC、自己开；
   - **炮艇模式**：玩家坐在直升机副座、由 NPC 驾驶时，它不再跟随玩家（玩家就在机上），而是主动飞到离最近的敌人 `HeliStandoff` 米处攻击。
   - 只接管本插件派了 NPC 的直升机。任务脚本里的直升机（演出机、运输机）不受影响。

所有参数都在 `EDF6VehicleCrew.ini`（中文注释）。游戏运行中改完保存，约 1 秒内生效。

## 安装 / 卸载

- 安装：把 `dist/Mods/Plugins/EDF6VehicleCrew.dll` 和 `EDF6VehicleCrew.ini` 复制到游戏的 `Mods/Plugins/`。
  2026-10-03 已装好，装之前整个 Plugins 目录备份在 `backup/Plugins-20261003-111632/`。
- 卸载：删掉 `Mods/Plugins/EDF6VehicleCrew.*`。
- 与 `EDF6AutoTurret` 共存：两边都 hook 了坦克的第 55 槽，本插件会串在它后面，也就是先执行它、再执行本插件，所以加载顺序无所谓。

## 批量实测清单

**先离线（单人）测**。联机时 NPC 是由主机生成的，客机这边会不会重复生成还没验证过。

日志在 `Mods/Plugins/EDF6VehicleCrew.log`。测试期请保持 `Debug=1`。

| # | 怎么测 | 期望 | 日志关键词 |
|---|---|---|---|
| 0 | 启动游戏进主菜单 | 不崩溃 | `HELI profile=1`、`HOOK crew findSeat=23/23 prompt=1` |
| 1 | 进任意任务 | — | `HOOK inputs=16`（若有 `chaining onto` 说明串在了 AutoTurret 后面，属正常） |
| 2 | 呼叫坦克（空降兵）或开着坦克再下车，走开等 3 秒 | 一名 NPC 上车并开走 / 开火 | `CREW v=... 403_Tank` |
| 3 | 走到 NPC 坦克边 | 出现上车提示；按键能上去，NPC 挪到副座 | `BUMP ... moved to gunner seat`（没有副座则为 `kicked`） |
| 4 | 玩家开着车、NPC 在副座 | 副座机枪是否会自己打（403/404 由 AutoTurret 负责，其他车看原版） | — |
| 5 | 下车走开 | 3 秒后 NPC 回到驾驶位 | `CREW` |
| 6 | 呼叫直升机（空降兵），站在旁边不动 | NPC 上机但停在地上不起飞（因为你站着没动） | `HELI v=... crewed`，每秒一行 `HELI ... land` |
| 7 | 走开 30 米以上 | 起飞，飞到你头顶约 35 米处跟随 | `HELI ... follow`，`y` 接近 `goal`，`vy` 绕 0 波动 |
| 8 | 有敌人时 | 机头转向敌人，机枪开火、导弹齐射 | `target=...`、`gun=1`、`msl=1` |
| 9 | 站着不动 6 秒 | 降落到你身边 | `land`，`ground=1` |
| 10 | 走过去按上车 | 你坐上驾驶位，NPC 挪到副座（如有） | `BUMP` |
| 11 | 坐到 NPC 直升机的副座 | 炮艇模式：NPC 飞去打最近的敌人 | `HELI ... gunship` |

如果直升机飞得不对，把整个 `.log` 发回来。飞控每秒记录高度、爬升率、油门、悬停油门、旋翼转速、三个摇杆量和偏航学习状态，靠这些就能调参，不必再跑一遍。常见现象：
- **一直原地打转**：偏航方向学反了，日志里应能看到 `yaw sign flipped`；
- **上下起伏**：把 `HeliClimbGain` 调小；
- **水平冲过头 / 来回晃**：把 `HeliBrakeGain` 调大，或把 `HeliMoveGain` 调小；
- **飞不起来**：看 `rotor` 和 `thr` 两项。

## 测试场（testrange/）

双击 `testrange/测试场.bat` 打开启动器（需要 Python 3，自带的 tkinter 即可）：

- **载具**：每种填数量，放在玩家出生点 30–160 米的平地上（最多 12 台）。可以设等级。
- **敌人波次**：选种类、每波数量、场上少于几只才刷下一波、开局延迟、间隔、等级。开局先留 30 秒给你试车。
- **安装到第 1 关**：生成 `Mods/MISSION/EDF6/M001/MISSION.AC`，地图用 M045 那片平原（`ig_Heigen601`，阴天）。
  它的点位文件 `MISSION.RMPA` 会从你本机的 `Root.cpk` 里取出来一起放进去。进游戏后选 **离线 → 第 1 关**，难度随意。
  这一关不会自己结束，测完从暂停菜单撤退即可。
- **卸载**：删掉整个 `M001` 目录，第 1 关恢复原样。目录里有 `EDF6TestRange.txt` 才会删；别的 mod 放在那里的文件不会动。
- 设置保存在 `testrange/testrange.json`，下次打开会还原。

## 源码

- `src/plugin.cpp`：入口、配置、日志、代码签名检查。
- `src/crew.cpp`：NPC 上车（Vehicle_RideAi）、顶替（slot 49 FindSeat + 上车提示访问器）、每帧入口（slot 55 串接）。
- `src/heli.cpp`：直升机自动驾驶。
- `docs/re-notes.md`：上车门槛与座位函数的逆向笔记；`docs/heli-input-re.md`：直升机输入块的逆向笔记。
- 构建：`build.cmd`（MSVC x64 + Ninja，RelWithDebInfo），产物输出到 `dist/Mods/Plugins/`。
- `testrange/`：测试场启动器（`gen.py` 生成脚本，`lib/` 是读 `Root.cpk` / RMPA 的工具，edf6-cpk 部分的许可见 `lib/LICENSE.edf6-cpk`）。
- `probe/`：早期调研用的任务脚本探针（用 CreateFriend 生成直升机），已不需要，也未安装。
