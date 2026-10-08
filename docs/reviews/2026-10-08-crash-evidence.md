# 2026-10-08 切换挂载后崩溃：原生后坐力参数类型不匹配

## 真实现场

- 本机 `EDF6Coop.log` 在 11:01:13.280 记录 `C0000005 at EDF+5FA056 reading 0xC`。
  Windows Application 1000/1001 与 `EDF6.exe.88740.dmp` 的异常上下文一致。
- 栈为 `5FA056 → 61AF83 → 630477 → 674B70 → 61B1CE`。反解 frame 2 的
  vehicle 是 `00000218CC1012E0`，与最后日志中的玩家 `505_Tank` 一致。
  不是根据最后一条直升机日志猜测肇因。
- `5FA056` 是 `mulss xmm0,[rdi+0xC]`，`rdi=0`。505 回调 `61AEA0` 在
  `61AEF1` 调 `5FA150(holder+0x20, 0)` 取 **BodyRecoil**，随后不检查返回值
  就传给 `5F9FB0`；后者立即使用该参数。601 和 Car 有相同契约。
- 已安装 `Mods/WEAPON/MPACK_B_WEAPON103.SGO` 实读为
  `EDF6VC_V505_TANK_MPACK2_STORES`；新增机枪项确为
  `['EDF6VC_COAX_MG.SGO', ['AimRecoil', [0, 0.0026000000070780516]]]`。
  `tools/make_stock_stores.py` 原先将 403 炮手专用 AimRecoil 无条件移植到
  505/601/Car。原生 getter 对不同类型返回 null，形成与现场完全相同的访问冲突。
- 安装的 VehicleCrew DLL 的 SHA-256 是
  `410313c810f3f5b9a1ff5abd36d7321835d9125551afee65b3e6859408ff833b`，
  PE timestamp `0x6AC6FA72`，日志版本仅为 `0.8.0`；没有可验证的 Git SHA 标识。
  因此不能仅凭版本字符串把用户运行的 DLL 当作审查基线 `f1ed432`。
  本次分析未修改游戏安装、未启动游戏、未注入或附着运行中的进程。

## 修复

删除给 505/601/Car 共 8 种车型强加的机枪：旧生成器仅复制了主炮挂点，
没有机枪模型或独立机枪炮口，不能据此宣称载具具有同轴机枪。保留原版已有的
403/Titan 机枪及其它原版武器。所有新增火箭/导弹使用零强度 BodyRecoil；生成结果
校验会拒绝 AimRecoil，防止再次向要求 BodyRecoil 的原生回调传错类型。

安装器每次都会重建 stockstores 请求（不走模型缓存）；新请求不再含旧机枪。
ledger 会释放此前由 stockstores 写入且仍无人共享的旧 COAX 文件。只换 DLL
不能更新武器请求数据，应通过安装器完成升级。其它工具或用户改写的文件遵循
既有 ledger 保护，不强行覆盖。

## 验证边界

- `tests/stock_store_recoil_native_audit.py`：执行真实 EDF.dll 的完整
  505/601/Car slot 48、参数 getter、强度 getter、BodyRecoil 运算。
  每类旧 AimRecoil 输入均复现 read-at-0xC；零/正 BodyRecoil 均完成，
  并检查线速度及角速度增量，共 **9 项**。世界物理读写与动画服务为测试桩；
  DLL 使用 `DONT_RESOLVE_DLL_REFERENCES` 私有加载，没有执行 DllMain。
- `tests/test_stock_store_recoil.py`：**3 项**回归覆盖全部 21 种载具的请求生成、
  保留原武器、无虚构机枪、拒绝旧 AimRecoil、升级请求及旧资源释放。
- `tools/selftest.py::stock_stores_build` 对真实 Root.cpk 的请求/模型和
  AutoTurret overlay 校验通过。
- 未在游戏内重复用户操作，未执行 projectile flight 或双机验收。
