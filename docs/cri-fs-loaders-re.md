# CRI File System 的 loader 池：测试场一进关就崩（EDF+0x1134CCB）

2026-10-18 实机（`feat/testrange-refresh`，`tests/autopilot/drive.py run range`，cdb 跟踪）。修复：`src/fsbudget.cpp`；
离线核对：`tests/fsbudget_native_audit.py`（ctest `fsbudget_native`，需 `EDF6_NATIVE_DLL`）。地址均为 EDF.dll（TimeDateStamp
`0x678CCB46`）的 RVA。

## 现象

- 装好本分支后进测试场，任务开始约 4 秒（`MISSION phases done` 之后）游戏退出；连续 3 次，`EDF6Coop.log` 每次两个线程同时
  `EXCEPTION C0000005 at EDF+1134CCB reading 0000000000000058`，栈 `EDF+1110F80 ← EDF+110B275 ← EDF+693DD ← … ← EDF+7A052F`（加载任务线程）。
- 与 4.1 外观无关（关掉外观照崩），与巴拉姆无关（最后打开的 SGO 是它，但出事的不是它）。

## 根因（已验证）

1. 崩溃点（静态）：`0x1110E80` 是 `sgm::res::TextureResource`（vtable `0x1A3B0E8`）的加载。档案里名字带 `.lod.` 的贴图成员由分发处
   （`0x110B182..0x110B232`）带上档案自己的路径（`[r13+0xA8]`）调用它；它用 `0x1133C50(mgr, &rec, 路径)` 取这个档案的 HD 流式记录
   （找不到就新建并 `RandomAccessArchive::Open` `0x6AF50`），再交给 `0x1134B10`，后者在 `0x1134CC4..0x1134CCB` 读 `rec+0x58`
   （HD 排序索引，`0x71230` 二分查找）**不判空**。`Open` 失败时 `0x1133C50` 给回空记录 → 读 `0+0x58` → c0000005。
2. `Open` 为什么失败（实机，ModLoader `GameLog=True` + cdb）：游戏日志 `RandomAccessArchive::Open( error : APP:\OBJECT\E503_ARMOR_GRENADE.MRAB )`
   等（文件明明在 Root.cpk 里）；cdb 在 `0x3BCA7`（sgs::fs 打开的返回）看到 `/cri_bind/OBJECT/E503_ARMOR_GRENADE.MRAB` 失败、LastError 2；
   在 CRI 的错误通知 `0x10826B0`（游戏注册的错误回调 `0x75140` 是空函数，所以平时什么都不写）断到：
   `E2008070931:Can not allocate loader handle. (Increase num_loaders of CriFsConfig.)`，此时 `RandomAccessArchive::Open` 正好第 95 次。
3. 池子多大：`0x73D10`（另一条一样的路是 `0x73EE0`）在栈上组 `CriFsConfig`（`rsp+0x20`：thread_model 0、num_binders 1、**num_loaders 0x60**、
   num_group_loaders 0、num_stdio_handles 0x60、num_installers 0、max_binds 0x60、max_files 0x60、max_path 0x100、version -1），
   调用者给了附加结构时各加它的值（本机实测 num_loaders = 130），然后 `criFs_InitializeLibrary 0x1092990(cfg, NULL, 0)`
   （`0x109317C` 校验配置，按配置算 work 大小自己分配）。
4. 谁占 loader：sgs::fs 打开文件读（`0x741C0`，ModLoader 的「Mods folder redirector」挂的就是这个槽）在 `0x742BA` 调
   `criFsLoader_Create 0x1099184`，文件对象活多久 loader 就占多久；带 HD-TEXTURE 的模型档案为了流式读 HD 贴图一直开着。
5. 谁把它用完：本分支新增的 M 地图「摆放」面板在测试场进关时预载整份清单（`DEBUGSPAWN on (the test range's place panel): 40 vehicles
   and 51 enemies preloaded`），加上测试场自己的 34 台载具，常驻的 HD 档案超过了 130 个 loader。哪个档案碰上第一个失败取决于加载顺序
   （三次分别是 E503 的两个附件档案、E601_MARTIAN.MRAB）。

## 修复

`src/fsbudget.cpp`：插件加载时（`EML6_Load`，在应用初始化走到 `0x73D10` 之前；此时文件系统管理器 `EDF+0x20B2880` 还是空）把
`criFs_InitializeLibrary` 的两个调用点 `0x73E8E` / `0x73FCD` 重定向：先把游戏组好的配置的 `num_loaders`（`+0x08`）提到至少 512，
再调原函数（CRI 按新配置分配 work）。签名不符、管理器已建、调用点被别的插件以外的东西占了都不改，日志 `FSBUDGET ...` 写明。

不改游戏的 TextureResource（不给 `0x1134CCB` 补判空）：那只会把崩溃换成贴图缺失、掩盖池子不够；根因是池子按原版内容定的大小装不下插件加的内容。

## 验证（实机，2026-10-18）

- 修复前：r1 / r2 / r3 三次进测试场都在 `EDF+0x1134CCB` 崩（WER `APPCRASH`，Fault offset `0x1134ccb`）。
- 修复后（r6，cdb 同样跟踪）：日志 `FSBUDGET criFs_InitializeLibrary (EDF+0x73e8e): num_loaders 130 -> 512`，`RandomAccessArchive::Open`
  123 次无一失败，CRI 只剩找不到文件的 `W2007060101`（原版也有），无访问违例，任务里停满 60 秒后正常退出；r7–r9 同样正常。
