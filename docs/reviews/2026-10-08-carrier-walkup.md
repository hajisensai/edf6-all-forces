# 空母入口与几何检查

检查基线 `f1ed432`，只读本机 Root.cpk、已安装 SGO/MRAB 与旧日志，未启动游戏或改安装目录。

## 已确认并修复：入口留在翼尖外

空母原来的入口沿用了完整飞行包围盒半宽。即便已经换成贴合机体的 Havok compound，入口仍在 `mdl` 坐标 `(30.303,-8.516,1.8)`，距机腹外侧约 23 m。

`vcobjects.walkup_box` 从真实机腹宽度计算候选入口，并检查从外翼到入口的整个走廊。使用与生成碰撞相同的 convex cells，要求 1 m 宽、2.5 m 高的站立净空；低机翼、起落架或吊舱挡路时仍保留外侧入口，不移除碰撞。

本机真实模型的空母候选入口为 `(7.686,-8.516,1.8)`；走廊上最低碰撞表面在模型 y=4.246 m，允许人员接近。截击机、多用途机的低翼不满足净空，入口保持原位。NPC、停放及请求版走同一个生成函数。

## 已确认并修复：构造期把贴合碰撞拒绝掉

`body506.cpp AirframeShape` 原来在构造 flight body 时要求 `vehicle+0x162C` 已有插件飞机标记。但真实 EDF.dll 的 `0x64E501` 清零 `movement+0xAC`（即该标记）；`0x64E9A6` 调的 `0x6574F0` 只写 `movement+0x90..+0xA8`，随后 `0x64E9B6` 进入 `0x656E90`，在 `0x65713C` 调用选 shape 的 hook。`mission_setup` 标记此刻仍为 0。结果即便贴合碰撞资源已经生成，构造时仍退回整翼大盒；fallback 日志自己也按标记过滤，所以日志只有 `airframe=1`，没有选中/失败记录。本机 2026-10-08 最后一次现有日志符合这个表现。

修复在已校验的 HelicopterBase / 506 vtable 下读取现有 ragdoll part；只有 shape 的原生 compound vtable 和专属 `EDF6AIR1` userData 同时匹配才采用。原版直升机、沙扎比的未标记 shape 和读不到的 shape 都保持原来的 box。构造期不再依赖尚未加载的配置标记。该修复也是上面机腹走廊真正可走的前提，不能只改生成器入口。

## 已确认并修复：继承的直升机动画抬高了飞机网格

只看模型的 bind 顶点不能发现这个问题。真实 `V506_HELI.CAS` 的 `default` 对 `body` 写绝对局部平移 `(0,1.637379,0)`，对应原版 V506 MDB 的 `body` 绑定高度 `1.637380`。生成的空母、无人机 MDB 中 `body` 的局部绑定均为 0，却继续用了这个 CAS。空母 5890 个顶点全部蒙在 `body` 或它的四个推进器子骨上，所以整机被抬高；`mdl` 上的门不随 `body` 抬高。

生成器现在为这两个模型输出专用 CAS，按源/目标骨骼真实局部绑定差值重定向 `default`，不碰两个 `roter_*_add` 的增量平移、旋转/尺度通道、关键帧和 CAS 状态图。三种空母和无人机的 NPC、停放、请求版均引用专用资源；完整安装和独立测试场均生成、登记它们。

使用真实 MDB 的 inverse-bind、逐骨骼层级 world、全部 BLENDINDICES/BLENDWEIGHT 重放实际 CANM：两种飞机修前可见网格最低 y=`1.6373790503`，修后为 `-0.0000009537`；修后全部顶点与 bind 顶点误差小于 `0.00001 m`。这复现并修复了模型相对自身碰撞/门悬空约 1.64 m 的数据根因，不是游戏内截图验收。

## 入口地下与运行时高度：证据边界

当前模型的 y 范围是 `0..17.03125`，全模型碰撞中心/半高均为 `8.516`。SHKT 使用减去同一中心后的模型顶点；MAB 挂在 `mdl`，其门 y 加上全模型中心为 0。ragdoll 正反绑定分别是 `+centre/-centre`。已安装的三种空母 SGO 和 MRAB 也具有这一数据，并非本机旧资源缺少这个偏移。

旧日志实际记录 carrier position `(-297,9,16)`，门相对载具中心 `(30.30,-8.52,1.80)`。不能用这条日志证明用户本次地形/姿态下门的世界高度；它没有记录门脚下地形或本次悬空画面。

代码中 `Body506::PhysicsHook` 只在原版物理步骤之后写线/角速度，不重写模型矩阵。玩家 `FloorClear` 从中心的离地高度减去 `RestOver` 一次；`HoldOffGround` 的 `pos-clear` 是允许的中心最低高度，不能再减一次中心来修复。门在世界地形下的坐标还需要结合原版中心/原点转换阶段，不能拿 InputHook 日志的中心高度直接推断其它阶段的骨骼原点。

`HoverDone` 在接近地面、低速时转 `parked` 并设 `active=false`，之后 `PlayerJetBodyStep` 不接管速度，原版物理继续。`JetFrame` 因 `PlayerJetHolds` 在写 `seen` 前返回，NPC `JetBodyStep` 的上一帧速度最多残留到 200 ms 超时，不能据此推出永久悬停。`kFloorGap=1` 的飞行安全距离也不足以证明最终停机后仍悬空。整翼 box 和 compound 的最低 y 相同，因此更换 shape 本身不能被当成 y 偏差已修复的证明。

## 验证

- `python tools/aircraft_collision_check.py --game`：8/8，通过真实 Root.cpk 生成、回读 SHKT/SGO，空母 365 个凸包覆盖 6887 个表面样本。
- 新回归验证空母门在机腹旁、门落回模型地面、低翼机保留外门；负对照给走廊加碰撞阻挡后必须拒绝内移。
- 生产 `AirframeShape` 直接纳入 C++ fixture，MSVC `/W4 /WX` 编译，10 项合同检查通过（另外检查 fixture 内存分配）；恢复旧的构造期 mark gate 后首个 tagged-aircraft 场景失败，恢复修复后通过。
- `airframe_constructor_native_audit.py <EDF.dll>`：原版 mark 清零指令实际执行，4 种输入标记均变成 0；构造函数调用顺序和已审阅 movement loader 指纹匹配。没有执行完整构造函数或 Havok world。
- 现有 `selftest.jet_door_on_the_ground_beside_its_box()` 通过。
- CAS 蒙皮重放正/负对照通过；在安装目录外临时目录生成独立空母测试场，SGO 引用和登记的 CAS 字节与重定向输出一致。
- 未进行游戏内/联机验收。
