# EDF4.1 外观（新增外观，不顶替原版）

用户原话（2026-10-17）：「外观能选4.1的角色外观」；随后纠正方向：「能不能想更好的办法统一弄之类的」「顶替原版外观 1 谁让你顶替的，加一个外观啊」。

所以本功能 = **每个兵种多一个「EDF4.1」外观**（原版 4 个外观 + 任务默认之外的第 6 个选项），原版文件一个不替换；转换是**一套通用管线**，四个兵种只各给一张例外表。

EDF.dll TimeDateStamp `0x678CCB46`，地址为 RVA。置信度：H = 反汇编 / 数据直接证实，M = 推断，L = 猜测。**全部静态分析与离线渲染，未实机。**

## 0. 结论与落地

| 部分 | 文件 | 状态 |
|---|---|---|
| 通用重定向管线 | `pylib/outfit41.py` | 四个兵种都跑通，离线渲染无明显扭曲（见 §4） |
| 生成器（开发机，需 4.1 + EDF6） | `tools/make_edf41_outfits.py` → `edf41port/outfits.json` + `edf41port/outfits/` | 已生成并提交，19.2 MB（贴图按 4.1 原样，rabpool 去重） |
| 安装（玩家不需要 4.1） | `tools/edf41_outfits.py`，`tools/installer.py` 默认安装，归属账本 owner `edf41outfit` | 只写新文件名，卸载一并删除 |
| 插件：选择与模型路径 | `src/outfit41.cpp`、`src/outfit41_logic.h`、`src/outfit41_signatures.inc`（`tools/outfit41_sigs.py` 生成） | 编译通过、签名审计通过，**未实机** |
| 测试 | `tests/outfit41_test.cpp`（ctest `outfit41`）、`tests/outfit41_native_audit.py`（ctest `outfit41_native`）、`tools/selftest.py` 的 `edf41_outfits_*` 四项 | 全绿 |

新文件（每兵种三个）：`OBJECT/EDF41_P_<RANGER|WINGDIVER|AIRRAIDER|FENCER>.SGO`、同名 `.MRAB`、`MENUOBJECT/EDF41_P_<…>.SGO`。

## 1. 为什么不能加第 5 个原版外观槽，以及插件怎么绕开

- 外观编号是存档字段 `GS + 0x6E94 + player*0x3E60`，取值 -1..3（-1 = 任务默认）。H（`docs/loadout-re.md`）
- `0xE34E0(GS+0x130, class, outfit)` = `CONFIG.SGO SoldierInit[class][2][outfit]`，`0xE3420` = `SoldierInit[class][5][1][outfit]`：**两者都不查边界**（只是 SGO 表的偏移运算，§5 反汇编）。H
- 颜色表按 `(outfit + class*4)` 取槽，每兵种 4 槽（`docs/room-3d-preview-re.md` §1）。H
- 所以存档里写 4 = 越界：卸载插件后读档、或联机发给没装 MOD 的人，都会读到 SoldierInit 之外的内存。**存档始终只存原版 -1..3。**

做法：
1. 选择由插件自己保存：`Mods/Plugins/EDF6VehicleCrew.outfit41.ini`，`[Outfit41] Ranger= WingDiver= AirRaider= Fencer=`（1 = 开），按兵种。原子替换写入（`loadout_file.cpp ReplaceFileAtomic`）。
2. 插件在游戏取「本机第一名玩家的模型路径」时换成我们的新 SGO（§2）。
3. HQ 的外观弹出菜单多一项「EDF4.1」（§3）。选它：本兵种开，存档外观值**不变**（保持你之前选的原版外观）。选任一原版项：本兵种关。
4. **颜色**：沿用当前存档里所选原版外观的颜色槽（`GS (outfit+class*4)` 那一组）。4.1 的材质（`snd_Chara_2ColorChange` / `_Alpha`）带 `change_color0/1` 参数，EDF6 的 `soldier_color` / 菜单 `change_color` 按参数名上色，所以 HQ 改色对 4.1 外观有效；4.1 贴图的 cco 遮罩决定哪些部位变色，效果与原版外观不同。M（参数一致 H；实际观感未实机）

## 2. 模型路径钩子（逆向）

