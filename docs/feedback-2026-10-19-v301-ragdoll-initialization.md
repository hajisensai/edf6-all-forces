# 测试场 V301 坦克物理绑定缺口（2026-10-19）

用户在 `51eb03e` 修复版上反馈：「加载测试场时仍闪退」。本次已经完成启动和任务预加载，并成功查询到导入音效；新崩溃为 `C0000005 at EDF.dll+0x6E8284`，读取空指针。

## 实际资源与原生契约

转储中 1196 个已捕获 SGO 字节唯一完整匹配 `EDF41_RANGE_VEHICLE301_TANK.SGO`。实际 `PhysicsSystem.rigidBodies` vector 有 18 个 body，index 3 为 `RagDollProxys.cannon_slide`；它在原生 `0xC0` 条目中的 `+0x60` 为空，正是 `0x6E8284` 无条件解引用的对象。不能用 Havok objects 的出现顺序代替该 vector 顺序。

原生 BindDependency 按 `animation_from_ragdoll` 为每个 body 建立模型链接。转换器为了让 CAS 独占炮管后坐，删除了指向 `cannon_slide` 的原始映射；该物理 body 因而完全没有任何反向初始化链接。原版 EDF4.1 的 38 行变成 37 行。只校验已存在的映射、骨名和 bind frame 会漏掉整条缺行。

## 修复

保留 `cannon_slide` 的 CAS 动画和原 `ragdoll_from_animation`，为物理回写新增无几何、同父节点、同 local/inverse bind 的 `edf6vc_cannon_slide_physics` 骨骼。原被删除映射仅将目标改为此骨骼，保留原平移、四元数 `(-0.5,+0.5,+0.5,+0.5)` 和比例。模型通过现有 DFS 插骨辅助更新骨链、mesh 根和 skin indices。

转换时新增逐 body 的 AFR 覆盖检查，并验证物理回写仅指向隐藏骨、原炮管仍由动画驱动、碰撞体仍从炮管动画取姿态。同步重新生成七个源变体及六个测试场副本，登记表和共享模型成员随之更新。

## 同类范围与验证

修复前只读扫描覆盖本机 Mods 的 21 类、214 个 Vehicle SGO、95 个实际模型和 45 个 physics：唯一缺口是 V301 全族 13 条；其余 201 条覆盖完整，所有现有映射骨骼均存在，未知格式为零。DRILL 删除的是同一 body 的冗余映射，仍有完整覆盖。

新增合成检查覆盖非 identity bind、DFS 中段插骨、mesh/skin 语义与 archive roundtrip；七个真实源变体检查完整初始化链接、原 offset 和正向绑定保留。游戏实测结果另存交付凭据，不以离线检查代替。
