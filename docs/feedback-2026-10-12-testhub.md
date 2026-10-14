# 2026-10-12 测试站：处理状态一列、日志分诊、分块上传

> 「检查一下日志站的日志。同时已完成的改成已完成，增加一列。」

## 1. 站点改动（已部署，Worker 版本 d41bde70，2026-10-12）

- **反馈的处理状态**：`reports.status`（`open` 待处理 / `doing` 处理中 / `verify` 已修待确认 / `done` 已完成 / `closed` 不处理）+ `status_at`。
  线上库用 `testhub/migrations/0001-report-status.sql` 加列（已执行，12 条旧反馈全部为 `open`，数据不动）；`schema.sql` 同步含这两列。
- **页面**：「4. 反馈记录」上方新增一览表，列：# / 结果 / **处理状态** / 说明 / 谁 / 时间 / 回复；已完成和不处理的行变淡，表头显示「已完成 N 条」。
  开发者账号在表里用下拉框直接改状态，测试者只读；每张反馈卡片上也显示处理状态。测试项状态加 `done`（已完成）。
- **接口**：`POST /api/report_status {report_id, status}`（仅开发者）；`POST /api/reply` 可带 `status`，回复与标状态一步完成。
- **CLI**：`testhub.py status ID S`、`testhub.py reply ID TEXT --status S`；`state` 每条反馈显示 `[状态]`。

## 2. 顺手根治：main CI「Mirror to the test site」恒 413

2026-10-10 起安装包约 113 MB，超过 Worker 单请求 100 MB（站点 `MAX_UPLOAD` 95 MB），main 每次推送最后一步都 413，站上测试版停在 10-10 的 90e8e85。

改为 R2 分块上传：`POST /api/build/start` → 每块 32 MB 一次 `PUT /api/build/part` → `POST /api/build/finish`（各块 etag、总大小、上传方计算的整包 SHA-256；
合并后大小对不上就删除对象、不写库）；失败时 `/api/build/abort`。`testhub.py publish` 在包超过 95 MB 时自动走分块，小包仍是一次 PUT（Worker 算哈希）。
CI 调用的是仓库里的 `tools/testhub.py`，**本 PR 合入 main 后下一次推送的镜像才会用上**（Worker 侧已经部署）。

## 3. 验证

- `node --test test/auth.test.mjs test/plan.test.mjs test/hub.test.mjs`：23/23 通过。新增 `test/hub.test.mjs`：状态权限（测试者 403、非法值 400、不存在 404）、回复带状态、用例 `done`、分块上传（假 R2：三块合并、下载哈希一致、大小不符丢弃、坏参数拒绝、abort）、迁移脚本在旧库上执行后列与新建库一致且旧数据保留。
- 本地端到端：node 把 Worker（d1 + 假 R2）起成 HTTP 服务，`testhub._publish` 以 1 MB 分块上传 3 MB 包：站点记录的 SHA、下载回来的 SHA、本地 SHA 三者一致；小包走单次 PUT。
- 线上：`/app.js`、`/`、`/style.css` 均为新版；`/api/state` 带 `status`；测试者改状态 403、非法状态 400、测试者 `build/start` 403。
  真实 R2 分块：start → 5 MiB + 1 KiB 两块 → 故意报错总大小 finish，Worker 返回「the parts make 5243904 bytes, not 5243905: discarded」，随后 `/dl/` 为 404、build 列表不变——真实 R2 的 create / uploadPart / complete / delete 全部走通且没留下测试数据。
- **未验证**：113 MB 真包经 CI 分块镜像（要等本 PR 合入 main 后的第一次推送）。

## 4. 反馈分诊（#3–#12）

