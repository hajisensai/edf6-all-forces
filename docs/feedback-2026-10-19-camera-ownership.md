# 2026-10-19 机体第一人称自动偏转与相机所有权

用户原话：“战斗机体有第一人称脖子强制往左不能转回去”。未指定具体机体，本轮覆盖已找到的共享相机反馈路径及 Sazabi 的独立装饰颈部路径；未写游戏目录、启动游戏或操作用户输入。

## 控制视线与渲染结果分离

原生座位更新的输入经过 `(-RX, RY)` 后驱动瞄准轴。Nix 的 yaw/pitch 控制 spine/body，Proteus 驾驶席控制 neck/body，炮手席控制各自的 mount/barrel。第一人称 `vc_eye_<seat>` 正在这些骨骼链中。

旧 `turretcam` 将最后 HUD `CameraRay` 反解为同一座位轴的世界目标；FP 最终画面又跟随这组受控骨骼。普通 FP 没有 mounted optic 已有的隔离门，因此可形成“受控骨骼 → 第一人称画面 → 旧渲染射线 → 同一组轴”的闭环。原生轴到骨骼使用带限位和控制点插值的映射，不能假设实际骨骼角恒等于原始轴角。

修复后，FP、mounted optic、自由观察以及地图拥有视图时，立即撤销旧目标、free/returning、steer 与 normal ray，保留原生/stabilizer 输入；座位身份和 rig 测量继续存在，俯瞰入口可正常切入。普通操炮只消费本座位、本次 take 由正常 Camera step 明确发布的请求 eye/dir；不再消费 HUD `CameraRay`。没有正常来源时按当前原生轴初始化并透传输入，来源由实际相机事件恢复，不等待固定帧数，也不依靠输入抵消或新增平滑。

普通请求来源位于原生 F6760 碰撞修正之前。近墙最终画面的像素射线可能与请求不同；贴墙操炮手感属于实机验收范围。

## Sazabi 的相机与瞄准同源

Sazabi 的 `vc_eye_0` 挂在 sz_head，原实现完整继承装饰性头部旋转。程序动画会把切换盾牌武器的 40 度左 glance、举盾与近战余量写入该头部；同时武器 Aim/Assist 始终使用肩后 45 米、侧方 11 米的第三人称射线。鼠标控制的 heading/pitch 无法直接控制这个额外头部角度。

`SazabiFirstPersonFrame` 现在保留 raw marker 的真实世界眼位，以发布的权威 heading/pitch 构造旋转。第一人称 Aim/Assist 也从实际乘员 marker 眼位使用同一方向；第三人称保持肩后 rig。没有修改装饰动画、世界骨骼或网络姿态以抵消镜头。

原单个全局 View 会使两台本地 Sazabi 相互覆盖。新快照按 Mech 槽独立保存，验证 vehicle、seat、rider 与 control 身份及 freshness；单车退出只清自己的视图，任务 reset 才全清。游戏线程物理中心与相机阶段模型原点的 12.8 米差异保持原生空间合同。

## 相机写入与模式交接

FP、普通 free-look 和敌体控制使用同一份 camera write 事务，分别跟踪旋转和眼位。此前 whole-matrix memcmp 会在外层只改眼位时放过旧旋转，下一次 native no-write 分支再次叠转；无条件还原整帧又会覆盖另一个拥有者的新方向。新事务只归还仍等于自己写入值的对应字段。

敌控相机接在真实 CharacterGhostCamera native step 之后、human FP 之前；命中时退休 human viewmodel、near、batch 并取消跨模式按键手势。外层地图和 TV 继续拥有最终优先级；退出敌控后按当前 native frame 恢复原 FP 请求。敌控 Camera 只读控制器角度，不在相机 fetch 中再次积分输入。其可变 sessions 接口限于游戏线程相机 step，绘制线程使用发布快照。

## FP 与 NPC 绘制隔离

交叉检查发现三组明确的数据所有权缺陷，并补防护：

- 旧全局 pose 备份会被嵌套模型覆盖。备份现由每次 owned draw 独立保存；仅完整捕获的 instance/pose/bones/count 才允许恢复。缺 bones 的 LOD、部分变换异常及 native buffer 更换都不能写入未初始化或别的实例的备份。普通 NPC/native 快速路径不分配该大栈帧。
- 旧 TLS body skin 标记没有嵌套 scope，外来模型可继承本地 body mask，内层 body 又可清掉外层标记。每次 DrawHook 现保存、隔离并恢复精确 mask 状态，包含原生/阴影快速路径。
- body、ride、weapon、inHand 缓存现包含采集时的 owner control。绘制、laser、palette、补交、包围盒及 batch 归还先核当前嵌入 owner，拒绝同址新对象和原生退休 flag 4；不解引用 cached control。旧 batch 身份在“仍在手中”快捷分支前撤销，避免复用地址累积占槽。

原生 `0x11002D0` 构造实例时按骨数 ×64 分配自身 pose，`0x1100F21/2C` 从自身骨记录复制，没有发现正常 NPC 与本地 actor 共享 pose 数组的证据。嵌套/缓存防护能在生产 fixture 复现，但尚不能据此认定它们就是用户红 NPC 扭肩截图的实机根因。主动补绘仍沿用原生渲染队列的对象存活合同，本轮未新增弱引用 pin 架构。

## 验证

- `fp_ride_lifecycle_test` 和 `fp_view_check`：独立 MSVC `/MT /W4 /WX /utf-8 /permissive-` 构建与运行通过，覆盖独立眼位更新、外部新方向、真实座位/过期 control、敌控/FP/P2交接、嵌套 pose/mask、缺失 LOD 与部分异常恢复、旧 owner/batch。
- `turret_cam_runtime_check`：99 项通过；把 FP 隔离撤回会失败 5 项，把来源改回 HUD 缓存会失败 7 项。
- `camera_payload_lifecycle_test`：61 项通过，保留既有断言。
- `sazabi_runtime_test`：73 项；`sazabi_net_runtime_test`：19 项；全部通过。
- `camera_frame_native_test <EDF.dll>`：完整核对原生 LookTo 函数与四个数学导入边界，私有 DONT_RESOLVE 映像执行，80 项验证控制器 frame 和 enemy eye/look 基底与真实原生结果一致。
- tmp 故障注入恢复全局 pose 备份、移除 skin scope，分别准确触发对应生产 fixture 失败；未改动工作树生产代码进行注入。

未进行实际游戏画面、输入手感、墙边碰撞、双机联机或真实双人分屏验收；以上是已验证的代码/原生离线结论。
