# 原生弹丸的 accepted outcome 合同

仅对应受支持 Steam EDF.dll 0x678CCB46。`enemy_control_projectile_native` 不安装工厂 hook；每个调用者通过中央 dispatcher 注册实际调用点，并负责自身可靠队列、稳定的炮位/技能序号、原生 post-create 缓存及网络权威。

## 捕获及回放

原生 1194280 ABI 是 `(manager, actualMatrix, localFactory, descriptor)`。非空返回值表示工厂已创建并向原生 GameObjectMgr 登记实际对象。它不能单独证明某个 emitter 的声音、持续光束、回收和网络绑定均已完成。

共享 helper 只接受 `BulletCreateInfo` 最终 vtable 1769028、大小 1E0 的实际合同。1001D0 在构造中先写 base vtable 1762068，再于 1002EE/1002F5 换成最终类型；不能仅按构造前半段判断为另一个 base-only 类型。

每个 codec 明确逐字段序列化 actual matrix、需要时的 persistent muzzle matrix、30 个已证明 u32 数值字段、11 个 u8 字段、u16、3 个尾部数值，以及目标完整 native ID/LockPoint ordinal。禁止直接写出 sample 或 descriptor 的内存块。未定义的 48 padding 及 resource handle 的高 32 位 padding 都不参与资源身份比较。

速度验证按原生 231DC3..231E4B 的消费路径检查：94 必须有限，forward × speed 及有效继承速度的加法不能溢出。50/54/58 的 ordered sum 为 NaN 时，原生跳过该可选向量，不能把合法哨兵统一当成坏数据。

资源、native variant 和所有 weak/control 指针保持本地。原生 32EF10 复制完整模板，100C90 成对清理；捕获时确认实际资源/variant 与调用者的本地模板相容。180/190 的 presence 单独进入 sample，接收端没有这些资源时拒绝准备，不能用空模型/材质假装成功。

## 目标生命周期

指挥标记的活体筛选不适用于已被主机接受的历史弹丸。共享 helper 读取原生网络 registry 的已证明布局：session view 20B2AC0，主对象相对 -98 的 D0/D8 interface/shared control；interface50 指向 registry。

75C910 的 native 查找只比较 ID 首 20 字节。helper 沿相同 bucket 遍历，比较首 20 字节和 24..31 的后缀；20..23 为非语义 padding，wire 必须为零。仍存在的死亡对象允许匹配。精确匹配记录中的 expired weak 按原生失效目标处理，不再解引用已释放的 body。没有记录不能据此判断已销毁，仍由调用者保留 pending。

身份解析只在可信游戏线程读取并复核 session、registry、record 快照，不获取需要事后清理的临时 strong 引用。公开 Actor / LockPoint resolver 仅在最后交付时增加一次最终 strong 引用，交付后没有可能失败的中间 actor 析构。调用者释放时用 consumed 区分尚未消费与已经进入原生释放的异常，不能盲目再减一次计数。

## 失败后的所有权

`NativeProjectileJournal` 是持久本地账本，不能序列化，busy 时不能移动。新调用者应在 stream 中保存它，并使用 `WithNativeProjectileSampleInPlace` 或 `ReplayNativeProjectileSampleInPlace`。创建、copy、析构、strong release 的异常都保留 descriptor、未消费引用、原身份、已返回 created 对象和阶段标记。

尚未开始清理时，写保护等前置条件失败可以在恢复后重试。已经进入原生清理后发生异常则标记 uncertain，禁止盲目重复完整析构。`Empty` 和 `Restore` 必须同时检查并清理这个账本；真实工厂已经成功而清理失败时不能 ACK，也不能再次创建一个弹丸。

旧包装函数同样使用有界持久 arena，调用者必须纳入 `NativeProjectileJournalsEmpty` / `RestoreNativeProjectileJournals`。同一 lease 仍有未清理账本时禁止再找新槽发射。

## 验证边界

`tests/enemy_control_projectile_native_test.cpp` 在隔离内存和真实生产 helper 上检查引用生命周期、真实返回/拒绝/SEH、资源一致性和完整身份。`tests/enemy_control_projectile_native_audit.py` 只读实际 DLL 验证 RVA、指令、字段宽度、owner/team 到真实 GDI 的链路。它们不等于双机实机验收。