| 调用点 | 调用 | 上下文 | 置信 |
|---|---|---|---|
| `0x5A3FFC` | `0xE34E0(tables, desc+8 class, desc+0xC outfit)` | 建人 `0x5A3F90(desc, …)` 里唯一一次取战斗模型路径；`desc+0 == -1` 的人被标成远端（`0x5A4087..0x5A40B5`：对象 `+0x128` 位 0 远端 / 位 1 本机） | H |
| `0x591234` | `0x5A3F90` | 联机建人 `0x591130`；desc 由 `0x595960` 填：`desc+0 = 成员远端位 ? -1 : 0`（`0x595AE2`，远端位 = 成员 `+0x10` 位 1，`0x12AC420`） | H |
| `0x59145B` | `0x5A3F90` | 离线建人 `0x591410`；desc 由 `0x595C80` 填：`desc+0 = 本机槽位`（`0x595CBD`，单人 0，分屏 0/1） | H |
| `0x59DE91` | `0xD8730(GS, player, class)` | 离线预载 `0x59DE50`（每个本机槽位），`0xD8730` 尾跳 `0xE34E0` | H |
| `0x59DD27` | `0xE34E0` | 联机预载 `0x59DC90(member)`，每个会话成员一次；成员指针只在函数入口（`rcx`）可得 | H |
| `0x93B130 / 0x93B24C / 0x93B426 / 0x93BD03` | `0xE3420` | HQ / 房间 3D 视图（`UiHQ_3DModelView`）的 msg 3 / 6 与外观变化；视图只为本机玩家建（`GS+0x14FF4` 个） | H |

插件（`src/outfit41.cpp`）：
- `0x591234` / `0x59145B` 改调包装：读 `desc+0`，只有 **0（本机第一名玩家）** 才把线程标记设为「建本机人」，再调原函数；`0x5A3FFC` 的路径钩子见标记且本兵种开、且**本兵种这次已按 4.1 预载过**才返回新路径（资源必须先预载：只换建人不换预载可能建人失败，M）。
- `0x59DE91` 改调：`player == 0` 且本兵种开 → 新路径，并记「已预载」。
- 联机预载：`0x1B8E98 / 0x225FAC → 0x59DC90` 已被 `src/mission.cpp` 改调（任务开始的钩子），所以不重复改这两处，而由 `mission.cpp` 的 `SessionPreloadHook` 把调用交给 `Outfit41SessionPreload`：读成员 `+0x10` 位 1，本机成员才设标记；`0x59DD27` 的路径钩子同上。
- 4 处 `0xE3420` 改调：本兵种开 → 新菜单 SGO。分屏第二名玩家与第一名同兵种时，其预览也会显示 4.1（战斗模型不会）——已知限制。
- 签名：`tools/outfit41_sigs.py` 的 29 段字节（含每个改调点前后的指令、`0x5A3F90` 读 desc 的指令、远端位函数、弹出菜单构造与 lambda），版本不符整个功能 `OUTFIT41 REFUSED`、不打任何补丁。`RedirectCall` 另外核对每个调用点仍指向原被调者（别的钩子先占了就不改，日志说明）。

## 3. HQ 外观弹出菜单（逆向）

真正改外观的是兵种界面 `0x8AB310`（`docs/loadout-re.md` 里 `UiSoldierSelect 0x7DB7D0` 也写外观，但它是逐行文本菜单，推测为调试界面，M；真正的外观弹窗写 `GS+0x6E94` 的只有下面五个 lambda，另有一个 setter `0x8AC6E0` 在全模块找不到任何调用、函数指针或 RIP 引用；按字节扫描 `0x6E94` 的全部 13 处访问得到，H）：

