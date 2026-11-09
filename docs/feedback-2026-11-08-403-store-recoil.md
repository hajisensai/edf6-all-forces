# 2026-11-08 反馈：派 NPC 驾驶电磁炮 403 闪退（测试站 #12）

测试站 #12（tester-n0xe，`EDF6VehicleCrew-0.8.0-build.fb4c60c`，EDF6Coop 2.5.0）原话：

> 「派NPC驾驶的战斗机体不开火 NPC普罗透斯不开火，并且地图上无法选中 试图派NPC驾驶电磁炮时闪退 NPC无法驾驶巴尔加 潜水母舰在水里也没法召唤 巴拉姆模型依然是巴尔加」

本文只处理其中「试图派NPC驾驶电磁炮时闪退」。其余几项在 v0.8.0 已有修复（#146、#309 等，见分诊），不在这里改。

## 证据（回传日志，只读）

- `EDF6Coop.log`：`[21:47:23.305] EXCEPTION C0000005 at EDF+5F9F2B reading 000000000000000C`，`rax=00000283218A43E0 rcx=0 rbp=3`；
  栈 `EDF+5F9F2B ← EDF+5FEE78 ← EDF+630477 ← …`。
- 插件日志：`[21:47:13.055] STORES v=00000282FF5483E0 403_Tank: holder 3 built (00000283218A43E0)`（地址就是崩溃时的 `rax`），
  `[21:47:22.415] NPCAI soldier … boards v=00000282FF5483E0 seat 0: seated / driver … runs the AI pass`：NPC 坐上约 0.9 秒后崩溃。

## 根因（静态逆向，EDF.dll `0x678CCB46`，详见 `docs/recoil-re.md` §3.1）

- VehicleBase slot 5 在 `0x630471` 对每个本帧开过火的挂点调本类 slot 48。403 的 slot 48 `0x5FED60` 只按**挂点下标**
  （`0x62DB10`：在 `veh+0x638` 数组里的位置）分支：下标 0（电磁炮）取 BodyRecoil（`0x5FA150` variant 0）推车体；
  下标 i ≠ 0 一律当成「座位 i 的机枪」，取 AimRecoil（variant 1）加到座位 i 的瞄准抖动上（`0x5F9F10 → 0x5FA8F0`，
  座位 i = `[veh+0x608] + i*0x340`），**不和座位数 `veh+0x618` 比较**。
- 原版 403 有 3 个挂点、3 个座位，挂点 i 就在座位 i。`tools/make_stock_stores.py` 给 403 追加的 AGM-114 是第 4 个挂点（下标 3，座位 0）。
  它一开火，原版代码把它当成座位 3 的机枪：它的记录是 BodyRecoil，取 AimRecoil 得到 null，`0x5F9F2B` 读 `[0+0xC]` 崩溃。
  NPC 驾驶员开 AGM 与玩家开都走这条路。
- **分诊给的修法（改成 AimRecoil）不成立**：换成 AimRecoil 不再读空指针，但抖动会写进座位 3——3 个座位的数组之外的堆内存
  （`tests/stock_store_recoil_native_audit.py` 在真实 EDF.dll 上执行 `0x5FED60` 证实了写越界）。对下标 3，原版代码没有任何安全的记录。
- `make_stock_stores.py` 原注释「slot 48 branches on the seat index」是错的：分支看的是挂点下标，已更正。

## 修法

- `src/storerecoil.cpp`：串接 403 的 slot 48。下标超出座位数的挂点（`store_recoil::Route`）改跑 Vehicle404_Tank 的 slot 48 `0x5FFEE0`——
  只用 VehicleBase 字段，与 403 自己下标 0 分支的步骤相同（`0x62EA10`、阵亡判断、variant 0 记录、车体刚体存活、`0x62D7F0`、`0x5F9FB0`），
  按挂载自己的 BodyRecoil（导弹为 `[0, 0]`）推车体。原版挂点仍走原版代码。安装前核对 403 vtable 与两段指令字节。
- `tools/make_stock_stores.py`：`SLOT48_RECOIL`（每个会造额外挂点的类：下标 0 / 有座位的非 0 下标 / 超出座位的下标各要哪种记录，逐个反汇编核实）
  与 `SLOT48_ROUTED`（插件接管的位置）；`derived_vehicle` 生成前用 `check_store_recoil` 核对每个挂载挂点，原版代码对任何记录都不安全
  且插件没接管时拒绝生成。数据（BodyRecoil 对）本身不变，**已安装的 Mods 文件不用重写，换新 DLL 即可**。

## 验证

- `store_recoil_test`（C++，替身 EDF.dll）：下标 0/1/2 走原版，三座位 403 的下标 3 走车体路径，有第 4 个座位时走原版；字节不符不安装。
- `stock_store_recoil_native`（真实 EDF.dll，需 `EDF6_NATIVE_DLL`）：403 下标 0 推车体、下标 1 只抖座位 1；下标 3 + BodyRecoil 复现 `reading 0xC`，
  下标 3 + AimRecoil 写进座位数组之外；`0x5FFEE0` 对下标 3 按 BodyRecoil 推车体、不碰座位。
- `test_stock_store_recoil.py`：表覆盖 `BUILT_CLASSES`；去掉 `SLOT48_ROUTED` 时 403 的挂载被拒。

## 待实机

- NPC 驾驶 403 打完 3 发电磁炮后切到 AGM-114 开火不再闪退；玩家开 403 发射 AGM-114 同样不闪退（未开游戏）。
