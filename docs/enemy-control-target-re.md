# 玩家操控敌体的真实目标契约

证据对象为 Steam `EDF.dll`，TimeDateStamp `0x678CCB46`。以下均为 RVA。只读文件反汇编，未加载 DLL、未启动游戏；离线 fixture 也不访问游戏目录。

`src/enemy_control_target.cpp` 将准星世界点与真实 actor 目标分开：普通方向射击可没有 actor；捕获、投掷和导引必须获得当前世界里的真实弱身份。不得给原生 actor 参数传栈上的点或伪对象。

## 目标与锁点

- `0x5E0F20(manager, int observerTeam, functor)` 按有向关系 `relation==2` 遍历登记对象，回调为 functor vtable slot 1，`void(visitThis, realActor)`。`0x5E0FB6` 跳过 `actor+0x2E8` 死亡；`0x5E0FBF` 跳过 `actor+0x380 & 0x80`（NoTarget）。helper 同时检查登记位 `0x40`、真实 team、source/control/target 弱身份。来源是显式传入的原 Human 与受控本体，没有 P1 全局回退。
- 受控本体处于关闭阶段、`+0x314 == -1` 时，以原生保留的 `+0x318` 作观察阵营。`SetTeam 0x54EE70` 在 `0x54EEE0` 写 `+314`，`0x54EEE6..0x54EEEB` 证明传 `-1` 时保留 `+318`。候选自身 team `-1` 仍不可选。
- 原生物理 `ObjectRay` 用 layer 11，其 hit body 交 `0x108260(uint32 bodyId) -> SceneObject*`。该函数经 `0x11AD2E0` 取 body，并以 property key 0 取真实对象。地图射线处理地形/建筑 LOS；对象射线只越过原 Human/受控本体，其他对象遮挡。
- 原生 actor 的 `+0x368` 指向 16 字节 `{LockPoint*, control*}` 数组，`+0x378` 是长度。LockPoint `+8` 是真实 owner actor，`+0x10` 为位置，`+0x29/+0x2A` 是有效/可锁标志，`+0` 是种类。helper 逐项核对数组归属、control 强计数、owner、标志、点的有限性、准星范围和 LOS。
- 准星精确射中大型本体时，不要求其原生 locator 同时落在小锥内，仍取这个真实 actor 的原生 locator。没有 locator 的 actor 可作为真实物理/捕获目标，`Selection.lock` 保持空；不会制造 homing 点。

`0x548EF0` 的完整 ABI 是：

```cpp
void* __fastcall GetLockPointWeak(const void* actor, void* outWeak16, int index);
```

RCX 必须是真实 actor。`0x548EF3` 无条件读 `actor+378`，`0x548F07` 读 `actor+368`；`0x548F28` 增加返回条目 control `+0xC` 的弱计数。空 actor 会崩溃。该 API 返回的是 LockPoint 弱引用，不能用 GameObject 的 `ObjRef::Is()` 校验它。helper 的 `CopyLock` 对已经缓存且重验成功的真实条目执行相同弱持有契约，不重新搜索、不移动准星，也不把 actor 与 LockPoint 混用。

## 原生 current/forced target

- `current` 在 `root+0x518/+0x520`；`forced` 在 `root+0x528/+0x530`。
- `0x364EB0(void* root, const Weak16* consumedByValue)` 是已确认的 current+forced setter：先为两个字段各增加一个新弱计数，再分别释放旧弱；退出时消费输入弱参数一次。调用者必须额外持有一次传入参数。生产 helper 使用这个原生入口，fixture 执行该函数完整 `0xD4` 字节机器码，验证计数平衡。
- `0x387550(void* root, void* realActorOrNull)` 是 current-only setter。它从 actor 的 `+28/+30` 锁 native weak-this，动态转 GameObjectBase，写 current；null 会清 current。该 API 不改 forced。
- `NativeLease::Begin` 分别持有旧 current、旧 forced 与受控本体的弱控制块。`Apply` 可接受空选择；`Restore` 独立恢复两个原始字段，先持新、再发布、再释放旧。过期旧 weak 可以恢复，且不会通过其旧 actor 指针解引用。
- 仅弱身份存储不负责保活对象实体；死亡或地址复用不能把旧目标当作新实体。

**空目标不能靠每帧清字段保证。** `GameObjectBase::Update 0x54BE40` 在 forced 空时仍会调用 slot 14，选原 AI 的目标。core 必须在受控期间把该槽返回值替换成同一帧缓存的合法 actor/null。

slot 14 的准确 ABI 为：

```cpp
void* __fastcall SelectTarget(void* root); // raw actor/null in RAX
```

证据：基类 `0x54C4B0` 在 `0x54C506` 枚举后，`0x54C50C` 把 callback 的 raw actor 放进 RAX 并返回。调用者 `0x54C010` 调 `[vtable+0x70]`，随后 `0x54C01C/+0x54C025` 从结果读 `+30/+28`，锁其弱引用并在 `0x54C06A` 动态转换。没有 out 参数，返回值也不是 Weak16。

## 弱引用清理失败与重试

`TryAssign/TryClear` 是 core 使用的可失败入口。替换或释放前，预检所有现有、新增、retired journal 的 control 页；除常规 `Readable` 外再次用 `VirtualQuery` 核对实际保护，避免同帧缓存把新设只读页误当可写。

