# 空母入口与几何检查

检查基线 `f1ed432`，只读本机 Root.cpk、已安装 SGO/MRAB 与旧日志，未启动游戏或改安装目录。

## 已确认并修复：入口留在翼尖外

空母原来的入口沿用了完整飞行包围盒半宽。即便已经换成贴合机体的 Havok compound，入口仍在 `mdl` 坐标 `(30.303,-8.516,1.8)`，距机腹外侧约 23 m。

`vcobjects.walkup_box` 从真实机腹宽度计算候选入口，并检查从外翼到入口的整个走廊。使用与生成碰撞相同的 convex cells，要求 1 m 宽、2.5 m 高的站立净空；低机翼、起落架或吊舱挡路时仍保留外侧入口，不移除碰撞。

本机真实模型的空母候选入口为 `(7.686,-8.516,1.8)`；走廊上最低碰撞表面在模型 y=4.246 m，允许人员接近。截击机、多用途机的低翼不满足净空，入口保持原位。NPC、停放及请求版走同一个生成函数。

## 已确认并修复：构造期把贴合碰撞拒绝掉

`body506.cpp AirframeShape` 原来在构造 flight body 时要求 `vehicle+0x162C` 已有插件飞机标记。但真实 EDF.dll 的 `0x64E501` 清零 `movement+0xAC`（即该标记）；`0x64E9A6` 调的 `0x6574F0` 只写 `movement+0x90..+0xA8`，随后 `0x64E9B6` 进入 `0x656E90`，在 `0x65713C` 调用选 shape 的 hook。`mission_setup` 标记此刻仍为 0。结果即便贴合碰撞资源已经生成，构造时仍退回整翼大盒；fallback 日志自己也按标记过滤，所以日志只有 `airframe=1`，没有选中/失败记录。本机 2026-10-08 最后一次现有日志符合这个表现。

修复在已校验的 HelicopterBase / 506 vtable 下读取现有 ragdoll part；只有 shape 的原生 compound vtable 和专属 `EDF6AIR1` userData 同时匹配才采用。原版直升机、沙扎比的未标记 shape 和读不到的 shape 都保持原来的 box。构造期不再依赖尚未加载的配置标记。该修复也是上面机腹走廊真正可走的前提，不能只改生成器入口。

## 悬空/入口地下：证据边界

当前模型的 y 范围是 `0..17.03125`，全模型碰撞中心/半高均为 `8.516`。SHKT 使用减去同一中心后的模型顶点；MAB 挂在 `mdl`，其门 y 加上全模型中心为 0。ragdoll 正反绑定分别是 `+centre/-centre`。已安装的三种空母 SGO 和 MRAB 也具有这一数据，并非本机旧资源缺少这个偏移。

旧日志实际记录 carrier position `(-297,9,16)`，门相对载具中心 `(30.30,-8.52,1.80)`。不能用这条日志证明用户本次地形/姿态下门的世界高度；它没有记录门脚下地形或本次悬空画面。

代码中 `Body506::PhysicsHook` 只在原版物理步骤之后写线/角速度，不重写模型矩阵。玩家 `FloorClear` 从中心的离地高度减去 `RestOver` 一次；`HoldOffGround` 的 `pos-clear` 是允许的中心最低高度，不能再减一次中心来修复。尚未证实当前模型悬空的运行时根因，因此没有凭猜测改 y 偏移，也不声称这一部分已修复。

`HoverDone` 在接近地面、低速时转 `parked` 并设 `active=false`，之后 `PlayerJetBodyStep` 不接管速度，原版物理继续。`JetFrame` 因 `PlayerJetHolds` 在写 `seen` 前返回，NPC `JetBodyStep` 的上一帧速度最多残留到 200 ms 超时，不能据此推出永久悬停。`kFloorGap=1` 的飞行安全距离也不足以证明最终停机后仍悬空。整翼 box 和 compound 的最低 y 相同，因此更换 shape 本身不能被当成 y 偏差已修复的证明。

## 验证

- `python tools/aircraft_collision_check.py --game`：8/8，通过真实 Root.cpk 生成、回读 SHKT/SGO，空母 365 个凸包覆盖 6887 个表面样本。
- 新回归验证空母门在机腹旁、门落回模型地面、低翼机保留外门；负对照给走廊加碰撞阻挡后必须拒绝内移。
- 生产 `AirframeShape` 直接纳入 C++ fixture，MSVC `/W4 /WX` 编译，10 项合同检查通过（另外检查 fixture 内存分配）；恢复旧的构造期 mark gate 后首个 tagged-aircraft 场景失败，恢复修复后通过。
- `airframe_constructor_native_audit.py <EDF.dll>`：原版 mark 清零指令实际执行，4 种输入标记均变成 0；构造函数调用顺序和已审阅 movement loader 指纹匹配。没有执行完整构造函数或 Havok world。
- 现有 `selftest.jet_door_on_the_ground_beside_its_box()` 通过。
- 未进行游戏内/联机验收。
