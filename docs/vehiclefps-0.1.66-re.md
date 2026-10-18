# 第三方 EDF6VehicleFPS / RangerFPS / CrawlerThirdPerson 0.1.66：逆向事实对照

2026-10-18 读了第三方源码包 `EDF6VehicleFPS-0.1.66-crawler-lower-braces-source.zip`（只读，没运行其中任何脚本），把其中的 EDF.dll 事实逐条和本仓库对照。有争议的几条用 `tools/edfre.py` 在 Steam 版 EDF.dll 上做了静态复核。第三方自己的 `status.json` 也写着 `gameplayVerified:false`。它的字节签名会在加载时校验，所以「地址在」是可信的；「语义对」只有一部分有用户实测佐证。

H / M / L 是置信度：H 已静态坐实，M 为间接成立，L 为第三方的说法、未核。

## 1. 第三方是错的、不能照搬的

| 第三方的说法 | 实际 | 证据 |
|---|---|---|
| `obj+0x314` 是对象 id，拿 (指针, +0x314) 做身份快照（`ranger_fps.cpp`、`crawler_probe.inc`、`nix_damage.inc`） | 阵营字段（`common/edf/layout.h` `kTeam`） | `0x547DBF movsxd r8,[rdi+0x314]` 按阵营索引队伍集合（`src/subcarrier.cpp`）。本 MOD `crew.cpp` `WithTeamField` 会临时写载具的 +0x314，可能让它误判成「换了车」（同线程短时写入，风险低，未实测） |
| `w+0xBD4==4 && w+0xE68>0` 才是换弹（`weapon_motion.inc`） | 3 和 4 都是：3 是换弹动画（`0x693DB5` 写），4 是收尾 `_finish`（`0x693D0E` 写），`+0xE68` 是剩余帧数 | `0x693BAF-0x693DB5`；本仓库 `docs/fp-soldier-re.md` 换弹进度一行已据此升级，`firstperson.cpp` Hand 已改用 |

## 2. 本仓库原来没有的事实

| 事实 | 第三方出处 | 置信 | 用处 |
|---|---|---|---|
| 相机 vtable 第 9 槽 `0x1768C58` → `0xF7500` 是「HUD 是否可见」判定：`!cam+0x3F0 && (!cam+0x301 \|\| 目标 +0x1550 存活)` | `nix_instruments.inc` | H（反汇编） | 补进 `camera-re.md`；`+0x3F0` 非 0 时既改用角度旋转（本仓库已有），HUD 也判为不可见 |
| `cam+0x4D0` 本帧实际提交的相机矩阵，`+0xB00` 历史矩阵 | `ranger_fps.cpp` | M（第三方实机记录） | 本仓库用提交调用点 `0x118E10A` 的钩子取，时序更严，不换 |
| 座位对象 vtable `0x17D9868` = `RideInfo@VehicleBase`；`seat+8` 指回载具 | `nix_camera.inc` | H（RTTI）/ M | 可做座位指针防御校验，现无对应 bug |
| 资源对象 `+0x68` 有符号骨骼数；尼克斯 V504 49 根 / 金粉 50 / V612 43 / V608 42，蜘蛛 31（金 32） | `nix_camera.inc`、`crawler_probe.inc` | M | 本仓库按骨骼名和 `vc_eye_*` 标记骨找，不用序号。**本仓库安装器加标记骨后骨骼数变了，第三方座舱因此拒绝启用** |
| 蜘蛛 vtable 第 6 槽 `0x62E770` | `crawler_probe.inc` | H | 本仓库用第 4 槽 `0x612D20`，不冲突 |
| HUiHudWeapon `+0xC88` 开关、`+0xC90` 透明度（进 RGBA 的 alpha）、`+0xAA8` 乘员 | `nix_hud.inc` | H（`0x831AD1-0x831ADA`） | `stockgauge.cpp` 已走原生布局隐藏，不换 |
| HUiHudRader 第 2 槽 `0x82B4D0` 是绘制 / 布局（本仓库 `map.cpp`、`camera-re.md` 称「更新」） | `nix_instruments.inc` | M | 按 `hud-re.md` §9.1 的规律（第 1 槽更新、第 2 槽布局），第三方的叫法更一致；`map.cpp` 只借用它调用的 `0x5E0F20`，不影响功能 |
| UiDebugMessage（字幕）`0x17F4298` 第 2 槽 `0x7D8940` | `nix_instruments.inc` | L | — |
| HUiHudDamage `0x17FC970`：通知第 21 槽 `0x820250`、绘制第 2 槽 `0x8202B0`；事件 id `[0x20B37AC]`；`payload+0x18` 有符号血量变化；`ui+0x778` 拥有者、`ui+0xBF8` 时钟；合并窗口 `[0x1F77888]` 帧 × `[0x17FCBD8]`(1/60) | `nix_damage.inc` | H（vtable 槽与 `0x820250` 已核） | 只覆盖本机玩家打出去的伤害；`damagestats` 在伤害源头统计，范围更大，不换 |
| LYT_HUDRADER01：1920×1080 下十字中心 (1659,201) | `nix_cockpit_draw.cpp` | M | 本仓库没有画在雷达上的功能 |
| `0x1183A82` 战斗中锁鼠标的 `GetCursorPos`，`0x1183B7D` `SetCursorPos`；`[0x1FEEE1D]==0` 表示鼠标锁定；主 / 副窗口 HWND `0x21360A8/0x21360B0` | `nix_controls.inc` | H（`0x1183B64`） | 可用来在菜单 / 光标显示时屏蔽热键；已有 `GamePaused()`，收益有限 |
| `0x1118B45` EDF 自己调 Present 的位置（9 字节）；网格 DrawIndexed 调用 `0x10FF556/0x10FF5C1` | `nix_present.inc`、`arm_draw.cpp` | M | 本仓库不在 D3D 层画 |
| Ranger 冲刺：`[h+0x1EF8]+0x30` 的 vtable == `0x17CE3D0`（`State_Dash@AssultSoldier`） | `ranger_fps.cpp` | H（RTTI） | 只有 Ranger 有；本仓库 `+0x5D0==0x29E` 足够 |
| 武器 `w+0x1C0` 图标路径、`w+0xF8` 运行时 SGO（DSGO v16） | `weapon_metadata.inc` | M | NPC 选枪不需要按图标分类 |

