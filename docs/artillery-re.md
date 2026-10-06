# 自行榴弹炮（双管坦克）炮塔不转：物理骨架与模型骨架不一致

EDF.dll TimeDateStamp `0x678CCB46`，地址都是 RVA。纯静态分析 + 离线核对（Root.cpk 只读），**没有起游戏**。
置信度：**H** = 指令 / 数据直接可见；**M** = 证据一致的强推断；**L** = 需要实机核对。

用户（2026-10-06）：「双管坦克那个炮塔不能旋转的，固定死的」。

## 1. 排除掉的方向

- **蒙皮**：炮塔外壳 100% 蒙在 `cannon_main`，套筒在 `cannon_l/_r`、身管在 `cannon_slide_l/_r`（H，逐顶点统计；
  `artillery_model.check` 现在逐项核对 `turret_weights`）。离线渲染把 `cannon_main` 转 45° / 90°、炮管抬 30°，只有炮塔和炮管动
  （`skin_*.png`）。
- **SGO**：`EDF6VC_ARTILLERY.SGO` 与 `V603_FLAK.SGO` 只差模型路径、镜头、耐久和两把武器路径（H，逐键比对）；
  `vehicle_setup` 的转台参数、`car_base_constraint_data`、`car_base_rigid_body` 原样。
- **插件**：EDF6VehicleCrew 没有任何代码写自行榴弹炮的瞄准轴或炮塔骨骼（`katyusha.cpp` 只认带 `edf6vc_ram_rod` 的模型，
  `launcher.cpp` 只读俯仰轴的上下限，`highcam.cpp` 只动镜头）(H)。

## 2. Kepler 的炮塔是物理铰链（H）

- `Ragdoll_v603_flak.shkt`：22 个 Havok 刚体（`ragdoll_body`、`ragdoll_cannon_main`、`ragdoll_cannon_l/_r`、雷达、16 个轮子），
  21 个约束：炮塔—车体 `hkpLimitedHingeConstraintData`，两门炮—炮塔、两个雷达—炮塔同样是铰链，轮子—车体是
  `hkpPrismaticConstraintData`（悬挂）。每个约束的 transformA 平移为 0（关节在子刚体原点），transformB 是子刚体原点在父刚体坐标系里
  的位置；ragdoll 骨架的参考姿态同理。刚体位置 = 原版 Kepler 对应骨骼的绑定位置。
- SGO 的 `ragdoll` 第二项（绑定）：`animation_from_ragdoll`（刚体 → 骨骼，偏移）和 `ragdoll_from_animation`（骨骼 → 刚体）。
  Kepler 的偏移除 `catapi_body`（0, 0.772, 0）和轮子的 90° 转角外都是单位。
- 每帧：`0x6EDCA0` 把每个动力学刚体（记录 `+0x9C == 4`）的世界矩阵乘上偏移写进骨骼的世界行（`+0xB0..+0xEF`），并把骨骼的
  `+8` 清 0；第 45 槽 `0x621510` 再调 `0x661C00(veh, cannon_main, body)`：炮塔原点 = 绑定局部 × 车体，朝向取刚刚写进去的
  世界前向、投到车体平面上；`0x661810` 按瞄准轴的俯仰把两门炮的绑定局部转过去挂到炮塔上；最后 `0x6ED800` 把受动画驱动的
  刚体（模式 1 / 2）摆到「骨骼 ∘ 偏移」。第 4 槽 `0x621800` 用 `ragdoll_cannon_r / _l` 的相对转角给左炮刚体设角速度（左炮跟右炮）。
- 刚体建好时全部设成控制器 `+0xC8` 的模式（`0x6EB9F5..0x6EBA15`，1 或 2：动画驱动），即先被摆到模型骨骼处。
  CarBase 初始化（`0x65CA5D call 0x667140`）读 `car_base_constraint_data` 时，按两刚体**当时**的世界矩阵记下相对姿态
  （`0x6676C1..0x667746` 存进约束项 `+0x30..+0x6F`），再给它挂马达（`0x667823` 起，最大力 408200）。

所以炮塔怎么转、画在哪里，取决于物理刚体；物理骨架必须和模型骨架一致。离线核对：Kepler、Blacker、Naegling、Freed 摩托、
V506 直升机的原版模型和原版 ragdoll 逐项一致（`ragdoll_fit.problems` 为空，selftest `artillery_ragdoll_is_the_models`）。

