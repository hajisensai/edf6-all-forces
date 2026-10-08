# 进入关卡崩溃：现场转储定位到 MK2 Proteus 动画

现场：`EDF6.exe.105920.dmp`，2026-10-08 17:13:59，进程启动约 39 秒。
配套 Coop 日志在 17:13:55 记录访问冲突。安装的 VehicleCrew DLL SHA-256 为
`A4FC15721885285E45145FAAC6F9AD98A4E6279CD4C881476F01883FBF4F5F6C`，与 #89
最终安装包一致；此次不是把旧 DLL 误认作已修复版本。

## 异常指令与调用契约

- EDF.dll 基址 `0x7FFBA9CE0000`，异常 RVA `0x1160620`：
  `movaps xmm0,xmmword ptr [rbx]`。
- RBX 为 `0x2339AC0E074`，低四位为 4，不满足 MOVAPS 的 16 字节对齐要求。
  这类对齐异常呈现 `C0000005 reading FFFFFFFFFFFFFFFF`；后者不是被访问对象的真实地址。
- 栈为 `1160620 → 116C56C → 116B269 → 116C88C → 1165FEB → 1165BF3 → 116F63B`
  及动画工作线程。故障位于原生动画采样，不是根据临近 HUD/载具日志猜测。
- `1160500(context, outPose, boneDescriptor, frame)` 从 context 的 CANM 与 clip 指针中
  取得当前骨骼的通道。旋转通道类型 2 在 `1160620` 直接读取通道记录开头的 quaternion，
  随后写入对齐的 `outPose+0x10`。该次输出 `0x2346D8228E0` 本身已正确对齐。

## 从寄存器恢复资源、片段与骨骼

虽然本 mini dump 不包含相关 heap 正文，寄存器与栈中的三个独立相对偏移足以匹配
实际安装文件，不能将其描述成直接从 dump 读取出了文件名字符串。

| 现场值 | 推导 | 实际安装 CAS 对应值 |
|---|---|---|
| RDX `0x2339ABA17A0` | CANM 基址 | CANM 文件偏移 `0x1660` |
| RAX `0x2339ABA18F0` | CANM + `7×48` | 旋转通道 7 |
| RBX `0x2339AC0E074` | 通道表相对 CANM `0x6C784`，加第 7 个记录 | 文件通道表 `0x6DDE4` |
| RSI `0x2339AC15FA4` | 当前 clip 行表相对 CANM `0x74804` | `wake_up` 行表 |
| `[rsp+0x30]` `0`，栈地址 `0xC047DCF810` | 函数保存的初始 track 索引 0 | `wake_up[0] = globalSRT` |

唯一符合这些通道/片段位置的已安装文件为
`Mods/OBJECT/EDF6VC_V614_PROTEUS_MK2.CAS`，SHA-256：
`BE6B1B47E6A1863E8BA946A7945E48C141464594AAB49DA64650A54189B09E6D`。
其 `wake_up/globalSRT` 的 translation=6、rotation=7、scale=-1，通道 7 为类型 2，
值 `(0,0,0,1)`。不是新护盾骨骼先出错，而是被搬到未对齐新表的原版骨骼旋转先出错。
动画 context 为 `0x2343B6AF8A0`；未收录 heap，不能进一步声称取得 GameObject 实例或
SGO 对象指针。由 CANM 文件位置推导的 CAS 内存起点为 `0x2339ABA0140`。

`proteus_model.animation` 原来按 4 字节补齐再搬运 48 字节通道记录，MK2 的新表
地址余数为 4，另一 Proteus 生成文件余数为 8。48 字节步长不会改变这个错误余数。
需要修正数据生成契约；不得把 MOVAPS 改成非对齐指令来掩盖格式错误。

## 独立验证及限制

只读实际安装 CAS，私有加载支持版本 EDF.dll（`DONT_RESOLVE_DLL_REFERENCES`，不运行
DllMain），用 `wake_up[0]` 与只采样旋转的 descriptor 调用完整原生 `1160500`：

- 原始安装文件复现 `reading FFFFFFFFFFFFFFFF`。
- 仅在测试内存中把该表移至 16 字节边界，同一函数返回 `(0,0,0,1)`。

这个诊断只验证现场静态旋转通道；生产生成器修复及所有其他通道的验证由 Proteus
资源修复分支完成。未启动/注入/附着游戏，未改安装目录，未声称进入关卡实测通过。
