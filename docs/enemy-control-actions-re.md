# 敌体技能：原生受理结果与实际发射边界

本页对应 Steam `EDF.dll` TimeDateStamp `0x678CCB46`。地址均为 RVA。实现入口为 `enemy_control_action_native.*`、`enemy_control_command_actions.*` 与 `enemy_control_gun_factory.*`；具体弹丸、持续效果及整个附属部件的原生事务由各家族 extension 保留。

## 主状态受理

| 对象 | 状态机 / 时钟 / callback | 原生进入入口 | 同步的结果 |
|---|---|---|---|
| Deiroi | EE0 / E20,E24 / E48 | 321300，登记表 ordinal | 实际选中主状态、周期、腿序号、目标完整 NativeID、世界点和原生阶段参数 |
| GiantDangoLarge | 13B0 / 1310,1314 / 1318 | 369F20，固定本地 descriptor | 地面／空中滚动状态、四元数、接触法线、目标和滤波速度、原生门 |
| TimeShip | 10B0 / 1010,1014 / 1018 | 3F00D0，固定本地 descriptor | 十二种合法主状态及其世界点、曲线、门和阶段；实际 Enable 事件独立一次性重放 |

线上只发送语义编号、本地登记表 ordinal、数值和完整对象身份。没有函数地址、CAS 对象、原始指针或未经解释的 Agent 内存进入 wire。状态 0 的原生 dispatch 前读取主对象 `3E8/490` 两个 RNG 游标；同 callback 的新周期仍是新的真实进入，旧周期的阶段校正不会重置 MAB。

受控 guest 的主状态更新门阻止再次选择或触发同一主动作；独立武器、子状态、碰撞和伤害维护仍走各自原生路径。Deiroi 脚炮和主炮实际 Factory 结果使用专用 extension；Dango 的原生 tag8 发弹链保留；TimeShip 原生 tag6/7 仅操作 `1110` 子 Agent，不能当成主舰 Warp。主状态进入或位置同步均不能替代这些实际结果。

Dango 的滚动 GDI 写入 `367E19` 位于独立 collision callback `3679A0..368241`。AI7 `367800..367926` 不包含它；不能按相邻 profile 地址误判函数边界，再据此停掉或恢复整个 AI7。

Deiroi 踩踏 `325350` 的目标门只要求非空。已死亡但仍分配的目标用原生 registry 完整身份查回并强保活，不能用存活目标筛选器使已受理技能永久 pending。已确认过期的空目标无法启动该原生动作，返回 failed，不能把 null 分支的无操作当成 applied。整个批次在进入前预检目标，强引用在 finally 释放；预检拒绝可重试，已进入 native decrement 后异常保留不确定 journal，不再次递减。

## 目录命令受理

`ExecuteAcceptedCommand` 在真实目录定义验证后拦截 Monster、Merman、Boss 和 Carrier 的零按键命令。离线保持原入口；线上未绑定可信角色或网络上下文未知即拒绝。host 保留原忙碌／姿态门，确认真实 setter 的 callback、实际行和参数之后排队；guest 消费原输入，由已认证的 accepted 批调用已核实的合法原生 receiver／状态机入口，避免再被本地冷却拒绝。

- Monster 的 host `4B7AC0` 保留 `8B0 & 4`；guest 通过 `4BE500` 和本地 `4BEC00` descriptor 完整退出／进入。延后的 MAB fire 和真正 Unit 创建另由 Monster projectile extension 发送。
- Merman 使用实际表行 `3A6710`。整租期的旧 tag6 sender／receiver 门防止晚到原生包再次进入；不改 `128` authority 标志来欺骗原生网络。实际枪与毒雾由其 accepted projectile／持续效果 extension 处理。
- Boss 具名动作在 host `475BE0` 接受后，guest 调 `446D90` 完整状态 setter；手部命令依据真实 `444770` getter，而不是 `444C10` 无关的返回 AL，guest 调 `4471A0`。手部动画模式限 `0..0x7FFFFFFE`，排除 native 有符号 abs/mod 产生负索引的 INT_MIN。
- Carrier `4F6FB0` 的 AL 和 `D88==1` 只证明请求受理。原生 hatch tag4、生成器 20104/20105 与其 owner/sequence/SGO/子对象身份负责后续结果，不能将这一个请求位宣称为已孵化。

命令批有逐事件 receipt 和序号，原生进入期间保留 in-flight 门，重入 Restore／Abort 不能清掉正在发布的已受理事件。根对象最后强引用释放前开启 cleanup 门；析构重入不可重复释放，进入析构后异常保留 root identity 和不确定标记。

## 实际 Factory 与持续效果

中央 dispatcher 是进程内 provider 选择器，不扩展 wire。Gun 仅拥有 `6CFE2A -> 6D05C0`；Unit 仅接已核实的 `1194280` caller；每个 provider 必须提供只影响所属 lease 的 Fault。未知对象继续原生调用，多个 provider 同时声称拥有则该次所属 lease 明确失败。Ready 同时验证 callsite 和近跳 thunk 的真实目标，不能仅检查五字节表面。

`6D05C0` 射击尝试后仍可能推进 holder 计数；`84C40` 返回 outWeak 的地址也不等于创建成功。实际 Unit 层只在 `1194280` 返回真实非空注册对象后记录一次结果。家族 provider 还须完成其原生 weak cache、list、callback、网络 ID 或父子链接后，才发布整个已受理事务；中央 Factory 本身不替这些后续步骤作保证。

已被其它可靠 guard 持有的 caller 用 `DispatchNativeUnitFactory` 进入同一 broker。Walking Part 的 `3525A3` 仍由 PvP guard 唯一拥有；whole-part ctor extension 只注册 provider，不再安装竞争 hook。guest 必须在完整 accepted ctor 事务内同步得到真实 bullet 并绑定原生链接，不能先构造空链接再补。

持续 beam 的 `6D31BA` 是原生 vslot12 refresh/keepalive 许可，不是新发弹；`233C92 -> 234420` 才是真正消费矩阵／停止门的 step。provider 以稳定发射周期和 muzzle 身份表达 active/stop；guest 在真实 step 消费之后才 applied。局部 MAB cadence、同一个 seed 或一次函数 void 返回都不证明该结果。

## 退出与证据边界

正常退出先 Quiesce 停止未来触发，保留已受理 outbox 和角色，实际排空并收到 ACK 后再 Restore。只有可信 epoch／连接失效或权威单 lease 故障截止可以显式 Abort 旧事务。恢复或引用释放不确定时保留 journal，不借清队列伪装交付完成。

生产 fixture 使用真实 codec、provider 和生命周期实现，替换的是游戏分配／ABI 边界；只读 native audit 核对支持 DLL 的指令、表和 stock 资产。它们验证源合同和回归，不能冒充游戏执行轨迹、真实碰撞观感或双机验收。本次未安装 DLL、未启动或控制游戏。
