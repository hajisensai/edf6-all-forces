# 2026-10-11 实机反馈：「飞机起飞的时候会抖抖的」

只做了静态代码分析、离线仿真和构建；**没有启动游戏、没有安装 DLL、没有游戏内实测**。

## 1. 玩家驾驶的固定翼（战斗机、攻击机及所有插件机翼）：离地下一帧被当成落地（主因）

- `playerjet.cpp Ground` 抬头离地：`phase=air`、`vel.y=kLiftOffClimb`（5 m/s）。
- 抬离的那一步物理开始时机轮还在跑道上，接触回调 `0x652B30` 照样置 `veh+0x1580` bit1；Havok 对几厘米内的接触还会保留几帧。
- 下一帧 `Fly` 先跑 `ReconcileGround`（2026-10-06 c6da133 为「回弹着陆」加的）：空中 + bit1 + `clear<=kTouch` → `Touch`。
  5 m/s 爬升算作轻柔着陆（sink 0）→ `Landed`：回到滑跑、`vel.y=0`、迎角 / up / 瞄准清零，没按加力键时油门清零。
  - 松开加力键（地面油门杆停在原位的正常用法）：油门 0，在跑道上刹车，抬头一跳后又落回去；
  - 按住加力键：同一帧 `Ground` 又起飞，俯仰角速度被 `Ground` 的“只绕 up 偏航”覆盖，迎角从 0 重来；机轮还贴地的几帧里
    地面契约（求解器的法向与俯仰横滚）和空中整份写入交替 —— 抖。
- 修法：`pjet_handling.h OwnLiftOffContact`。起飞后 `kLiftOffGraceMs`（1 s，按 5 m/s 0.6 s 就过 kTouch）内、仍在爬升（飞控 vy>0）时，
  这个接触是自己的机轮离地，`ReconcileGround` 不当着陆；窗口内下沉（vy≤0）照旧着陆，`Air` 的地面步也照旧。
- 测试：`tests/playerjet_recovery_test.cpp` 新增起飞仿真（生产 `Lever`/`ReconcileGround`/`Air`/`Ground`，替身物理按接触回调置位）。
  未修：松开加力 1 次起飞 1 次落回、1.5 s 时离地 0 m、最后在地面；按住加力 2 次起飞 1 次落回。修后：两种都是 1 次起飞、0 次落回、
  爬升不反号、1.5 s 时 12–14 m。`playerjet_recovery_check.py` 新负控 `takeoff-own-wheels-land`（去掉宽限就失败）。

## 2. NPC 直升机旋翼净空（#136）在降落、起飞时的横推（次因，审阅意见）

- `rotor_clear.h Clear` 的外推（每米 1.2 m/s，至多 8 m/s）在降落模式下也生效，和降落点的期望速度对抗；离地 1.5 s 无横向杆
  结束时整份外推一步加入，离地几米时横向猛冲。
- 修法：`rotorclear::PushShare`：降落、在地面、离地窗口内为 0，之后 `kPushEaseMs`（1.5 s）线性加满；
  朝墙速度的削减（保证桨叶不碰墙）不受影响。`heli.cpp Steer` 按 `h.groundAt` 传入。
- 测试：`tools/rotor_clear_check.cpp`：离墙 4 m 起飞，命令速度最大单帧跳变 4.56 → 0.05 m/s、不反向、加满后仍退出墙外；
  墙边降落：以前被推离降落点 3.29 m，现在 0.00 m。

## 未实测

- 游戏里 Havok 接触保留几帧、机尾抬头时是否擦地（机尾触地在宽限内不会再触发落地，但求解器仍会顶一下）需实机看；
- 玩家驾驶的原版直升机（`AimFly`）和插件旋翼机（`HoverStep`）本次未发现起飞时的来回切换，未改；
- 若实机仍抖，请附 `debug=1` 日志里的 `PJET ... takeoff at` / `landed at` 行（修前每次起飞后紧跟一行 `landed at`）。