## 3. 双管坦克破坏了这个一致性（H）与炮塔卡死（M）

`pylib/artillery_model.py` 把 Kepler 的骨骼挪到了这个模型上（炮塔到 E551 的炮塔轴，炮管到炮塔前脸的耳轴和炮口，轮子 / 履带到
E551 的负重轮），**但 ragdoll 和绑定还是原版 Kepler 的**：

| | 模型骨骼 | 原版刚体 / 关节 |
|---|---|---|
| 炮塔 `cannon_main` | (0, 1.342, −0.025) | (0, 1.449, 0.643)：铰链轴在炮塔中心前方 0.67 m |
| 炮 `cannon_l` | (0.493, 2.381, 0.579) | (0.747, 2.218, −0.438)：铰链在耳轴后方 1.0 m |
| 负重轮 `tire_moveB_l` | (1.134, 0.377, 2.027) | (1.226, 0.404, 1.756) |
| 履带体 `catapi_body` | y 0.881 | 绑定偏移 y 0.772 |

- 刚体先按模型骨骼摆放（动画驱动），转成动力学后又要服从 shkt 里在原版位置的铰链；CarBase 约束记下的相对姿态取决于记录时
  刚体在哪里。炮塔被两个不在同一条竖直轴上的支点同时约束时，绕任一竖直轴转动都会拉开另一个支点，转不动——这与「固定死」吻合 (M：
  CarBase 记录相对姿态与刚体第一次被摆到骨骼处的先后没有逐条跟到)。
- 画面上也有可见偏差 (H)：轮子和履带的骨骼由刚体写入，画在 Kepler 的轮位上，与 E551 的网格错开 0.1～0.3 m（`frame_before.png`）。

## 4. 修法

`pylib/ragdoll_fit.py`：把物理骨架改成模型的——每个刚体移到 `ragdoll_from_animation` 的骨骼现在摆它的位置（朝向不变，骨骼也只平移），
每个关节留在子刚体上（transformA 不变，父刚体的 transformB 跟着算），ragdoll 骨架参考姿态按刚体重算，`animation_from_ragdoll`
的偏移按刚体和骨骼重算；先核对原版 ragdoll 与原版模型一致，结束后再核对新的也一致（原版模型重拟合逐字节不变）。
`tools/make_artillery.py` 生成 `Mods/OBJECT/EDF6VC_ARTILLERY_RAGDOLL.SHKT`，SGO 的 `ragdoll` 指向它并带上新的绑定；没有模型文件夹时
仍是 Kepler 原版外形和原版 ragdoll。

改变（刚体随骨骼）：炮塔铰链到 E551 炮塔轴，炮的铰链到耳轴，负重轮刚体前移约 0.3 m、低 2.7 cm（车身离地相应变化约 2～3 cm），
主动轮 / 诱导轮刚体到 E551 的位置。Kepler 的碰撞外形跟着刚体一起移动。

## 5. 需要实机确认（L）

- 炮塔随镜头 / 摇杆转动，两门炮俯仰正常、开炮后坐正常。
- 车辆停稳后车身高度、履带贴地是否自然（负重轮刚体的半径仍是 Kepler 的）。
- 喀秋莎有同一类问题（H，离线核对，本次没有改）：`pylib/katyusha_model.py` 把 Naegling 的发射架（`Rocketcannon_base` 子树）挪到
  了卡车车斗上，`ragdoll_fit.problems` 报 30 处不一致——转台 / 发射架骨骼离 `RagDollProxys.canon_base / canon_main` 0.27 m，轮子骨骼
  被并到卡车的 3 根轴上（几根轮骨同在一点），而 `Vehicle402_Rocket.shkt` 的轮子刚体还在 Naegling 的 7 个轮位。轮子要先定方案
  （几个刚体叠在同一点不行），所以留作单独的任务；转台部分可以直接用 `ragdoll_fit.fit`。

## 6. 2026-10-06：炮塔固定（用户要求）与炮口 / 抛壳点

用户（0.8.0 试玩）：「双管战车的炮塔应该不能转。并且他的炮管装反了」。

### 6.1 炮塔的偏航上下限从哪来（H）