| # | 测试者 / 构建 | 说明 | 结论 | 站上状态 |
|---|---|---|---|---|
| 3 | 20qs / feedback-final | 进新平原地图崩溃 | EDF6Coop 崩溃处理器记录 10-07 00:19、00:21 多个线程同时 `bad_alloc`（栈 EDF+0x12D9B7F / 0x12D85E5 / 0x3FD43），即内存耗尽。4ed51ea（PR #79，TerrainShare，10-08）之后该机再无 bad_alloc | 已完成 |
| 4 | n0xe / review.17eb8f6 | 每关 MISSION COMPLETE 后闪退 | 转储：`c0000409` FAST_FAIL_INVALID_ARG 于 EDF+0x961A75（`invalid_parameter_noinfo_noreturn`，容器释放时块头已损坏），调用链在 `csharp_RequestExit` 附近的退场释放路径（EDF+0x960B4D → 0x119D473 → 0x7030D0 / 0x70341C / 0x6FF47F）。**是堆被写坏，没有找到对应修复**；该机 10-10、10-11 的两次反馈没再提 | 待处理（已请复测） |
| 5 | n0xe / review.17eb8f6 | 进战利品页面前闪退；尼克斯不开火、泰坦不用主炮 | 闪退与 #4 同一处（同一栈）。NPC 不开火：#120（188942e）10-10 已合入，但 #12（fb4c60c，含 #120）仍报不开火 → 未修好 | 处理中 |
| 6 | n0xe / 90e8e85 | 普罗透斯展开护盾闪退 | ead8fcf 修复（`docs/feedback-2026-10-10-proteus-shield-crash.md`）。回传的转储是 10-07 另一套安装的旧文件（与 #5 同一 dump），见下面「旁支问题」 | 已完成 |
| 7 | 20qs / 90e8e85 | 召唤步兵到达前消失、卡车跟着消失 | e1c819f + c70a33e（送达后先下车再撤离；撤离时只请下车不删除），均在 e2b9283 / main | 已完成 |
| 8 | 20qs / fe730ac | 炸了 | 两次直接中断、无异常记录：20:11 在大地图任务里约 3 分钟（最后是 BIGWORLD 探测、碎石物理日志），20:19 停在任务加载预载阶段。该机 8 GB 内存，进大地图可用内存常为 0.2–1.7 GB。**无法定根因**（疑似内存不足/卡死后被结束） | 待处理（已追问） |
| 9 | 20qs / fe730ac | 杂兵冷暴力我 | 日志看不出（日志里的 AssultSoldier / HeavyArmor 是 team 2 友军，不是敌人） | 待处理（已追问） |
| 10 | 20qs / fb4c60c | （无文字） | 10-11 19:18 进大地图任务开始后直接中断，无异常记录，机器可用内存约 1.7 GB；与 #8 同一形态 | 待处理（已追问） |
| 11 | 20qs / e2b9283 | （无文字） | 单机：支援目录 16 三次被拒 `no verified entry for this unit`（`src/support_dispatch.cpp:441`，`AircraftSpecOf` 查不到该航空单位）；联机客机：20:35 `SUPPORT NET suspended`（EDF6Coop 扩展房间未就绪，`src/support_net.cpp:270`） | 待处理（已追问） |
| 12 | n0xe / fb4c60c | NPC 战斗机体/普罗透斯不开火、地图选不中、派 NPC 开电磁炮闪退、巴尔加开不了、潜水母舰水里召唤不了、巴拉姆模型仍是巴尔加 | 归 npc-fire / models-summon 主题。补充证据：EDF6Coop.log 21:00:11、21:53:57 各 16 次 `C0000005` 于 EDF+0x63AED9（栈 EDF+0x63AC85 → 0x63C3E1（机甲 AI 动作 slot 55 区）→ EDF6VehicleCrew+0x1E342B → +0x1DA569），紧挨着插件日志 `NPCAI driver … runs the AI pass of v=… (not on the AI list)`（#120 的机制），读到的地址是 `0x00000007000048CD` / `-1`；另有 21:47:23 一次 EDF+0x5F9F2B 读 0xC | 处理中 |

### 旁支问题（只诊断，未修）

- **WATER 日志刷屏**：`src/heli.cpp` 的 `LogSea` 只在结果变化时记录，但两处探测一个在水面、一个在陆地，结果每次来回切换，于是每次都写：#11 一局 2.6 万行、#9 一局 3400 行，占满 12 MB 回传尾巴、挤掉有用日志。日志格式也坏（`no water here` 时输出 `…on the map0.0`）。
- **回传的崩溃转储可能不是这台/这次的**：`tools/testhub.py` 只取 `%LOCALAPPDATA%\CrashDumps` 里三天内最新的 dump，不核对游戏路径与插件版本（#6 带的是 #5 的旧 dump；10-10 的 proteus 文档 §1.1 已记录）。
- 20qs 的机器只有 8 GB 内存，大地图任务里频繁静默中断（Coop 日志 53 次 `PREVIOUS RUN ended without a shutdown line`），值得单独看大地图内存。
