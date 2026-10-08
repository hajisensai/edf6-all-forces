# 空母入口与几何检查

检查基线 `f1ed432`，只读本机 Root.cpk、已安装 SGO/MRAB 与旧日志，未启动游戏或改安装目录。

## 已确认并修复：入口留在翼尖外

空母原来的入口沿用了完整飞行包围盒半宽。即便已经换成贴合机体的 Havok compound，入口仍在 `mdl` 坐标 `(30.303,-8.516,1.8)`，距机腹外侧约 23 m。

`vcobjects.walkup_box` 从真实机腹宽度计算候选入口，并检查从外翼到入口的整个走廊。使用与生成碰撞相同的 convex cells，要求 1 m 宽、2.5 m 高的站立净空；低机翼、起落架或吊舱挡路时仍保留外侧入口，不移除碰撞。

本机真实模型的空母入口最终为模型原点坐标 `(7.686,0,-1.309)`；走廊上最低碰撞表面在模型 y=4.246 m，允许人员接近。截击机、多用途机的低翼不满足净空，仍放在机翼外侧。NPC、停放及请求版走同一个生成函数。

## 已确认并修复：构造期把贴合碰撞拒绝掉

`body506.cpp AirframeShape` 原来在构造 flight body 时要求 `vehicle+0x162C` 已有插件飞机标记。但真实 EDF.dll 的 `0x64E501` 清零 `movement+0xAC`（即该标记）；`0x64E9A6` 调的 `0x6574F0` 只写 `movement+0x90..+0xA8`，随后 `0x64E9B6` 进入 `0x656E90`，在 `0x65713C` 调用选 shape 的 hook。`mission_setup` 标记此刻仍为 0。结果即便贴合碰撞资源已经生成，构造时仍退回整翼大盒；fallback 日志自己也按标记过滤，所以日志只有 `airframe=1`，没有选中/失败记录。本机 2026-10-08 最后一次现有日志符合这个表现。

修复在已校验的 HelicopterBase / 506 vtable 下读取现有 ragdoll part；只有 shape 的原生 compound vtable 和专属 `EDF6AIR1` userData 同时匹配才采用。原版直升机、沙扎比的未标记 shape 和读不到的 shape 都保持原来的 box。构造期不再依赖尚未加载的配置标记。该修复也是上面机腹走廊真正可走的前提，不能只改生成器入口。

## 已确认并修复：继承的直升机动画抬高了飞机网格

只看模型的 bind 顶点不能发现这个问题。真实 `V506_HELI.CAS` 的 `default` 对 `body` 写绝对局部平移 `(0,1.637379,0)`，对应原版 V506 MDB 的 `body` 绑定高度 `1.637380`。生成的空母、无人机 MDB 中 `body` 的局部绑定均为 0，却继续用了这个 CAS。空母 5890 个顶点全部蒙在 `body` 或它的四个推进器子骨上，所以整机被抬高；`mdl` 上的门不随 `body` 抬高。

生成器现在为这两个模型输出专用 CAS，按源/目标骨骼真实局部绑定差值重定向 `default`，不碰两个 `roter_*_add` 的增量平移、旋转/尺度通道、关键帧和 CAS 状态图。三种空母和无人机的 NPC、停放、请求版均引用专用资源；完整安装和独立测试场均生成、登记它们。

使用真实 MDB 的 inverse-bind、逐骨骼层级 world、全部 BLENDINDICES/BLENDWEIGHT 重放实际 CANM：两种飞机修前可见网格最低 y=`1.6373790503`，修后为 `-0.0000009537`；修后全部顶点与 bind 顶点误差小于 `0.00001 m`。这复现并修复了模型相对自身碰撞/门悬空约 1.64 m 的数据根因，不是游戏内截图验收。

## 已确认并修复：把输入阶段的碰撞中心当成了定位点原点

模型的 bind y 范围是 `0..17.03125`，全模型碰撞中心/半高均为 `8.516`。SHKT 使用减去同一中心后的模型顶点；已安装旧 MAB 挂在 `mdl` 却写 y=`-8.516`。ragdoll 正反绑定分别是 `+centre/-centre`，属于物理代理的中心合同，不能套到 MAB 定位点上。

