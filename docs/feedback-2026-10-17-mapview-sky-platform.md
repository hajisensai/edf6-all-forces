# 2026-10-17 反馈：M 地图视角被空中平台顶住

分支 `fix/mapview-sky-platform-1017`，基于 `origin/main` f2ed99b（#188）。
**本文的改动只做了离线验证（静态逆向 EDF.dll、编译、离线 check、ctest），没有进游戏实测。**

## 原话（玩家反馈，不可信数据，只作证据）

> 有天空的那种平台怪的时候，按m视角只能在平台上面，下不去

## 根因

### 地图视角的高度怎么算（代码事实）
`src/map.cpp` `MapFrame`：焦点高度取 `GroundAt`（`MapGroundNear`：竖直射线上与参考高度最近的那层地面），
`mapcam::Place` 把眼睛放在焦点上方 `height`（200–3000 m）、往后退 `height / tan(pitch)`，然后：

```cpp
const float under=TopAt(eye[0],eye[2],eye[1]-v.height);
if(eye[1]<under+kEyeClear)eye[1]=under+kEyeClear;   // kEyeClear = 40 m
```

旧的 `TopAt` 从 4000 m 高处往下打一条地图射线（`MapRay` = layer 22 + 游戏的地面收集器 0x179CBE8，只收 `flags & 2` 的命中），
取**第一个**命中，也就是眼睛正下方「一切东西的顶面」。原本的用意是让眼睛不钻进山脊、建筑和洞穴顶的岩体。

### 为什么空中的平台会顶住眼睛（逆向事实 H，推断已标出）
- 碰撞层：`CollisionFilter` 构造 0x105510 的 141 对层碰撞（本次重新解析）里，layer 22 只与 {15,16,17,18,19,20,26} 碰撞（与 `docs/raycast-re.md` §3 一致）。
- **EventAnimationObject 的构造 0x5B5FB0 在 0x5B6401 调 0x6E9320(ragdoll, 0x11)，把自己 ragdoll 的全部刚体放在 layer 17**；
  MovingFortress 0x4CBAC0 在 `+0x1A8C == 0` 时同样放 17。layer 17 是地图物体 / 建筑的层，所以地图射线会打到它们。
  对比：UfoCarrier（0x4F5A50）、UfoMother511（0x4FE740）放 layer 7，地图射线打不到。
- 插件移植的地球吞噬者 UfoMother401 的 11 种部件都以 EAO 为底（`docs/edf41-ufomother401.md`），所以在天上排成一大片的部件就是
  地图射线眼里的「地面」。`edf41classes.cpp GroundHit` 的注释记录过同一现象：UfoRobo 的地面射线打到了自己的脚（M411 实测）。
- 于是当眼睛低于平台顶 + 40 m（放大到 200 m 左右、平台在几百米高）时，`TopAt` 返回平台顶，眼睛被抬到平台上方 40 m，
  怎么缩放都下不去。这和原话吻合。
- 「天空的平台怪」具体是哪一种：**推断**为地球吞噬者（UfoMother401 部件）；原生 EAO 类的大型敌人、MovingFortress 也会这样。
  原生传送船 UfoCarrier508、母舰 UfoMother301（以 UfoCarrier508 为壳）在 layer 7，不会触发。

## 修复

区分「地图」和「会动的物体」用的是物理刚体的运动类型，不是层（layer 17 同时装着建筑和 EAO，按层分不开）：

- hknpWorld（`*(g+0x68)+0x20`，虚表 0x18BC9C0）第 16 槽 0xDAB170 是取刚体：`*(world+0x18) + (id & 0xFFFFFF) * 0xB0`；
  刚体 `+0x50` 是 id、`+0x54` 是标志；第 13 槽 0xDAC200 以 `flags & 3` 且 id 一致判有效（H）。
- 游戏自己读运动类型的 0x11B1960：`flags & 1` → 0（静态），`flags & 4` → 1，否则 2（H）。ragdoll 切模式 0x6ED480 的模式 2–4 把刚体
  设成运动类型 1 / 2（H），也就是非静态。
- 新增 `mapfloor::BodyOf` / `mapfloor::Fixed`（`src/map_floor.h`，纯逻辑）与 `MapFixedRay`（`src/heli.cpp`）：沿地图射线逐个命中，
  命中的刚体若是非静态就从它后面 5 cm 接着打；静态或读不出（签名不符、世界不在、id 对不上）就照旧当地面。
  `CastRay` 多了一个可选输出参数：命中刚体 id（收集器 `+0x78`）。
- `map.cpp TopAt` 改用 `MapFixedRay`。地形、建筑、桥、洞穴岩体都是静态刚体，眼睛仍会被抬到它们上方（原设计保留）；只有会动的刚体
  （空中的敌方部件、倒塌中的碎块）不再顶住眼睛。
- 启动时校验：第 16 槽仍指向 0xDAB170 且字节一致、0x11B19AD 的 `mov ecx,[rax+0x54]; test cl,1` 一致，否则 `bodyOk=0`，行为与旧版完全相同。
  日志：`MAP ray bodies=1 (...)`；首次穿过会动的刚体写 `MAP fixed ray: passed moving body XXXXXX`（每次运行最多 8 行）。

### 有意不改的
- **地图光标落点**（`mapcmd.cpp GroundAlong` → `MapFloorRay`）：眼睛到了平台下面后，光标射线从下方出发，本来就打不到平台；
  眼睛在平台上方（拉远）时点到平台上仍落在平台顶。支援呼叫可能就是要打那块平台，穿过去会把空袭落到平台下面的地上，所以保留。
- **焦点高度**（`GroundAt` / `MapGroundNear`）：跟随时取与玩家高度最近的那层；玩家站在平台上时焦点应在平台上。保留。
- `MapRay` / `MapFloorRay` 的其它使用者（直升机避障、下车落点、地面导航等）不受影响：只有地图视角的 `TopAt` 换了射线。

## 验证
- `build.cmd`（/W4 /WX）通过。
- `tools/map_cmd_check.cpp` 新增「眼睛的地面」组：标志位解码（静态 / 动态 / 运动学 / 无效）；穿过平台顶与底到地形；平台下方 250 m 的
  眼睛不被抬升；平台下面的建筑顶仍算；洞穴岩顶照旧；读不出的刚体照旧挡住；只有会动的刚体时返回无；超过 `kMaxHitsDown` 放弃。
  `map_cmd_check` 5701 例 0 失败，`map_cam_check` 通过；`offline_checks` + `ctest` 见提交记录。

## 未验证（需要实机）
1. 地球吞噬者部件的刚体在游戏里确实是非静态（`flags & 1 == 0`）。依据只是 ragdoll 模式与运动类型的静态逆向；若 EAO 的刚体
   是静态体被逐帧搬动，本修复不起作用（不会更糟），日志里会看不到 `MAP fixed ray: passed moving body`。
2. 玩家原话里的「平台怪」是否就是地球吞噬者（可能是别的 EAO 类或 MovingFortress，修法相同）。
3. 眼睛位于平台厚度之内（平台底 < 眼睛 < 平台顶）时画面会被平台挡住一部分，需要玩家再缩放；没有为此另加处理。
4. 射线从凸体内部出发时 hknp 是否报命中：若报 0 距离命中，`Fixed` 每次前进 5 cm，最多看 24 个命中后放弃（返回无 → 眼睛只按焦点高度）。