- `Drop` 仅在 `InterlockedDecrement` 成功后消费对应记录，写计数失败不会先丢掉引用。
- Selection 保留 `retiredActor/retiredLock`，NativeLease 保留 native current/forced/applied 的 retired token。部分释放只移除已消费 token，尚未释放的仍在 journal。
- `Restore` 在任何本体字段或计数写入前，检查全部 saved/applied/retired/root counter 和两个 native target 字段。暂时只读/不可读并不能证明对象已死亡；失败保留 lease，解除保护后可重试。
- 旧 void `Assign/Clear` 仅委托新入口，不会因失败把记录整体清零。core 应检查 bool 返回值并保留待清理 session。
- fixture 以独立 VirtualAlloc 页配合真实 `VirtualProtect(PAGE_READONLY)`，分别保护 root control、target control、本体字段页，验证失败时 native 字段、引用计数、完整 lease 和消息次数不变，解除保护后一次成功恢复。

## stock target 消息

原生目标通知存在，可以复用，但必须沿正确 serializer 与拥有权门。

原生 `0x54C15D..0x54C1EB` 的条件与 ABI：

1. `0x7748F0(...)` 表示处于原生在线会话；它不读 RCX，不能单独当作 object authority。
2. `!(root+0x128 & 1)` 是本对象非 remote-owned 门；`root+0x591 != 0` 启用该族原生目标通知；本体未死亡。
3. `0x79A460(stream, 0x40)` 构造消息流；`0x12B5790(stream, signedChar(1))` 写消息类型 1。流的 cursor 在 `+0x5F0`；helper 用 `0x610` 字节栈存储。
4. `root vtable slot 37`：`void(root, stream, realActorOrNull)`。基类函数 `0x54CAA0` 用 `0x785050` 将 actor 转原生网络 ID，再经 `0x774B90` 写出；派生族仍调用自己的真实 serializer。
5. `root+0x120` NetworkObject 的 slot 14：`void(networkSubobject, stream, true)` 发送。最后用 `0x760180(stream)` 释放。
6. 接收 slot 38 基类 `0x54C900` 用 `0x784C60` 将原生 ID 解析成 actor，并在 `0x54C9A3` 调 current-only setter `0x387550`。

helper 只在 current 的 `{object,control}` 变化时通知。重复 Apply 不再次通知；guest 即使在线，也被 `root+128 bit0` 拦住。`+591==0` 的族保持原生不发送；自定义受控输入仍以完整 NativeNetId32 将 focus 同步到各 peer，不假设所有族都靠消息 1 完成。

## 跨 peer 与调用顺序

- `Pick/Resolve` 输出 borrowed `Selection`，调用者在游戏线程立即 `Assign/Clear` 持有 actor 与 LockPoint 的弱控制块。枪 hook 只读 core 的缓存，不枚举世界、不做物理射线。
- 离线真实 actor 可能没有网络身份：`Pick` 仍可选择，其 `nativeId[32]` 为零。不会生成本地指针 token。
- `Resolve/Normalize` 网络入口只接受非零 canonical NativeNetId32：20..23 padding 必须为零，比较全部 32 字节，host 重新按真实 team、NoTarget、准星范围和 LOS 解析。
- host 的 `Ray` 由 source/受控本体与 accepted absolute angles 重建，不接收客户端自报 origin。失效目标将 focus ID 清零；空目标不是 lease 失效，也不能因此退出控制。

## 通用 NativeWeaponUnit 的 target 参数来源

补充只读追踪，供武器 hook 核验：`6D51C0 → 6D5734 call 6D2640`；后者从 unit `+2B0/+2B8` 锁 owner root，再于 `6D2F3B/6D2F69` 读 root current `+520/+518`，`6D2FCB` 用 `548EF0(realActor,out,0)` 取得第 0 个 LockPoint。`6D2FDE/6D2FEC` 将它放入 InitParam（栈 `rbp+180`）的 `+60/+68`。

该参数构造前还在 `6D28B6` 以 `32F130(InitParam+30,unit+330)` 复制模板；`32F179..32F198` 证实复制模板 `+30/+38` weak（unit `+360/+368 → InitParam +60/+68`）。没有 current 目标时，后面的覆盖分支跳过，因此仅清 root current 不能独立证明模板 weak 为空。不能把 point 路径验证等同于 generic homing 缓存验证；应在武器参数 hook 处替换为同一缓存的真实 LockPoint/empty，正确释放模板复制已持有的旧 weak。

`6D05C0 → 6D068F call 6CEBB0` 的另一个 weak copy 来自该调用传入的发射/holder 记录 `+0/+8`，`6CEBB0` 直接证明其源，不是 root current target。这不消除上述模板和第 0 locator 的独立问题。

## 验证

- `tests/enemy_control_target_fixture.cpp` 包含生产实现并执行真实 native setter 完整机器码，覆盖 full ID、弱身份复用、真实 LockPoint 所属、source/control 排除、team/NoTarget/登记、LOS、分屏来源、离线真实目标、无 locator 目标、清空/恢复、弱计数、重复 Apply、guest/离线消息门。
- `tests/enemy_control_target_native_audit.py EDF.dll` 只读 PE 并逐指令核对上述 RVA/ABI、完整 setter fixture 字节、sender/receiver 和 virtual slot。
- 没有游戏实测或双机 E2E 结论。