- `0x8AB5A9..0x8ABB36`：构造一个请求对象（栈上 `rbp+0xE0`，vtable `0x17621F8`），5 个选项各为 0x90 字节 `{wstring 文本, std::function<void(ctx*)>, …}`，文本键 `ModelIndexMission / Trainee / Soldier / EDF5Trainee / EDF5Soldier`（`0x957EC0` 查本地化文本），回调 lambda 的 vtable 依次 `0x1805CF0 / D28 / D60 / D98 / DD0`，分别把外观设为 -1 / 2 / 3 / 1 / 0（`0x8AD560` 等：写屏幕 `+0x7A0`、`GS+0x6E94`，清 `+0x818`，再尾跳 `0x7E2EB0` 关闭弹窗）。H
- 选项向量在请求 `+0x150`：`+0x158` data、`+0x160` capacity、`+0x168` size；满了按 `reserve(cap*2+2)`（`0xB91D0`），逐个用 `0x89950(vec, dst, src)` 拷贝构造。H
- `+0x128 = 6`（对话类型），`+0x148` = 光标（按 `+0x7A0` 映射），`+0x12C = 1`；`0x8ABC10` 调 `0x119C600(system, out, type, request)` 显示。H
- lambda 实现 = `{vtable, 屏幕指针}`；屏幕 `+0x798` 玩家槽、`+0x7A0` 外观。MSVC `_Func_impl` vtable 6 槽：`_Copy / _Move / _Do_call / _Target_type / _Delete_this / _Get`，`_Copy` / `_Move` 写回自己的 vtable（`0x8AD410` / `0x8AD780`）。H

插件：
- 改调 `0x8ABC10`：请求类型为 6、正好 5 项、第 0 项的 vtable 是 `0x1805CF0`、屏幕玩家槽为 0、本兵种三个文件都在 Mods → 用游戏自己的 `0x89950` 把第 0 项拷贝一份追加为第 6 项，文本换成 `EDF4.1`（`0x3D450` wstring::assign），其 `std::function` 的 vtable 换成插件的一份拷贝（`_Copy` / `_Move` 调原函数后再写回我们的 vtable，`_Do_call` 是我们的）。开着时光标落在第 6 项。
- 我们的 `_Do_call`：本兵种开、写状态文件、关闭弹窗（`0x7E2EB0`，与原版 lambda 同）、调 `0x8BDE80(0, class, 0xD8700(GS,0))` 重建 3D 预览（原版选择会改存档值从而触发预览重建，我们不改存档值，所以自己发 msg 3）。
- 五个原版项的 `_Do_call` 用 `ChainVtableSlot` 串钩：本兵种原来开着则关掉、存文件、调原函数、再重建预览。五个都串上才加我们的项（否则选了关不掉）。
- 未覆盖：分屏第二名玩家的弹窗不加项；文本未本地化（各语言都显示 `EDF4.1`）；界面上若别处显示「当前外观名」，显示的是存档里的原版外观。

## 4. 通用重定向管线（`pylib/outfit41.py`）

每个兵种一份 `Spec`：4.1 的玩家 SGO（`ARMYSOLDIER / PALEWING / ENGINEER / HEAVYARMOR.SGO`）、作为底的 EDF6 SGO（外观 0：`P505_RANGER / P506_WINGDIVER / P508_AIRRADER / P507_FENCER`，EDF5 系身体，与 4.1 骨架最接近）、新文件名、例外表。流程完全相同：

1. **摆姿势**：用 4.1 自己的骨骼对网格做 LBS，摆到 EDF6 的绑定姿势。父骨骼优先遍历；一根骨骼继承父骨骼的转动；配对骨骼若有配对子骨骼离 EDF6 关节超过 `POSE_REACH = 3 cm`，再按子骨骼偏移方向做 Kabsch（单子或共线：最短转动），小于 `MIN_TURN = 3°` 不转。位置按 4.1 自己的骨长正向运动学（不把网格拉伸到 EDF6 关节）。游骑兵 / 空降兵两套绑定姿势几乎一致（只有外侧三指的根转了 10°），飞翼的手臂（EDF6 的低 10–18 cm）被同一段代码转下来。
2. **改挂**：4.1 每根有权重的骨骼按名字（不分大小写）配 EDF6 骨骼，或按例外表；BLENDINDICES 改成 EDF6 骨骼号、用 EDF6 的 inverse bind。同一顶点落到同一根 EDF6 骨骼的权重合并，按权重排序，`flags[2]`（最大影响数）重算。
3. **4.1 独有骨骼**（装甲、裙甲、腰包、雷达、推进器…）：`mdb.insert_bone` 挂到对应父骨骼下，局部矩阵 = 摆好后的世界矩阵相对父骨骼，inverse bind = 其逆。没有任何 EDF6 动画驱动它们 → 刚性跟随父骨骼。骨骼总数超过 `MAX_BONES = 126`（原版 738 个模型里最大的骨架：e601_martian / e661_martian_leader）时改为**并入父骨骼**（画面完全相同，因为不动的子骨骼与父骨骼等价）：飞翼 99 + 34 = 133 → 并入。
4. 本体与 LOD1 / LOD2 同一骨架、同一流程；材质、贴图、顶点格式沿用 `mdb_legacy.mdb_from_legacy` 的 4.1→EDF6 转换；骨骼包围盒按原版规则重算（该骨骼所带顶点在其绑定坐标系里的 AABB；按此重算 p501 / p505 的 74 根、p506 的 90 根蒙皮骨骼，与原版差 < 0.5 mm，bounded 标志一致）。
5. 打包：4.1 的 TEXTURE / HD-TEXTURE 成员按原存储字节放在模型之前（`insert_member` 的加载顺序约定），模型 CMPL 压缩；战斗 SGO = 底 SGO 只换 `animation_model[0] = [新 MRAB, 本体, [[lod1,15],[lod2,30]]]`；菜单 SGO = 底菜单 SGO 只换 `animation_model[0]`。CAS、布娃娃 SHKT、MAB 挂点等全用 EDF6 原版（它们只认 EDF6 骨骼名，全部保留）。

