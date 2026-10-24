# 敌方玩家操控：完整技能与原生动作契约

2026-10-19；Steam EDF.dll TimeDateStamp `0x678CCB46`。地址均为RVA。本文区分真实游戏能力、共用动画资产以及调试输入；完整目录不以CAS状态数量代替技能数量。

## 技能目录与执行

`src/enemy_control_catalog.cpp` 的 `Abilities` 从已准入NativeLease及对象实际组件取技能。`ExecuteAbility` 只接受本对象目录内的完整定义，拒绝传入任意状态字符串、越界索引或其它家族命令。对象身份始终核weak控制块，兼容runtime的逐对象克隆虚表。 Deiroi与Humanoid的已验证零武器数和枚举失败分别返回；未知活部件、失配owner／weak或已丢失的选枪钩子不能被静默省略后仍称完整支持。

原生pad记录为button base `0x70`、stride `0x18`，held在+0，pressed在+1。元数据分别携带两者；玩家按下时保留原生按住／边沿语义。额外命令只在去重后的按下边沿调用，调用前给原生 `+0x3E8`、`+0x490` 两条随机游标设置本次网络种子；被原生能力／模式门拒绝的动作不消耗新种子。

## 按实际表覆盖全部技能

- **Monster501 / Monster501_King / Monster504**：实际 `attack_setting` 分别18／15／38项（Monster504两种任务SGO均38项）。对象 `+0x14A8` 为stride `0x38` 表、`+0x14B8` 数量、条目首项是动作名。菜单逐项列出；`4B7AC0(this,index)` 保留 `+0x8B0 & 4` 门，通过原生状态 `4BEC00` 使用该表的名字和速率，正常处理MAB攻击事件、完成和取消。未把只有轮选功能的pad10／11或强制倒地／受伤的pad6／7当额外技能。
- **Merman**：三种原版SGO均10项。`+0x16F8`、`+0x1708`、stride `0x88`；条目+8为可用模式，+0x1C为已解析CAS状态。站立模式4项、爬行5项、水中1项。目录使用稳定的绝对索引；原生pad0..7只是当前模式内的相对索引。调用 `3A6710(this,index,true)` 前保留 `+0x17F4 & 1 == 0`、当前 `+0x17F0` 与条目模式相等、有效CAS状态门。整个目录不随运动模式变换而改变身份。
- **FlyingDyno / Radon**：`+0x13F8`为stride `0xE0` 的原生Action表，原生pad通过 `3ACAD0` 选条目并走普通／俯冲完整状态。普通及其它剧情版10项，M051只有7项，目录按实际count裁剪。原生第二形态按钮调用 `3B9340(this,1)`，连同模型组、特效和句柄切换保留；MAB回调 `3ADC60` 读真实shoot_no驱动原生枪组。

## Humanoid与E516 Boss

Humanoid的 `slot4=470CA0` 只推进布娃娃／模型；真实pad读取在base update `470D10`，由Basic、Martian、Venus及Boss各自slot5调用。因此玩家输入借用必须覆盖这些对象的完整slot5，不能只包slot4。

原生按钮4的举枪准备与按钮5的trigger分开；菜单齐射提供这两个正常条件。`enemy_control_humanoid.cpp`另外按真实`+1120 / +1130`武器向量为四臂装备逐槽提供独立开火：只在本线程当前受控身体RunInput期间，筛选其自己枪械的v17调用，保留原数组和网络槽序；作用域外、其它身体以及原生后续武器状态完全不拦。普通／重武器／软体枪／气泡弹幕均调用原始v17，装填与弹药不绕过。

盾的v17=45A030立即false，没有主动射击；其v14=459ED0在原生SetUsable4A4C80的5E2可用性变化时展开／收回，原生自动防御保留，不给盾伪造一发攻击。掩体、蹲伏、跳跃、翻滚保持原生有效状态／骨骼／装备门。

E516的独有八招直接取自原版嵌入 `ai_script` BVM，不靠共用CAS猜测：

| 技能 | 原AI实际调用 | 插件执行 |
|---|---|---|
| 手部光束／追踪光束／光枪 | Native1/230，参数4／5／6 | 完整OnMessage `444C10`，`ObjectAction=0x1000000D` |
| 回旋激光／灭绝光炮／陨石 | Native1/101，具名动作+0.1 | 原生 `475BE0` |
| 飞踢／俯冲 | Native1/102，具名动作+0.1+速率 | 原生 `475BE0` |

命名动作描述符是`wstring(0x20), priority@0x20, blend@0x24, speed@0x28, bool@0x2C`。`475BE0`进入完整Humanoid状态机，虚槽61在E516上是`447A50`；该状态正常处理MAB及取消，退出还调用`6C8760`和`444060`清理特效和部件。插件没有仅调用CAS播放器后自行伪造伤害。原生Busy／姿态／受伤门仍会拒绝当前不能使用的招式。

## Walking实际攻击表与独立臂炮

原生共同按钮只分主攻击、武器攻击与跳跃，不等于每类只有一招。`AttackSetting[1]`的实际条目由原生`345310`在进入攻击状态时选择，`+17D8`保留当前行；目录追加每个实际条目，并保留原按钮的上下文连段。原生主CAS必须已完整初始化，每个条目的开始／结束状态都经过真实查找；Hector继承但实际CAS不存在的armStomp不会进入目录，合法零近战也不显示幻影通用主击。过滤后保留原SGO索引，不重排其它技能的身份。指定招式只在原生完整进入过程的`34544B -> D7480(animation_speed)`边界选择已验证的本体SGO行，动画、MAB、结束与退出继续由原状态机管理。它不把CAS名字直接交给播放器。

