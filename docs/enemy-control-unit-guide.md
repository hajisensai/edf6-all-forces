# 通用 Unit 最终制导锁点

该模块修复“弹道已经指向玩家准星，但制导弱引用仍指向旧 AI 目标或模板目标”的共享路径。它只改变本次弹丸的 guide weak，不改原生炮口、骨骼、重力/速度、扫射/扇形矩阵、随机数和长期 unit 模板。

## 原生接点与身份

支持版 EDF.dll 的 Unit 射击函数6D2640先将 unit+330 模板复制到本帧描述符 RBP+180。模板 guide unit+360/368 因此被复制到 RBP+1E0/1E8，即描述符+60/+68。原 current-target 分支遇空、过期或无锁点时保留模板；成功时548EF0固定取 ordinal0。5D0和610回调也能更改描述符。

最终接点是6D3301，完整14字节为 LEA R9,[RBP+180] 和 MOV R8,[RSI+2F0]。它位于610回调之后、实际1194280创建之前；整段1244条指令/221条direct控制流无跳进补丁中部，重放后续接6D330F。所有正常新建弹丸都经过它；原生 beam refresh 不创建新弹丸，保留其已有生命周期。

RSI是 plain unit，不能将它当有独立self/control的SceneObject。身份同时核实 unit+2B0/+2B8、本帧已强持有的 [RBP-58]/[-50]、owner真实self+28/+30、正strong count、原profile COL、Core当前private vptr租期与NetworkContext。没有公开原生地址或伪造目标对象，也不扫描堆。

当前明确依赖该hook的10类为 FlyingDyno、AntEX、BeeEX、SpiderEX、Middle、TypeShooter、DangoLarge、Merman、ShellFish和Spinner。矩阵为authored sweep或vertical的unit仍应使用正确guide，不沿用point消费者的排除条件。普通Ant、Squid、Ufo507/515等已有独立实际工厂路径，不因该无关hook失败而被拒绝；未知profile不冒充已登记profile。

## 本地焦点与弱引用所有权

使用 Core WeaponLock/CopyLock复制真正被选中的LockPoint，其内部按实际selected ordinal验证成员，不再硬取ordinal0。它不依赖portable网络ID，离线未登记网络身份的真实锁点也有效。合法空焦点或暂时没有规范锁点时，清空本次guide，不能重新使用旧模板/AI目标。

64项持久journal保存exact owner/control和所持root weak。先将当前shot已经拥有的old weak所有权转入journal，再清空本帧字段，不改变计数；Core新weak直接写入journal.next。原58EB40仅负责把next消费、发布到已空字段，因此旧weak释放不会在不可见的native局部变量里丢失。

旧weak和未发布next使用与原生一致的control+C减计数及vslot1最终删除。只有实际Interlocked成功后才标token已消费；临时只读拒绝保持token，恢复可重试。最终删除callback若部分失败保留uncertain状态，绝不重复减计数。native move在消费前、消费后未发布、已发布后三个异常阶段分别按实际source/destination归属恢复账本；不保留离开本帧后的栈指针。

失败时机器码返回原生null-result路径6D3322，保留原始清理，阻止生成带旧guide的弹丸。Core每Frame对需要该模块的profile动态检查Ready，失败进入fault/quiesce；已接受网络outbox先排空，随后 exact root/control 的 RestoreEnemyControlUnitGuide gate 成功，才能释放Pvp/目标/原生控制器和根引用。正常瞬时busy不让Ready失败，但重入Restore必须拒绝。

## 验证

生产模块 src/enemy_control_unit_guide.cpp 使用独立安装与profile-aware Ready，无late factory安装循环。当前site14字节、整个thunk和原生weak move指纹均逐字节复核，foreign修改不能被当作已就绪。

- tests/enemy_control_unit_guide_test.cpp：严格MSVC 6332项通过。真实执行生成x64，比较全部volatile GPR/XMM、非volatile、flags/DF、栈和完整480字节描述符；除guide外不变。覆盖真本地锁点、合法empty、暂时cache失败、unowned/native透传、只读old control保留与解锁重试、三阶段发布异常、最终weak析构异常不重复和busy重入。
- tests/enemy_control_unit_guide_native_audit.py：支持版DLL374项通过；缺输入返回77。它只读文件，不加载DLL或写游戏目录。
- 独立复核已复跑当时5847项机器码夹具及374项原生审计，未发现P1/P2；后续新增合法empty成功返回场景，最新6332项通过。

这些是离线/native证据，未进行游戏实机或双机联机验收。
