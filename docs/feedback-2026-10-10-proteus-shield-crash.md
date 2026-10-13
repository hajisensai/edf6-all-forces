# 2026-10-10 用户反馈：普罗透斯按 B 展开护盾闪退

> 「普罗透斯按b展开护盾时会闪退」（测试站报告 #6，测试玩家 tester-n0xe，构建 nightly `EDF6VehicleCrew-0.8.0-build.90e8e85`，说明「驾驶普罗透斯时展开护盾会闪退」）

分支 `fix/proteus-shield-crash`（基于 origin/main `90e8e85`）。**修复没有进游戏验证**（按约定不启动游戏、不安装）；下面分清已确认（H）、推断（M）与未实测。

## 1. 证据

### 1.1 回传包里的 minidump 不是这次的崩溃

`crash/EDF6.exe.5468.dmp`（cdb `.ecxr; kb; lmv`）：

| 项 | dump 里 | 玩家这次的安装（versions.txt） |
|---|---|---|
| 时间 | 2026-10-07 22:10:53 | 2026-10-10 14:55 |
| 游戏路径 | `E:\ttt\steamapps\common\EARTH DEFENSE FORCE 6` | `E:\v20250122\...\EARTH.DEFENSE.FORCE.6` |
| EDF6VehicleCrew.dll PE 时间戳 | `0x6AC5DF87`（10-07 13:58） | `0x6AC9C50B`（10-10） |
| 异常 | `c0000409` FAST_FAIL_INVALID_ARG，`ucrtbase!invoke_watson`，栈 `EDF+0x961A7B` 调 `invalid_parameter_noinfo_noreturn` | — |

这是三天前另一套安装、另一版插件的崩溃。`tools/testhub.py` 收集日志时只按「`%LOCALAPPDATA%\CrashDumps` 里最新、且在 DUMP_AGE 内」挑 dump，不核对它是不是这台游戏、这版插件的（旁支问题，本分支未改，见 §5）。这次的三次闪退都没有写出 WER dump。

### 1.2 决定性证据：EDF6Coop 的崩溃日志（H）

插件日志 `EDF6VehicleCrew.log` 有 6 次启动；第 4、5、6 次都在开着普罗透斯（`PROTEUS v=... reworked`）时结束，且全部日志里 **一次 `shield on` 都没有**（调试行每 2 s 打一次，护盾一立就会出现）。日志每 50 ms 才落盘，崩溃瞬间的行会丢，所以插件日志本身看不到崩溃点。

`EDF6Coop.log` 的崩溃处理器记下了第 4、5 次的异常（下一次启动报 `PREVIOUS RUN ended without a shutdown line`）：

```
[14:45:10.542] EXCEPTION E06D7363 thread 26176 at KERNELBASE.dll+5FE4C type=.?AUInvalidVariantException@ut@sgs@@
  r13=000000000000FFFF ...
  #00 KERNELBASE.dll+5FE4C
  #01 VCRUNTIME140.dll+55A9
  #02 EDF+6BF044
  #03 EDF+28FD07
  #04 EDF+28F758
  #05 EDF+11942C3
  #06 EDF+2B9F6D
  #07 EDF+5B5D67
  #08 EDF+1199410
  ...
[14:47:58.416] EXCEPTION E06D7363 ... type=.?AUInvalidVariantException@ut@sgs@@   （同一条栈）
```

- 异常码 `E06D7363` = MSVC C++ 异常；类型 `sgs::ut::InvalidVariantException`；没人捕获，游戏直接没了（不经 SHUTDOWN）。
- 第 6 次（14:53:43 插件日志最后一行）的结束是一次正常退出（`EOSSDK ... Shutting down` → `SHUTDOWN the game exited`，14:53:47–50），没有异常记录；插件日志在退出前 4 s 就停了，原因不明（M：玩家在卡住/崩溃提示后关了游戏？），不作为证据。

### 1.3 栈逐帧对到代码（H，静态，EDF.dll TimeDateStamp `0x678CCB46`，与玩家 versions.txt 的 sha256 `0d5092…0916` 相同）

