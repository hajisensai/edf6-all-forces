# M163 / BMPT：旋转炮管、真实几何碰撞与原生悬挂合同

本轮在 `aaf35f9f` 的自建两车基础上补齐数据与运行逻辑。仍是明确标注的自建模型，外部 API 仍缺认证，未下载成功。本轮全部检查只读本机游戏文件，未运行游戏、未写游戏目录。

## 六管真实运动

M163 新增 `edf6vc_m61_rotor`，在 `cannon_slide_l` 下按原生 preorder 插入为第 6 号骨，六根炮管与夹箍真正蒙皮到该骨。40 个原生骨的世界 bind、父子关系不变；私有 CAS 仅关闭两根旧雷达的 T/R 通道引用，其他字节不变；接收机、抛壳和轴心炮口保持各自原有合同。

不能把子骨简单追加到 40 号：原生 subtree 范围按连续 preorder 计算。`check_skeleton` 用真实父子范围和原 40 骨 fingerprint 检查。原 `V603_FLAK.CAS` 的 skeleton 表本来就是 0，模型却有 40 骨；native draw `0x1100F10` 按模型数量复制所有骨到 palette，不按 CAS 名表过滤，因此无需伪造 CANM 轨道。

实际渲染链为 `603[3]=0x680130 → 0x674A10 → 0x630220 → 0x6C06A0 → 0x1100F10`。旋转钩子仅改 `0x621654 → 0x1100B90` 的最后一次 world build 之前的专属视觉骨，原函数以原参数恰好执行一次。原 V603 和 BMPT 没有专属叶骨，保持原调用。

开火活动取实际原生武器状态 1、同一武器弹药减少、或原生远端 received pending-shot 的增长。装填/空弹链/历史计数不制造活动。两种 M163 弹链同驱一根转轴。马达以 500 rpm 运行（公开 3000 rounds/min 循环模式 / 六管），有启动和停火滑行，同帧 pose 不重复推进，暂停不推进，大时钟跳变不追帧。模型重用从 immutable MDB bind 恢复，避免叠加已渲染的旧相位。

