# 交接新鲜度按场景步进序号判断（mod-7）

审计出处：`docs/reviews/2026-11-24-optimization-audit.md` mod-7。实现：`src/step_fresh.h`，离线测试：`tests/step_fresh_test.cpp`
以及下文各站点对应的用例。

## 规则

- 发布方在发布时记 `stepfresh::Now()`，读取方用 `stepfresh::Fresh(at, lag)` 判断「发布步 ≥ 当前步 − lag」。
- `Now()` 数的是**插件看到实际跑过的场景遍历**（`crew.cpp` SeeFrame 里 `stepfresh::Traversals`：`crew::GameFrame()` 每变一次 +1，
  不论变了多少；暂停中看到的变化不算），一帧最多 3 步（`docs/perf-probe-re.md` §1），初值 1（0 专指「从未发布」）。
  不直接用 `GameFrame()`：System::update 在步进循环开头 0x1198FD0 就给 +0xCE0 自增，暂停判定 0x11990C0 在它之后才跳过场景，
  所以暂停期间原生计数照涨，恢复后第一次遍历会一下跳过整段暂停，暂停前发布的数据全被误判过期。
- `kEveryStep = 2`：每步发布的数据，读取方在同一步前 / 后读，再多容一步（发布方某一步没轮到）。
- `kFromDraw = 4`：绘制线程每帧发布一次（HUD pass，在本帧步进之前录制），游戏线程在本帧最多 3 步里读，再多容一帧没绘制。
- **绘制线程看到的是什么**：`Now()` 是游戏线程最后跑过的那一步，即本次录制画面所属的场景；该步或之前 `kEveryStep` 步里盖章的数据就是这个画面的。
  帧调度出现 n = 0 帧（`docs/unlimited-fps-re.md` §4.3）时步进不动，绘制继续显示上一步的数据。
- 帧再慢（stress:100 时一帧 204 ms，或加载卡顿超过 500 ms），只要发布方在上一步跑过，数据就不会被判为过期。
- 步进计数判断不了的情况，都改成显式状态：
  - **暂停菜单**：场景不跑，`Now()` 不动（见上），按计数数据在暂停中和恢复后都「新鲜」，直到恢复后再过 lag 步仍没重新发布。暂停时必须隐藏的读取方显式检查 `GamePaused()`（HUD 的 `HudDraw` 原本就查）；需要在暂停时保留画面的读取方（热成像、炮塔相机）正好依赖这一点。
  - **新任务 / 加载**：`ResetCrew` 调 `stepfresh::NewMission`，把 `Now()` 往后推 `kMissionGap = 2^32` 步，上一任务盖的章全部过期。各模块原有的 `Reset*` 清理不变。
  - **状态转换**（下车、关地图、关瞄准镜）：发布方看到转换时自己清空数据。步进计数只兜底「发布方整个不跑了」的情况（载具没了、士兵帧不再调用）。

## 各站点结论