Bommer的主击实际是`suicideBomb_banzai/try`自爆；Large_PUNCH的主击含`attack_shoot_both/lr/rl`火箭拳，不能统一标成近战。Grapple的抓取、拉拽、释放保留原上下文门；明确表技能只在合法的原生主攻击上下文可用。Large实际`gun_control`另外逐槽提供独立开火，原齐射保留。

## 昆虫与当前动作上下文

基类解码四颗按钮不意味着所有派生都有四个技能。普通蚁与EX只消费4／5：无捕获物时分别attack喷酸、capture抓取；已捕获时分别投掷和咬／甩连段。蜂类4起飞、5地面或空中针刺；蜘蛛4跳跃、5吐丝；团子4进入翻滚、6滚动攻击。蜻蜓／飞龙4起飞，5按地面／飞行／已抓取上下文走capture、glide／fly_attack或投掷，6仅捕获后咬甩。Dragon按钮7实际是fly_damage_large强制受伤，目录排除。

4.1大龙的现有builder把原ground_attack作为native capture别名；EDF41 dragon维护器按该真实状态窗口发原生火焰，因此保留capture完整路径即可覆盖地面喷火，不另加裸CAS调用。

## 小型独立家族

- AlienTrailer按钮0完整切换高功率护盾状态，含屏障碰撞与特效；不是直接改屏障开关。
- Carrier的真实ObjectAction只证实64：保持开舱并使用任务既有生成器继续投送，幂等。没有臆造65关门，也不制造未配置的敌人波次。
- ShellFish只列开壳、闭壳、炮口射击，排除强制受伤反应。
- SpinnerUfo保留吸附／脱离、真实射击；未把仅开壳旋转的调试姿态当攻击。
- SquidSmall只把button5列为射击；6／7是上下移动，归运动轴。
- GiantDangoLarge保留滚动运动技能和真实针弹组件发射。
- Nephila只证实button4的attack_far吐丝。button5写入的F25未被类消费；CAS中的attack_near未被初始化或AI绑定，不能因存在动画就裸播。
- TimeShip以独立原生命令替换本地摄像机pad射线。Warp／Warp2直接走原AS调用的完整状态，复制当前完整world矩阵，采用原pad的600帧；局部形变不代表世界坐标传送。锚投放使用同一受控输入的准星world point，走原子对象目标／释放链。两种撤离保留真实移除生命周期；胶囊准备／投放与舱门开关按真实链接与配置处理，不把空胶囊冒称召兵。详细合同见`enemy-control-timeship.md`。

## Deiroi独立腿炮

根配置的原腿／部件顺序给每门炮稳定身份`(leg<<16)|part`，断肢后其它炮不按现存数量重新编号。目录为每门炮分别提供准星射击和恢复自动，通过独立`enemy_control_deiroi`适配而非根的两颗踩踏／主炮按钮。根本体仍使用原生移动、踩踏和主炮路径。

## UFO固定变种与同体模式

详见`enemy-control-ufo.md`。507的普通／HI是Beam，ACE／CHARGE是Laser，BIG是Plasma轰炸；`bullet_pattern`在构造时三选一，BIG的big_bomber参数覆盖同一弹丸块，`ai_attack_pattern`决定AI攻击航线，均不是同体第二武器。515的唯一bullet_object驱动全部battery_config，原版双炮以及4.1 UFO301／ACE六炮口均按实际count走完整开舱／发射／关舱循环。主击已覆盖，不能因CAS有开闭状态或配置有多炮口就伪造“切换武器”技能。

## 状态读取

`ReadAbilityState`只刷新当前能力状态，不重建目录、不修改ID／名字／网络hash。Monster读取原生当前攻击索引与动作门，Merman读取真实形态和忙碌位，Boss读取原生Busy与姿态门，独立Humanoid枪读取真实v18忙碌谓词和5E2装备可用性，Deiroi读取实际部件身份／手控与原生charge/fire状态。玩家按住按钮本身不构成“正在攻击”的证据。无法得知剩余时间时不显示伪造的冷却秒数。

## 联机接受结果

四族额外命令（Monster、Merman、E516、Carrier）在目录准入前注册可靠accepted扩展，定义白名单通过后优先交给该扩展；客端消费意图或拒绝时均不得落回离线路径。相同按键和随机种子不足以证明同一招式已被各端执行。Walking明确行、独立炮和TimeShip胶囊具有原输入包无法表达的数据，必须由可信host接受后发送实际稳定条目／对象身份及真实原生结果，guest成功一次后才ACK。原生root动作状态包也不自动覆盖独立脚炮、胶囊创建和延迟投放。这些接口的离线测试与只读原生证据不能替代生产桥接和双机验收。

## 验证

- `tests/enemy_control_humanoid_test.cpp`直接运行生产v17筛选包装器，覆盖四枪独立／齐射、被动盾、其它owner、作用域外不拦、失败作用域恢复及断臂原槽不重编号。
- `tests/enemy_control_catalog_test.cpp`直接运行生产目录与命令边界，覆盖完整表数量、King裁剪、38招不截断、形态稳定ID、模式／Busy／签名拒绝、双RNG种子和拒绝时不消耗种子。
- `tests/enemy_control_catalog_native_audit.py`只读原版SGO、E516原AI BVM和本机EDF.dll，对照真实原生调用及状态完成／取消链。它不加载或执行游戏DLL。
- 上述证据不等于实机操控、伤害观感或双机联机验收。
