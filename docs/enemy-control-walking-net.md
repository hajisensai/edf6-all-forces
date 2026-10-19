# Walking 原生技能的 accepted outcome

该实现是游戏线程上的受控原生边界，支持已准入的 Walking 六子类。Large 使用实际 `gun_control` 原始序号；Middle/Shooter 使用真实 `root+1A60/+1A68` GunBattery，固定 key 0。火箭拳和抓钩的完整部件创建由 `enemy_control_walking_parts.*` 独立负责，保留包装 GOB 与真实弹丸的 `5D0/5D8` 关联。

## 原生调用与线格式

本模块只通过中央 `enemy_control_gun_factory` 注册 provider：原始 `6D331D -> 1194280`、已有光束刷新 `6D31BA`、真实连续光束状态消费 `233C92 -> 234420`。它不重复 patch 通用 Factory、Refresh 或 Step。非受控对象保留原生行为；受控对象依据 Core 保留的不可变 network context 与真实 native action role 处理，未知上下文不授予离线权限。

主技能消息包含实际 AttackSetting 行、entry 前两条 RNG、部件绑定字节 `root+1940` 和 canonical point。GB2 消息包含实际非空原生 Factory 提交前的 typed BulletCreateInfo、真实两个矩阵、原始 muzzle ordinal、资源 mask，以及实际完整 actor ID 与 LockPoint index。所有字段逐项编码，不传 native 指针、padding 或 std::function。v2 单 shot 消息由生产 Capture/Encode 实际编码为 **372 字节**，含 8 字节 batch header。

host 使用 Core 缓存的真实选中 LockPoint（合法空目标也会清旧弱目标），保持真实原生原始 Factory 及其 caller 后续逻辑。guest 的原生预测触发不自行生成物理弹丸；收到 accepted 事件后，从本地等价资源模板通过完整 native copy constructor 重建描述符，再经真实原始 Factory 注册对象。只有实际非空且未删除的原生结果才可继续确认。

## 接管准备

Core 在联网 Begin 的 pending 阶段先调用本模块 Before。该边界保留完整物理炮位表，包括被技能目录隐藏的 Hector 盾槽，先核对全部旧缓存身份并取得活体引用，再一次性调用原生 Stop `6D3EC0`，并按 `6D4040` 的完整语义对已锁定的真实快照逐个进入原生 Delete `118A1B0`。Large 同时取消原 AI 的 `gun+38` 待射倒计时。Middle/Shooter 使用各自真实单 battery；其它子类不套用这些布局。

Stop 不会删除持续束，Delete 也不会清 weak pair，因此需核对实际删除标记，再通过原生 `58EB40` 清 `muzzle+B0/B8`。过期 weak 不解引用旧地址；已删除对象只清缓存，不重复 Delete。每次原生回调后重核缓存身份，原回调换入的其它对象不会被动态遍历误删。这个准备事务不把旧 AI 光束伪装成玩家 accepted shot，不触及既有普通飞行弹丸。原生清理失败会保留引用和 uncertain 账本；旧缓存仍被中央 provider 识别并冻结，避免失败后继续原 AI 的光束伤害。准备每个 battery 只执行一次，不会误删此后本 lease 的新束。

## 创建后生命周期

guest 重放继续完整原生后处理：真实 RTTI 转为 BulletBase；连续光束加入 `muzzle+B0/B8` 的弱缓存；所有弹丸加入 `battery+290/+298` 的原生 32 字节弱引用链表。原版 Large/Middle/Shooter 没有 `650/658` 后处理回调；运行时出现未支持回调会在生成物理对象之前拒绝。Shooter 原有 `610` 对矩阵及 scalar `94` 的实际改动直接体现在 host 样本中。

连续光束使用已接受 shot serial 与 muzzle 绑定 active cycle。真实刷新必须在非过期对象上进入原生 keepalive；guest 保持该 cycle 的 host 矩阵，Step 时消费该矩阵并运行真正原生状态函数。host 的正常过期刷新保持原生无操作，结束 Step 仍保留原生 FX/声音清理；不会把调用一个 void 方法当成物理成功。typed Stop 运行真正原生 Delete 并验证删除标记。guest 若提前失去 accepted 活体，则进入可见 fault 并保留账本。

## 事务与关闭

物理创建前检查队列空间与整个样本依赖。接收先验证完整包，再逐个提交；已提交前缀记入 receipt，后续 pending 重试不会重复生成弹丸。队列满时拒绝未来创建，已接受事件保留供 drain。native copy/create/cleanup、结果 pin、链表节点及光束删除的异常均保存在持久 journal 中，不因返回失败而遗失，也不盲目重试已可能消费的释放。

普通退出先 quiesce 未来触发，host 发送并排空已接受 Stop/shot。guest 即使先收到 Drain，也保留已接受 cycle、host 矩阵和原生 keepalive，直到对应 host Stop 到达；可信 Abort 才本地强制删除。Empty 包括未 Capture、pending 主动作、未完成描述符/节点和光束账本。只有这些实际完成后才能释放 root、gun、battery、beam 的引用。Abort 仅由上层可信旧 epoch/link loss 使用。

Walking 两个扩展额外保持因果：Parts 仍有未排空组时新明确主动作和炮位命令背压；Parts Capture 在主 entry 尚未进入可靠 journal 时返回成功空批，保留完整组。由全局可靠 actionSequence 继续维护跨包顺序，避免较早部件的 RNG 回写覆盖较新 entry。

## 验证边界

- `tests/enemy_control_walking_net_test.cpp` 直接包含生产实现，所有原生边界只使用隔离 mock，覆盖真实生产编码、创建/缓存/链表、光束、重发、坏包、pending、4096 队列压力及恢复。
- `tools/enemy_control_walking_net_trace_assets.py` 只读提取实际 stock/ported SGO 的 cadence。fixture 可使用 `--dump-trace` 输出生产 Capture/Encode JSONL；对 VM 表达式保留元数据，不假定其难度相关数值。
- `tests/enemy_control_walking_net_native_audit.py` 只读核对实际支持 DLL 与 25 个 Large/Hector、11 个 Middle/Shooter 的结构/原生边界。

这些离线检查不是游戏实机或双机验收；transport 的窗口、丢包与 normal drain 另由真实生产编码 trace 回放验证。