| 站点 | 结论 | 说明 |
|---|---|---|
| `infantryscope.cpp` Published.at（原 kFreshMs） | 步进判断 + 显式暂停 | `Live()` = 有人 && `Fresh(at)` && `!GamePaused()`，CamStepHook / InfantryScopeNow / WorldEnd 共用 |
| `scope_lens.cpp` wantAt（原 kFreshMs 200） | 步进判断 + 显式暂停 | 相机步发布、录制读；`ArmFor` 加 `!GamePaused()`（暂停时 CamStepHook 不再发 Want） |
| `helisight.cpp` latest（原 kFreshMs） | 步进判断 + 发布方显式清空 | 死亡、不是本人瞄准、没座位、解算不出时，对 latestVehicle 调 `clear` |
| `launcher.cpp` latestMs | 步进判断 | 原本就有 `clear` lambda 处理状态转换 |
| `map.cpp` pose / readoutAt（原 kFreshMs 300「关地图（暂停、加载）」） | 步进判断 + 显式暂停 | 相机步（暂停时仍在跑）看到 `GamePaused()` 就退回原版视角，并设 `pauseSeen`；下一步 `Frame` 据此 `Close("the game was paused")`；`ResetMap` 清 `pauseSeen` |
| `map.cpp` game.frameAt（另一本地玩家占用） | 步进判断 | `Fresh(game.frameAt)` |
| `map.cpp` scopeHeldAt（原 kScopeHoldMs 300，窗口线程读） | 步进判断 + 显式暂停 | WndProc 里 `Fresh(scopeHeldAt) && !GamePaused()`，暂停菜单时滚轮归菜单 |
| `mapcmd.cpp` readoutAt / g.frameAt（原 kFreshMs 300） | 步进判断（关地图本来就是显式状态） | `SuspendMapCommands`（map.cpp 的 Close，含暂停）把 frameAt / readoutAt / view.at 清零 |
| `mapcmd.cpp` view.at（HUD 画地图时发布，游戏线程读） | 步进判断，lag 用 `kFromDraw` | 方向是绘制 → 游戏 |
| `mapcmd.cpp` noteAt（kNoteMs） | 不改 | 提示显示时长，不属于交接 |
| `hud.cpp` Snapshot.tick（13 处 kFreshMs、1 处 kSazabiCueMs） | 步进判断 | 改成一个 `fresh = Fresh(snap.step)`；暂停时 HudDraw 原本就提前返回 |
| `hud.cpp` Work.seen 槽复用 | 步进判断 | 复用条件 `!Fresh(seen, step, 4*kEveryStep)` |
| `hud.cpp` HudSee player.at（GameMs 2 s） | 不改 | 同线程的 GameMs，暂停时停走，超过 250 ms 的间隔按 16 ms 计，慢帧安全 |
| `gear.cpp` / `warn.cpp` GearHudLatest / WarnLatest（kGearFreshMs） | 步进判断 | `GearHud.step`、`Warnings.step` |
| `sazabi.cpp` cueMs（kSazabiCueMs 250） | 步进判断 | `cueStep` |
| `drill.cpp` cueAt / `emc.cpp` cueAt（kCueFreshMs） | 步进判断 | `cueStep` |
| `heli.cpp` cueAt / heliHudAt（kCueFreshMs） | 步进判断 | `HeliCueStep` / `heliHudStep` |
| `heli.cpp` 救援提示（kRescueCueMs） | 不改 | 6 s 显示时长 |
| `enemy_control_radar.cpp` Published（kFreshMs） | 步进判断 | 发布方每步发布，会话结束时发空集合 |
| `npcai.cpp` NpcMarkReadout / NpcPingReadout（markPub.wall） | 步进判断 | `markPub.step`；ping 的 3 s 显示时长 kPingMs 不改 |
| `payload.cpp` latest（PlayerPayload）、selectableAt（PlayerSelectablePayload，第 1239 行） | 步进判断 | `latestStep` / `selectableAt` 改成步；下车时 `ClearChoiceContext` 显式清空 UI 快照 |
| `payload.cpp` RequestPayloadSelection / ConsumeChoice（choicePublication.at、request.posted） | 步进判断 | UI 发请求 → 游戏线程消费，`kEveryStep` |
| `payload.cpp` Pick.seen（PullHook、PayloadPicked、SeatFireWeapons） | 步进判断，lag = `kPickFreshSteps`（12） | NPC 炮手的选择按 AI 节奏刷新，保留旧的 200 ms ≈ 60 步/s 的 12 步；槽复用 120 步 |
| `payload.cpp` RemotePick.at / SentPick.at（kPickStaleMs） | 不改 | 网络消息间隔，与本机步进无关 |
| `ultimate.cpp:785` PlayerUltimateCue（kShowMs 8000） | 不改 | 8 s 提示显示时长 |
| `qmark.cpp` viewWall（kPublishFreshMs 500） | 步进判断 | QMarkFrame 每步发布一次，改用 `viewStep` |
| `turretcam.cpp` seenMs / aimMs / Out.at（kFreshMs 200、kAimFreshMs 150，GameMs） | 步进判断 | 帧 / 瞄准钩子每步一次；暂停时相机步保留最后的画面（与原来 GameMs 停走一致） |
| `stockgauge.cpp` armorShown.at（kArmorFreshMs 250） | 步进判断，lag 用 `kFromDraw` | 原版护甲条每帧更新，HudPublish 在步进里读 |
| `highcam.cpp` / `nix.cpp` cue（kCueMs 200） | 步进判断 | HUD 提示每步发布 |
| `seatswitch.cpp` latestMs（kFreshMs 200） | 步进判断 | 座位列表（SeatList）原来是 `ms+kFreshMs` 的 until，慢帧时会被 `ms>=shownUntil` 藏掉；改为显式 `latestListed`，只要新鲜就显示；提示 / 拒绝仍按各自时长 |
| `seatswitch.cpp` Rider_.seen 槽复用 | 不改 | 同线程 GameMs，2 s 槽回收（改名为 kRiderGoneMs） |
| `sidecar.cpp` PassengerPair.at（kGoneMs 500，子弹钩子读） | 步进判断 | `SidecarBulletPass` 用 `Fresh(match.at)` |
| `sidecar.cpp` sidecar.seen（GameMs 500，「摩托没了」） | 不改 | 同线程 GameMs，慢帧安全 |
| `subcarrier.cpp` Snapshot.tick（kGaugeMs 1000「游戏线程还在发布（未暂停）」） | 步进判断 + 显式暂停 | `Fresh(shot.step) && !GamePaused()` |
| `subcarrier.cpp` player.at / inputAt（GameMs） | 不改 | 同线程 GameMs，慢帧安全 |
| `thermal.cpp` shot.at（kFreshMs 1000） | 步进判断 | 暂停时保留最后的热成像画面（不再 1 s 后关掉） |
| `thermal.cpp` rejectedAt | 不改 | 提示显示时长 |
| `boarding.cpp` ask.at（kAskMs 250） | 步进判断 | 命中钩子发、BoardingTick 每步取 |
| `jetaudio.cpp` beatAt（kQuietMs 150） | 不改 | 音频线程靠墙钟判断「游戏停了」（暂停、加载、卡死都要静音），步进计数恰好在这些情况下不动，不能用 |
| `airstrike.cpp` kShowMs、`support_net.cpp` 版本提示、`debug_spawn.cpp` 状态、`npcai.cpp` sweep / 队形提示 | 不改 | 都是显示时长；npcai 只允许动 markPub |
| 闪烁、HP 条拖尾等动画计时 | 不改 | 纯动画墙钟，不是交接 |

