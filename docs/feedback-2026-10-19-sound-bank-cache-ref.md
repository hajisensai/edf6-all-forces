# 进入测试场时 EDF4.1 音效查找崩溃（2026-10-19）

用户在启动时序修复版 `fa092a8` 上反馈：「进测试场闪退了」。该次启动已完成网络桥与全部插件加载；18:38:18 创建玩家时另发生 `C0000005 at EDF.dll+0x1042536`。匹配 DLL 的 PDB 将栈定位到 `edf41sound::LookupCue` 的 imported 分支及 `LookupHook`。

真实 dump 中传入声音名为 `weapon_Ranger_RL_plasmaLauncherA3`。崩溃在 CRI 的 UTF cue 表查名过程中读取了无效表头。磁盘 EDF41_WEAPON_SE.ACB 与转换器输出逐字节一致，1469 个 cue 与嵌套 UTF 表可解析。

根因有两处原生 ABI 错误：

- `0x7AA440` 输出的是 `{CacheNode*, CacheGlobal*}`，真正 Bank 内嵌在 `node+0x48`。原代码将 cache node 当作 Bank 传给 `0x7A9F80`，并错误地将 node 填进返回的 CueRef.bank。
- `0x7A9F80` 接收窄字符名称，原代码直接传了 UTF-16 指针。原生上层 `0x7AEAE0` 本来会完成宽字符转换。

修复保留缓存引用对至游戏 shutdown，并复用原生 `Lookup(system,out,0x41,wideCue)` 查询已注册的私有类型 Bank。原生接口负责 set 遍历、字符转换与正确的 `{Bank*,cueIndex}` 返回。原版音效优先、导入库其次、有限时长驻留兜底最后的顺序保持不变。

voice lookup/play 与缓存释放接口接收的本来就是引用对，继续原样转发；不能对这些路径统一加 `0x48`。`0x7AA440` 的 bool 返回也由全部成功/失败分支核实，不改成指针返回。

新增回归直接包含生产 sound hook、使用 dump 的真实声音名与私有原生端点。旧代码已编译后运行失败于错误 direct-Find 路径；修复后的 76 项检查已通过，覆盖真实 Bank 身份、查询顺序、非 SE 类型透传以及缓存引用只释放一次；另锁定真实结构布局与九处调用点。实际游戏加载结果另记交付凭据。