| 帧 | RVA | 是什么 |
|---|---|---|
| #07 | `0x5B5D67` | DemoIndirectFire 的更新（slot 5 循环 `0x1199410`） |
| #06 | `0x2B9F6D` | IFC 发射：`0x2B9F55 lea r9,[r14+0x40]`（InitParam = IFC+0x40）→ `0x2B9F68 call 0x1194280` |
| #05 | `0x11942C3` | 工厂：`call [rax+0x10]`（弹种工厂的 create） |
| #04 | `0x28F758` | BarrierBullet01 工厂 create → ctor `0x28FB00` |
| #03 | `0x28FD07` | ctor 里 `0x28FCEC lea r8,[rsi+0x1B0]`、`0x28FD02 call 0x6BB890`（用 InitParam+0x1B0 建装置模型） |
| #02 | `0x6BF044` | `0x6BB890` 第一条检查 `0x6BB91E movzx eax,word [r8+0x10]; 0x6BB927 cmp ax,r13w; je 0x6BF033` → `0x6BF03F call 0x12DA768`（`_CxxThrowException`）。`0x6BF033` 只有 `0x6BB927` 这一处跳入。寄存器 `r13=0xFFFF` 正是这里比较的「valueless」值 |

**InitParam+0x1B0 是谁填的**：

- InitParam 的 ctor 把它置空：`0x100321 mov word [rbx+0x1C0],di`（di = 0xFFFF，variant 的 alternative 在值后 +0x10）。
- 武器初始化把自己 SGO 的 `animation_model` 拷进去：`0x68DA8A` 按名字 `animation_model` 查 → `0x68DAD5 lea rcx,[rsi+0x9B0]` → `0x68DAE3 call 0x244CB0`。武器的 InitParam 在 weapon+0x800（CP 在 `0x68D9DB` 的 +0x8E8 = 0x800+0xE8，与 IFC 的 CP 在 InitParam+0xE8 一致），所以 +0x9B0 = InitParam+0x1B0。
- IFC 的配置 `0x2B5F40` 从不写 IFC+0x1F0..+0x201（整段扫描），DemoIndirectFire 的 ctor 也不读自己 SGO 的 `animation_model`。

**根因**：护盾（`proteus_shield.inc BarrierFrame` → `EmcFire(EmcRound::proteusShield)` → `jet_bay.cpp ShellMake`）用一个生成的 DemoIndirectFire（`EDF6VC_PROTEUS_SHIELD.SGO`）发 `BarrierBullet01`。这个弹种原版只由空袭兵武器（EWEAPON196 电磁碉堡）发射，它的 ctor 依赖「发射它的武器的 `animation_model`」（碉堡的装置模型 e_support_barrier01）。IFC 路径下这个 variant 永远是空的，第一发就抛异常 → 每按一次 B（护盾该立起的那一帧之后、IFC 的第一步）必然闪退。前期怀疑的「发射者身份当作空袭兵解引用」「CustomParameter 形状」「BarrierStep 写 +0xC90 的矩阵」都不是：崩在 ctor 里，墙还没造出来，BarrierStep 从未运行（与日志里从没有 `shield on` 一致）。

安装核对：玩家 `Mods/.edf6vc_proteus.json` 写入的 `EDF6VC_PROTEUS_SHIELD.SGO` sha256 `d61b8cc0…b222`，与本机同版本安装的文件逐字节相同；插件预载日志 `proteus shield 1`。EDF6Coop 日志该局为 `MISSION CreatePlayers for 1 player(s)`（单机），与联机无关。

## 2. 修法

数据与契约补齐，不吞异常、不加延迟：

1. **资源**（`tools/make_proteus.py`）：护盾 SGO 带上 EWEAPON196 的 `animation_model` 原样（`[[e_support_barrier01.rab, .mdb], .cas, MAB 790 字节]`，从 DSGO 无损读出）。`check_shield` 断言它在。原版预载 `0x7A3780` 本来就按 SGO 的 `animation_model` 预载模型，所以装置模型随关卡预载。
2. **插件**（`src/ifc_model.h`、`src/jet_bay.cpp`）：`EmcFile` 增加 `model` 标记（只有护盾为 true）。`EmcFire` 在 `ShellMake` 之后、IFC 发射之前（IFC 在同帧稍后的 slot 5 才发），按原版 DemoIndirectFire ctor 读自己 SGO 的同一套做法——SGO 根在 +0x100，用该 alternative 的 find（`0x179EBA8` 表）按名字取下标、get（`0x179EAF0` 表）把成员写成节点引用（alternative 2 = {文档, 下标}，`0x2390D0`）——直接写进 IFC 的 InitParam+0x1B0（IFC+0x1F0，此时必为空）。这就是武器初始化对武器弹做的事。
3. **拿不到就不发**：SGO 根不可读、没有 `animation_model`（旧安装）、槽位已有值、或拷贝后仍为空，都记日志、在发射前删掉这个 DemoIndirectFire、本关停用护盾（`emcReady=false`；HUD 显示护盾不可用），不再把它交给会抛异常的构造。
4. **签名**：`kIfcModelSigs` 6 条（`0x28FCEC`、`0x28FD02`、`0x100321`、`0x5B56F9`、`0x5B5723`、`0x5B5767`），不符则护盾这一项不预载。

