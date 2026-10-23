# 2026-10-21 反馈：飞机入场尾烟与实际位置偏移

用户原话：「飞机的入场尾烟与实际偏移了好多」。（2026-10-14 的「入场尾烟在飞机很后面」是同一症状，当时只核对到发射器、没找到根因，见 [feedback-2026-10-14-air.md](feedback-2026-10-14-air.md)。）

## 是哪种飞机、哪种尾烟

插件的支援呼叫飞机（日志 `JET`；战机 / 对地攻击机 / 截击机 / 多用途机 / 航母等，`jet_internal.h Entering` 期间）的「入场尾烟」：`src/booster.cpp` `JetSmoke` → `SmokeFrame` 给每个喷口建的原版 `LineStripObject`（导弹烟带的发射器，ini `JetEntrySmoke`）。不是尾焰（Booster），也不是原版空袭或 4.1 移植机。

## 根因

`TrailMake` 调工厂 `0x842E0(mgr, out, matrix, InitParam)` 时把**喷口的世界矩阵**当作物体矩阵传了进去。静态逆向（EDF.dll `0x678CCB46`）：

- 基类构造 `0x1189C00` 把 `InitParam+0x08` 指向的矩阵复制到 `obj+0x60`，之后没人再改它；
- 渲染任务 `0x2F6310` 每帧把 `obj+0x60` 复制到渲染器 `+0x260`，绘制 `0x2F583A` 调 `0x11544F0(.., 渲染器+0x260, 顶点, ..)`，`+0x260` 是这次绘制的世界矩阵；
- 顶点 `0x2F5AC0` 原样取每个节点 `+0x00`；节点 `0x2F6110 → 0x2F4C10` 原样放在发射器 `core+0x50`（世界坐标，插件每帧写入）。

所以每个节点被画在「节点 × 尾烟开始那一刻的喷口矩阵」：先按当时航向转一遍、再平移一遍当时的位置，烟离飞机几百米到几公里。原版自己的世界坐标烟带都用单位阵画（内联烟带 `0x24B8EC` 传 `0x1FFF3D0` 处的单位阵；工厂唯一的原版调用 `0xA01A0` 传不旋转的矩阵）。

## 修复

- `src/smoke_trail.h`（纯函数）：`kObjectFrame` = 单位阵，`TrailMake` 把它交给工厂；`Emitter` 是喷口 → 发射器位置 / 方向的唯一换算，建烟和每帧都用它（原先建烟时方向取了 `-z`、之后取 `+z`，速度为 0 时不影响位置，一并统一）。
- `SmokeFrame` 调试日志（`Debug=1`，每条烟一次）：最新节点按物体矩阵画出来离喷口多远、物体矩阵原点、与单位阵的偏差。

## 实机证据（`tests/autopilot/drive.py run range`，后台，DebugSpawn 呼叫对地攻击机，同一按键序列）

| | 物体矩阵原点 | 最新节点画出位置离喷口 |
|---|---|---|
| 修复前（`TrailMake` 传 `m`） | (1973.9, 163.6, 5432.5)，旋转偏离单位阵 0.383 | 5785 m |
| 修复后 | (0, 0, 0)，偏离 0.000 | 3.16 m |

3.16 m 是在输入步（写入本帧位置之前）量到的：节点是上一帧写入的位置（「laid 0.00 m from where the emitter was put last frame」），机体此后飞了一帧（约 190 m/s ÷ 60）。发射器更新是 slot 5 排进任务队列执行的，在本帧写入之后；它是否在同帧绘制之前跑完未单独证实（M），最坏一帧，与尾焰原先的一帧滞后同量级。截图（每秒一张）里镜头没对着入场航线，看不到飞机，没有形成可用的对比图。

## 联机

尾烟是本机效果对象，`online_authority.h`：插件生成的呼叫飞机每台机器各有一份本地副本、各自运行 `JetFrame` → `JetSmoke` → 同一 `TrailMake`，修复对主机和客户端相同；已注册的远端无人机副本（`supportDrone && !OnlineRunsHere`）本来就不跑插件飞行、不出烟，不变。没有做多机实测。

## 测试

- `tools/smoke_trail_check.cpp`（`offline_checks` / ctest `smoke_trail_check`）：215 / 235 / 245 m/s 直飞与转弯，按原版方式铺节点、经物体矩阵绘制；断言烟头在喷口、每个节点在铺下时喷口所在处，并断言用喷口矩阵（旧做法）时偏出 100 m 以上。
- `tools/selftest.py arrival_smoke_is_drawn_where_it_is_laid`：`TrailMake` 交给工厂的只能是 `smoke::kObjectFrame`，两处发射器都经 `smoke::Emitter`，`kObjectFrame` 是单位阵。
- 变异实测：`TrailMake` 改回传 `m` → selftest 失败；`kObjectFrame` 原点改 (0,3,10) → check 6/6 失败（10.44 m）；`Emitter` 方向取反 → check 失败。