## 暂停下的行为（逐个判定）

暂停时场景遍历被跳过，游戏线程上的发布方和读取方都不跑；`Now()` 不动，所以下表每个判定在暂停中和恢复后都把暂停前的章当新鲜，
恢复后第一步读到的仍是暂停前最后一次发布。暂停中还在跑的读取方只有相机步、绘制线程、窗口线程，它们的行为：

| 读取方（暂停中仍跑） | 暂停中 | 依据 |
|---|---|---|
| HUD 全部（hud.cpp Snapshot / gear / warn / cue 类 / qmark / mapcmd view 等） | 不画 | `HudDraw` 原本就在 `GamePaused()` 时返回 |
| infantryscope `Live()`（相机步、WorldEnd） | 不挂瞄准镜画面 | 显式 `!GamePaused()` |
| scope_lens `ArmFor`（录制） | 不挂镜头 | 显式 `!GamePaused()` |
| map Camera | 退回原版视角并设 `pauseSeen`，恢复后第一步 `Frame` 关地图 | 显式 `GamePaused()`；pose 在暂停中保持新鲜，所以一定能看到暂停 |
| map scopeHeldAt（WndProc） | 滚轮归暂停菜单 | 显式 `!GamePaused()` |
| subcarrier GaugeHook（绘制） | 不画 | 显式 `!GamePaused()` |
| turretcam 相机 | 保留最后画面 | 与原来按 GameMs（暂停停走）一致 |
| thermal 视图阶段 | 保留最后的热成像画面 | 原来墙钟 1 s 后关；现在暂停中不关（行为变化，见风险） |
| 游戏线程上的其余判定（payload / seatswitch / sidecar / boarding / enemy_control_radar / npcai mark / launcher / helisight / stockgauge / highcam / nix / drill / emc / heli / sazabi） | 不运行 | 恢复后按暂停前的章继续，跨暂停的交接（例如暂停前点的载荷选择、暂停前的上车请求）不丢 |

## 测试

- `step_fresh`：慢帧（相邻两步间隔 600 ms）不判过期；暂停（GameFrame 跳 600）只算一步，暂停前的数据恢复后仍新鲜，暂停中看到的变化不算步，同一遍历多处 SeeFrame 只算一步；`kEveryStep` / `kFromDraw` 边界；n = 0 帧；章比读取方的 Now() 还新；0 表示从未发布；跨线程读；NewMission 之后旧章全部过期。
- `infantry_scope_pip`：260 ms 墙钟不过期；超过 `kEveryStep` 步过期；暂停时隐藏、恢复后显示；新任务后过期。
- `scope_lens`：Want 超过 kEveryStep 步没续就不挂镜头；暂停时不挂；250 ms 慢帧仍挂。
- `enemy_control_radar_native`：520 ms 墙钟 + kEveryStep 步仍有效；再多一步失效；NewMission 失效。
- `payload_sight` / `payload_runtime` / `payload_click` / `payload_snapshot`：fixture 的 Tick 推进一步；原来的「now+=201 ms」改成「发布方停了 kPickFreshSteps+1 步」。
