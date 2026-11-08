# 2026-11-08 测试站 #31：NPC 上车绕不过车体（tester-20qs，0.8.0-build.eba4372）

原话：

> NPC被指挥上下载具降智 能做到半天都蹭不到载具登入口然后开始驾驶 然后空袭兵移植的炮台 拿着的动作有问题 并且局内无法丢出 丢出后会减慢空袭兵本人速度并且可能原地踏步

本文只处理第一项（上车）。空袭兵的移植哨戒炮（EDF5 304 类挂进 305 类）要实机确认，另行处理。

## 日志（logs-20261030-202436，测试者回传，只当证据）

- `NPCAI soldier 000001B115D344E0: board order v=000001B1A1F58EC0 seat 0`，车是 `603_Flak pos=(-102,-0,272)`；
  26 s 后 `board order dropped (too long) seat 0 point (-104.8,-0.0,270.8) reach 2.3 soldier (-100.1,0.1,268.9) 5.1 m off`，
  1 s 后重新下令，20 s 后又以同样的 5.1 m 作废。士兵位置一直没变。
- 更早一局 `601_Tank pos=(-284,1,497)`：士兵连续 6 次 `dropped (too long) ... 4.6 m off`，坐标一直是 (-286.7,0.8,499.1)。
- 整份日志 162 次上车成功、22 次作废。作废的几例里，乘车点和士兵都离车心约 3 m、分在车体两侧，两点连线穿过车体。

## 根因

- 上车走位（`src/npcai.cpp` Board → MoveTo → `src/ground_navigation.cpp` GroundNavigate）的边检测只打地图射线
  `MapRay`（layer 22，`filterInfo=0x16`）。这一层只和地图 / 建筑层碰撞，不和角色、载具碰撞（`docs/raycast-re.md` §1、§3）。
- 所以规划器看不见车体：乘车点在车另一侧时，路线直接穿过车体，士兵顶在车上。1.5 s 不动会重新规划，
  但重新规划还是同一条只看地图的路线；20 s 没靠近 `kBoardProgress` 就作废命令（`npc::BoardWalking`），
  CREW 模块马上又下令，于是一直循环。

## 修法

- `crew::Hull`（`src/ground_navigation.h`）：GroundNavigate 可以多带一个「路线必须绕开的物体」。上车时传的是目标车：
  - 查询用子弹的对象层 layer 11（`heli.cpp` ObjectRay）。它能打到载具的刚体，命中体经 `0x108260(body id)` 得到所属对象，
    等于目标车就判为挡住（`docs/bullet-pass-re.md`、`docs/boarding-re.md`）。打到别的对象（其他士兵、走路的士兵自己）就从命中点往后接着打，
    最多 `kHullHits` 次。
  - 只在中心线上扫（落脚点的头顶空间和三个高度的水平扫掠，`npc::navigation::WalkEdge` 的 `hull`）。士兵中心不会在车体里，
    所以每条扫掠都从车外开始；侧边采样点在士兵贴着车时可能已经在车体里，射线从里面打出去测不到车体表面。
  - 上车圈（`npc::BoardRoom`：士兵站在乘车点下方地面这个水平半径内就在触发球里，会直接上车）以内不查车体。
    乘车点常常贴着车身，甚至在车体占地范围里，圈内要是也查车体，就到不了。
  - 带不同物体（或从带到不带）时重新搜索（`BasicState::hull`），同一目标点之前不看车体搜出的路线不会继续沿用。
- 没有加长 `kBoardMs`，也没有改重新下令的逻辑。

## 验证

- 离线：`tests/ground_navigation_test.cpp` BoardFarSide（反例：不看车体时路线穿过车体；看车体后绕行到车另一侧的乘车点；
  乘车点在车体占地内时靠上车圈到达）。`tests/ground_navigation_runtime_test.cpp` HullRoute（生产适配层确实调用车体查询、
  共用查询预算、带上车体后重新搜索）。
- 需要实机：layer 11 射线能打到各类载具车体、`0x108260` 对车体刚体返回车本身（文档记为 H，但这条路径上没有实测）。
  另外要用 #31 那样的站位（乘车点在车另一侧）看士兵是不是绕到车另一侧上车。
