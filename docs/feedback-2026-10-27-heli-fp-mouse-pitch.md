# 直升机第一人称：俯仰不跟鼠标

用户原话：「直升机还是没办法根据鼠标调视角啊，修一下」，补充「是俯仰不跟，左右跟」。

## 根因

本机日志（`a43f80b6`，11:23）：`HELI INPUT` 的 `aim.y` 随鼠标在 −0.30…+0.21 之间变化，鼠标 Y 已进入鼠标飞行；同一段里
`FP in a vehicle: 506_Helicopter … seat 0, eye at its cockpit`——玩家在**载具第一人称**里飞。

[2026-10-12 的修复](feedback-2026-10-12-heli-mouse-camera.md)只接管了第三人称：`PlayerHeliCamera` 挂在 Type-1 座位镜头的
定位点取点 `0xFC01B`（`turretcam.cpp LookHook`）。第一人称由 `firstperson.cpp Ride` 在原版镜头步之后整块写镜头矩阵，朝向取座舱
标记 `vc_eye_0` 所在骨骼的帧，也就是机体姿态：机身会转向鼠标航向，所以「左右跟」；机身俯仰归 W/S，鼠标仰角从不进入这个帧，
所以「俯仰不跟」。

## 修复

- `heli.cpp PilotViewDir`：两种视角共用的门控（本机、0 号座、键鼠、启用鼠标飞行、250 ms 内的记录）与缓动方向（80 ms，按游戏时间，
  同帧不重复积分）。第三人称的轨道半径 / 高度仍在原版定位点镜头首次摆放时取。
- `heli.cpp PlayerHeliFirstPersonFrame`：眼睛留在座舱标记上，朝向换成同一鼠标视线（水平地平线，与 Sazabi 第一人称一致）；
  `firstperson.cpp Ride` 在 Sazabi 之后调用它。座舱屏幕的帧（`screens.frame`）仍是机体的，姿态仪不受影响；按住观察键的自由视角
  照旧叠加在上面。
- 手柄、NPC、远端玩家、其它座位、关闭 HeliMouseAim：保持座舱骨骼帧。

`tests/heli_player_input_test.cpp` 走生产代码：机体水平时鼠标下压，第一人称视线跟着下压、眼睛不动；其它座位与手柄不改帧。
实机手感未验证（游戏开着时没装 DLL）。
