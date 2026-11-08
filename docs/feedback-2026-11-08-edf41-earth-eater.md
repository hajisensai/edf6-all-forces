# 2026-11-08 反馈：噬地者（Earth Eater / アースイーター，4.1 的 UfoMother401）被敌人当成地面

分支 `fix/1108-edf41-earth-eater-layers`。地址：4.1 为 `EDF41.exe` RVA（ImageBase 0x140000000），EDF6 为 `EDF.dll` RVA（TimeDateStamp 0x678CCB46）。
可信度：**H** 反汇编 / 游戏文件直接读到；**M** 由读到的代码推出；**U** 没核。逆向细节在 `docs/edf41-ufomother401.md` 2.7。

## 用户原话（不可信数据，只当证据）

英文玩家打完了 4.1 离线全部任务（不含 DLC），由维护者 11-07 转贴：

- 「Enemies get stuck on Earth Eaters.」
- 「Deroys go out of their way to stretch their legs and try to climb onto Earth Eaters.」
- 「Bees and dragons also climb onto Earth Eaters or get stuck underneath them.」
- 「Even ants and spiders, which aren't supposed to climb Earth Eaters, are sometimes spawned already on top of them if an Earth Eater is present at the start of a wave」
- 「Spiders, in particular, sometimes fly off the map—likely due to their behavior patterns—and continue falling endlessly into the distance, making the mission impossible to complete」

## 结论

1. **部件的碰撞层确实与 4.1 不同（H），已改。** 移植的 12 个类都以 EventAnimationObject 为底，它的构造（0x5B6401 `0x6E9320(ragdoll, 0x11)`）
   把所有刚体放到层 17。4.1 不是这样：面板的物理壳（PanelBase 0x26FAB0）在 `is_keyframe_ragdoll` 为 0 时放层 13（0xEEC00），为 1（只有核心
   COREPANEL）时放层 7（0x334370）；炮台（GunBatteryBase 0x2691E0）不改层，停在 ragdoll 默认的层 6（0x333FB0 `or ecx, 6`）。
   两作的碰撞表都从各自的 CollisionFilter 构造读出（EDF6 0x105510 共 141 对，4.1 0xEBC70 共 118 对），按「与身体层 1..8 的碰撞、地图射线能否看见、
   范围伤害查询能否看见」三条对齐，选出 EDF6 的对应层：**面板 13 → 15，核心 7 → 7，炮台 6 → 6**（`src/edf41_hitlayer.h` `Edf6For41`）。
   部件第一帧有刚体时按此改层（`src/edf41mother401.cpp` `PartLayer`）。改层函数的指纹不符、或运行时找到的过滤表与构造读出的不一致时，不改层并写日志。
   - 直接后果：EDF6 的范围伤害查询（层 13）看不见层 17，火箭、导弹炸不到任何 401 部件（301 母舰 2026-10-17 在 M130 实机就是这样）。改到 15 / 7 / 6
     后三者都能被这条查询看见。
2. **「敌人被刷到面板上、爬上去」不能靠改层修，分诊的推测不成立（H）。** 分诊推测 4.1 的面板对「打地图」的射线不可见，所以想选一个层 22
   看不见的层。读 4.1 的碰撞表后结论相反：4.1 的 `hkpWorld::castRay` 调用点（0x602FD0 / 0x6030D0 的 13 处调用里 10 处，含刷怪落点
   0x14D0F0；其余 1 处 0x14、2 处 0）用过滤信息 0x13，也就是层 19；4.1 的层 19 与面板的层 13 碰撞。层 13 与身体层的碰撞关系也与 4.1 的地图层（14–17）相同。
   所以在 4.1 里面板本来就是「地图类」的层，刷怪射线和爬墙逻辑都看得见它。在 EDF6 里把面板藏起来（例如放层 7），会让面板与 4.1 不一致
   （层 2、9 等身体层的碰撞、地图射线），属于凭猜测改行为，没有做。
   - 推测（U，需实机）：差别可能在别处，例如两作刷怪射线的起点高度不同。4.1 的 0x14D0F0 先从落点上方 1 m 往下打，再从命中点或给定高度
     打到 −1000；EDF6 的 CastRayCreatePosition 0x1D7490 起点怎么定还没读完。如果 EDF6 从高处往下打，就会落在悬空的面板顶上。这一点要单独逆向，
     再按 M018 / M019 实机比对。
3. **蜘蛛飞出地图后一直往下掉：根因未定，没改。** 只从代码看，MovePanel（`src/edf41mother401.cpp`）每帧直接写面板的世界矩阵，不给刚体速度；
   EventAnimationObject 怎样把这个矩阵带给刚体（关键帧还是瞬移），以及瞬移会不会把站在上面的蜘蛛挤飞，静态读不出来。玩家同时报了山地图
   地板漏，也可能是 SK 地形的碰撞洞。不加出界杀死之类的掩盖手段。需要玩家给关卡号，或实机复现。

## 需实机确认

- M018 / M019 等噬地者关：火箭 / 导弹能打伤面板、核心、炮台（日志 `EDF41M401 <类> ...: N bodies, layer 0x11 -> 15 / 7 / 6`）。
- 改层后玩家、NPC、敌人站在面板上的表现应与 4.1 相同（层 15 与身体层 1..8 的碰撞和 4.1 的层 13 一致）。
- 本插件的重力枪用层 15 的射线抓物体（`src/gravity.cpp`），现在能打到面板。它先按命中刚体所属对象的类族分派（虫、人形、怪物、人类、步行者……），EventAnimationObject 不在这些类族里；其余分支（载具、容器、碎片）会不会接住 401 部件没有核（U），实机时顺带看一眼。