### 骨骼对照（例外之外全部按名字自动配对）

共同例外（`COMMON`）：`otherFinger0..2_l/r → middle0..2_l/r`（4.1 外侧三指是一条链）、`kubi → head`（4.1 的 kubi 带着整个头，EDF6 拆成 kubi / head）。

| 兵种 | 底 | 自动配对 | 插入的 4.1 独有骨骼（→ 父） | 并入 | 摆姿势转动（°） | 摆完最大关节误差 |
|---|---|---|---|---|---|---|
| 游骑兵 | P505 | 39 | armArmor0/1_l/r→jowan0，footArmor_l/r→sune，waistPorch_l/r→koshi（91 骨） | — | otherFinger0 10.3、otherFinger1 3.3 | 3.2 cm（外侧三指） |
| 空降兵 | P508 | 39 | armArmor0/1_l/r→jowan0，radar0→mune→radar1→radar2→radarTop，backslide→mune，neckCover→mune（85 骨） | — | 同游骑兵 | 3.2 cm |
| 飞翼 | P506 | 45 | — | 34 根：booster_*、breast、skirt_f/b/l/r→mune/koshi，shoulderArmor、aux_arm→jowan0，armArmor→kawan2，footPanel / footJoint→ashi（99 骨） | jowan0 20.5、te 22.0、otherFinger0 26.3、kawan0 3.7 | 6.0 cm（肘：4.1 前臂更长） |
| 剑兵 | P507 | 33 | frontArmor_over / bCoverT / bCoverU / booster→mune，frontArmor_under→hara0，armArmor0_l/r→jowan2，skirt_l/r→koshi（110 骨） | — | te 12.5、thumb0 14.8、jowan0 4.2、kawan0 4.0 | 5.1 cm（拇指） |

剑兵例外表另有 `backWeapon(Joint)_l ↔ _r`（两套骨架左右互换，`0.726 m` 的「同名」差距就是它）；这几根没有顶点权重，目前不影响网格，留着给以后。

完整报告（每根配对、插入、并入、转动、误差）在 `edf41port/outfits.json` 的 `classes[].report`。

### 离线验收