旧日志实际记录 carrier position `(-297,9,16)`，门相对载具对象矩阵 `(30.30,-8.52,1.80)`。原来的结论错误地把两个日志阶段混在一起：`VEH` 来自输入钩子，此刻对象位置暂时是碰撞中心；`DOOR` 相对对象矩阵算局部位置，本身不能证明那个矩阵还是输入阶段的中心。

真实原生调用链闭环：构造函数 `64F0B3..64F15D` 把状态 `v+1780` 装到 `v+16E8`，其回调 `64F960` 转发到 slot 60；对象更新 `653680` 先执行状态，`652A60 -> 64F960 -> 652630` 在输入/物理/effect 三个槽之后，把 `v+1680`（负碰撞中心）经朝向旋转，加回 `v+90`，将中心变回模型原点。随后 `653790 -> 630250 -> slot45(652700) -> 62E7D0 -> 1100B90` 用这个原点更新模型 world。

私有 native fixture 实际执行状态分发、中心转换、ModelInstance::SetWorld 和 `6BB420` 的 MAB SeatPoint。只有无关 input/physics/effect 三个槽使用 no-op，外层更新顺序用已审阅代码指纹守卫。输入中心 y=9，转换后模型/mdl 原点 y=0.484：旧门 world y=`-8.032`，低于模型底 `8.516 m`；读本次真实生成 SGO 的门后 world y=`0.484`。三种空母、无人机、yaw 0/90 全部通过，并且把新门 y 改回 `-hy` 的负对照必然再次沉入地下。

生成器现在统一把飞机定位点写在模型原点上，门的 y=`cy-hy`，z=`cz+stockZ`；座位镜头的 fit/check 同时使用这个坐标系。玩家飞控仍在输入阶段处理中心，`FloorClear/RestOver` 不重复修改。

`HoverDone` 在接近地面、低速时转 `parked` 并设 `active=false`，之后 `PlayerJetBodyStep` 不接管速度，原版物理继续。`JetFrame` 因 `PlayerJetHolds` 在写 `seen` 前返回，NPC `JetBodyStep` 的上一帧速度最多残留到 200 ms 超时，不能据此推出永久悬停。`kFloorGap=1` 的飞行安全距离也不足以证明最终停机后仍悬空。整翼 box 和 compound 的最低 y 相同，因此更换 shape 本身不能被当成 y 偏差已修复的证明。

## 验证

- `python tools/aircraft_collision_check.py --game`：8/8，通过真实 Root.cpk 生成、回读 SHKT/SGO，空母 365 个凸包覆盖 6887 个表面样本。
- 新回归验证空母门在机腹旁、门落回模型地面、低翼机保留外门；负对照给走廊加碰撞阻挡后必须拒绝内移。
- 生产 `AirframeShape` 直接纳入 C++ fixture，MSVC `/W4 /WX` 编译，10 项合同检查通过（另外检查 fixture 内存分配）；恢复旧的构造期 mark gate 后首个 tagged-aircraft 场景失败，恢复修复后通过。
- `airframe_constructor_native_audit.py <EDF.dll>`：原版 mark 清零指令实际执行，4 种输入标记均变成 0；构造函数调用顺序和已审阅 movement loader 指纹匹配。没有执行完整构造函数或 Havok world。
- 现有 `selftest.jet_door_on_the_ground_beside_its_box()` 通过。
- CAS 蒙皮重放正/负对照通过；在安装目录外临时目录生成独立空母测试场，SGO 引用和登记的 CAS 字节与重定向输出一致。
- 最终真实资源检查 10/10；`aircraft_locator_native_audit.py` 实际执行 native 状态/模型根/定位点链，三种空母和无人机各 yaw 0/90、生成数据正对照和旧公式负对照均通过。没有执行完整外层车辆帧、完整 CAS 渲染器或 Havok world。
- 未进行游戏内/联机验收。
