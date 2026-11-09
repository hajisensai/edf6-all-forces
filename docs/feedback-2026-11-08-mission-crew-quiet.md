# 2026-11-08 任务 AI 载具乘员「安静」（测试站 #41）

## 玩家原话（测试站 #41，QQ 群转交，英文原文）

> NPC tanks speak like Rangers, and their icons appear.
> When certain enemies spawn, the sound effect for boarding a vehicle plays. (Confirmed in Mission 1.)

（同一条里的「NPC Rangers move strangely」是寻路问题，不在本次范围。）

## 分诊结论

插件把任务脚本生成的 AI 载具（4.1 第 1 关 M190 的 `VEHICLE301_TANK_AI`，`Main` 里 4 辆、`Func_22` 中途再加 3 辆）原版的隐形 Dummy 驾驶员换成了真实士兵（驾驶席用支援队长资源）。这些士兵坐在车里仍是游骑兵：带着游骑兵的无线电台词、字幕图标和头顶合流标记；中途增援的坦克生成乘员后士兵走上车，播原版上车声，玩家看到的「敌人出现时」就是这波增援与敌人同时出现。

## 维护者决定（2026-11-08，方案 A）

- 保留真实乘员。
- 任务脚本生成的 AI 载具里的乘员坐在车里时：关闭士兵个人的无线电语音与友军标记（图标），上车音效对这些任务乘员静音。
- 只针对任务 AI 载具：玩家自己上车、玩家指挥 / 支援的载具乘员不受影响。
- 下车后恢复原状；乘员死亡、载具毁坏、任务结束都要正确。

实现见 `docs/feature-mission-crew-quiet.md`，逆向见 `docs/mission-crew-quiet-re.md`。
