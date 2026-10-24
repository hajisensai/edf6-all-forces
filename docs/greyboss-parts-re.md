# Humanoid_BigGreyBoss（E516 银之人）的部件事件逆向笔记

Steam 版 EDF.dll（TimeDateStamp `0x678CCB46`），地址都是 RVA。修复在 `src/greyboss_parts.cpp`，离线检查
`tests/greyboss_parts_test.cpp`（ctest `greyboss_parts`）与 `tests/greyboss_parts_native_audit.py`（ctest
`greyboss_parts_native`，需 `EDF6_NATIVE_DLL`）。置信度：确认 = 反汇编 / dump 直接可见；H = 由数据离线推出。

## 1. 2026-10-24 22:30 的崩溃（本机 `EDF6.exe.160076.dmp`）

- 日志 `EDF6VehicleCrew.log`：`22:24:45 loading (build 351a41149e …)`（即 main 351a4114）；`22:29:09` 地图面板
  DEBUGSPAWN 了 `E516_BIGGREYBOSS`（任务进行中途临时加载）；`22:29:12 ENEMYCONTROL begin … profile=Humanoid_BigGreyBoss`；
  `22:30:14` 后 `CRASH … C0000005 at EDF.dll+0x44464A, reading 0x7200000067`。这一局没有维加泰 / 机甲。
- 栈（cdb `.ecxr; kn`）：`EDF+0x44464a` ← `EDF+0x443bbe` ← `0x116C7EC`（CAS 事件分发 `0x116C770`，对每个监听
  `call [vtbl+8]`）← `0x116F6A8` ← 帧更新。没有插件帧。
- `rdi`（进 `0x4444B0` 时的 this）= `0x1629D4596E0`，即日志里 spawned 的那只 E516；它的 vtable 是插件克隆的
  `Humanoid_BigGreyBoss`（`0x17BEC68`，enemy control 的克隆）。
- 查找的名字（帧 0 的局部 wstring，`rsp+0x20`，size 4）：`0x4E2D 0x5FC3 0x7832 0x53F0` = **中心砲台**。
- 崩溃时 `rbx == r15 ==` 列表头（`[root+0x17E8]`）：没找到这个名字，读的是头节点从未构造的值区
  （dump 里头节点 `+0x10` 起是残留的旧数据）。

## 2. 部件表与查找（确认）

- 构造 `0x441040`：`+0x17E0` 是 `unordered_map<wstring, weak_ptr<部件>>`（`0x441119` 建头节点，0x50 字节、只写
  next/prev；`+0x17E8` 头、`+0x17F0` 个数、`+0x17F8` 桶、`+0x1810` 掩码）；SGO `parts` 每行造一个子对象
  （`0x11945E0`）并按名字插入（`0x44270E` → `0x440BD0`）。节点：key wstring 在 `+0x10`（长度 `+0x20`、容量 `+0x28`，
  容量 ≥ 8 时 `+0x10` 是指针），weak_ptr 在 `+0x40` / `+0x48`。
- 模型 CAS 上注册的三个事件（`[root+0xA70]+0x330`，`0x116BE30`）：`parts_action` → `0x443B90`、`パーツ稼働` →
  `0x443BF0`、`パーツ停止` → `0x443C30`。三者都 `0x528A0(event, L"name")` 取名字，调 `0x4444B0(root, name)`，
  结果判空后才写部件（`+0xADD/+0xAE0`、`+0xADC`）。
- `0x4444B0` = `parts.find(name)->second.lock().get()`：FNV-1a 找桶、比长度和每个 UTF-16 单元；**没找到时
  `0x444600: mov rbx,r15`（头）后直接 `0x444641: mov rdx,[rbx+0x48]` 并对它 `lock cmpxchg`**，没有和 end() 比较。
  头节点的值区从未构造：内容是分配时的残留。全新页面上多半是 0（返回 null，看不出问题）；从复用的堆上分配
  （任务中途生成）就是旧数据，`lock` 的控制块地址是垃圾 → 崩。

## 3. 谁会发出“中心砲台”（H，离线读 `E516_BIGGREYBOSS.SGO`）

- `parts`（10 行）：`hand_muzzle_l/r`、`ジェノサイド砲`、`リング小/中/大`、`エフェクト中/大/キック/ダイブ`。**没有中心砲台**
  （包里有 `E516_BIGGREYBOSS_CENTERMUZZLE.SGO`，但这只 E516 不装）。
- `body_model[2]` 的 MAB 事件表：`parts_action name=中心砲台` 在 113.2 帧，挂在 `attack_rollingLazer`（回旋激光）
  和 `attack_generate` 的事件列表里（两个列表共用这条记录）。原版 `ai_script` 也引用 `attack_rollingLazer`；
  enemy control 的 `boss_rolling_laser` 直接播它（`src/enemy_control_catalog.cpp` `kBoss`）。
- 所以这不是插件“造出”的名字：原版数据就会对这只 E516 发一个它没有的部件名。原版任务里没听说崩，推测（H，
  未验证）是任务开局分配的头节点残留多为 0；插件让 E516 在任务中途被造出（DEBUGSPAWN），堆是复用的，残留不再是 0。

## 4. 修复

- `src/greyboss_parts.cpp` 把三个 handler 里对 `0x4444B0` 的 rel32 调用（`0x443BB9`、`0x443C15`、`0x443C55`，
  全 `.text` 仅此三处）改到 `Lookup`：按链表走同一张表、同样的比较；名字存在才调原版 `0x4444B0`，不存在返回
  null（三个调用方本来就处理 null）。链表走不通（不可读、链接或个数不一致）时照旧调原版，不改变行为。
- 第一次起每个缺失名字最多记 8 行 `GREYBOSS … names 中心砲台 … no part acts`。
- 未实机验证：E516 在 enemy control 下放回旋激光 / 中途 DEBUGSPAWN 后放回旋激光不再崩；`中心砲台` 事件原版本来
  就什么都不做（返回 null 的分支），所以观感应与任务开局生成的 E516 一致。