- `outfit41.check_archive`：重新序列化逐字节一致、贴图在模型前、kind 3 骨骼 inverse bind = 绑定逆、权重和为 1、索引不越界、depth_delta 规则——四个兵种全过（selftest `edf41_outfits_shipped`）。
- 骨骼名对账：`tests/outfit41_native_audit.py`（有游戏时）核对新 SGO 与底 SGO 除 `animation_model[0]` 外完全相同，所以 CAS / SHKT / MAB 引用的骨骼名都在。
- 可复现：`edf41_outfits_reproducible`（本机有 4.1 与 EDF6 时）重新生成与提交的字节一致。
- 渲染（`model_view` + `outfit41.clip_locals`，EDF6 CAS 的局部变换；先用原版 p505 验证了四元数约定）：`C:\Users\wrds\.claude\jobs\f71bcf0d\tmp\outfit_out\`
  - `edf41_ranger_poses.png`（绑定 / stand_assault / run_f_assault / stand_sniper）、`edf41_ranger_hand_r.png` 对照 `stock_ranger_hand_r.png`、`edf41_ranger_head.png`、`edf41_ranger_legs.png`、`edf41_ranger_lods.png`
  - `edf41_airraider_poses.png`、`edf41_wingdiver_poses.png`、`edf41_wingdiver_arm_r.png` 对照 `stock_wingdiver_arm_r.png`、`edf41_fencer_poses.png`
  - 自查结论：网格连贯，头 / 手 / 腿随动作正确，握枪手型与原版相近；飞翼手臂在 EDF6 绑定姿势下与原版同角度。

## 5. 已知还原度损失

- 4.1 的装甲片、裙甲、腰包、雷达、推进器、翅膀：刚性跟随父骨骼（EDF6 动画不驱动它们；4.1 里它们本来就多半只是挂件）。飞翼的推进器不会像 EDF6 原版的 booster5b 那样随飞行摆动；飞翼裙甲不摆。
- 无口型 / 表情：4.1 脸没有 mouth / joe 骨骼权重。
- 颈部枢轴：4.1 的 kubi（带头）改挂 EDF6 的 head，转头的枢轴比 4.1 高约 1.6–2 cm、靠前约 2 cm；EDF6 的 kubi 只带动 head，不直接带顶点。
- 外侧三指（中 / 无名 / 小指）同一条链，跟 EDF6 的中指走。
- 飞翼肘部与剑兵拇指的关节在摆姿势后仍差 5–6 cm（两作骨长不同，管线不拉伸网格），大幅弯曲时可能轻微变形（离线渲染看不出明显问题）。
- LOD：原版 P505 / P508 的 SGO 不带 LOD 表，新 SGO 按 4.1 / EDF6 飞翼与剑兵的写法加了 `[[lod1,15],[lod2,30]]`。AssultSoldier 读 LOD 表已被移植的 4.1 友军证实（`edf41port/objects` 的 `AIARMYSOLDIER_*`），Engineer 类未证实（实机清单）。
- `MAX_BONES = 126` 是「原版最大骨架」的保守上限，不是游戏的已知硬限制。

## 6. 联机

- 发给别人的始终是存档里的原版外观值：没装 MOD 的人看到原版外观、不会越界；装了 MOD 的人目前也看到原版外观（选择只在本机）。
- **第二步（本次不做，设计）**：经 EDF6Coop 的附加通道（`EDF6CoopGetMemberAttributeApi` 一类的成员属性）广播「成员 N 的兵种 C 用 4.1 外观」。接收方在 `0x59DD27`（会话预载）与 `0x5A3FFC`（`0x595960` 建出的远端成员，`desc+0 == -1`，需另传成员下标：`0x595960` 的 `r15d` / 成员 `+0x48`）按该成员的属性换路径；双方都要有同名文件（只在属性里写「用 4.1」而不是路径，接收方自己检查 Mods 里有没有文件，没有就保持原版）。预载与建人必须用同一判断，否则资源未预载。

## 7. 实机验证清单（未做）

1. 安装后 HQ 兵种界面 → 外观：出现第 6 项 `EDF4.1`；选中后 3D 预览立刻换成 4.1 外观；再开弹窗光标在 `EDF4.1`。
2. 选任一原版项：预览回到原版，`EDF6VehicleCrew.outfit41.ini` 对应兵种变 0；存档外观值不变（卸载插件后读档是原版外观）。
3. 四个兵种各进一次离线任务：玩家是 4.1 外观、动作正常（站立、跑、跳、射击、换弹、投掷、飞翼飞行、剑兵冲刺、空降兵背包）、布娃娃倒地、LOD 远近切换（尤其空降兵 Engineer 类）、改色有效。
4. 日志：`OUTFIT41 battle=5/5 menu=4/4 stock items=5/5 popup=1 files … on=…`；建人后无崩溃；关闭插件 / 删文件后自动回到原版（文件缺失时不提供选项）。
5. 联机（两台）：A 用 4.1 外观，B（装 / 不装 MOD）看到 A 的原版外观，双方不崩；A 自己看到 4.1。
6. 分屏双人：第二名玩家不受影响（已知：同兵种时其 HQ 预览可能显示 4.1）。
7. 与 EDF6Coop 同装：`OUTFIT41` 的调用点没有被其它钩子占用（日志无 `not redirected`）。