- Kepler 的构造在 `0x621251..0x6212B0` 为 0 号座位调两次 `0x669BA0(veh, seat 0, 约束号, 轴)`：约束 0（`cannon_main`/`body`）→ 瞄准轴 0（偏航），
  约束 1（`cannon_r`/`cannon_main`）→ 瞄准轴 1（俯仰）。`0x669BA0` 取 `veh+0x1880` 的第 n 个 CarBase 约束（0x70 一项，`+0x20` 的限位表），
  `0x5EC4A0` 按标志 `1 << (轴 + 3)` 找限位：找到就用它的 min / max，否则 ±π（常数 `0x1C369C0` / `0x1C369D4`），经 `0x5FB790` 写进轴的 {min, max}。
  `0x5FBC00` 在 max − min < 2π 时把轴角夹在其中（`docs/nix-re.md` §3）。
- 这些约束就是 SGO 的 `car_base_constraint_data`：`[子, 父, 2, [轴, [_, 限位, 马达]]]`，限位 `[0]` = 不限，`[1, min, max]`（度）。原版例子：
  每门炮的俯仰 `[1, -60, 5]`，RoboTruck 的腰 `[1, -170, 170]`（偏航，轴 1）。所以炮塔约束改成 `[1, 0, 0]`，铰链和驾驶座的偏航轴同时停在正前方
  （`tools/make_artillery.py TURRET_LIMITS`）：玩家输入、炮塔镜头、EDF6AutoTurret、原版 AI 都只能给输入，转不动它（AutoTurret 只写 `veh+0x2AA0`
  的输入，不写角度；稳定器不管间接射击的 0 号座位）。限位数值的单位（度）与 0..0 的锁定效果是按原版数据类推（M），没跟到解析函数 `0x65E550` 的换算。
- ragdoll 里炮塔自己的 `hkpLimitedHingeConstraintData` 限位仍是 ±π（没改）：CarBase 另建一条带马达的约束（`0x5EBC90`），锁住它就够了（M）。
- 炮塔镜头原来只接管偏航能转 10° 以上的炮位；现在间接射击的炮位不转也接管（`src/turretcam.cpp Turret`），镜头和高视角照旧，炮对不上镜头时 HUD 的白色方框标出炮口方向。

### 6.2 「炮管装反了」：模型方向核对与发现的真实错误

离线核对（`python pylib/artillery_model.py <wt>/tmp/art`，H）：两根炮管沿 +Z（车头，E551 车体的前方，原版 E551 的炮也朝 +Z）；
套筒 r 0.27（z 0.33～1.82，后粗前细）在后，身管 r 0.14 带两道箍，炮口在 z 4.59（喇叭口 r 0.20，内径 0.35 m，内凹 0.15 m 到锥底），
后端 z 0.33 开口但在炮塔里；绕序与法线和原版一致（几何法线与存储法线 100% 同向）；骨骼旋转全为单位阵（与 Kepler 相同），
ragdoll 刚体和铰链朝向不变。**模型本身没有装反。**

真实错误（H，数据）：Kepler 的炮把出口写在炮自己的 SGO 里（`animation_model` 第三项 MAB 的定位点）：L / R 炮都有 (0, 0, 2.894)（炮弹出口）和
(±0.185, 0.322, −1.451)（抛壳），相对 `vehicle_weapon_setting` 的骨骼 `cannon_slide_l / _r`。Kepler 的这根骨骼在身管根部（z 1.007），
1.007 + 2.894 = 3.90 正是它的炮口（E551 同理：骨骼 2.777 + 2.8 = 5.58 ≈ 炮口 5.60）；抛壳点在耳轴旁。双管坦克的骨骼在炮口平面（z 4.591），
照搬的结果：炮弹在 z 7.49 出现（炮口前 2.9 m 的空中），弹壳在 z 3.14、炮塔外的炮管中段抛出。`EDF6VC_HOWITZER_L/R.SGO` 现在按模型改写这两点
（`gun_mount`）：炮弹 (0, 0, 0) → z 4.591 炮口；弹壳 (0, 0.322, −3.963) → (轴, 2.703, 0.629)，耳轴（炮塔前脸 z 0.579）前 5 cm、炮管上方，不在炮塔体内
（炮塔体在耳轴后面把炮尾包住，原版的抛壳位置会落进炮塔里）。`check(files, game)` 用 `gun_problems` 核对（变异：不改写 → 两门炮各报两条）。
这两点的身份是按 E551 的对照推断的（M），没逆向 MAB 记录的名字。

仍缺：用户说的「装反」若指炮塔外形前后（炮塔斜面朝后、竖直面朝前带炮口），那是模型的造型，需要用户截图确认。