升级：生成器源码变了，安装器会重写 `EDF6VC_PROTEUS_SHIELD.SGO`；只换 DLL 不跑安装器时属于「没有 animation_model」那一支：不闪退，只是没有护盾，日志提示运行安装器。

## 3. 测试（全部离线）

- `ifc_model`（`tests/ifc_model_test.cpp`，13 项）：假映像里放和原版节点引用同样行为的 find/get visitor；成功时 IFC+0x1F0 变成 {本弹 SGO 的文档, animation_model 下标}；没有成员 / 槽位已占 / 根为空 / alternative 越界都返回失败且不调用 visitor 或不写槽。
- `ifc_model_guard`（`tests/ifc_model_guard.py`）：只有护盾是模型弹；`EmcFire` 在 `ShellMake` 之后调 `EmcCarryModel` 且失败不返回弹；失败分支删弹并停用；模型弹的预载要求签名通过；生成器写的成员名就是插件读的。
- `ifc_model_native`（`tests/ifc_model_native_audit.py`，只读真实 EDF.dll，无 DLL 时跳过 77）：§1.3 整条链逐条反汇编核对（含 `0x6BF03F` 调用的返回地址正是崩溃栈的 `0x6BF044`、`0x6BF033` 只有一处跳入、IFC 配置不写该槽、visitor 表前三项是代码第四项不是、alternative 2 无析构），以及插件 6 条签名与 DLL 逐字节一致。
- `proteus_assets` 新增：生成的 SGO 带装置模型；源武器没有 `animation_model` 时生成器拒绝；去掉它的 SGO `check_shield` 不通过；真实 Root.cpk 生成结果与 EWEAPON196 的 rab/mdb/cas/MAB 逐字节一致。
- 构建 `build.cmd` 退出码 0；`ctest` 209 项全部通过（30 项因缺游戏 / 素材跳过；本机有 EDF.dll，`ifc_model_native` 实际运行并通过）；`tools/selftest.py` 147/147。
- 变异实测（均已还原）：删掉 `EmcFire` 里的调用 → `ifc_model_guard` 红；生成器不写 `animation_model` → `proteus_assets` 3 失败 1 错误、`ifc_model_guard` 红；`CarryModel` 不调 get → `ifc_model` 9/13；`kIfcModel` 写成 0x1B0（漏了 IFC+0x40）→ `ifc_model` 12/13。

## 4. 未实测（必须进游戏确认）

- 带上模型后 `BarrierBullet01` 的 ctor 是否完整走完、墙是否立起（`shield on`、`barrier` 指针非 0）。ctor 后段（碰撞网格、`0x2E8510` 等）与空袭兵发射时的差别只在 InitParam+0x1B0 这一格（ctor 只在 `0x28FCEC` 读 InitParam 本身，其余走与其它弹种共用的基类 `0x22E9C0`）——这是静态推断（M）。
- 装置模型（电磁碉堡的发生器）会出现在墙的矩阵位置，即机体脚下中心；是否被机体挡住、是否随移动跟随（M：slot 5 每帧用 +0x60 定位，BarrierStep 写 +0xC90）。
- 节点引用 {文档, 下标} 指向 DemoIndirectFire 自己 SGO 的文档；这个 DemoIndirectFire 发完一发即删除。ctor 当场用它建模型；墙之后是否还会读这一格未逆清（M：原版空袭兵死亡 / 换武器后墙仍立着，同样的引用也会悬空而原版不崩，推测墙之后不读它）。
- 第 6 次退出（正常 SHUTDOWN、插件日志早停 4 s）的原因。
- docs/proteus-re.md §9 原列的外观、跟随碰撞、挡弹、锚定、联机两台机器的墙，仍全部未进游戏确认。

## 5. 旁支发现（未改）

- `tools/testhub.py` 回传日志时挑的 dump 可能属于别的安装 / 别的版本（本次即是）：应只取 dump 内模块路径与本次游戏目录一致、插件时间戳与已装 DLL 一致的，或在 versions.txt 里注明 dump 的时间与来源。
- 玩家要拿到可用的崩溃 dump：EDF6Coop.ini `CrashDump=1`，或 WER `HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\EDF6.exe`（`DumpType=2` 完整内存）。这次靠的是 EDF6Coop 的崩溃日志。
- 日志里多次出现 `PROTEUS ...: deploying` 后 26–40 ms `stock again (no player aboard)`，以及两席模式下「player in seat 3」（座位 2、3 已关闭）。与本次闪退无关，未查。