转速依据 [MCIA Saudi Arabia Country Handbook](https://info.publicintelligence.net/MCIA-SaudiArabiaHandbook.pdf) 的 M163 条目；游戏每帧射击参数仍属于 EDF 平衡值，视觉转速不声称逐发弹道模拟。

## 实际生成的碰撞和轮表

`tracked_collision.build` 以模型实际零件点集拟合独立 SHKT，重写凸包顶点、平面、邻面角、extern transforms、CMS top/section/SIMD 树、路由 key 与实体 AABB 质量 / 惯量缓存。保留 22 个 proxy、21 个关节和两个炮马达，不引入零体积或零质量“禁用”代理。

此前继承的 Kepler 车尾在 M163 外多出约 1.06m、旧炮口多出约 1.99m，BMPT 车宽则少约 .36m。现在代理在实际自建几何范围内：M163 开放炮塔仅拟合闭合转台底座，不把操作员空间填成实心；两枚旧雷达代理拟合实际瞄具，每个瞄具网格真实蒙皮到其原雷达骨；各 proxy 和可见组件在毁车后同走。两车 road-wheel ray 列表分别为每侧五 / 六轮，半径取真实轮网格，旧无对应几何轮站退役。字段也由原生代码核实：`0x679B80` 读取行 index 8，`0x679BFA` 存到 wheel record+5C；`0x679E53..60` 存其倒数给滚转，`0x6787BC..7FE` 按该半径从轮中心偏移到接触点，`0x67A45B → 0x11A6570` 以该半径构造轮形状。

同车 sibling 代理并不互相屏蔽：`hknpGroupCollisionFilter` 仍允许左右炮和 G/H 自碰撞。M163 两个炮代理分别拟合前管和后接收机/托架，保留 .20m 轴向间隙以包含 .15m 相对后坐量及接触外扩；G/H 后惰轮代理分成同一实际轮内外两半，轴向留 .04m。检查覆盖共同 yaw/pitch、滚转和相对后坐，拒绝重叠的假“双份同形代理”。

退役轮代理仍保留原 H 骨的唯一 AFR lookup owner，RFA 读实际 G 骨；两雷达则保留各自原骨的唯一 AFR/RFA，并按真实瞄具重算 offset。若删除旧 AFR，原生名字查找拿不到 proxy index，显式模式行会失效。`car_base_simulation_node` 明确列全 18 个轮/附件站，值 0 经原生恢复默认 mode 1 并关闭旧父 joint；不依赖“没写就自动关闭”。

M163 后惰轮可见网格也分成 G 内半和 H 外半。活车原生只在 mode 4 读回 AFR，故另一个专属 post-wheel hook 以 `HbindLocal * inverse(GbindLocal) * GposedWorld` 同步 mode 1 的外半世界矩阵，保留原 bind，不让它绕旧 H 轴偏心。死亡停止覆盖，原生 mode 4 分别带动真实两半网格。

该外半同步不能和 rotor 一样放在 `0x621654`：真实 603 slot 5 的 `0x674B6B → 0x630250` 先执行 slot45 pose，之后 `0x674B76` 调 slot56 更新轮缓存，`0x674B7F → 0x66F430` 才直接写最终 wheel world。第二钩子先完整执行这个原生 writer，再改 H world，保留原生 record+8 标志，不重建全车世界矩阵。两个钩子只有全部安装成功才开启自定义写入，部分失败仍原参转发原函数。

接收机/托架也绑定到其实际后炮代理，死亡不再出现“无可见几何的替身右炮”。

接触仍是有边界的近似：凸包分片对实际组件范围作仿射拟合，惯量用源 CMS 同款的实体 AABB 近似。没有实车悬挂、质量分布或驾驶手感标定。

## 瞄具活车与毁车一致性

先前仅将 radar proxy 的 RFA 映射到 cannon_main，却将瞄具网格仍留在 cannon_main 上；活车能对齐，但毁车 mode 4 后 proxy 独立飞散而网格不跟随，留下无可见组件的残骸碰撞。本轮移除了这个新回归。

两原 radar 骨本来就是 cannon_main 的直接叶骨。M163 左光学瞄具和右测距仪、BMPT 全景镜和枪手镜分别拆成真实零件组，蒙皮到各自 radar 骨；MDB 的所有原 bind/inverse-bind 保留。RFA 与 AFR 都对应各自真实 radar 骨，offset 与实际 proxy frame 配套。

私有 CAS 复用 `cas_pose.strip_bone`，仅将 `doppler_radar_loop` 内这两骨的 T/R 索引写成 -1。合计恰好 8 字节改变，原通道值、keys、Default 的 38 条轨道、状态图和 `fire_loop` 都不变；不修改被别处共享的 rotation channel 2。原 T21/T23 是 bind translation，因此关闭它们不造成平移。活车无需任何新 pose 覆盖，按原生父子关系固定随炮塔；死亡时 mode 4 的 AFR 自然带动各自网格。

Native 603 pose 在 CAS 采样后只调整 main 和左右 gun。初始化 `0x6210BA / 0x62113A / 0x6211BA` 对应这些骨缓存，`0x6215F4 / 0x62161C / 0x621644` 消费这些缓存；晚轮 writer 只消费 wheel 表，不是 radar writer。不存在“关闭 CAS 后还被另一原生雷达旋转器覆盖”的路径。

## 资源闭环

`make_tracked_combat.build` 输出 M163 六文件、BMPT 七文件。新增资源包含 `OBJECT/EDF6VC_VULCAN_COLLISION.SHKT`、`OBJECT/EDF6VC_BMPT_COLLISION.SHKT` 及对应 `OBJECT/EDF6VC_VULCAN.CAS`、`OBJECT/EDF6VC_BMPT.CAS`。实体 SGO、私有测试场 SGO、resource preload、安装器归属、打包和 M 地图依赖枚举统一使用该输出；不能只有生成脚本而继续指向原 SHKT。

408 邻接修复见 [真实三轮适配笔记](edf41-bike-re.md)：补上 CarBase 全体 mode 4 重置之后的显式 gun mode 2；实体/测试场/五个呼叫的机枪 holder 改成 Car 原生 BodyRecoil payload，保留 weapon FireRecoil=.3。

## 验证范围

- 真实 EDF.dll 的 firing、pose、pre-world、palette、模式行与原生旧版 408 合同使用离线 binary audit 核实。
- M163 两个 C++ 检查以 MSVC `/W4 /WX /utf-8 /permissive-` 直接编译运行。production fixture 直接包含实际 cpp，覆盖原参/一次调用、HE/AP、远端活动、暂停/同帧、identity 复用、流式模型恢复、SEH 后继续运行、分半轮原生writer之前/之后的真实调用次序、live / death、两hook部分安装失败与普通车型原样。
- Python 模型/武器/生成集合检查、独立碰撞损坏负例和真实 M045 测试场依赖闭环在本机 Root.cpk 上验证。原 408 八 proxy/三轮/双座、真实单枪与五条呼叫合同一起回归。

这些是实际资源、实际原生代码和离线 production routing 的证据。动态驾驶、碰撞手感、视觉效果和双机联机验收尚未执行。
