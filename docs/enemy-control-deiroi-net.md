# Deiroi 子部件可靠动作流

只支持已指纹确认的 Steam EDF.dll（TimeDateStamp `0x678CCB46`）。对应实现：`src/enemy_control_deiroi_net.cpp`、显式小端协议 `src/enemy_control_deiroi_net_codec.h`。

此模块把受控根对象整个租期的真实腿炮充能、射击、收炮和停止改由同一 host accepted stream 传递。原生网络模式 1 仅发送 charge 开始；若在各 peer 同时执行语义 SetCharge，迟到的原生消息 5 会在返回 idle 后再次启动 charge。模式 2 的原生消息 6 则直接生成弹丸；不能同时保留两套发射源。

## 原生边界

- 真实创建判据：中央 gun dispatcher 保留 `6CFE2A → 6D05C0(holder, matrix, uint64* seed)` 的完整原生生命周期，并在本次调用的受信 scope 内观察 `84C5A → 1194280` 返回的真实 SceneObject。只有非空真实结果才排队／确认。进入原生前预留一个 outbox 槽，真实工厂返回后立即提交；后置回调或引用清理异常仍保留已生成对象的记录，关闭流程可继续排空。`holder+34` 只证明原生尝试序号推进：即使实际创建返回空，`6D093F` 也会递增，不能把它当作创建成功。
- `holder=DeiroiFootParts_Battery+B90`。原生 `32CFF8 → 32B460` 消息 6 sender 和 `32B790` 的四个生命周期 sender 在 bound root 整租期停用，避免退出租期后迟到的原包与已重放射弹重复。
- 消息 5 的两处原生 SetState、消息 6 的 emit 均对 bound child 门控；原调用者仍自行消费原生 `D20/D21`，模块不改这些计数。
- 远端真实 State1 继续更新充能进度、定位器和 `holder+160` 的枪口循环特效。只在 `32C821`、`32D084`、`32C736` 阻止自主充能结束、收炮和 idle 入场；由可靠生命周期记录唯一调用原生 setter。死亡或 `+639` 禁用清理保留原生路径。
- 远端 shot 调用真正的 `6D05C0` 一次，保留矩阵、原生发射前的真实 shotIndex、初始 seed 和原生参数块 `+90/+94` 的暂存／设置／恢复；不调用会拒绝 remote mode 2 发弹的 `6CFC60`。两个参数字保留兼容命名，不能据名称推断为随机数。

## 两种原版射弹

普通 Bullet_TypeBullet 的真实参数是 `parameter+A0`，实体导弹 TypeEntity 的 getter 是 `parameter+300`，实际构造入参则是 `parameter+280` 的 RawIP，内嵌 BulletIP 位于 `RawIP+80`。每次真实创建精确采集最终 BulletIP 的 `+90/+94/+98/+9C/+1A0/+1A4` 六个字；远端只在同步原生构造期间覆盖相同六项，保留 native fan、速度、伤害与随机结果。中央 provider 同时核对本次 factory 和 descriptor，嵌套导弹构造使用不同 descriptor 时按原生透传。

实体导弹另带 RawIP `+38` 阵营和真实 LockPoint 的完整 actor ID／有序锁点序号。手动炮使用 Core 已确认的准星锁点，恢复自动的炮保留原 AI 锁点。远端整批先解析并强持有所有真实目标 actor 与锁点，让锁点 owner 在构造回调期间仍保持有效；缺失身份为 pending，已知死亡目标按原生弱引用失效处理。构造时使用对齐的本地 `0x260` RawIP 借用副本，保留本地原配置资源、设置真实锁点，由原生 `265EA0/247400` 复制并保留资源；不改 owner 的 `+518` 弱目标，也不析构未拥有资源的借用副本。独立受信 shim 仅让手动／accepted replay 跳过 `6CF95A` 的旧 AI 目标分支，自动炮和未受控调用仍走原生分支。

原生 `6D03C2 → 781630` 不是额外世界生成：它用父 ID 与原参数 `+90/+94` 派生 type 4 子 ID 并登记。双方保留此步骤，嵌套 MissileBullet 也由真正 EntityMissileBody 构造自然生成，不另发一次内部射弹。

## 身份和关闭

授权来自 `NativeActionRole(root,&authority)`，子对象只通过 Source 模块的原配置有序 key 和真实 Scene parent／owner／weak identity 桥解析。脚炮使用 group 0–31；根对象主炮使用 group 32 的 0–63 行，真实 holder 为 root 原生 `+1638` 表中每个 `0x290` 行的 `+100`，不伪造独立 SceneObject。主炮 Fire／Idle 走 `3211C0` 的 direct-callback ABI，原 tag 5 sender 在 bound root 下抑制。wire 不含进程指针。绑定时先发送全体已在运行的真实生命周期基线，确保中途接管时首个 accepted shot 之前已初始化原生 Fire 特效。真实 shotIndex 防止本轮第 N 发被远端当成又一个首发。整个租期保留真实根对象和每个原生子炮的 strong context；死亡状态不被重置，既有已接受弹丸可使用同一工厂上下文完成重放，死亡后不进入新技能状态。

队列固定每 root 4096 条，单批最多 12 条／512 字节；control 42 字节、普通 shot 121 字节、实体导弹 161 字节，头部 8 字节，因此最多装入 4 个普通 shot 或 3 个实体导弹。每条使用独立递增 serial；4096 条 receipt 保存规范 wire 的指纹，重传只可匹配既有内容，冲突或序列缺口拒绝。整批目标预检完成后才开始副作用，pending 不生成任何弹丸。

原生构造、状态 setter 与清理执行期间保留 in-flight 门控；回调重入的关闭、退休或同序列重放不能清除／复用该 Stream。Source 直接状态切换与此门控双向核对。

Quiesce 先停止新射击与自主技能，再使用原生 setter 取消正在运行的枪炮，保留并排空既有 accepted outbox。Restore 在队列未排空时失败并保留身份；强引用按子炮、根对象的顺序释放；释放前预检全体 control 可写，成功减计数后才清 ownership 标记，临时只读可恢复后重试。若最终原生析构已经进入但异常，则保留 uncertain ledger，禁止再次减计数。锁点引用同样区分未消费的可重试释放和已消费的异常。只有外部已确认的本地 epoch／link 重置可调用 Abort 丢弃旧 epoch，普通 peer 失败走停止和排空流程。

## 验证

生产边界夹具覆盖：模式 0／1／2 唯一来源、原生 sender／receiver 门控、生命周期与射弹顺序、部分目标 pending、精确重传和冲突拒绝、合法零 seed、原参数恢复、死亡后的真实上下文、4096 队列溢出、停止排空、引用释放和原生异常。独立 codec 夹具验证变长记录、畸形／截断／乱序输入及事务性输出。`tests/enemy_control_deiroi_net_native_audit.py` 对真实 DLL 的私有生命周期调用点、中央真实工厂边界及实际 RawObject 返回值作只读指令核对。

`enemy_control_deiroi_net_test --dump-trace <output> <30|60|144> tests/data/deiroi-xl-native-trace.json` 使用生产 Shot、真实成功构造 seam 与生产 Capture/Encode 导出 `time_us source_index actual_units hex_payload`。数值配置包含 Root.cpk 原始逻辑 SGO 解码字节的 SHA256 和指令来源，可在没有游戏资源的 CI 中复现；各 Hz 是明确的逻辑更新压力假设，不能当作渲染 FPS 或实测游戏频率。

这些证据不代替游戏内特效表现及双机延迟、丢包和关卡切换验收。