## 3. 两边一致的（不再重复）

相机 `0x1768C10` / 第 4 槽 `0xF86A0` / `+0x220` / `+0x24` FOV / `+0x350..0x360` 目标；兵种 vtable 与第 4 槽；`+0x340` 玩家输入；`+0x1540/+0x1548/+0x1550` 座位、载具、控制块；座位数组 `veh+0x608`/`+0x618`、步长 0x340、乘员 `seat+0x260`；尼克斯 / 蜘蛛 vtable；`veh+0xE40` AnimationModel 与骨架；武器 `+0x120/+0xF30/+0xBE8/+0x20C/+0x350/+0x1D0/+0x4C0`；HUiHudWeapon / PowerGuage 槽位；BulletBase / LaserBullet02 等子弹 vtable；瞄准弧线 `0x6888D9`。

## 4. 同装时双方的钩子

| 地址 | 第三方 | 本仓库 | 结果 |
|---|---|---|---|
| `0x1768C30` 相机第 4 槽 | RangerFPS 要求槽值仍是原版才交换 | `firstperson` / `map` / `sightzoom` 串接 | 按文件名加载 RangerFPS 先装，本仓库接在它外层，可共存 |
| `0x17C4038` AnimationModel 第 1 槽 | RangerFPS 交换 | `HookDrawOf` 只接原版 | **本仓库第一人称停用**；现在日志写明占用的模块（`firstperson.cpp` InstallFirstPerson） |
| `0x6888D9` 瞄准弧线 | RangerFPS 改写 | `RedirectCall` 要求原版目标，失败 | 无害（第一人称已停） |
| 相机第 9 槽、HUD 各类第 2 槽、16 个枪口火光调用点、`0x1118B45`、`0x1183A82`、D3D 上下文 33/35/36 号 | 第三方 | 不碰 | 不冲突 |

判断「玩家在载具里」时，第三方要求相机拥有者有 `+0x340`（NPC 为 0）、玩家坐 0 号座位且 `seat+0x260==玩家`，所以 NPC 开的车不会被它接管。未实测的风险：大地图 / TV 视角下 RangerFPS 可能仍把手臂摆到当前相机前方（它按 `cam+0x4D0` 摆）。

## 5. 借过来的工程做法

工程做法的对照另见本次 PR 说明，落地的有：
- 构建提交号和 DLL 文件信息写进 loading 行（`cmake/build_id.cmake`）；
- 恢复页保护失败计数（`edf::RestoreProtection`）；
- INI 数值严格解析（`edf::ParseFloat`）；
- `dbghelp.dll` 只从 System32 加载；
- 测试真加载编译好的两个插件 DLL；
- EDF.dll 指纹表的离线变异测试。

**没有采纳：给自带栈帧的 shim 注册 unwind 信息**（第三方 `nix_present.inc` 的做法）。本仓库的 shim 大多是从游戏函数中段 `jmp` 进来的，例如 `map.cpp` InstallHold、`physics.cpp` 接触检查。第三方的 shim 是被 `call` 进入的，写法不能照搬。

试过两种办法，都不合适：
- **用 `UNW_FLAG_CHAININFO` 链到游戏函数的条目**：本机实测（2026-10-18）RIP/RSP 能回溯对，但父函数的 SAVE_NONVOL 寄存器回溯错了。链入条目的寄存器保存偏移是按 shim 的帧基址算的。这些跳入点所在的游戏函数都大量用 SAVE_NONVOL / SAVE_XMM128，还带 C++ 异常处理例程，所以链接方案在这里是错的。
- **把父函数的展开码复制一份并平移偏移**：要改 6 处已经验证过的 hook。收益只是崩溃 / 挂起 dump 的栈完整，而且 MiniDumpNormal 是否带上这页的动态函数表也没坐实。风险大于收益。
