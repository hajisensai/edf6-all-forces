# 2026-11-24 全面优化摸底：九路审查原文

九个 reviewer 只读审查（不改文件、不开游戏）的结构化结果，原样整理。结论里标「推测」的未核实；凡 `需实机` 的都要实机验收。计划与排期见 `docs/plan-2026-11-24-optimization.md`。

标记：工作量 S/M/L/XL；**G** = 需实机；**P** = 需升联机协议（全员同版本，旧版拒绝进房）。

## 帧率与渲染/模拟解耦（fps）

现状：不限帧率只完成了阶段 1（frame_rate_logic.h 纯逻辑和 frame_rate_check），FrameRateCap 只被读取（plugin.cpp:1381-1385），游戏侧一处都没接。这次我用 vanilla-code 的完整汇编和 Ghidra 伪代码核对了主循环、Application::Update 和渲染投影链（vanilla.sqlite 被拒绝访问，vanilla.py 查不了，所以 refs 是用 rg 搜 assembly/EDF.dll.asm 得到的）。
文档里有三处要改，下面写的都已静态核实：
1）§2.4 对滚轮和鼠标的修法有错。每帧的顺序是：0x11A2FF3 先取输入，然后 0x705715 进 System::Update，步进读的是上一帧写好的封装，最后 0x7058C7 才把输入源写进封装。输入要过一帧才轮到步进，所以「n=0 帧不取滚轮」照样会丢。鼠标、滚轮应该和按键边沿一样，在封装这一层累加。
2）§4.1 说 SyncInterval「由游戏设置决定（M）」，实际是 H：值等于 Application+0xC==0（0x11A3098 → 0x111D890 → 0x1118B41 → Present）。原版一直开垂直同步，只有追帧后的 8 帧关掉，而且这 8 帧按渲染帧计数。
3）0x1197B50（读镜头 +0x220 的投影调用）已被 scope_lens.cpp:348 改写过，RedirectCall 会核对原目标，第二次挂必然失败。镜头插值只能并进 scope_lens 的 ProjectHook，或者先把它重构成一个共享派发。
最大瓶颈：阶段 2 的输入保留要用到封装表布局（0x7CDC0 每个玩家 0xDD0 字节），还要逐个核对动作；阶段 3 的角色根插值得在渲染线程按帧对齐 alpha。这两块都没法离线验收，必须实机。
EDF6Coop：每帧要做的事都挂在 EOS_Platform_Tick 的 IAT 钩子上（eos_hooks.cpp:2721-2755），只经 0x7056AE 调用。不加守卫时 n=0 帧会跟着多跑；把 0x7056AE 守在 n≥1 帧后，节奏和原版一样，不用升协议。
建议先做：1、2、3、4 这四步（帧门加节奏守卫加输入），开关只在 Debug 下生效，先实机验收「tick 稳在 59.9、不丢键、联机一致」；然后做 5、6（镜头抓取和插值）；最后 7（玩家和载具根变换）、8（插件镜头的切换协调），合在一起才能对用户发布。

### fps-1. 阶段2a：帧门小桩与按呈现槽等待（只在 Debug 下开）（M · G）

- **问题与证据**：主循环 0x11A2F4F / 0x11A2F6B 都是 `E8 call 0x11DAE60`，紧跟 0x11A2F54 / 0x11A2F70 的 `8B D8 mov ebx,eax`，0x11A2F76-0x11A2F80 再把 n 限到最多 3（汇编已核实，H）。0x11A2F65 的 Sleep 已被 frame_pacing.cpp 换成 TickWait（RedirectImportCall），安装前会整段比对 0x11DAE60 的 97 字节。插件的 PERF 行（crew.cpp:866）按 crew 帧计数，也就是按 tick 计；开了帧门之后它测不出呈现帧率。
- **方案**：按 unlimited-fps-re §4.1 做：两处 call 改指向近处小桩（AllocateNearCode 加 RtlAddFunctionTable，UWOP_ALLOC_SMALL 0x28），两处 8B D8 改成 8B DA。小桩里先调 framerate::Ready，到了槽才调原 0x11DAE60。把本帧的 n 写进一个全局 FrameClock{n, phase, frameId}，后面的守卫、输入、插值都读它。TickWait 在帧门开着时改成等 WaitCounts，即等到下一个槽。安装时先复用 kTickCountSig，再加上两处 8B D8 的签名，任何一处不符就整体不装。新增一行 FRAMERATE 日志，每 5 s 输出 tick 率、出帧率、n=0 帧占比和 n=0 帧最长耗时。补签名审计，写法照 tests/frame_pacing_sig_audit.py。
- **收益**：后面所有步骤都靠它。单独看没有画面收益，只验证模拟能否守住 59.94 Hz。
- **风险**：帧门开着但下面几步没接时会快速丢键、植被加速、EOS 跑得更勤，所以只能靠 Debug 开关验收，不能对用户开放。小桩在异常回溯中途断开会导致崩溃转储链断。

### fps-2. 阶段2b：节奏守卫逐个改调用，不用整块跳转桩（含 EDF6Coop 评估）（S · G）

- **问题与证据**：0x705633 是 `FF15 call [SteamAPI_RunCallbacks]`；0x705693 `call 0x788460`；0x7056AE `E8 call 0x12ADB30`；0x7056BA `E8 call 0x1230A0`（MapMgr 风时间 += 0.0125）；0x70595C-0x705961 是 `8B 46 6C / 85 C0 / 7E 11`（+0x6C 倒数，汇编已核实，H）。用 jmp 桩跳过 0x705633..0x7056B3 时，桩内 call 无法被 .pdata 正确回溯。EDF6Coop 用 IAT 钩住了 EOS_Platform_Tick（D:/APP/edf6_directnet/src/eos_hooks.cpp:3601），每帧要做的事都在 hookPlatformTick 里（2721-2755：trail/mesh/bulk/room/extension 等 tick）。0x12ADB30 的直接调用者只有 0x7056AE 和 0xCD468，后者是一个 Sleep 1000 ms 的循环（0xCD46D mov ecx,0x3E8）。所以不加守卫时 Coop 会按呈现帧率跑。它大多以 GetTickCount64 门控，语义大概率不变（推测，没有逐个函数看），但 EOS tick、p2p 收包循环（12C8CC0）和 0x788460 的频率会变。另外，§2.2 漏列了几处每帧调用：0x7056CB 0x2CA350（参数 dt，遍历对象发射线、置 +0xC4 标志）、0x7056D7 0x5DDE60（清零 +0xC0 后重新计数）、0x7056F2 0x112B1A0、0x705700 0x10FFFB0、0x705995 0x7C70F0、0x7059CA 0x11A1C50。从伪代码看前两个是幂等的重算（M），其余没看。
- **方案**：不用跳转桩，逐个改调用。0x705633 用 RedirectImportCall 换成 SteamRunCallbacksGuard；0x705693 / 0x7056AE / 0x7056BA 用 RedirectCall 换成守卫函数。四个守卫都读 FrameClock.n，n==0 就直接返回。前台检查 0x705639..0x705686 只在状态变化时调 EOS，不用管。+0x6C 处把 5 字节 `8B 46 6C 85 C0` 换成 `E8 call` 一个叶子桩：`mov eax,[rsi+6Ch]; cmp dword [r14+4],0; jne 1f; xor eax,eax; 1: test eax,eax; ret`。桩不改栈，所以不需要登记 unwind。漏列的那几处补进 unlimited-fps-re §2.2，逐个核对是否幂等。
- **收益**：Coop、Steam、EOS 和风速都回到原版节奏，同房间有人开有人不开时模拟一致，不用升 Coop 协议。
- **风险**：view.cpp 靠 0x1230A0（0x1232B5）把远裁剪写进镜头，守卫后只在 tick 帧写，插件每帧再抬高一次，结果不变。view.cpp:20-21 注释说 0x705530「不是每帧」，这是错的，要改注释。漏列的几处如果不幂等，会出现隐性加速。

### fps-3. 阶段2c：输入保留改在封装层统一做（纠正文档的滚轮 / 鼠标修法）（L · G）

- **问题与证据**：每帧的顺序已核实（H）：0x11A2FF3 call 0x1182A10（输入回调 0x1188270，0x1188479 取鼠标、0x1188480 取滚轮写进输入源），然后 0x11A3006 进 Application::Update；里面先 0x705715 call 0x1198DA0（System::Update：渲染加步进，步进读动作封装），后 0x7058C7 call 0x77320（`add rcx,0x5AE8; jmp 0x7CDC0`，edx=n）把输入源写进封装、清边沿再重设。所以第 j 帧步进读到的，是第 j-1 帧取的输入。文档 §2.4 的「n=0 帧不取滚轮」和在取的那一层累加鼠标都会在下一帧丢：j-1 帧取到的量，j 帧若是 n=0 就会被覆盖。0x1182BC0 一共 153 个直接调用：0x78xxx 34 个、0x7Axxx 69 个、0x7Cxxx 26 个、0x7Dxxx 5 个，另有 19 个在 0x101BA0。0x101BA0 没有直接调用者（虚函数）；它对 [0x20B2958]+0xAF0 置 1，并对自己 +0x350 起的封装传 n=1，调用节奏不明（M）。
- **方案**：用两个钩子覆盖所有封装。记录钩子：RedirectCall 0x7058C7（目标 0x77320）。调用前，若本帧 n==0，先把上一帧写好、还没被步进读过的边沿、鼠标位移、滚轮量记进 Shadow（就是 frame_rate_logic.h 的 BeforeUpdate）；然后照常调原函数，UI 每帧仍看到原生字节。注入钩子：RedirectCall 0x705715（目标 0x1198DA0）。本帧 n≥1 时，把累计的边沿 OR 回封装，鼠标和滚轮改成累计和（ForStep），再调 System::Update；调完把注入的值复原，不让 HUD 和 UI 再看到一次。封装表布局（mgr+0x5AE8+8，每个玩家 0xDD0）和鼠标封装的字段（+0xC，§2.4 标 M）要先读 0x7CDC0 补成 H。0x101BA0 那组先查清是谁、按什么节奏调用。frame_rate_check 增加一个「1 帧流水线」用例，证明在取的那一层修会丢。
- **收益**：高帧率下不丢快按的键，鼠标转角和滚轮切枪不随帧率变化。
- **风险**：按下又松开都在两步之间发生时，读按下边沿又看 +0x10 的动作（蓄力、按住）可能出问题，要逐个看。注入后复原的时机不对，UI 会看到两次（第三方 R19 的「按一下走两格」）。map.cpp 的地图拖动如果读鼠标封装也要核对。

### fps-4. 阶段2d：垂直同步由插件接管（SyncInterval 来源补成 H）（S · G）

- **问题与证据**：0x11A306B-0x11A3079：Application+0xC 每帧减 1（下限 0），n>1 时置 8。0x11A3095 `cmp [rdi+0xC],edx; sete dl` 后调 0x111D890，后者把 {this, flag} 作为渲染命令 0x1118AF0 记下（伪代码 part-0096.c:20671）。0x1118B3C-0x1118B45 取出 flag 放进 edx，调 `[rax+0x40]`，即 IDXGISwapChain::Present 的第 8 槽，参数是 SyncInterval。0x111D890 的直接调用者只有 0x11A309B。所以原版一直是 SyncInterval=1，只在追帧后的 8 帧是 0，文档 §4.1 写的「由游戏设置决定（M）」不对。另外这 8 帧按渲染帧计数，帧门开着时 144 fps 下只剩约 55 ms。
- **方案**：RedirectCall 0x11A309B（目标 0x111D890），由插件算 dl：FrameRateCap=0 时照原版；开着时，追帧窗口改成按 tick 计（8 个 tick），上限高于刷新率或不限时传 0。交换链是不是翻转模型、有没有 ALLOW_TEARING 没查（L），要实机看撕裂和帧率上限。文档 §4.1 跟着改。
- **收益**：上限高于显示器刷新率时，不会被 Present 卡在刷新率；60 Hz 显示器上的行为能说清楚。
- **风险**：关掉垂直同步会撕裂。全屏独占和窗口模式表现不同。

### fps-5. 阶段3a：在 0x11995A6 抓镜头步进结果（不用第三方 +0x52C）（S）

- **问题与证据**：0x118DF40 唯一的直接调用在 0x11995A6（`E8 95 49 FF FF`），位于 tick 循环 0x1198FD0..0x119972E 之内。暂停路径从 0x11990CE `jne 0x119953B` 进来，同样走到这里。外面按 4 个视口循环（0x11995 起 esi=4，镜头指针在 System+0x4D8+i*0x188）。0x118DF40 先 `call [rax+0x20]`，也就是 vtable 0x1768C10 的第 4 槽：map.cpp:936、firstperson.cpp:2723、sightzoom.cpp:280、infantryscope.cpp:322 都链在这一槽上。turretcam 的 LookHook 在第 4 槽里面（0xFC01B），也就是镜头状态 2 里。所以 0x11995A6 返回时，插件所有镜头功能都已写完 +0x220（H）。第三方用 +0x52C 判断「是不是新的一步」，这个值没核实，也不需要。
- **方案**：RedirectCall 0x11995A6，先调原函数，再给对应视口存一份快照：prev=cur，cur 取 +0x220..+0x25F 和 FOV +0x24 / near +0x28（0x118E03F / 0x118E039 渲染时会读），连同 tick 序号。追帧时 n=2、3 会抓多次，prev 自然是倒数第二步。快照只在游戏线程读写。
- **收益**：拿到准确的「上一步 / 这一步」镜头状态，不用猜步进代数，插件镜头功能写的结果也都包含在内。
- **风险**：很低，只读。不过这个调用点以后有别的功能要挂时，要并进同一个钩子。

### fps-6. 阶段3b：镜头插值并进 scope_lens 的投影钩子，下一帧步进前复原（M · G）

- **问题与证据**：渲染投影的调用在 0x1197B50 `call 0x118DFF0`（rcx 是视口镜头，rdx=System+0x504+i*0x188）。0x118DFF0 在 0x118E051 用 +0x220 经 0x71F70 算出 +0x1A0 的视图，在 0x118E10A / 0x118E11D 把 +0x220 交给 Umbra（0x11D46D0）。0x118DFF0 只有 0x1197B50 一个直接调用者（H）。scope_lens.cpp:36、348 已经用 RedirectCall 占了 0x1197B50，第二次挂会因目标核对失败。firstperson 的 CameraSubmitHook 挂在 0x118E10A（firstperson.cpp:157、2744），抓到的视图进 ViewHistory（kViewHistory=8，:343）；DrawTransforms 按 T=anchor⁻¹·L·C_draw 跟着实际用来画的镜头走（:2211-2233），所以第一人称视模型能自动跟上插值后的镜头。HUD 0x7058B7 在 System::Update 之后，n=0 帧如果已经复原成当前步的镜头，伤害数字、标记会和画面错开。
- **方案**：在 scope_lens 的 ProjectHook 最前面，或把 0x1197B50 抽成一个共享派发，让 scope_lens 和插值都挂在里面，按视口做：用快照算 InterpolateRigid(prev,cur,alpha) 写进 +0x220，FOV 做标量插值，再交给 scope_lens 的 Aim 和原 0x118DFF0。复原放在下一帧的 0x705715 钩子（和 2c 是同一个钩子），在 System::Update 之前写回 cur。n≥1 帧 alpha=1 正好等于 cur（frame_rate_check 已验证端点精确），所以步进的起点不变；n=0 帧没有步进，HUD、scope 和第一人称都看到同一个插值镜头。要核实步进之外没有模拟逻辑读 +0x220 / +0x1A0（M）。
- **收益**：每秒不同的镜头矩阵数从 60 提到出帧率，这是用户最先感觉到的流畅度。
- **风险**：一定要和 3c 同时上（§2.3：只插镜头，角色会按 tick 抖）。scope_lens 镜片视角与主镜头不一致时会错位。kViewHistory=8 在 240 fps 下能否盖住渲染线程最多 3 帧的延迟，是推测，要看日志。

### fps-7. 阶段3c：玩家角色与所乘载具的根变换插值（复用第一人称的改姿势机制）（XL · G）

- **问题与证据**：第三方说 CAS 蒙皮 0x11009A0 有 3 个调用点，这一条补成 H：0x6C2C8A、0x1102F2E、0x1104131。0x1100F10 有 27 个调用者（0x14F0C3、0x6C06D7、0x7F386D 等），多数在对象的 update 里，按 tick 跑。所以骨骼世界矩阵只在 tick 更新，渲染直接读骨骼实例的 pose 数组。firstperson 已经有一套在渲染线程上改再还原 pose 的机制：模型 vtable 第 1 槽的链（HookDrawOf）、Pose()、RestoreCapturedPoses（firstperson.cpp:2295-2308），还用 SeqPublish 把游戏线程的状态交给渲染线程。但它跳过了 pass 3/4（:2327），阴影没覆盖到。对象根矩阵在哪个偏移没查（M）。
- **方案**：新建一个模块，复用这套 draw 钩子：每个 tick 记下本地玩家人物和所乘载具（ride.vehicle+0xE40 的模型）的根矩阵 prev/cur。每次 CameraSubmit 把 {alpha, prev, cur} 跟视图一起存进 ViewHistory；渲染线程用视图匹配找到这一帧对应的 alpha，算 D=R_cur⁻¹·R_interp，乘到这个模型所有骨骼上（行向量约定，右乘），所有 pass 都乘，阴影也要。InterpolateRigid 拒绝的情况，比如瞬移或上下车，就原样画。和第一人称的 Pose 叠加时，第一人称的 T 已经跟 C_draw 走了，所以第一人称模式下身体和枪不能再乘 D，避免算两次。
- **收益**：第三人称和载具高速行驶时，角色相对画面不再按 tick 抖，镜头插值才能对外发布。
- **风险**：渲染线程最多落后 3 帧，pose 数组可能被下一步覆盖（现在的第一人称也是这样）。改的 pose 没还原会污染模拟。只做本地玩家和载具时，其他角色和敌人仍按 60 Hz 动（阶段 4）。

### fps-8. 插件镜头功能的切换协调：加一个明确的断点 API（M · G）

- **问题与证据**：第 4 槽上的写入者有 map.cpp CamStepHook（:880，地图俯视）、firstperson.cpp:1887、sightzoom.cpp:226（写 +0x220 和 FOV +0x24）、infantryscope.cpp:102（FOV +0x24 / +0x410）；turretcam.cpp LookHook（:1028，状态 2 下 0xFC01B）加上 chase_cam.h 的弹簧（按 GameMs 墙钟取 dt，每 tick 一个样本）；enemy_control 镜头和 highcam 走 turretcam 的机位。InterpolateRigid 只拒绝 maxMove / maxTurn 以上的跳变。第一人称和第三人称肩后视角之间相差一两米、开镜时 FOV 突变，都低于这个阈值，会被插值抹成一个 tick 的拖影。FOV 不插值的话，sightzoom 的缩放动画仍是 60 Hz 一档一档地跳。
- **方案**：frame_rate 模块提供 CameraCut(cam)：本步是切换，下一帧 prev=cur。map 开关、第一人称开关、敌控 / 炮塔接管和交还、sightzoom / infantryscope 进出镜时调用。FOV 做标量插值，切换时同样断开。chase_cam 不用改（已按 tick 采样，插值在它之后）。game_clock.h 的暂停规则不受影响。
- **收益**：镜头切换时不跳、不黑闪、不出拖影（§7.6 验收项）。
- **风险**：漏掉一个切换点，就会出现一个 tick 的拖影，不崩溃。

### fps-9. 插件自己每个呈现帧都会跑的钩子与性能统计（M · G）

- **问题与证据**：插件逻辑大多由 GameFrame()（场景步进计数 +0xCE0，game_frame.h；48 个文件、163 处调用）去重，n=0 帧不会多跑（H，文档 §2.1）。但按渲染帧跑的钩子会多跑：firstperson RenderHook（0x1198FA4，++renderFrame、PlaceAll，:1760）、scope_lens 的尾段和 SubmitHook、thermal 的 begin/end/submit、map 的 HudShow / DamageDraw、HUD 派发 0x7058B7 里的插件 HUD 和 dmgnum。凡是按调用次数推进动画或计数的，到高帧率都会变快（没逐个核实，推测）。PERF 的帧统计（crew.cpp:866）按 crew 帧算，perf_report.py 的 APP_FRAME=0x705530 采样也分不出 n=0 帧。
- **方案**：逐个检查这些钩子：动画按 GameMs 或 tick 推进的不用改，按调用次数推进的改成按 tick。perfrun / perf_report 增加 FRAMERATE 行的解析，把 n=0 帧和 n≥1 帧的耗时分开统计（对应阶段 6，第三方的数字是 n=0 帧约 9.1 ms、n≥1 帧约 5.5 ms，L）。
- **收益**：不会出现插件 HUD 或特效的隐性加速，也有数据衡量中端机实际能到多少帧。
- **风险**：低。

### fps-10. 联机实测：一人开一人不开（M · G）

- **问题与证据**：按 2b 守卫之后：EOS tick、Steam 回调、0x788460 只在 n≥1 帧跑，插件的 plugin_net 由 tick 驱动，模拟仍是 59.94 Hz。从静态看两端的模拟和网络节奏都和原版一样（文档 §4.4 的推论加上本次对 Coop 钩子的核对）。但 EDF6Coop 本地仓库停在 d5a7948（2.7.2，11-01），本地的 origin/main 也是它，没 fetch，可能不是最新。
- **方案**：做完 2a+2b+2c 后，用 tests/autopilot 加 Coop 在同一房间测：一端 FrameRateCap=144，另一端 0，打一关，对照敌人位置、伤害和进度。Coop 日志的 EOS tick 线程与频率统计（maybeLogStats）要和原版一致。之前先 fetch Coop 仓库，核对最新版的钩子有没有变。
- **收益**：确认不用升协议，满足「所有功能都要支持联机」。
- **风险**：Coop 新版如果加了别的每帧钩子（不在 EOS tick 里）要重新评估。

### fps-11. 文档里 M/L 未核实点的更新清单（S）

- **问题与证据**：本次补成 H 的：SyncInterval 的来源（见 2d）；0x118DFF0 只有 0x1197B50 一个调用者，0x118DF40 只有 0x11995A6 一个调用者，暂停路径 0x11990CE→0x119953B；输入流水线差一帧（见 2c）；0x77320 收 n（edx）后尾跳 0x7CDC0；0x11009A0 的 3 个调用点；0x12ADB30 的两个调用者。仍是 M 的：0x1230A0 的另一个调用者 0x9D773 所在的 0x9D700，唯一调用者 0x9D50F 位于 0x9D270，而 0x9D270 没有直接调用者（虚函数，vanilla.sqlite 被拒绝访问，没能查 vtable）；0x101BA0 那组 0x1182BC0 的调用节奏；[rdi+9] 由谁置位；鼠标封装字段 +0xC；0x7056CB 等漏列的每帧调用。
- **方案**：把上面的结论写回 docs/unlimited-fps-re.md 的 §2.2、§2.4、§4.1，再按仓库规则写回 vanilla-code 索引。等 sqlite 能访问后，用 `vanilla.py vtable` / `refs` 补上 0x9D270 和 0x101BA0 的虚函数归属。
- **收益**：阶段 2 的设计不会建在错误前提上，尤其是输入那部分。
- **风险**：无。

## 主循环 CPU 热点与多线程（cpu）

现状：委托方给的前提「Havok 开多线程后 stress:30 仍 47.87 ms、stress:100 仍 108 ms」已经过时。这两个数来自 docs/havok-mt-re.md §13 和 PR #331，测的时候 #330 还不是最终版。#330 合入后的最新实测是 stress:30 32.8 ms、stress:100 77.4 ms（PR #330 正文）。其中插件 ThinkHook 每帧 21.4 ms，edf::Readable 每帧 8.5 ms（docs/npc-ai-design.md:100），Havok 约 9 ms/帧（havok-mt-re.md §13）。剩下约 40 ms 是原版自己的时间，至今没有按阶段拆过，仓库和 tmp 里也没有留下 #330 之后的 stress 报告。
已静态核实的关键事实：
（1）System::Update 每帧先画一次（0x1198FA4 call 0x11978D0），再步进 n 次（0x1198FD0..0x119972E，0x1199713 inc/cmp r12d/jne）。n 在 0x11A2F76 被夹到 ≤3，所以 stress 下帧时间 ≈ 1 次绘制 + 3 次步进，模拟本身已经变慢。PERF 行没有记步数，「ms/帧」会误导优化方向。
（2）原版已经有一个 xgs 作业池：0x1191829 _Thrd_hardware_concurrency-1，再由 0x1191845 调 0x65B90 建成，16 线程机器上是 15 个 worker。每个阶段末尾都用 0x65E90（逐 worker ResetEvent+SetEvent）和 0x65F10（逐 worker WaitForSingleObject INFINITE）fork-join 一次 System+0x358 的作业表。但各阶段的对象虚函数循环本身是串行的：ai_update 0x1199120 call [rax+0x38]、pre_update 0x1199250 call [rax+0x20]、object_update call [rax+0x28]。Havok 池在这台机器上只给出 7 个 worker。
最大瓶颈：仍然是游戏线程单核。可控的大头是插件 ThinkHook 和 Readable，合计约 30 ms/帧；原版 40 ms 是黑箱。
建议先做：第 1 条，补 perf_report 分区并在当前 main 上重测 stress:30/100 基线。同时并行做第 2 条 Readable 的算法改造，这条风险最低、收益明确。插件 AI 的两阶段并行（第 3 条）等测量确认后再动。原版对象循环并行化和「渲染与模拟流水线化」现在都不建议做（第 9、10 条）。帧步进等待、窗口线程 Sleep(0) 已经修过（docs/frame-pacing-re.md），不要重复。

### cpu-1. 补测量：perf_report 按「步」归一、识别全部线程角色、细分绘制与帧外时间，并在当前 main 上重测 stress:30/100 基线（S · G）

- **问题与证据**：1) 现有唯一的分区来源是 tools/perf_report.py:33-35 的 PHASES，只覆盖 System::Update 内的 7 段。thread_role（perf_report.py:357-368）只认游戏线程和窗口线程，其余线程一律归为「leaf …」：xgs 作业池的 15 个 worker、Havok 的 7 个 worker、渲染线程、ai::World 线程（docs/aiworld-re.md:7 说崩在 AI 线程）、音频都分不出来，也就判断不了「是游戏线程在等 worker，还是 worker 没活干」。2) PERF 行（src/crew.cpp:866）只记帧时间，不记每帧步数。而 0x11A2F76 cmova 把 n 夹到 3，System::Update 0x1199713-0x119972E 按 n 循环步进，stress 下一帧最多 3 步，帧时间 ≈ 绘制 + n×步进，各阶段的「ms/帧」无法换算成每步成本。3) 'setup: draw 0x11978D0' 是一整块，没拆出 Umbra/镜头 0x118DFF0、作业池冲刷 0x1197A0B→0x1197090、提交。每阶段末尾 0x65F10 里 WaitForSingleObject 的时间混在各阶段里。'outside System::update' 也没按 Application::Update 0x705530 的子调用分桶（unlimited-fps-re.md §2.2 已列出 0x705633..0x7056AE 网络/Steam/EOS、0x7056BA MapMgr、0x7058C7 输入、0x7058B7 HUD；栅栏 0x11A305A→0x111CC20）。4) #330 之后没有任何保存下来的 stress 报告：搜了 main/tmp、各 worktree，都没有 report.md。
- **方案**：tools/perf_report.py：(a) thread_role 增加识别规则：栈上出现 0x65F10/0x65B0E 等待或池线程入口 0x3E630 的是 xgs 池 worker；出现 0x130FE20（processTaskQueue，havok-mt-re.md §12）的是 Havok worker；出现 0x111CC20 的另一侧或 0x51EC2（frame-pacing-re.md 表）的是渲染线程；出现 0xF3xxx 的 ai::World 函数（0xF3AA0/0xF3E60）的是 AI 线程。(b) PHASES 之外加子阶段：draw 内按 0x118DFF0 / 0x1197090 / 其余拆开；每个阶段里叶子在 0x65F10 的样本单列为「等池」。(c) 帧外样本按 0x705530 的返回地址区间分桶。(d) 每阶段的类按 inclusive 列 top N。src/crew.cpp PerfTick：每 5 s 记录 System+0xCE0（GameFrame）的增量，在 PERF 行补上 steps/frame 和 sim ticks/s。perf_report 用它把各阶段换算成 ms/步。然后在当前 main 上跑 perfrun stress:30、stress:100 各一次作为基线，写进 docs/stress-re.md。
- **收益**：本身不提速，但它决定后面所有条目的取舍：拆出原版约 40 ms/帧（stress:100：77.4 − 21.4 ThinkHook − 8.5 Readable − 约 9 Havok）具体在哪个阶段、哪个类。还能看出作业池 worker 是否在干活、游戏线程等池的时间有多少。
- **风险**：低。只改测量工具和 Debug 日志。PERF 行格式变了要同步 perf_report 的 PERF 正则（perf_report.py:38-39），否则旧日志解析会断。

### cpu-2. edf::Readable 每帧 8.5 ms（stress:100）：对游戏自己列表交来的对象改用「本步存活集合」判定，不再逐页问 QueryWorkingSetEx（M · G）

- **问题与证据**：docs/npc-ai-design.md:100：stress:100 采样 Readable 每帧 8.5 ms，PERF MEM 每帧约 8.7 万次询问、1.05 万次工作集调用，共 5.7 ms。实现见 common/memory.cpp:133-161：页缓存是 thread_local，按 GetTickCount64 失效；缓存未命中时调 K32QueryWorkingSetEx。npc-ai-design.md:98 已评估过「加长缓存窗口」并否决（会降低安全性）。之后两轮都是「调用处少问」，治标。另外，页可读不等于对象存活：对象释放后页仍被映射时，Readable 照样返回真，所以它对已释放对象本来就不提供保证。它实际的作用只有一个：在 __try/Fault 之外，或者为了少抛几次 AV，省掉异常的开销。
- **方案**：在 Gather 遍历 TeamManager 时（npcai.cpp:528 附近的帧列表，IndexWorld 写在 __finally 里），顺手把本步所有存活对象的指针放进一张开放寻址哈希集合，复用 common/edf/hash_index.h。凡是「来自游戏对象列表的对象指针」，判断是否可用都改成查这张集合，O(1)，没有系统调用。Readable 只保留给对象内部的二级裸指针（武器表、WeaponSet entry 等），这些继续用 PrefetchReadableSpans 批量问。逐处核对：凡是把 Readable 当「对象还活着」用的地方，用集合判定反而更正确，因为集合按本步列表建，已释放的对象不会在里面。
- **收益**：按已测的 8.5 ms/帧估，预计 stress:100 省 5–8 ms/帧，约 7–10%（推测：剩下的二级指针询问量还没拆分）。60 fps 场景下几乎为 0。
- **风险**：中。对象删除发生在本步 Gather 之后、同一步内被引用时，集合答「活」而对象已经死了。必须核对删除时机：原版的对象删除是否只在阶段边界发生（推测是，未核实）。所有 Readable 调用点都要逐一归类，不能整体替换。

### cpu-3. 插件士兵 AI（ThinkHook，stress:100 每帧 21.4 ms）拆成「并行决策 + 串行执行」两段（L · G）

- **问题与证据**：PR #330 正文：#330 之后 stress:100 下 ThinkHook 仍占 21.4 ms/帧（77.4 ms 帧的 28%）。它挂在 ai_update 串行循环里（0x1199120-0x119913B，逐对象 call [rax+0x38]，即 vtable 槽 7，src/npcai.cpp:2724-2752）。每名士兵的 Think 依次跑 PreThink、原版 nextThink、插件 Think（选目标、ShotClear、PickArm、MoveTo 等）、DriveVehicleAi，全部在游戏线程上。已具备的并行前提：npc_grid 在 Gather 的 __finally 里每帧建一次，之后只读（npc-ai-design.md:96）；Readable 的缓存是 thread_local（common/memory.cpp:63,71,78-80），可以多线程调用。不具备的：Think 里有游戏调用（地图射线、换枪写 d82+i、Havok 同步查询、路线预算 GroundNavigate 的帧轮转状态），这些都不是线程安全的。
- **方案**：在 ai_update 开始前（或第一次进 ThinkHook 时，以 GameFrame 去重），对本步全部受控士兵用 parallel-for 做纯计算：PickTarget 网格查询、Evade/FriendNear、PickArm 打分、ShotClear 的友军网格检查。输入只用 Gather 快照和只读网格，结果写进按士兵索引的数组。各士兵的 ThinkHook 串行读结果，只做游戏调用和写意图。前提是先核对：插件 Think 里有没有「后面的士兵看见前面士兵本步决策」的依赖（如目标分摊、路线预算轮转），有的话那一部分留在串行段。并行池用插件自建的小线程池（第 4 条），不要在游戏 phase 循环中途重入原版 xgs 池。npc_core_check 加一项：并行版与串行版逐项比对，必须相同。
- **收益**：推测：纯计算部分如果占 ThinkHook 的 60–70%，在 8+ 核上 stress:100 可省约 10–13 ms/帧，stress:30 按比例约 3–5 ms。数据以第 1 条测量后 ThinkHook 内部的 self 时间分布为准。
- **风险**：中高。读写分离不彻底就会出现竞态，行为也会和串行版不一致；SEH 的 Fault 处理换到 worker 线程上要另做（__try 不跨线程）。必须带等价性离线检查和实机 A/B。
- **依赖**：第 1 条（确认 ThinkHook 内纯计算占比）、第 4 条（并行基础设施）

### cpu-4. 插件侧并行基础设施：自建固定 worker 池 + parallel-for（不重入原版 xgs 池）（M）

- **问题与证据**：原版 xgs 池（0x1191829 _Thrd_hardware_concurrency()-1，由 0x1191845 调 0x65B90 建，worker 在 0x65F80 里 CreateThread，入口 0x3E630）是同步 fork-join：0x65E90 把 fn/arr/count 写进池 +0x30/+0x20/+0x28，逐 worker ResetEvent+SetEvent；0x65F10 逐 worker WaitForSingleObject(INFINITE)。每个阶段末尾都冲刷一次 System+0x358 的作业表（0x1199174、0x119929D、0x1199499、0x1199633、0x11996D3，以及 draw 里的 0x1197A0B→0x1197090）。这个池的状态是单槽的，任务字段只有一份，插件在阶段中途调用会覆盖原版任务（推测），不能共用。
- **方案**：在 common/ 加一个插件自有的 parallel-for：固定 N = min(hardware_concurrency-1, 8) 个 worker，懒创建；用 WaitOnAddress 或事件唤醒，主线程也参与干活；每个 worker 自带 SEH 包装，把故障带回主线程处理。只从游戏线程调用，调用期间游戏线程阻塞，语义上等同于原版 phase 内串行。退出顺序参照 havok-mt-re.md §9：先 close，再等 worker 退出，再释放。加离线压力检查。
- **收益**：本身不提速，是第 3 条的前提；以后 rubble_pass、enemy_points 等每步共用的扫描也能用。
- **风险**：中。三个池（原版 xgs 15、Havok 7、插件 N）同时醒着会超订核心，所以插件池只在 ai_update 段内使用，那时另外两个池都空闲（阶段末才冲刷；Havok 只在 havok_update 段跑）。

### cpu-5. 按类定位原版 ai_update / object_update 的热点，先找算法问题（O(n²) 搜索、重复查询），用钩子修，而不是并行化（M · G）

- **问题与证据**：stress:100 下原版自己约 40 ms/帧没有拆分（见摘要的算术）。这几段都是对象列表上串行的虚函数调用：ai_update 遍历 [System+0x488]、槽 0x38（0x1199102-0x119913B）；pre_update 遍历 [System+0x468]、槽 0x20（0x1199237-0x119926B）；object_update 槽 0x28（0x119940D）；camera_update；外加 [System+0xCC0] 列表的槽 0x30（0x11991BE-0x11991F5）。perf_report 的 phases() 已经能按 vtable 类给出「阶段 > 类」的占比（perf_report.py:370-413），但从来没有对 #330 之后的 stress 跑过。docs/stress-re.md 只记了插件和 Havok 的占比。蚂蚁（1000+ 只）的 Think 是否有随敌我数量平方增长的目标搜索，未知。
- **方案**：拿第 1 条的报告，取原版每步 self 时间前 5 的函数，用 vanilla-code（vanilla.py c/refs）逐个读。如果是对全体对象的线性扫描（选目标、避让、群聚），按插件 npc_grid 的做法，用钩子换成网格查询，答案必须与原版逐项相同，并配离线比对检查。是纯数学且只写自身的，再考虑放进第 4 条的池。新发现写回 vanilla-code 并提交（按用户要求）。
- **收益**：未知，取决于测量。如果原版蚂蚁或士兵的选目标是 O(n·m)，在 3000+ 敌人、2000+ 友军的规模下可能有十几 ms 级的收益（推测）。
- **风险**：中。改原版逻辑必须证明逐项等价，否则联机两端行为会和原版不一致。
- **依赖**：第 1 条

### cpu-6. Havok 只拿到 7 个 worker：查清池大小的来源，评估与 xgs 池（15）对齐（S · G）

- **问题与证据**：havok-mt-re.md:294：16 线程机器上 Havok 池只给出 7 个 worker，而原版 xgs 池是 hardware_concurrency-1 = 15（0x1191829）。havok_update 在 stress:100 仍约 9 ms/帧，按 3 步算约 3 ms/步（§13 表）。另外，MTQueryQueue 的批量射线（§5）也在这同一个池上并行。插件只负责把已建好的池接上队列（0x119932F BeforeStep → 0x130FE20），池的大小由原版在别处决定，没有逆向过。
- **方案**：用 vanilla-code 追 hkCpuThreadPool 的构造（holder 0x20B30A0 的 [G+8]，vtable 0x1AFED80）和传入的线程数。看是不是 hc/2-1、上限是多少。如果能在构造前改参数（等长 imm 改写，全有或全无，并加签名审计），就用 perfrun --ab 比较 7 与 15。
- **收益**：推测：约 1–3 ms/帧（stress:100）。物理已经并行化，求解有串行段（三个世界级任务单实例），worker 加倍收益递减。
- **风险**：低到中。线程数影响 hknp 多线程求解的确定性，而联机逐位一致本来就还没验证（§13 第 5 项）；worker 多了也可能和 xgs 池抢核。

### cpu-7. 原版 xgs 池每阶段 fork-join 的同步成本：测量 SetEvent/Wait 往返，判断是否值得改成「无作业不唤醒」或自旋等待（M · G）

- **问题与证据**：每步至少 4 次阶段冲刷（0x1199174/0x119929D/0x1199499/0x1199633，另有 0x11996D3、0x1197090）。每次 0x65E90 对 15 个 worker 各做一次 ResetEvent+SetEvent，0x65F10 对 15 个 worker 各做一次 WaitForSingleObject，即每步约 6 × 45 次内核调用，外加 15 次线程唤醒和调度延迟。调用前有 [r13+0x18]≠0 的检查（0x1199159-0x1199160），作业表为空时不唤醒，但每个 worker 实际分到多少活不知道。
- **方案**：先测量：第 1 条把游戏线程在 0x65F10 里等待的样本单列，并统计 worker 的 CPU。如果「等池」占比高而 worker 的 CPU 低，说明分片太细或唤醒延迟大。可选的根因方案是钩 0x65E90/0x65F10，改用 WaitOnAddress，或者作业数少于阈值时由游戏线程自己执行（0x118FC00 本来就是逐项调槽 1 的串行循环）。必须先核实 worker 端的协议（线程入口 0x3E630 → 对象 vtable[0]）。
- **收益**：推测：0–2 ms/帧，没有数据之前不动。
- **风险**：中。改的是原版同步原语，有死锁风险；退出和复位顺序要比照 havok-mt-re.md §9 审计。
- **依赖**：第 1 条

### cpu-8. 插件每步重复的工作：区分「每 tick 必须」和「每帧一次即可」（S · G）

- **问题与证据**：插件逻辑以 GameFrame（System+0xCE0，在 0x1198FD0-0x1198FDA 每步 +1）去重（unlimited-fps-re.md §2.1），所以 stress 下每帧会跑 3 遍（n≤3，0x11A2F76）。这在模拟语义上是对的。但有些工作只影响呈现或诊断（地图 UI、HUD 数据准备、调试统计），每步都做会白白乘 3。目前没有清单。
- **方案**：第 1 条有了 ms/步之后，按插件函数列出每步成本。逐个判断依赖：只给渲染或 UI 用的数据，改成以「帧」去重，在 Application::Update 帧路径或绘制前执行一次；凡是影响模拟或联机同步的，保持每 tick。
- **收益**：推测：stress 下几 ms；60 fps 下 n=1，基本为 0。
- **风险**：低到中。把某个影响模拟的计算错归到「每帧」，就会在追帧时跳步，联机结果也会不同。
- **依赖**：第 1 条

### cpu-9. 不建议：直接并行化原版对象的 Think / Update 虚函数循环（XL · G）

- **问题与证据**：ai_update/pre_update/object_update 循环里的原版虚函数（例如 0x1199131 call [rax+0x38]）会写共享状态：ai::World 的分配表在 +0x58 锁内，而 0xF3E60 在 AI 线程上不加锁读（aiworld-re.md:59）；TeamManager 红黑树（0x5E11D0）；Havok 同步查询；对象生成和删除；声音；RagdollManager（stress-re.md 布娃娃一节）。havok-mt-re.md §1 能放心并行，靠的是 Havok 自己保证世界级任务单实例、并且审计过 37 个回调；原版对象循环没有这样的保证，也没有审计。
- **方案**：不做整体并行。只对第 5 条测出来的、经逐函数审计证实「只读全局、只写自身」的纯计算子段，用钩子剥离出来放进第 4 条的池。
- **收益**：避免引入难以复现的竞态崩溃和联机分歧。
- **风险**：如果硬做：随机崩溃、联机分歧，而且无法离线证明正确。

### cpu-10. 不建议（现阶段）：渲染准备与模拟流水线化；D3D11 提交另开线程（XL · G）

- **问题与证据**：System::Update 先在游戏线程上绘制（0x1198FA4 → 0x11978D0 → 0x1197B50 → 0x118DFF0，读镜头 +0x220、交给 Umbra、建绘制），再步进；帧末 0x11A305A 等渲染栅栏 0x111CC20（unlimited-fps-re.md §1）。渲染线程已经存在，frame-pacing-re.md 的采样显示它大部分时间在等游戏线程的命令（0x51EC2）。stress 下 GPU 3D 只占 3–4%（stress-re.md 表），渲染不在关键路径上。要让绘制和下一步重叠，就得把场景双缓冲（绘制读的对象正是步进要写的），工作量相当于不限帧率阶段 3–5 的插值快照。
- **方案**：不单独做。等不限帧率阶段 3–5 建好「上一步 / 这一步」的渲染快照以后，再评估让绘制读快照、与步进重叠。在那之前，只用第 1 条量出 draw 子段（Umbra/提交）占游戏线程的比例。
- **收益**：现阶段 stress 的瓶颈在步进，这条收益很小。60 fps 场景下也已经顶在 60 帧。
- **风险**：高。读写竞态，Umbra 数据库被破坏（参考 feedback-2026-10-25-greymap.md 里越界盒子毁掉 Umbra 的先例）。

### cpu-11. 不限帧率阶段 2 之前先量 n=0 帧的成本（绘制 + Umbra + 栅栏）（S · G）

- **问题与证据**：unlimited-fps-re.md §3 只有第三方的说法（L 级）：n=0 帧约 9.1 ms，Umbra processVisibility 每帧约 3 次、每次约 3 ms，图形栅栏约 7 ms。我方没有自测。§5 的阶段 6（性能）还没做。如果 n=0 帧本身就要约 9 ms，解耦后中端机最多也就 90 帧左右，绘制路径的单线程成本会成为新的瓶颈。
- **方案**：在第 1 条的 draw 子分区基础上，用 perfrun 跑 M001 并带 Debug 日志。这一步不需要接帧门：直接把 draw 子段（0x118DFF0 / 0x1197090 / 提交）的 ms/帧和渲染线程的 CPU 当作 n=0 帧成本的下限。结果写进 unlimited-fps-re.md §5 阶段 6，决定阶段 2 之后先优化 Umbra 还是先上插值。
- **收益**：给不限帧率的上限定量，避免做完阶段 2–5 才发现帧率上不去。
- **风险**：低，只做测量。
- **依赖**：第 1 条

## 插件旧功能的每帧开销与可维护性（mod）

现状：两个插件的热点在 11-07/08 已经修过一轮：enemy_points 让各侧敌人每帧只抄一份给炮手用，npc_grid 每帧建网格，WatchTarget 观测和询问分开，Readable 改成页缓存，日志改成异步双缓冲。但 docs/stress-re.md 里「插件 ThinkHook 占游戏线程 60%」是修复前的数据，修复后插件实际占多少一直没测过（已核实：文档和 11-08 之后的提交里都没有新的剖析数据）。所以第一件事是重新压测，拿到真实占比，不要凭旧数据决定要不要上多线程。

静态能确认的最大问题（都已核实）：
（1）改 ini 后的重载在游戏线程上同步跑：124KB 的 ini 一次重载要调 455 次以上 GetPrivateProfile，外加按支援单位循环读的若干轮，每秒还在游戏线程上查一次文件时间；
（2）敌人 / 友军的遍历仍然各模块各走各的：heli、proteus、barrage、floatgun、gravity、mapcmd 等十几处每帧直接走锁定登记表或队伍管理器，AutoTurret 自己再抄一份，它的帧计数还是靠「同一辆车第二次出现就算新一帧」来猜；
（3）裸地址严重重复：kObjectMgr 0x20B2958 定义了 27 次，kTeamManager 0x20B2978 约 20 次，Relations() 有 3 份；全仓库通用的射线和敌人遍历层埋在 4161 行的 heli.cpp 里。

建议顺序：先 1（实机复测，定优先级）；并行做 2、5、9（纯离线、低风险）；再做 3、4（统一每帧世界快照）；12（多线程 AI）等 1 的数据出来再决定。本轮没有改文件、没有开游戏、没有跑测试。

### mod-1. 1. 实机复测：插件在修复后占游戏线程多少（其余各条按这个排先后）（S · G）

- **问题与证据**：docs/stress-re.md:22-30 记的是 2026-11-07 的数据：stress:10/30 时插件 ThinkHook 占游戏线程 63%/60%，其中 WastedFire 58%/52%。之后合进了一批优化（git log：b8925d95 炮手共用每帧敌方点表，2bd0ae49/24a63dd6 npcai 改查每帧网格，ccec249e WatchTarget 观测与询问分开，common/memory.cpp:7-28 的页缓存），但修复后插件的占比在文档和提交里都没有新数据（已核实）。docs/havok-mt-re.md §13 只说明 stress:30 开 Havok 多线程后是 47.87 ms，其中 havok 约 5 ms，剩下约 43 ms 是谁的没有拆。
- **方案**：按 CLAUDE.md 先排队拿游戏（gamequeue hold），用 tools/perfrun.py 在当前 origin/main 上跑 stress:10/30/100，Debug=1。在 perf_report 的「Plugin functions」里记下 ThinkHook、WastedFire、PickTarget、GunnerVisit、Readable（含 PERF MEM 行的 asks/wsCalls/VirtualQuery 每帧开销，crew.cpp:857-882），以及 heli 的 ForEachEnemy 和 CastRay 各占多少，写进 docs/stress-re.md 新开一节。
- **收益**：决定第 3、10、12 条值不值得做：如果插件已经降到 10% 以下，多线程 AI 就不必做。
- **风险**：只测不改。注意多个会话共用游戏目录，看结果前先核对 DLL 的 sha256

### mod-2. 2. 配置重载移出游戏线程：只读一次 ini 文件，并改成后台监视（M）

- **问题与证据**：crew.cpp:942：每辆车的 InputHook 每帧都调 ReloadConfigIfChanged；common/host.cpp:33-41 每秒在游戏线程上做一次 GetFileAttributesExW。ini 一变，plugin.cpp:1268-1270 就在游戏线程上同步执行 LoadConfig（plugin.cpp:534 起，用 awk 数出约 455 次 ReadBool/ReadInt/ReadFloat，每次都是一次 GetPrivateProfile*，见 plugin.cpp:102-127），接着 LoadSupportConfig（support_config.cpp:146-185）按支援单位数再循环三轮 GetPrivateProfileStringW。仓库里的 EDF6VehicleCrew.ini 有 1416 行、124393 字节，每次 GetPrivateProfile 都要重新打开、扫描整个文件。游戏内配装保存走 loadout_store.cpp:72 的 ReloadConfigNow，也会触发一次全量重载。重载耗时没有测过，推测在几十到上百毫秒，会造成一次卡顿。
- **方案**：(a) 加一个 IniTable：ReadFile 读一次，解析成「节→键→值」表，按 GetPrivateProfile 的语义做（键大小写不敏感、重复键取第一个、去掉首尾空白和引号），ReadBool/ReadInt/ReadFloat/ReadLanguage 和 support_config 的 read 回调（support_config.cpp:193-197 已经抽象成回调）都改查这张表；(b) 文件监视改成后台线程用 ReadDirectoryChangesW / FindFirstChangeNotification，在后台把新的 Config 建好，再用现有的原子指针发布（plugin.cpp:81-84、1150-1151）。SetSupportUnitOnAllies 要写代码洞，必须留在游戏线程执行：发布时置一个标志，由 SharedFrameTick 去应用；(c) 删掉 InputHook 里每辆车都调一次 ReloadConfigIfChanged 的写法。加离线检查：同一份 ini 分别用旧路径（GetPrivateProfile）和新路径读，逐个字段比对。
- **收益**：游戏线程上的 ini 访问从「每秒一次查文件时间，变更时 500 次以上文件解析」降到 0；配装保存和改 ini 不再卡帧（卡多久没测过，属推测）
- **风险**：IniTable 的解析和 GetPrivateProfile 有语义差异时，某个配置会被悄悄改值，所以离线逐字段比对是必须的。另一个风险是 KeyConfigTick 的 RepublishStockKeys 也会发布 Config，和后台发布可能交错，需要统一由一处写入

### mod-3. 3. 每帧世界快照只建一份：敌人、友军按阵营各遍历一次，所有消费者都读这份（L · G）

- **问题与证据**：敌人表每帧被不同模块各自走一遍（链表遍历，每个节点都会 cache miss）。npcai Gather 走一次（npcai.cpp:528-560）；炮手用 PointsThisStep 再走一次（heli.cpp:4030-4046）；直升机每架每帧还要现场走 ForEachEnemy（heli.cpp:862 ForEachTarget、870 EnemyInLine、2014 是否在降落、3350 指挥）；另外 barrage.cpp:273、floatgun.cpp:411/619、gravity.cpp:287/597、gravitygun.cpp:197、mapcmd.cpp:795/837/866、edf41mother401.cpp:2189、carrierlaser.cpp:479 也各走一次。友军方面，npcai.cpp:545、heli.cpp:748 的 FriendsNow、proteus.cpp:403（每个展开的 Proteus 每帧一次）、sidecar.cpp:301 各自在队伍管理器的锁下走一遍。enemy_points.h:5-6 的注释记录过修复前的数字：仅炮手遍历就要 3.7 ms/帧（stress:100）。
- **方案**：新建 src/world_snapshot.{h,cpp}：在每个 GameFrame 第一次被询问时建好「各阵营的敌方锁定点」（enemypoints::Collect）、「各队伍的友军」（一次 kEnumAllTeams）以及 npc_grid 网格。npcai World、heli 的 StepPoints/FriendsNow、proteus 和上面列出的每一处都改成读这份快照，并删掉 VisitEnemiesOf / ForEachEnemy 的现场遍历回退分支（heli.cpp:4049-4055）。对象死亡照旧用 Visit/Nearest 里的 kDead 检查来过滤。扩展 tools/enemy_points_check.cpp：快照访问和现场遍历要逐项一致。
- **收益**：每帧对敌人表的完整遍历从十几次降到一次，大规模时敌人上千、节点上千。具体能省多少毫秒要等第 1 条的数据（推测为数毫秒）
- **风险**：有的消费者依赖「本帧刚生成的敌人立刻能看到」，例如帧中途刷出的敌人；mapcmd、gravity 有些在帧中途调用，要确认读到上一快照是可以接受的。WatchTarget 目前在两处被调用（npcai.cpp:557、heli.cpp:4044），并入快照后只保留一处
- **依赖**：1

### mod-4. 4. AutoTurret 改用真实场景帧时钟，按网格找敌人，Relations 和遍历代码收进 common（M · G）

- **问题与证据**：autoturret/src/plugin.cpp:100-166 的帧计数靠「同一辆车的输入第二次出现就算新一帧」（frameSeen[64]）。这正是 src/game_frame.h:5 注释里说的「对象枚举不是时钟」，而 VehicleCrew 已经有 gameframe::Clock。RefreshWorld（plugin.cpp:220-243）按这个猜出来的帧刷新，然后 ScanEnemies（plugin.cpp:251 起）对每辆车把全部敌人线性扫一遍，每个敌人都做一次 ToLocal，复杂度 O(车×敌)。Relations() 一模一样地有三份：autoturret/src/plugin.cpp:209、src/heli.cpp:649、src/damagestats.cpp:103；锁定登记表的遍历在 plugin.cpp:224-242 和 src/enemy_points.h:27-43 各写了一份。
- **方案**：把 game_frame.h 的 Clock 和读场景步进计数的那段代码、enemy_points.h（Walk/Collect）、npc_grid.h、Relations 都移到 common/edf/ 下。AutoTurret 改用场景步进计数做帧时钟，RefreshWorld 改调 common 的 Collect，ScanEnemies 改成按射程查网格。
- **收益**：同一帧里某辆车输入走两遍时不会误判成两帧、重复刷新快照（这是正确性问题）；扫敌开销从 O(V×E) 降到 O(V×附近格子数)；删掉三份重复代码
- **风险**：AutoTurret 的 GunFuse「本帧或上一帧」判定（plugin.cpp:91-94）依赖帧号；换时钟后要确认两种节奏下语义不变

### mod-5. 5. 原版地址集中登记，原版遍历的胶水代码只留一份（M）

- **问题与证据**：已核实的重复：kObjectMgr/kSystem/kHavokGlobal=0x20B2958 有 27 处 constexpr 定义；kTeamManager/kTeams=0x20B2978 约 20 处（carrierlaser.cpp:60、commander.cpp:26、crew.cpp:842、damagestats.cpp:47、enemy_control_target.cpp:12、heli.cpp:82、map.cpp:73、mission_crew_radar.h:37、mission_participants.cpp:9、net_object_resolve.cpp:14、npcai.cpp:93、proteus.cpp:67、pvp_native.cpp:21、sidecar.cpp:95、subcarrier.cpp:79、support_points.cpp:18、support_source.cpp:10、team_native_logic.h:47、vehicle_target.cpp:26、autoturret/src/turret.h:125）。kTeamWalk/kEnumAllTeams/kOneTeamWalk 加上「伪 vtable 仿函数 + reinterpret_cast<WalkFn>」这套代码复制了 9 处以上（commander.cpp:96、map.cpp:484、mission_participants.cpp:75、net_object_resolve.cpp:148、npcai.cpp:545、proteus.cpp:403、pvp_native.cpp:215、sidecar.cpp:301、support_source.cpp:52）。共有 237 个文件里有 5 位以上的十六进制 constexpr，而 common/edf/layout.h 只有 44 行。
- **方案**：新建 common/edf/rva.h：全局单例地址和原版函数的 RVA，函数附带签名字节，统一在启动时用 Matches 校验一次。新建 common/edf/teams.h：Relations(team)、TeamWalk(team,F&&)、EnumAllTeams(F&&) 模板，内部生成仿函数 vtable，并在 __try 里调用。各模块只做机械替换（适合派 worker），每批改完都跑 check.py。
- **收益**：维护性：每个地址只在一处定义、只校验一次签名；新功能不再复制遍历胶水；减少约 30 份重复代码
- **风险**：低。属于纯重构，离线检查和源码守卫（*_guard.py）可能因为匹配源码文本需要跟着更新

### mod-6. 6. 把射线和敌人登记表这一层从 heli.cpp 拆成独立的世界查询模块（M）

- **问题与证据**：全仓库共用的物理查询都写在 4161 行的 heli.cpp 里：CastRay/CastRayBodies（heli.cpp:302-440）、MapRay/MapFloorRay/BuildingRay/GroundClearance（heli.cpp:3924-4014），还有敌人登记表、Relations、VisitEnemiesOf、VisitLockPoints（heli.cpp:649-690、4018-4087）。heli.h 被 23 个文件直接包含，crew.h 末尾也包含了 heli.h，而 crew.h 被 246 个 .cpp 中的 213 个包含（已核实）。射线调用点分布在 heli 26 处、npcai 13 处、jet_flight 8 处等，没有每帧计数，也没有预算。
- **方案**：新建 src/world_query.{h,cpp}，把上面这些挪进去，heli.cpp 只保留直升机逻辑。在 CastRay 里加每帧射线计数，输出到 PERF 行（crew.cpp:857 PerfTick）。这一步也是给以后批量或异步射线留接口。
- **收益**：维护性和编译耦合；射线开销能看见了，可以作为以后做射线预算或批量的依据
- **风险**：低，纯搬迁。rayOk 和签名初始化的顺序要保持不变

### mod-7. 7. 跨线程 / 跨钩子交接时不再按墙钟判「新鲜」，改用帧序号（M · G）

- **问题与证据**：约 25 处用墙钟窗口判断交接的数据还算不算新鲜：infantryscope.cpp:27（200 ms，墙钟）、helisight.cpp:52 和 launcher.cpp:34（200 ms）、map.cpp:113（300 ms，墙钟）、heli.cpp:2493/2530 与 drill.cpp:173、emc.cpp:105（300 ms）、hud.cpp:129 和 enemy_control_radar.cpp:15（500 ms）、npcai.cpp:4593（GetTickCount64()-markPub.wall<=500）、payload.cpp:1239、ultimate.cpp:785。havok-mt-re.md §13 实测 stress:100 平均帧时间 108-124 ms，最差帧还会更长：一次卡顿超过 200 ms，对应的 HUD 读数就会被当成过期、消失一帧（这是推测，没有实测到闪烁）。另外不限帧率阶段 3 以后，绘制和模拟的频率会分开（unlimited-fps-re.md:162），墙钟窗口在两种节奏下都不可靠。
- **方案**：发布方在每份数据上盖「场景步进序号」（gameframe::Clock），读取方的判据改成「上一个完成的步进发布的」；暂停、加载、地图打开这几种情况用显式状态表示，不再靠超时来推断。可以先从 hud.cpp 的读数和 npcai 的标记这两组改起。
- **收益**：低帧率和卡顿时 HUD / 标记不再闪；解除不限帧率阶段 3 以后的一个阻碍
- **风险**：有的窗口本来就用来表示「玩家下车了」「地图关了」这类状态迁移，改之前要逐个核对它的语义，避免引入残影

### mod-8. 8. 日志统一成 common 里的异步双缓冲，去掉 AutoTurret 在游戏线程上的同步写盘（S）

- **问题与证据**：AutoTurret 的 Log（autoturret/src/plugin.cpp:138-146）经 common/append_log.h:13-22 在调用线程上，在 SRW 锁内直接 WriteFile。它的 Log 调用有 50 处（plugin.cpp 34、designate.cpp 8、gunner.cpp 8），游戏线程会被磁盘或杀软扫描拖住。VehicleCrew 已经有一套更好的实现：src/log_buffer.h 加 src/plugin.cpp:1170-1262，两块 1MB 缓冲、50 ms 写盘线程、日志轮转、崩溃时直接追加。两个插件用的是两套实现。
- **方案**：把 log_buffer.h 和写线程、轮转逻辑挪到 common（edf::AsyncLog），两个插件都用它；AutoTurret 的崩溃路径用 AppendCrashLine。顺手把 AutoTurret 日志的时间戳补上毫秒，和 VehicleCrew 对齐。
- **收益**：AutoTurret 的日志不再在游戏线程上做 IO；日志只剩一套实现
- **风险**：低。AutoTurret 进程退出前要 Flush，否则最后 50 ms 的日志会丢

### mod-9. 9. 每辆车的输入钩子按类型注册所需步骤，并用一张表找各模块的车辆状态（L · G）

- **问题与证据**：crew.cpp:921-982 的 InputHook 对每辆车每帧无差别地调约 45 个 Guarded 步骤，每一步自己判断车型，例如 HeliStep 先 IsHelicopter，SubStep 先 BodyOf；然后在自己的表里线性查找：heli.cpp:625（helis[16]）、crew.cpp:185（states[128]）、seat_turret.cpp:61、gear.cpp:52、payload.cpp:196、sazabi.cpp:116、dreadnought.cpp:121 等十几张表。步骤之间的先后约束（Seats 必须在前，Ram 必须在 Drill/Sidecar 之后等）只写在注释里。
- **方案**：InputHook<I> 本来就是按类型实例化的模板：在安装钩子时按 kClasses[I] 和机体生成本类型实际需要的步骤表，步骤用声明式注册，带上先后依赖，并加 static_assert 或启动检查。再建一个按对象查的上下文，用 common/edf/hash_index.h 一次查表拿到各模块的状态槽，替代各模块自己的线性 Find。
- **收益**：推测每辆车每帧能省几十次空调用和线性查找，但量级是微秒，所以收益主要在维护性：步骤顺序从注释变成可以检查的约束
- **风险**：顺序约束迁移时漏掉一条，就会出现隐性的行为变化；需要实机回归各种车型
- **依赖**：5

### mod-10. 10. 收紧 Readable 的用法：已知存活的对象不再每次都问内存页（L · G）

- **问题与证据**：src/autoturret 中共有 2151 处 Readable( 调用，分布在 npcai.cpp 89、enemy_control_pvp_native.cpp 74、enemy_control_native_pose.cpp 69、enemy_control_weapon_aim.cpp 66 等。common/memory.cpp:7-28 记录过它曾经占游戏线程 35-47%（约 7 ms/帧）；现在已经有每线程页缓存和 PrefetchReadable，但缓存按 GetTickCount64 的 tick（约 16 ms）失效，在 stress 场景（131 ms/帧）下每帧仍会重问约 8 次（memory.cpp:20-21 注释）。很多指针来自原版本帧的遍历，或者已经有 ObjRef（crew.h:801）确认身份，再查一次页属性是多余的。
- **方案**：先看第 1 条复测里 PERF MEM 行的 asks/帧和耗时。如果仍然明显，就定一条规则：从世界快照（第 3 条）或原版遍历拿到的对象指针，在本步进内视为可读，读成员时只用 At<> 加 __try；Readable 只留给来源不可信的指针（网络对象 id、旧帧缓存、跨模块传来的指针）。按文件逐步收敛，从调用最多的 npcai 和 enemy_control_* 开始。
- **收益**：取决于第 1 条：如果 PERF MEM 仍有每帧上千次 asks，收益是毫秒级；否则以降低噪音和可读性为主
- **风险**：去掉检查后，对象被原版在同一步进里提前释放时会读到野指针；__try 能兜住访问违例，但兜不住读到被别的对象复用的内存。所以只能用在「本步进由原版遍历产生」的指针上
- **依赖**：1,3

### mod-11. 11. 拆 crew.h 这个总头文件和几个超大源文件（L）

- **问题与证据**：src/crew.h 有 1631 行，被 246 个 .cpp 中的 213 个包含；它末尾还连带包含 core/ground/heli/hud/nix/vhud/mapbounds/payload/map/damagestats/mapcmd/proteus/npcai/qmark/infantryscope 共 15 个模块头（crew.h 尾部，已核实），改任何一个都几乎全量重编。超大文件：hud.cpp 6056 行、npcai.cpp 4605、heli.cpp 4161、firstperson.cpp 2768、edf41robo.cpp 2596、edf41mother.cpp 2590。enemy_control_boss_gun_net.cpp:93 和 enemy_control_walking_parts.cpp:92 的 FindRoot 逐字相同。
- **方案**：crew.h 只保留真正的核心内容（ObjRef、Cfg、Log、PlayerFix、帧时钟），删掉末尾那 15 个模块头的连带包含，各 .cpp 按实际需要包含。hud.cpp 按读数收集、绘制、文本拆开；npcai.cpp 按快照、选目标、移动、指挥拆开。重复的小函数提到共享头里。
- **收益**：增量编译时间明显下降（推测：改一个模块头从全量重编约 213 个文件降到几十个）；减少审查和合并冲突
- **风险**：低，纯结构调整。*_guard.py 源码守卫是按文件路径匹配的，要跟着改
- **依赖**：6

### mod-12. 12. （有条件）NPC 士兵 AI 的决策放到工作线程，并行处理（XL · G）

- **问题与证据**：推测项。只有当第 1 条复测显示 ThinkHook 仍占游戏线程大头时才值得做（修复前是 60%，docs/stress-re.md:30；stress:30 时游戏线程单核跑满、GPU 只用 3%）。现在 Gather 已经每帧把敌人和友军抄成快照、建好网格（npcai.cpp:522-560），选目标是 npc_logic.h 里的纯函数。但 wastedfire.cpp 的 shooterMemo/TargetMemo/plans 都是没有加锁的全局 memo（wastedfire.cpp:380-400、436-449），HoldFire 内部还会发射线，这些都不能直接在别的线程上跑。
- **方案**：分三段：游戏线程上建快照（Gather，再加上 WastedFire 需要的目标窗口和运动观测）；工作池按士兵分片，只读快照、只算决策（选目标、移动目标），射线请求先排进队列；回到游戏线程统一执行射线和写游戏对象。线程池用插件自己的固定线程，不要借 Havok 的 worker。
- **收益**：推测：若 AI 决策仍占游戏线程约 40% 以上，8 核机器上能把这部分降到原来的 1/3 到 1/4
- **风险**：高：离开游戏线程读游戏内存、memo 的线程安全、决策晚一帧生效带来的行为差异。必须先完成第 3 条，并且把决策做成纯函数加离线等价检查
- **依赖**：1,3

## 联机（net）

现状：插件联机消息都走 EDF6Coop 扩展通道（ABI v1，只有 send 和 poll 两个入口）。所有消息都是 ReliableOrdered，而且和游戏流量共用每条链路上同一个序号空间。Coop 那边的游戏流量已经做过 W1/W2（状态包分类、玩家外推、兴趣管理），插件这条通道还停在「全部可靠有序、失败就整体关掉」的阶段。
最大的风险在 Coop：扩展通道一次积压超限（1 MiB 或 8192 包，游戏流量也算在里面）或收件箱超过 4096 条，就会置 extensionFault_。这个标志只有房间或成员变了才清，所以本关剩下的时间里，插件的全部联机功能（支援、怪物操控、重力枪等）都不再可用。10-30 那次修的「瞬断后恢复」覆盖不到这条路径。
其次：插件里以 NetId 指名的引用，每解析一次都要全世界走一遍（5E0C80）。怪物操控每帧对每个活动租约走一次，客机每收到一个 33 ms 的姿态包也走一次。
另外，插件节奏全按 GetTickCount64（15.6 ms 粒度）算，33 ms 的采样按算术推导实际约 50 ms 一次（这一条是推测）。
已核实不是问题的：游戏收包钩子（12C8CC0 传 RequestedChannel=null，pop 是 O(1)），以及 DirectNet 的解码在锁外做。
建议先做：①补房主侧按通道的 PERF 计数（缺房主数据，现在不能说「已修好卡顿」）；②修 Coop 的粘滞故障；③给 NetId 解析加每帧缓存。之后再做协议级的「扩展状态类不可靠、只留最新一份 + 独立有序域」。
需要升级的：联机线协议要全员同版本时，按规则由房主拒绝旧版本进房，不写兼容分支。
本次只读：未开游戏，未跑测试。vanilla.py 打不开数据库（报 unable to open database file），改为直接读 assembly/EDF.dll.asm 做的核验。

### net-1. 先补房主侧联机性能数据：按通道统计消息数、字节数和每帧世界遍历次数（S · G）

- **问题与证据**：docs/feedback-2026-10-30-online-lag-support.md 第 24-31 行写明手上只有客机日志，「房主侧的卡顿还没有实测，不能下结论说已经修好」。现有的 PERF NET（src/support_net.cpp:159-162、542）只记支援 tick 的总耗时、收到的消息数和通道帧数，不分通道，也不记字节数和 ResolveNetRefs 触发了几次世界遍历。tools/perfrun.py 和 tools/perf_report.py 里 grep 不到 net 或 coop 字样，这两个工具不覆盖联机。
- **方案**：plugin_net::Receive / Broadcast / Send（src/plugin_net.cpp:110-142）按 Channel.name 累加收发条数和字节数。net_object_resolve.cpp 的 WalkWorld（:144）累加每帧的遍历次数和耗时。这些都并入 PERF NET 那一行（每 5 秒一行）。perf_report.py 增加解析 PERF NET 和 EDF6Coop 的 STATS 行。
- **收益**：后面各项的收益能拿到实测基线。现在除 10-30 那次以外都只能按代码推算。
- **风险**：只加 Debug=1 时的日志，行为不变。

### net-2. Coop 扩展通道的 extensionFault_ 是粘滞的：一次积压或收件箱溢出，本关剩余时间里插件联机全部失效（M · G）

- **问题与证据**：D:/APP/edf6_directnet/src/extension_transport.cpp 有两处置故障。:147-151，failClosed 发送超过 kPendingBytes=1 MiB 或 8192 包时置 extensionFault_=true；这个计数包含游戏自己的流量，插件的 api.send、以及房主替客人中转扩展消息的 sendExtensionDataLocked（:139、:209）都是 failClosed。:212-215，收件箱达到 kInboxMessages=4096 时同样置故障。之后 refreshExtensionLocked（:70）要求 !extensionFault_，而清除只在 setExtensionRoom 判定 room/host/members/worldEpoch 变化时（:37）和 clearExtensionRoom（:57）。也就是说，同一关里故障永远不会被清。AF 侧 RunSupportNetTick（src/support_net.cpp:487-492）只会一直挂起，10-30 加的「恢复同一房间」路径（同文件 :475-480）永远等不到 ready。另外，扩展消息都是 reliability=2，不会过期（reliable.h 中 expiresMs=0），链路一卡，旧的姿态和状态包全都留在积压里，正好把它推过上限。
- **方案**：改 Coop 侧。超限时不设粘滞故障：本地发送对插件统一返回 WOULD_BLOCK（调用方本来就要处理 send 失败）。中转超限时只丢这一条，换 generation 一次（AF 会看到 generation 变化然后走已有的恢复路径），积压降到阈值以下后自动恢复 ready。收件箱溢出也一样：换 generation、清空收件箱，不设粘滞。在 docs/extension-transport.md 写明「故障只换 generation，不封死」。AF 侧 support_net.cpp 的最低 Coop 版本跟着提高，房主拒绝旧版 Coop 进房。
- **收益**：消除「联机打着打着，支援、怪物操控、重力枪全部不能用，要重开关卡」这一类失败（10-30 反馈里的第二个症状，代码路径上还有这一个根因没修）。
- **风险**：generation 变化会让各通道 Reset，正在飞的事务会中断（已有恢复语义，只要核对怪物操控租约能正确失败并恢复原状）。

### net-3. 按 NetId 指名的引用每解析一次就全世界走一遍：怪物操控每帧对每个租约都走，客机每收到一个姿态包也走（M · G）

- **问题与证据**：src/net_object_resolve.cpp:229-245：只要查询里有 native（NetId）引用，cached 就是 false，必然 WalkWorld，即原生全队伍枚举 5E0C80（:144-153），只有纯玩家引用才命中帧缓存。src/enemy_control_net.cpp:325-331 的 EnemyControlNetworkFrame 每帧对每个 active 租约调 ResolveActors，ResolveActors 再调 ResolveCommandIdentities，并且带 native 的 focus，所以每个租约每帧走一遍（kMaxLeases=16，见 enemy_control_protocol.h:31）。客机收姿态包时（enemy_control_net.cpp:171）也调 ResolveActors，房主那边 33 ms 发一个，于是又多一遍。同样会走的地方还有 enemy_control_target.cpp:240、enemy_control_projectile_native.cpp:186、qmark.cpp:72、gravity_net.cpp:113。敌人越多，每遍越贵（O(全场对象)）。
- **方案**：在 net_object_resolve.cpp 里加每帧（GameFrame）的 NetId 解析缓存。同一帧里第一次遍历时，把本帧所有已查询的 NetId 结果，或者整张 NetId 到 ObjRef 的表（固定容量开放寻址）记下来；后续查询先查缓存，再用现成的 StillNetRef 复核弱引用和 NetId，复核失败才重走，和现有玩家缓存的契约一致。另一种做法：EnemyControlNetworkFrame 在租约存续期间缓存 Actors，每帧只做 StillNetRef 复核。
- **收益**：每帧遍历次数从「活动租约数 + 本帧收到的需解析消息数」降到最多 1 次。单次遍历的耗时没有实测，要第 1 项的计数补上（推测在敌人多的地图上是联机侧最大的单项 CPU 开销）。
- **风险**：缓存失效要和对象删除和复用同帧发生的情况对齐；必须保留 StillNetRef 复核，不能把指针跨帧留着不复核。tests/player_identity_guard.py 的守卫要同步。

### net-4. 插件联机节奏建在 GetTickCount64（约 15.6 ms 粒度）上：33 ms 和 50 ms 的采样实际被拉长（S · G）

- **问题与证据**：crew.cpp:88 的 GameMs 和 :93 的 WallMs 都基于 GetTickCount64。enemy_control_net.cpp:27 的 kSampleMs=33、:326 的 now=GetTickCount64()，以及 gravity_net_protocol.h:35 的 kHoldGapMs=50（GameMs），判定都是 now-sent>=gap。GetTickCount64 只以约 15.6 ms 步进，而且不受 timeBeginPeriod 影响。60 fps 下隔两帧的差值是 31 或 32 ms，小于 33，要等第三帧（约 50 ms）。按算术推导（推测，没有日志实测）：怪物操控姿态从标称 30 Hz 掉到约 20 Hz，重力枪持握从 20 Hz 掉到约 15 Hz，而且间隔抖动。support_net.cpp:484 的 if(lastTick==now)return; 也用 GetTickCount64 去重。等不限帧率的阶段 2 上了，渲染帧和模拟 tick 分开，这种按墙钟毫秒去重的写法会在 n=0 帧上多跑或漏跑（docs/unlimited-fps-re.md §4.4、阶段 6 未做）。
- **方案**：联机节奏改用 QPC 单调毫秒（或者直接按模拟 tick 计数）。support_net 的去重改为每个模拟 tick 跑一次（GameFrame，即 System+0xCE0 逻辑帧）。采样间隔用 tick 数表示（30 Hz 就是每 2 个 60 Hz tick）。这一项和不限帧率阶段 2 的「节奏守卫」一起挂接。
- **收益**：姿态和持握发送频率回到设计值（按推导大约 +50% 和 +33% 的更新率），间隔更均匀。不限帧率之后联机节奏也不受帧率影响。
- **风险**：发送频率真的提高会增加带宽（这几条通道每条都小于 1 KB，可接受），要配合第 1 项的计数确认。
- **依赖**：不限帧率阶段 2 的 tick 门（tick 计数部分）；QPC 替换可以先做

### net-5. 扩展通道没有「状态类」：高频状态全走可靠有序，而且和游戏流量共用同一个序号空间，产生队头阻塞（L · G · P）

- **问题与证据**：D:/APP/edf6_directnet/src/extension_transport.cpp:177 的 msg.reliability=2，且「Always one stable ordered path」（:173-176），所有扩展消息都经房主。direct_net.cpp:928-935：游戏的未分类不可靠包会被 upgradeUnreliable，和扩展消息一起用 link.tx.nextSeq()，是同一个序号空间。reliable.cpp:221-227：ReliableOrdered 包要等所有更小的序号，包括被升级的游戏包；那些包要到 kExpireMs=2000 后发 Forward 才放行（reliable.h:46-49）。所以链路上任何一个包丢了，后面所有插件消息都要停一个 RTO（至少 150 ms，见 reliable.cpp:12）甚至更久，然后一次性涌入。插件里属于「后一份替代前一份」的状态有：怪物操控姿态 33 ms（enemy_control_net.cpp:343-350）、重力持握 50 ms 和飞行 100 ms（gravity_net_protocol.h:35-38）、终极打击 keep 1 s（ultimate_net_protocol.h:31）。这些都被当成可靠事务重传，而且永不过期。Coop 自己的游戏流量已经按 netclass.h 分出 State / Event，扩展通道还没有。
- **方案**：Coop 扩展 ABI 升到 v2，新增 sendState(generation, peer, key, data)：按 (发送者, key) 只保留最新一份，带序号，不重传或短期过期；接收端丢弃比已收序号旧的。扩展的有序消息改用独立的有序域（按扩展 channel 各自一个 ReliableReceiver），不再排在游戏包后面。AF 侧 plugin_net 增加 SendState / BroadcastState，把上面列的状态通道迁过去；事务类消息（租约、支援、动作批）仍走可靠有序。
- **收益**：丢包时插件状态的延迟尖峰从 RTO（≥150 ms，最坏约 2 s）降到 0，后一份直接替代。陈旧状态不再重传，不再占积压（同时降低第 2 项的触发概率）。
- **风险**：改链路线协议和扩展 ABI：Coop 协议升级，全员同版本，房主拒绝旧版。AF 房间协议 kAllForcesRoomProtocol（src/mod_room.h:58）要随之加一。各通道要逐个核对哪些消息真的可以丢。
- **依赖**：第 2 项（故障语义）先落地

### net-6. 去掉插件联机里已经被房间协议覆盖的兼容层：按功能的 kExt 位、PeerHas 门、每 3 秒的名册广播、命令协议 v6/v7 协商（M）

- **问题与证据**：src/mod_room.h:6-8、58 已经保证同房间所有机器线协议一致，旧版本找不到也进不了房间。但 plugin_net 仍然逐个对端按功能位决定发不发（plugin_net.h:3-4、plugin_net.cpp:130-133 的 PeerHas、:41-54 SendRoster 每 kRosterGapMs=3000 广播一次、:93-101 房主每帧逐对端核对扩展字），support_protocol.h:67-73 有 12 个 kExt 位，support_net.cpp:176-178 的「older ones read their own versions (version 0)」、:274-285 的 NoteForeignVersion 和 command_net Compatible(v6/v7) 也都还在。这违反「联机不适配旧版本」规则。每个新功能都要选位、登记、写 Features 提示，可维护性成本高。
- **方案**：整份 build 的协议只认 kAllForcesRoomProtocol（必要时进房时附带 build 哈希，由房主拒绝）。删掉 kExt 功能位（只保留运行时会变的 kExtVariants）、PeerHas 门、名册通道和版本 0 分支。保留「对端 hello 已到」这个就绪握手，它是 joined 回调的前提。
- **收益**：可维护性：新功能不再需要选功能位、加版本提示和兼容分支。运行时省掉每帧逐对端核对扩展字和周期性的名册广播（量很小）。
- **风险**：名册还被用来区分「对端已就绪 / 未就绪」，删之前要确认 Joins 只依赖 hello。改动线上消息，所以 AF 房间协议要升到 16。

### net-7. 收包循环每条消息都单独加锁、刷新房间状态，AF 侧还要逐条清零 1 KB、线性查对端（S）

- **问题与证据**：AF 侧 src/support_net.cpp:496-503：每帧最多 512 条，每条都在栈上 1 KB 清零（bytes[...]{}），并用 strcmp 线性查找 peerCount 个对端。Coop 侧 extension_transport.cpp:198-205：每次 extensionPoll 都拿 mu_，并调 refreshExtensionLocked（:69-123，每次重建 links vector、对 members 做二分查找）。这把 mu_ 和 DirectNet 工作线程是共用的，后者每 5 ms 的 select 循环都会在锁里 tick，并在锁里处理每个数据报（direct_net.cpp:2000-2033、1530-1535）。高负载时游戏线程每帧最多 512 次加锁并和网络线程争用。
- **方案**：Coop 新增可选导出 EDF6CoopPollExtensionBatch：一次加锁、一次 refresh，把 N 条消息拷进调用方提供的缓冲区，返回每条的长度和发送者下标（直接给下标，不给 PUID 字符串）。这是像 TrySend 那样的可选导出，不改 ABI v1。AF 侧改用批量取，去掉逐条清零和 strcmp 查找。
- **收益**：消息多的帧，锁的获取次数从 N 降到 1，游戏线程和网络线程的锁争用随之下降。实际收益要看第 1 项的数据，平常每帧消息不多时收益很小。
- **风险**：新的可选导出要求 Coop 最低版本提高（房主拒绝旧版）。线协议不变。

### net-8. 每帧整套通道更新至少跑两遍，Publish 每帧多次、每条消息还加一次（S）

- **问题与证据**：src/support_net.cpp:494 和 :525 在帧首、帧尾各调一次 CommandContext，也就是 plugin_net::Update。这会跑全部 12 个通道的 update（plugin_net.cpp:103），其中怪物操控的 UpdateEnemyControlNetwork 每次都 Tick+Publish（enemy_control_net.cpp:319）。crew.cpp:900 又单独调 EnemyControlNetworkFrame（再 Tick 一次，:362 再 Publish 一次）。ReceiveEnemyControlNetwork 每收一条也 Publish 一次（:322），而 Publish 要拿 SRW 锁并拷贝状态表。10-30 的修复把「每条消息一遍」降到了「每帧 2 遍」，但没有降到 1 遍。
- **方案**：通道 update 每帧只在收包之后调一次。帧首那次只在会话 ready/epoch 变化时才调（现在 :507 已经有这个判断）。Publish 改成脏标记，每帧末尾发布一次。EnemyControlNetworkFrame 和 update 里的 Tick 合并。
- **收益**：联机帧的通道开销大约减半。绝对值预计很小（客机实测联机段平均 17.0 ms 帧时间里插件没被认定为瓶颈，见 feedback 第 11 行），属于顺手清理。
- **风险**：有些通道可能依赖帧首那次 update 先跑再收包（比如在 joined 里发全量状态），要逐个核对顺序。

### net-9. 怪物操控的远端姿态直接套用，不带时间戳，也没有插值（L · G）

- **问题与证据**：房主每 33 ms 采样一次（enemy_control_net.cpp:343-350）。客机收到后经 EnemyControlApplyNetworkPose 直接调 enemy_native::ApplyPose（enemy_control.cpp:1086-1090），包里没有发送时刻和速度。再叠加第 4 项（实际约 20 Hz）和第 5 项（丢包时成批到达），客机上被操控的怪物很可能一顿一顿、跳着走（推测：没有实机观察记录，ApplyPose 写进原生 reader 0x77C2C0 之后游戏自己会不会软拉，也没核实）。Coop 已经给玩家做了带发送时钟和速度的外推和收敛（multislot/src/netplayer.h:1-14、netplayer.cpp:180-196），可以照着做。
- **方案**：姿态包加发送单调时钟和线速度，客机按「最新样本 + 一个采样间隔延迟」插值或外推，误差按时间常数收敛（复用 netplayer 的数学和单元测试思路）。先逆向 0x77C2C0 的 reader 是直接写还是软拉，再决定插值放在哪一层。
- **收益**：客机上被操控怪物的动作变平滑，不再随丢包和节奏抖动。
- **风险**：enemy_control_protocol kVersion 从 7 升到 8，AF 房间协议加一。和原生姿态表（34 行 PoseRow，enemy_control_native_net.cpp:20-126）按逐个 profile 核对过的写法耦合。
- **依赖**：第 4、5 项（节奏和状态类）

### net-10. Havok 多线程已经默认开，但联机分歧没做对比（M · G）

- **问题与证据**：docs/havok-mt-re.md:232 写明「联机对比与音效逐项对比还没做」，:287 说 hknp 多线程求解是否和单线程逐位一致没有验证，房主和客人各算各的物理对象可能比原版分歧更大，:310 再次列为未做。原版物理对象本来就是各机自算（D:/APP/edf6_directnet/docs/netcode-rewrite-plan.md §1），多线程可能放大尸体、碎片、集装箱、载具位置的不一致。
- **方案**：两台机器分别做「一开一关」和「都开」对比：同一关同一路线，记录关键物理对象坐标的偏差。如果确认分歧明显放大，再决定是要求房间内统一设置（作为房间公告参数），还是把物理对象纳入同步，不默认把它当成没问题。
- **收益**：消除一项「性能优化反过来伤害联机一致性」的未知风险。
- **风险**：需要两台机器，并且要遵守实机测试铁律。

### net-11. 客人之间的扩展消息一律经房主中转、可靠有序，大房间时房主上行和单点积压成为瓶颈（XL · G · P）

- **问题与证据**：extension_transport.cpp:173-176：「Joiner-to-joiner packets go through the host」。房主的 extensionPacket（:207-211）替每对客人用 failClosed 再发一次。Coop 的游戏流量已经有中转预算、兴趣管理过滤（direct_net.cpp:973-991）和多路径计划（netcode-rewrite-plan W1、W6），扩展通道完全没有：N 人房间里每个客人 Broadcast 一次，房主就要再发 N-2 份，而且计入房主到每个客人的那条可靠积压（又会触发第 2 项）。4 人房影响小，Coop 目标是 1024 人房（W6），这里不可扩展。
- **方案**：做完第 5 项的独立有序域之后，扩展状态类可以走客人直连（W1 的网状路径），只有事务类留在房主中转。中转的状态类套用现有的 relayBytesPerSecond 预算和兴趣过滤；超出预算就丢旧的，不再 fail-closed。
- **收益**：大房间里房主上行流量和积压随人数的增长从 O(N²) 降到接近 O(N)；对 4 人以内的房间收益很小。
- **风险**：和 Coop 的多路径改造（W1，未完成）强耦合，需要 Coop 协议升级。
- **依赖**：第 2、5 项，以及 Coop W1 多路径

## 卡顿尖峰与加载（hitch）

现状：帧节奏（Sleep 精度）、Havok 多线程、CRI loader 池（130→275）和主插件的异步日志都已经做完，本轮不再重复提。原版 ResourceManager 已经有 4 个 worker 并行加载（已验证：0x7A38D0 分发线程，+0x19C 起 4 个槽）。
最大的瓶颈有两处，都在实测数据里。
（1）运行中的单帧尖峰：本机日志最近 400 个 PERF 窗口里，最差帧的中位数 27.1 ms，p90 78.9 ms，最大 867.9 ms，其中 52 个窗口超过 150 ms。17 次打开 M 地图里有 13 次在同一窗口内出现 ≥150 ms 的单帧（最高 216.8 ms）；603 原版输入卡过一次 293 ms，暂停菜单那次 868 ms。这些尖峰的根因现有工具定位不到：PERF 只按 5 秒聚合，采样器的栈也没有时间戳。
（2）进关：插件不分场合预载全部喷气机、武器和支援士兵，游戏线程在加载队列反压（0x51D00 里的 WaitForSingleObject）上卡了约 1.06 s（player jets 406 ms + support soldiers 578 ms）。从 mission start 到 map in 约 5.6–6.2 s，内存涨 2.8 GB。
建议顺序：先花 S/M 的工作量补齐尖峰归因（慢帧带 QPC 时间戳、采样器按时间对齐、同步加载探测），再用它定位并修 M 地图的尖峰和其余未知长卡；之后按房间成员的配装收窄插件预载，按需部分走已有的 RequestLoad 异步模式。
AutoTurret 日志在游戏线程上同步写盘、不轮转（本机已有 70.9 MB），顺手用 common 里的异步写盘修掉。
推测项都已在条目里标出；凡要改游戏行为的条目都要实机验证。

### hitch-1. 尖峰归因工具：慢帧带 QPC 时间戳，采样器按时间对齐出慢帧的栈（S · G）

- **问题与证据**：src/crew.cpp:852-881 PerfTick 只按 5 秒输出 mean/worst/over20/over33，不记慢帧的具体时刻。tools/rip_sampler.cpp 输出的 "T tid delta frames…" 也没有逐样本时间戳，perf_report 因此无法把某一帧的尖峰对应到具体的栈。本机日志里有 52 个窗口 worst≥150 ms，最大 867.9 ms，原因大多查不出来。
- **方案**：PerfTick 遇到超过 33 ms 的帧时，单独记一行 SLOW FRAME <qpc_begin> <ms>（写进现有的异步日志缓冲）。rip_sampler 每个样本带上 QPC。perf_report 新增「慢帧窗口内的栈」一节：只汇总落在慢帧 [begin,end] 区间里的样本，按函数 RVA 排序。不改游戏行为。
- **收益**：让 150–870 ms 级的尖峰能归到函数级；后面第 3、4、6 条都要靠它出数据。本身不提帧率。
- **风险**：很低。只多几行日志和离线工具；慢帧本来就少，日志量几乎不变。

### hitch-2. 同步加载探测：记录游戏线程上 0x84E80 未命中缓存后现场加载的路径和耗时（M · G）

- **问题与证据**：CreateObject 0x11945E0 调资源缓存 0x84E80（全局 0x20B28D0）。缓存未命中时，0x89360 在调用线程里同步加载。这一点是按 Ghidra 结构推断的，具体分支还没核实（推测）。已知案例：dreadnought-plan.md:165 记录机库第一次按键卡 1.9 s、轰炸机卡 1.6 s，原因是喷气机武器的 SGO 没预载。现在没有手段系统地找出原版和插件里还有哪些没预载的资源。
- **方案**：先用 vanilla-code 伪代码和反汇编核实 0x84E80 的未命中分支。在该分支外包一层计时 hook，仅在游戏线程上记录 SYNC LOAD <path> <ms>，用 Debug 开关控制。拿到清单后，把相应资源补进对应功能的预载，或改走 debug_spawn.cpp:28-80 的 RequestLoad/LoadTick 异步模式。
- **收益**：直接找出运行中秒级卡顿的来源。历史上同类问题修一个就省 1.6–1.9 s。
- **风险**：中。hook 位置要靠签名校验；同一函数可能也在加载线程上调用，只能统计游戏线程，否则统计会失真。

### hitch-3. M 地图打开时 120–220 ms 的单帧卡顿：定位根因后在进关时预热（M · G）

- **问题与证据**：本机日志里 17 次 MAP open 有 13 次在同一 PERF 窗口出现 ≥150 ms 单帧，例如 17:18:07.068 打开后窗口 worst 216.8 ms（"1 over 33 ms"），17:01:50.073 为 212.7 ms，21:41:03 为 131 ms，21:41:32 为 119.5 ms。相关代码在 src/map.cpp:567（打开）、src/view.cpp:127-134（ViewMapClip 把 env 和相机的远裁剪面一次抬到 6000 m）、worldmap_tiles.cpp:61（CreateFileW 读瓦片）。哪一步造成卡顿还没确定（推测候选：远裁剪抬到 6000 m 后，远景和 LOD 在第一帧集中可见或流式加载；瓦片和纹理在游戏线程上同步读和创建；原版 HUD 隐藏；标记构建）。
- **方案**：用第 1 条的工具在打开地图的那一帧抓栈，并在 map 打开路径上逐段记 QPC。修法按根因定：如果是瓦片或纹理创建，就在任务开始时异步读盘、预建纹理；如果是 6000 m 裁剪导致第一帧暴露大量远景对象，就让裁剪距离在几帧内逐步抬高，或在进关加载屏期间预先抬一帧（属于根因层面的数据或生命周期调整，不是延时掩盖）。
- **收益**：去掉每次打开地图时 120–220 ms 的顿挫。M 地图是高频操作（指挥、摆放、呼叫都要用）。
- **风险**：中。改裁剪可能影响 view-distance-re.md 里记录的远景 pass 行为；预热会增加一点进关时间和内存。
- **依赖**：尖峰归因工具

### hitch-4. 插件进关预载按需收窄：按本机和房间成员实际会用到的内容预载（L · G · P）

- **问题与证据**：src/mission.cpp:182-203 在游戏线程上依次跑 PreloadJets 到 PreloadDebugSpawn。src/jet_spawn.cpp:563-603 只要文件存在就预载：约 33 架机体（dn*/xm*/sup*/c130）、24 种武器，另有 dreadnought 弹 111 个、andryusha 11 个；dreadnought-plan.md:99 写明按需加载还没做。playerjet.cpp:1883-1917 预载 7 架接机机体的双份，support_soldier.cpp:381-394 预载所有已安装兵种各 2 个机体。本机首个任务日志：player jets 406 ms、support soldiers 578 ms，整个 MissionStart 在游戏线程上约 1.06 s（43.284→44.341），时间几乎都花在 0x7A3780 入队时 0x51D00 队列满后的 WaitForSingleObject 上（已验证）。第二个任务同样的阶段约 1 ms，说明是文件缓存冷热造成的差异。mission start 时内存 1595→2137 MB，map in 时 4447 MB。插件预载在总加载量里的占比还没测。
- **方案**：(1) 先测量：perfrun 对比 PreloadJets 全量和仅原版两种情况，看 player preload→map in 的时长和 LogMemory。(2) 改成按配装预载：喷气机、武器、支援士兵只预载本机和房间成员配装里呼叫得到的项，做法与 PreloadPlayerVehicles 经 roomsync 收集成员配置相同；离线可以对冷门项改用 RequestLoad/LoadTick 按需加载（debug_spawn.cpp:28-80，加载完成后补做 0x111CC20 渲染栅栏）。(3) 联机时所有成员必须预载同一份资源，预载集合由房主在 MissionSync 阶段汇总后下发（经 plugin_net 通道和能力位）。旧版插件按规则在进房时拒绝，不写兼容分支。
- **收益**：首次进关的游戏线程阻塞预计从约 1.06 s 降到 0.1–0.3 s（推测，取决于配装），并省下数百 MB 常驻内存（推测，需要先测）。同时缓解 4447 MB 的内存压力，以及随之而来的换页尖峰。
- **风险**：中高。预载漏项时，第一次呼叫会退回同步加载，出现秒级卡顿（第 2 条的探测器可以兜住检查）。联机的预载集合必须一致，否则敌人和载具无法走联机生成。
- **依赖**：同步加载探测（用于验证收窄后没有漏项）

### hitch-5. 进关耗时基线：perfrun 加入 player preload→map in 的分段计时（S · G）

- **问题与证据**：进关耗时目前只能从日志手工推算（mission start→map in 约 5.6–6.2 s；WatchMissionPhase 各阶段的 ms 只在首个任务里明显）。perfrun 只测战斗中的帧，没有加载时间指标，第 4 条和后续的加载优化都没有可回归的基线。
- **方案**：在 perfrun/perf_report 里解析 WatchMissionPhase 各阶段耗时、MissionStart 总耗时、player preload→首帧可操作的时长，以及 LogMemory 的三个内存点。按冷缓存和热缓存（第一次和第二次进同一关）分别报告。
- **收益**：给加载优化提供可量化、可回归的指标，本身不提速。
- **风险**：很低。

### hitch-6. 未知长卡：603_Flak 原版输入 293 ms、暂停菜单 868 ms（M · G）

- **问题与证据**：日志 17:24:10 有 "SLOW v=… 603_Flak: stock input 292.9 ms, plugin 0.2 ms"，所在窗口 worst 308 ms，时间花在原版代码里。12:40:44.545 esc=1（打开暂停菜单）所在窗口 worst 867.9 ms，是全日志最大值。两处的根因都不明；推测暂停菜单是第一次打开时同步加载 UI 资源，603 可能是第一次开火或炮塔时同步加载资源。
- **方案**：用第 1 条的慢帧栈和第 2 条的 SYNC LOAD 探测器各复现一次，确定是哪个函数或资源。如果是首次加载 UI 或武器资源，就补进进关预载或改为异步预热；如果是原版算法本身慢，就另开逆向任务。
- **收益**：去掉 300–870 ms 级的偶发卡顿（暂停菜单是每关必按的操作）。
- **风险**：低到中。先定位再动手；修法取决于根因。
- **依赖**：尖峰归因工具；同步加载探测

### hitch-7. AutoTurret 日志改为异步写盘并加轮转（S）

- **问题与证据**：autoturret/src/plugin.cpp:138-146 的 Log 调 common/append_log.h 的 AppendLog，每一行都在游戏线程上同步 WriteFile，而且不轮转。本机 EDF6AutoTurret.log 已有 70.9 MB；debug 打开时每秒约 30 行（AIM/DIAG/GUNNER）。ini 默认 Debug=0（autoturret/EDF6AutoTurret.ini:11），但本机装的是 debug=1。主插件早就改成了 1 MB 双缓冲、50 ms 写线程加 32 MB 轮转（src/plugin.cpp:1172-1230）。
- **方案**：把主插件的异步缓冲、写线程和轮转实现下沉到 common/edf6common，AutoTurret 改用它，删掉 append_log.h 里的同步写法。
- **收益**：消除游戏线程上每行一次的同步磁盘写（磁盘或杀软扫描繁忙时单次可能卡几 ms 到几十 ms，属推测），并控制日志体积。
- **风险**：低。注意进程退出时要把缓冲刷完（崩溃日志要靠它）。

### hitch-8. VehicleCrew 默认 Debug=1 的日志量（S）

- **问题与证据**：EDF6VehicleCrew.ini:12 默认 Debug=1，本机日志峰值约 1300 行/秒，大部分是每名士兵每 2 秒一行的 NPCAI Ranger。写盘已经异步，但格式化仍在游戏线程上做，而且日志轮转频繁。
- **方案**：发布版默认改为 Debug=0；或把 NPCAI 这类逐对象周期行降到单独的 Verbose 级别。逐帧开销本身归逐帧开销专项处理，这里只调默认等级。
- **收益**：减少游戏线程上的 snprintf 开销和日志体积（不大，属推测）。
- **风险**：低。用户反馈时日志信息会变少，需要让测试站回传时能临时打开。

### hitch-9. ModFileIn 改为加载时一次缓存 Mods 文件清单（S）

- **问题与证据**：src/mod_file.h:8-17 每次检查都调一次 GetModuleFileNameW 和一次 GetFileAttributesW。各 Preload* 在进关时的游戏线程上会对上百个文件逐个检查。
- **方案**：插件加载时枚举一次 Mods 下相关目录，建成文件名集合，ModFileIn 改为查这个集合。安装包一定是完整安装，运行期间文件不会变，所以不需要兼容逻辑。
- **收益**：进关时省下几百次文件系统调用，大约几毫秒到几十毫秒（推测，杀软在场时更多）。
- **风险**：低。

### hitch-10. 着色器首用的驱动编译卡顿：先测量，不预设结论（M · G）

- **问题与证据**：EDF.dll 从 d3dcompiler_47 只导入 D3DReflect，运行时不编译 HLSL，着色器已预编译在 DX11.cpk 里（已验证）。但驱动第一次使用某个着色器时仍可能做二次编译。目前没有测量数据（推测，优先级低）。
- **方案**：第 1 条的工具就位后，只要慢帧栈落在驱动 DLL（nvwgf2umx 等）的 CreatePixelShader 或首次 Draw 上，就在进关加载屏期间把该任务用到的着色器各绘制一次预热；不出现就不做。
- **收益**：未知；只有数据支持时才做。
- **风险**：低（先测量后决定）。
- **依赖**：尖峰归因工具

### hitch-11. ResourceManager 加载并行度（固定 4 个 worker 槽）：只有数据证明加载端是 CPU 瓶颈时才动（XL · G）

- **问题与证据**：分发线程 0x7A38D0 处理加载命令时，要等内联结构里 4 个槽中有一个空闲（+0x19C/+0x1A0 + i*0x90，事件在 +0x190，已验证）。槽数写死在结构布局里。进关加载中有多少时间花在 CMPL/CRILAYLA 解压（CPU）而不是 IO 上，还没测。
- **方案**：先用第 5 条的基线加采样，看加载期间 4 个 worker 线程是否都满载。只有确认是 CPU 瓶颈，才考虑在外部另建槽数组并重定向全部访问点（XL）。否则不做。
- **收益**：如果确认是瓶颈，进关时长可能明显缩短（推测）；否则为零。
- **风险**：高。要改原版结构布局的所有访问点，容易出现竞态或崩溃。
- **依赖**：进关耗时基线

## 输入延迟与帧呈现（latency）

现状（静态核实）：输入在每帧 TickWait 之后的 0x11A2FF3 采一次（键盘、鼠标、XInput 都在回调 0x1188270 里读）。动作封装的更新 0x77320 排在 0x7058C7，在 System::Update(0x705715) 之后，而 System::Update 是先渲染再步进。所以一次输入要过约 2 个 tick 才进到 Present 命令，再加 GPU 和显示排队，估计 50–75 ms（推测，待实测）。
交换链是旧的 blt 模型：DISCARD、BufferCount=1、Flags=ALLOW_MODE_SWITCH（0x111B4C0..0x111B53A，在 0x111B340 处 CreateSwapChain）。全 EDF.dll 没有调用 SetMaximumFrameLatency，排队上限是 DXGI 默认的 3。SyncInterval 写死为 ([main+0xC]==0)，平时恒为 1；追帧（n>1）后 8 帧改为 0（0x11A306B..0x11A309B → 0x111D890 → 0x1118AF0 → 0x1118B45 Present(edx,0)）。unlimited-fps-re.md 里「由游戏设置决定、来源未查」这句要更正：游戏里没有垂直同步开关。
最大瓶颈：(1) GPU 跟不上，或显示器刷新率不高于 59.94 Hz 时，3 帧的 DXGI 队列会被填满；(2) 封装更新排在步进之后，白白多一个 tick；(3) 无边框 / 窗口模式走 DWM 合成，用不了 VRR；(4) 显示端的帧节奏和延迟从来没有测过，perfrun 和 PERF 只有游戏线程的帧间隔。
建议先做：同一批里先做 SetMaximumFrameLatency(1)（S，改动最小）和 perfrun 的 PresentMon + 延迟打点（M，后面所有改动的验收前提）；再把封装更新挪到步进之前（M，稳定省 16.7 ms）。翻转模型、可等待交换链放到不限帧率阶段 2 之前一起做，否则高帧率下排队延迟反而变大。
各条的证据都来自 tools/edfre.py 对 Steam 版 EDF.dll 只读反汇编，以及 vanilla-code/assembly/EDF.dll.asm。vanilla.py 打开 vanilla.sqlite 报「unable to open database file」，所以没用上索引库，直接查的汇编文本。

### latency-1. 把 DXGI 最大排队帧数从默认的 3 设为 1（IDXGIDevice1::SetMaximumFrameLatency）（S · G）

- **问题与证据**：已验证：设备初始化 0x111B420（唯一调用者是 0x11A33DF，rcx=渲染器 EDF+0x2136FF0）在 0x111B646 用 IID_IDXGIDevice1（0x1A3B768）做 QI，只为在 0x111B65E 调 GetAdapter（vtbl+0x38），0x111B67D 就 Release 了。整个 EDF.dll 只有这一处引用 IDXGIDevice1 的 IID，IDXGISwapChain2 的 IID（0x1AFBAE8）没有任何引用。所以排队上限一直是 DXGI 默认的 3。
已验证：Present 是游戏线程在 0x111D890 塞进渲染环 +0x28A8 的命令，由渲染线程 0x1118AF0 执行；下一帧的栅栏 0x111CC20 要等环排空，也就是等上一帧 Present 返回。所以 CPU 最多领先 1 帧加上 DXGI 队列里的 3 帧。
推测：GPU 跟不上（帧时间 >16.7 ms）时 n≥2，0x11A3079 把 [main+0xC] 置 8，接下来 8 帧 SyncInterval=0，Present 不等垂直同步，只在队列满 3 帧时阻塞，延迟多出约 2 帧（60 fps 时约 33 ms，40 fps 时约 50 ms）。显示器实际刷新率 ≤59.94 Hz 时，tick 产帧比显示快，稳态下队列也会填满。
测试机上（frame-pacing-re.md 的采样）渲染线程大多在等游戏线程，没有 GPU 瓶颈，所以这个问题在本机测不到。
- **方案**：插件在渲染器就绪后（EDF+0x2136FF0 +0x80 是 ID3D11Device，scope_lens.cpp:28 已经在用这个渲染器全局量）QI 出 IDXGIDevice1，调一次 SetMaximumFrameLatency(1)。设备重建时（0x111C8EC 会再走 0x111B220 重建交换链；设备本身是否重建要核实）也要重设。值做成 ini（1 或 2），默认 1；调用失败就记一行日志。不改任何游戏字节。
- **收益**：GPU 跟不上或刷新率 ≤59.94 Hz 时，输入到显示少 1–2 帧（约 17–50 ms）。没有 GPU 瓶颈时基本不变。这也是后面不限帧率不让延迟变大的前提。
- **风险**：GPU 跟不上时 CPU 和 GPU 的重叠变少，平均帧率可能降几个百分点（需要 PresentMon A/B）。和 EDF6Coop、ReShade 一类注入工具不冲突，只是设备上的一个属性。

### latency-2. perfrun 加显示端测量：PresentMon 帧节奏 + 插件内输入到 Present 的延迟打点（M · G）

- **问题与证据**：已验证：现有性能数据只有游戏线程的帧间隔。src/crew.cpp:866 的 PERF 行，以及 docs/frame-pacing-re.md 实测表里的「超过 20 ms 的帧 / 每 5 s 最差帧」，都是主循环秒表 0x66D00 的值；tools/perfrun.py 和 perf_report.py 里没有 Present、PresentMon 或 ETW。
真正呈现在渲染线程 0x1118B45，之后还要排 DXGI 队列和 DWM。所以「顿挫已消除」只在游戏线程这一侧成立，显示端的重复帧、拍频、DWM 多出的那一帧都从来没测过。本清单其他条目的收益也都无法验收。
- **方案**：(a) perfrun 场景里同时跑 PresentMon（只记本进程），报告 MsBetweenDisplayChange 分布、重复帧/丢帧数、MsUntilDisplayed、PresentMode（看是 Composed:Copy 还是 Hardware: Independent Flip）。
(b) 插件 Debug=1 时打两个 QPC 点：0x1182A10 被调用时（原始输入采样），以及渲染线程执行到 0x1118AF0 时（可以在环里自己的位置插一条打点命令，或者用 Present 返回后的读数）。按帧号配对，算采样到 Present 的时间和 tick 数。
(c) autopilot 场景用合成按键加镜头矩阵变化，量一次端到端的 tick 数，作为第 3 条的验收依据。
- **收益**：为第 1、3、4、5、6、7 条提供能量化的 A/B 验收，避免凭感觉改。也能直接确认本机走的是 Composed 还是 Independent Flip（Windows 11 的「窗口化游戏优化」可能已经自动升级成翻转模型）。
- **风险**：PresentMon 需要 ETW 会话，可能要管理员或 Performance Log Users 组权限（本机要核实）。拿不到时只用 (b) 的插件内打点。只用于测试，不进发布包。

### latency-3. 把动作封装更新（0x77320）挪到 System::Update 之前，省掉一个 tick 的输入延迟（M · G）

- **问题与证据**：已验证的顺序：主循环 0x11A2FF3 调 0x1182A10，它遍历回调表（0x1182A33 call [rbx]），回调 0x1188270 在这里采原始输入：鼠标累加量 0x11831F0、键盘缓冲 0x21360E0、XInputGetState 0x11883D1，写进各通道 +0x48/+0x4C。之后 Application::Update 0x705530 先在 0x705715 调 System::Update（先渲染 0x1198FA4，再按 tick 步进 0x1198FD0..），然后才在 0x7058C7 调 0x77320→0x7CDC0。只有 0x7CDC0 每帧跑；0x1182BC0 的 154 个调用者里其余 0x7AB10/0x78630/0x101BA0 都是初始化或重置。0x1182BC0 把通道 +0x48/+0x4C 拷进封装 +0xC/+0x10，并算出 +0x11/+0x12/+0x13 边沿。
unlimited-fps-re.md §2.4（H）说步进读的是封装字节，并且是在下一帧的 System::Update 里读。所以本帧开头采到的输入，下一帧才进步进，再下一帧才被渲染出来：比必要的多一个 tick（16.7 ms）。鼠标位移经封装 +0xC 被镜头读，文档标的是 M，要逐层核实。
- **方案**：在 0x705715 的 call 之前插一个近处小桩，先调 0x77320(rcx 同 0x7058C7 处)，原 0x7058C7 的调用改为跳过（或把封装更新整体前移）。前提是先核实三件事：(1) 0x705715..0x7058C7 之间（0x112B4C0、0x705B10、0x703480、HUD 0x11977F0 等）有没有依赖「封装还是上一帧值」或在封装更新前改输入模式的；(2) 0x705963 的 +0x6C 抑制倒数和 0x77200 的顺序；(3) 与不限帧率阶段 2 的边沿保留（frame_rate_logic.h Shadow，挂点同是 0x77327）一起设计：前移后 n=0 帧设的边沿会在 n=0 帧的 System::Update 前就绪但没有步进读，仍需要 Shadow。
- **收益**：键盘、手柄、鼠标到模拟统一少 1 个 tick（约 16.7 ms），所有显示模式都受益，不依赖 GPU。
- **风险**：UI 和菜单读边沿的时机变了：可能出现菜单一按走两格、按住重复速率变化、暂停/地图开关那一帧吞键。插件里读动作字节的功能（敌控、炮塔镜头、地图键等）要逐个复核时序。必须实机验收：快按 100 次不丢、菜单每按一次只走一格、联机两端各打一关。
- **依赖**：第 2 条（量化验收）；与不限帧率阶段 2 的输入边沿保留共用挂点，需要合并设计

### latency-4. 更正 SyncInterval 的来源，并把「限制排队帧数」定为不限帧率阶段 2 的前置条件（S）

- **问题与证据**：已验证：0x11A306B 每帧把 [main+0xC] 减 1（到负数夹回 0）；0x11A3074 处 n>1 时置 8；0x11A3095 cmp [main+0xC],esi(0) / sete dl，然后 0x111D890(rcx=0x2136FF0, edx=dl) 把 {renderer, dl} 打包塞进渲染环（0x111D8F0），渲染线程 0x1118B41 取出放进 edx，0x1118B45 调 Present(edx, 0)。
所以 SyncInterval 写死为「平时 1、追帧后 8 帧为 0」，游戏没有垂直同步设置。docs/unlimited-fps-re.md §4.1 写的「SyncInterval 由游戏设置决定（来源未查，M）」不对。
推论：开 FrameRateCap 后，产帧率一旦高于刷新率，SyncInterval=1 加默认 3 帧的队列会让 Present 一直阻塞在队列满的状态，延迟多 2–3 个刷新周期。这正好抵消高帧率想要的低延迟。另外 n 的语义在帧门下变了（n=0 帧），[main+0xC] 的追帧判断也要按 tick 重新核对。
- **方案**：(1) 改 docs/unlimited-fps-re.md §4.1 和 frame_rate_logic.h:20 的注释：SyncInterval 由 0x11A3098 的 sete 决定，不是设置项。
(2) 阶段 2 的设计里写明：FrameRateCap≠0 时必须同时生效第 1 条（MaxFrameLatency=1）。有 VRR 时，用第 5 条的翻转模型加 ALLOW_TEARING，把上限设在刷新率减 3 附近。
(3) 若要提供关垂直同步，就改 0x11A3095..0x11A3098 处传给 0x111D890 的 dl，不去动渲染线程。
- **收益**：防止阶段 2 上线后高帧率反而多出 33–50 ms 延迟，也让文档结论正确。文档部分零风险。
- **风险**：只改文档和设计时没有风险。真接入时属于阶段 2 的实机验收范围。
- **依赖**：第 1 条

### latency-5. 交换链改为翻转模型（FLIP_DISCARD + 可等待对象 + ALLOW_TEARING），摆脱 DWM 复制、支持窗口 VRR（L · G）

- **问题与证据**：已验证：0x111B4C0..0x111B53A 在栈上组好 DXGI_SWAP_CHAIN_DESC，0x111B603 起存进渲染器 +0xA8..+0xEF。字段是：BufferDesc 60/1 Hz、R8G8B8A8_UNORM(0x1C)、BufferUsage 0x20、BufferCount=1(+0xD0)、OutputWindow=hwnd、Windowed=([r14+0x24]!=1)、SwapEffect=0 DISCARD(+0xE4)、Flags=2 ALLOW_MODE_SWITCH(+0xE8)。0x111B220 复制后在 0x111B340 调 IDXGIFactory::CreateSwapChain（vtbl+0x50），0x111B7B3 调 MakeWindowAssociation(hwnd, 2=NO_ALT_ENTER)。
推测：窗口和无边框模式下，blt 模型要经 DWM 合成复制，比独立翻转多约 1 个刷新周期，用不了窗口模式的 G-Sync/FreeSync，也没有可等待对象。另外已验证：MSAA 开着时 0x111B28D..0x111B2E4 用 CheckMultisampleQualityLevels（vtbl+0xF0）把交换链缓冲本身设成多重采样，而翻转模型不允许多重采样缓冲。
- **方案**：(1) 改 +0xA8 起的描述（或钩 CreateSwapChain）：SwapEffect=FLIP_DISCARD(4)，BufferCount=2 或 3，Flags 加 FRAME_LATENCY_WAITABLE_OBJECT；系统支持时（IDXGIFactory5::CheckFeatureSupport）再加 ALLOW_TEARING。
(2) MSAA 路径：另建一张多重采样渲染目标代替交换链缓冲交给游戏，Present 前 ResolveSubresource。或者先核实 0x1183CD0 返回的 MSAA 开关在 PC 版设置里能否打开；能打开就必须做 resolve，不能只给不开 MSAA 的人用。
(3) 钩 ResizeBuffers/全屏切换，保持 Flags 一致。
(4) 拿到可等待对象后，按第 4 条把主循环的等待和输入采样对齐到它。动手前先用第 2 条确认本机现在的 PresentMode：Windows 11 的「窗口化游戏优化」可能已经自动升级成翻转模型。
- **收益**：窗口/无边框下少约 1 个刷新周期（约 16.7 ms，推测），支持窗口 VRR，消除第 7 条的拍频和高刷抖动（VRR 显示器上），并为可等待交换链和低延迟帧起点打基础。
- **风险**：翻转模型下 D3D11 在 Present 后会解绑交换链缓冲的渲染目标，引擎如果只设一次渲染目标就会黑屏；插件自己的 GPU 合成（scope_pip_gpu.cpp、scope_lens.cpp 抓主画面）也要复核。全屏独占切换、Alt-Tab、设备移除（0x1118B48 存 DXGI_ERROR_DEVICE_REMOVED/HUNG/RESET）、Steam/EOS 覆盖层都要测。
- **依赖**：第 2 条（确认现状 PresentMode）；第 1 条

### latency-6. 原始输入改在渲染录制之后、tick 步进之前采样（M · G）

- **问题与证据**：已验证：原始采样在 0x11A2FF3，在 Application::Update 之前。之后才依次做 Steam/EOS 回调和网络块（0x705633..0x7056AE）、MapMgr（0x7056BA），以及 System::Update 里先做的整帧渲染录制（0x1198FA4 → 0x11978D0 → 0x118DFF0，含 Umbra），然后才进 tick 循环 0x1198FB0。
推测：渲染录制和这些前置工作每帧要几毫秒。第三方数据（unlimited-fps-re.md §3，L 级）是 Umbra processVisibility 每帧约 3 次、每次约 3 ms，n≥1 帧约 5.5 ms。这段时间都算在输入延迟里。
- **方案**：在 0x1198FB0（tick 循环入口，n>0 分支）前加挂点，依次调 0x1182A10（原始采样）和第 3 条前移后的 0x77320。主循环 0x11A2FF3 的调用对应去掉，保证每帧只采一次。要核实渲染录制、HUD 有没有读本帧鼠标或光标（菜单光标、地图拖动），以及 0x1182A10 加锁 [rsi+0x48]（0x1182A47 call 0x50BC0）的契约在 System::Update 内部有没有重入问题。
- **收益**：输入到步进再少几毫秒（大约等于渲染录制耗时，推测 3–8 ms），和第 3 条叠加。
- **风险**：菜单和加载状态下 System::Update 走 0x1198E3E/0x1198ED0 的非任务分支，不进 0x1198FB0，要另找挂点，否则菜单里不采输入。比第 3 条风险更高，应在第 3 条验收后再做。
- **依赖**：第 3 条

### latency-7. 59.94 Hz tick 与显示刷新率的拍频和高刷抖动：把 tick 相位锁到 vblank（只在刷新率约等于 60 或 120 时）（M · G）

- **问题与证据**：已验证：tick 周期是 freq*100/5994（frame-pacing-re.md，0x11DAE88 的魔数），Present 恒为 SyncInterval=1（第 4 条）。
推测（理论，未实测）：60.000 Hz 显示器上新帧到达的相位每帧漂约 0.0167 ms，约每 1/(60-59.94)=16.7 s 会有一个 vblank 没有新帧，画面重复一帧。相位贴近 vblank 的约 2 s 里，渲染线程 ±1 ms 的抖动还可能让重复和跳帧成串出现。144 Hz 上每帧显示 2 或 3 个刷新（13.9 ms 与 20.8 ms 交替），平移画面有持续抖动。
frame-pacing-re.md 测的「每 5 s 最差 18.3 ms」是游戏线程秒表，看不到这些。
- **方案**：先用第 2 条的 PresentMon 确认重复帧周期。确认后分情况：
- VRR 显示器走第 5 条，这是根因解法。
- 固定刷新、且刷新率在 59.94 或 60 的 ±0.2%（或 120/240 的整数倍）以内时，用 DXGI 帧统计（GetFrameStatistics）或 IDXGIOutput::WaitForVBlank 量出实际刷新周期，让 TickWait 的边界按 vblank 相位对齐：只调 base 0x2139D10，每次不超过一个小量，tick 率随之变成显示器的实际刷新率。
- 144 Hz 这类非整数倍的，留给不限帧率加插值去解决。
- **收益**：60 和 120 Hz 固定刷新显示器上消除周期性重复帧（推测：每 16.7 s 一次 33 ms 卡顿），并把 Present 到显示的等待固定在一个小值（目前在 0–16.7 ms 之间锯齿漂移）。
- **风险**：模拟速率会变成显示器的实际频率（与 59.94 相差约 0.1%），联机里不同显示器的成员模拟速率会有 0.1% 差异。EDF6Coop 对此是否敏感未知，必须做联机实测（协议不变，但所有成员都要装同版本）。与不限帧率的帧门共用 tick 时钟，要合并设计。
- **依赖**：第 2 条；与第 5 条、不限帧率阶段 2 协调

### latency-8. 鼠标改用 Raw Input（WM_INPUT）累加位移，不再用 GetCursorPos/SetCursorPos 回中（M · G）

- **问题与证据**：已验证：窗口线程 0x1183880 循环里，0x1183B13..0x1183B53 用 GetCursorPos 的结果减去窗口中心（cvtdq2ps 转浮点），累加进 0x1FEEE20/0x1FEEE24，0x1183B7D 再 SetCursorPos 拉回中心（camera-re.md:327 有同样描述）。游戏线程在 0x11831F0 取走并清零。
推测：这条路径吃 Windows 指针加速（提高指针精确度）的曲线，位移按屏幕整数像素量化；高 DPI 鼠标在 1 ms 内移动超过窗口半宽时还会被屏幕边界截断。转视角手感随系统鼠标设置、DPI 缩放变化，不一致。延迟本身不受影响（PumpWait 已改成有输入就醒）。
- **方案**：窗口线程注册 RAWINPUTDEVICE（usage page 1、usage 2，挂在游戏窗口上，不用 RIDEV_NOLEGACY，免得菜单光标受影响）。在窗口过程 0x1183E20 里（或用子类化）把 WM_INPUT 的 lLastX/lLastY（MOUSE_MOVE_RELATIVE）乘一个换算系数，累加进 0x1FEEE20，与原累加量用同一把锁（0x1183B0E 的 0x50BC0 锁）。锁鼠标时跳过 GetCursorPos 那段，SetCursorPos 只用来把光标留在窗口中心。换算系数默认取当前 Windows 指针速度在无加速时的等效值，保证中速手感基本不变。
- **收益**：转视角不再受系统指针加速影响，也没有像素量化和大幅甩动截断，手感一致（不是延迟收益）。
- **风险**：灵敏度会变，老玩家需要适应，做成 ini 开关。远程桌面、触控板、绝对坐标设备（MOUSE_MOVE_ABSOLUTE）要按规范处理。和插件里地图拖动、直升机鼠标镜头这些读 0x1FEEE20 语义的功能要复核。

### latency-9. XInput 每帧对同一个手柄槽都调 GetCapabilities + GetState，改为只在设备变化时探测（先测）（S · G）

- **问题与证据**：已验证：XInput 由 0x2F7A4 动态加载 xinput9_1_0.dll（字符串在 0x1AE18D8），函数指针存在 0x2136510 起。采样回调 0x1188270 里每帧先在 0x11883BC 调 XInputGetCapabilities（一个槽号），成功了才在 0x11883D1 调 XInputGetState。它在游戏线程上、原始采样那一步，紧挨着步进之前。
推测：没插手柄时对空槽调 XInputGetCapabilities，在部分 XInput 实现上开销明显（社区常见报告是亚毫秒级），每帧都直接算在输入到步进的路径上。耗时没测过。
- **方案**：先用 tools/rip_sampler 或 perfrun 在不插手柄的情况下看 0x1188270 的样本占比。确实明显的话，钩 0x11883BC 这处调用：已连接时直通；未连接时只在收到 WM_DEVICECHANGE（窗口线程）或每 1 s 才真正探测一次，其余帧直接返回 ERROR_DEVICE_NOT_CONNECTED。
- **收益**：不确定：0 到约 0.5 ms 一帧（推测），只对键鼠玩家有效。
- **风险**：热插拔手柄最多晚 1 s 被识别。测下来开销不明显就不做。
- **依赖**：第 2 条或 rip_sampler 实测

## GPU 与渲染提交（gpu）

现状（静态核实，EDF.dll 0x678CCB46）：设备只建一个立即上下文（0x111B595 D3D11CreateDevice，adapter=NULL、Flags=0、只请求 FL11_0），没有 deferred context。游戏线程在 0x11978D0 里把整帧录进自研命令环 cmdctx(0x2136FF0)+0x28A8，同时在游戏线程上同步跑 Umbra resolveVisibility（0x11B6B1D）。processVisibility、绘制和 Present(0x1118B45) 都由唯一的渲染线程（0x111B8E0）消费。交换链是老式 blt：DISCARD、BufferCount=1、60/1 Hz、ALLOW_MODE_SWITCH（0x111B4C0..0x111B53A），全程不调 SetMaximumFrameLatency，也不用任何 DXGI 1.2+ 接口。
最大问题：GPU 不是瓶颈。stress-re 实测 GPU 3D 只有 3–17%，重场景卡在游戏线程。按渲染线程算账只在「不限帧率」时才划算：第三方测得 n=0 帧约 9 ms，其中图形栅栏约 7 ms（L）。所以「deferred context 并行提交」不建议做。
新发现一处与现有文档矛盾：游戏线程每帧等渲染栅栏的 0x111CC20 是 WaitForSingleObject(h,0)+Sleep(0) 的忙等。frame-pacing-re.md:99 写的「真等待、不空转」不对，这是本方向里收益/成本比最高的一处。
建议顺序：先改栅栏忙等（S），同时给 perfrun 加渲染侧计时、各 pass 的 draw 计数、适配器和交换链日志（S）；拿到数据后再做 flip model+tearing+帧延迟（M）。蒙皮调色板缓存、小物体剔除距离、阴影 LOD 偏移这几项要先有第 2 条的数据才值得动。
注意：本次 vanilla.sqlite 被占用（Permission denied），所有地址都用 vanilla-code/assembly/EDF.dll.asm 和游戏目录 EDF.dll 的 PE 字节扫描（IAT、rel32 交叉引用）核实。

### gpu-1. 渲染栅栏 0x111CC20 是 Sleep(0) 忙等，改成真等待（S · G）

- **问题与证据**：已验证（汇编）：主循环每帧 0x11A305A call 0x111CC20，加载 / WaitPreload 等另有约 40 处调用。0x111CC20 先录一条信号命令并发布（0x111CC52..0x111CCB2），再进循环：0x111CD49 call 0x50E00，即 WaitForSingleObject(h, 0)（0x50E07 xor edx,edx；IAT 0x17556F8）；0x111CCE8 秒表超过 1.0 s（常量 0x1C36994=1.0）才 PeekMessage/Translate/Dispatch；0x111CD3B call [Sleep] 的参数 ecx=0。也就是零超时轮询加 Sleep(0)，游戏线程等渲染线程的整段时间都占满一个核，还和渲染线程、Umbra 的 4 个线程、Havok worker 抢核（SMT 兄弟核）。docs/frame-pacing-re.md:99「主要是 WaitForSingleObject 真等待，不空转」与汇编矛盾：采样落在 WaitForSingleObject 里，是因为它每圈都进一次内核。另外每次调用都新建 / 关闭一个 auto-reset 事件（0x50D10→0x50CD0 CreateEventW，0x50D70 关闭）。
- **方案**：把 0x111CD49 的 E8 call 0x50E00 改指向近处小桩：对同一个句柄（[rcx]）做 WaitForSingleObject(h, 1000)，返回值语义不变（0 → al=1）。这样事件一到立即醒，1 s 后照原样走消息泵分支。只验 0x111CCC4..0x111CD50 的签名（含 0x50E00 的 19 字节），不符就记一行 REFUSED、不改。frame_pacing.cpp 已有 AllocateNearCode/unwind 的套路，可以复用。同时更正 frame-pacing-re.md:99。
- **收益**：省掉游戏线程等渲染期间的空转 CPU（推测：轻场景每帧占数毫秒，不限帧率、渲染受限时接近一整核），也减少对渲染线程和 Umbra 线程的抢占。帧率本身不一定变，收益主要在笔记本功耗 / 温度墙和 SMT 争用。frame-pacing 的同类改法曾把进程 CPU 从 110 s 降到 36 s / 75 s，这一处要用 perfrun A/B 实测。
- **风险**：低。等待对象是本次调用私有的事件，语义不变。调用点签名要和第三方 120fps 包、EDF6Coop 的改动核对冲突。游戏线程本身不拥有窗口（窗口线程在 0x1184460），1 s 后消息泵改为每秒一次，属于推测无影响。

### gpu-2. 先补渲染侧测量：渲染线程分段计时、各 pass 的 draw 数、适配器和交换链参数（S · G）

- **问题与证据**：tools/perf_report.py 只有 typeperf GPU 引擎利用率（perfrun.py:151）、线程 CPU 和栈，没有渲染线程每帧耗时、Umbra resolve/process 时间、各 pass 的 draw 数，也没有 Present 间隔。unlimited-fps-re.md:130 引用的「栅栏约 7 ms、processVisibility 约 3 ms×3」是第三方数据（L）。本条之后的各项收益都没法量化。
- **方案**：Debug=1 时每 5 s 写一行 RENDER 日志：(1) 0x111CC20 进出时间（游戏线程等渲染的时间）；(2) 渲染线程 processVisibility 命令 0x11B5A70 / 0x11B5B30 的耗时，以及游戏线程 resolveVisibility 调用点 0x11B6B1D、0x11B6808、0x11B7001、0x11B71D1 的耗时；(3) 0x11E1630 按 pass 0..5 计数，0x10FF4E0 计 DrawIndexed 数；(4) Present(0x1118B45) 间隔，以及 0x1631860 GetData 的次数和 S_FALSE 次数；(5) 启动时记一行适配器名（[cmdctx+0xF0]）、交换链 desc（[cmdctx+0xA8..0xF0]）、FeatureLevel（[cmdctx+0x7C]）。perf_report 解析这几行；另外可选接 PresentMon 拿 GPU 忙时间。
- **收益**：第 3–8 条的收益从推测变成数据，也能回答渲染受限还是游戏线程受限。成本很低，只在 Debug 下生效。
- **风险**：低。只计时，不改行为。渲染线程上的计数要用线程私有累加，或原子量 / 每 5 s 发布一次。

### gpu-3. 交换链改 flip model（FLIP_DISCARD、2–3 缓冲、ALLOW_TEARING），并设最大帧延迟（M · G）

- **问题与证据**：已验证：0x111B4C0..0x111B53A 构造 DXGI_SWAP_CHAIN_DESC：RefreshRate=60/1、Format=0x1C(R8G8B8A8_UNORM)、SampleDesc 来自参数 [r14+0x10]、BufferUsage=0x20、BufferCount=1、Windowed=([r14+0x24]!=1)、SwapEffect=0(DISCARD)、Flags=2(ALLOW_MODE_SWITCH)。它被存到 cmdctx+0xA8..0xF0，0x111B220 复制后在 0x111B340 调 IDXGIFactory::CreateSwapChain（vt+0x50）；换模式时也是整条重建，不走 ResizeBuffers。Present 在渲染线程 0x1118B45 调 Present(SyncInterval,0)。IDXGIDevice1 的 IID 只在 0x111B64B 用过一次（拿 GetAdapter），没有 SetMaximumFrameLatency（默认 3）；IDXGIFactory2/SwapChain1 的 IID 没有任何引用。后果：(a) 无边框窗口下只能走 DWM 复制合成，多一次拷贝和至少一帧延迟，也拿不到 VRR/tearing，不限帧率时超出刷新率的帧被 DWM 丢掉；(b) 独占全屏强制 60/1 Hz 模式，144 Hz 显示器在独占全屏下被锁 60 Hz（按 DXGI 选最接近模式的公开行为推断）。
- **方案**：把 0x111B340 的 call [rax+0x50] 改成调近处小桩（不改 COM 公共虚表）：改栈上 desc（rsp+0x30）为 SwapEffect=FLIP_DISCARD、BufferCount=2（不限帧率时 3）；IDXGIFactory5::CheckFeatureSupport(ALLOW_TEARING) 通过时 Flags|=ALLOW_TEARING，并让 0x1118AF0 在 SyncInterval=0 且窗口化时 Present 带 DXGI_PRESENT_ALLOW_TEARING；RefreshRate 改为当前显示器模式（或 0/0）。在 0x111B6BA 之后对 IDXGIDevice1 调 SetMaximumFrameLatency(1 或 2)。前置：用第 2 条的日志先确认实际 SampleDesc.Count。flip model 不允许 MSAA 后台缓冲，若 0x111B220 的 dl 分支实际给出 Count>1，要另建 MSAA RT 并在 Present 前 ResolveSubresource，不能静默降回 blt。
- **收益**：无边框窗口少一次 DWM 拷贝，延迟少约 1 帧（推测）。可用 VRR/G-Sync，是「不限帧率」真正上屏的前提。GPU 受限时帧延迟设置可再减最多 2 帧排队。帧率本身预计变化不大（GPU 3D 3–17%，stress-re.md:20-24）。Win11 的「窗口化游戏优化」对部分用户已自动升级 blt→flip，收益因用户设置而异。
- **风险**：中。所有取后台缓冲的地方（GetBuffer vt+0x48：0x11146D6/0x1115454/0x111568F）在 D3D11 flip 下 buffer 0 仍有效，但要核对有没有缓存 RTV 跨帧复用、有没有把后台缓冲当 SRV 读或 Copy。插件已有 thermal/scope 直接画在主 RTV 上，要回归。ALLOW_TEARING 会改变 SetFullscreenState / ResizeTarget（0x111BFEC、0x111A506、0x111CA4F）的约束。EDF6VR 等第三方 mod 可能也钩交换链。

### gpu-4. 混合显卡笔记本可能跑在核显上：D3D11CreateDevice 用默认适配器，exe 不导出 NvOptimus/AmdPowerXpress（S · G）

- **问题与证据**：已验证：0x111B595 调 D3D11CreateDevice 时 rcx=0（默认适配器），驱动类型依次试 HARDWARE/WARP/REFERENCE（0x111B4C0..0x111B4CE）。EDF6.exe 导出表为空，没有 NvOptimusEnablement / AmdPowerXpressRequestHighPerformance（pefile 读取）。没有驱动配置文件、也没在 Windows 图形设置里指定的混合显卡机器会落到核显（推测，取决于驱动配置文件）。插件在应用初始化之前加载（aiworld-re.md:60、cri-fs-loaders-re.md:61），设备在 init 里创建，所以插件来得及介入。
- **方案**：先用第 2 条记适配器名，确认真有用户落在核显。确认后把 0x111B595 的 call 改到小桩：CreateDXGIFactory2 → IDXGIFactory6::EnumAdapterByGpuPreference(HIGH_PERFORMANCE)，拿到适配器后以 D3D_DRIVER_TYPE_UNKNOWN 调原 D3D11CreateDevice。系统不支持 DXGI 1.6 时按原参数调用（这是系统 API 能力分支，不是资源缺失回退）。
- **收益**：受影响的笔记本用户帧率可提高数倍（推测，核显对独显）。没受影响的用户无变化。
- **风险**：低到中。Optimus 下独显渲染、核显输出是 DXGI 的标准跨适配器路径。要确认游戏没有别处假定适配器 0（[cmdctx+0xF0] 由 GetAdapter 重新取，0x111B65E）。

### gpu-5. Umbra 遮挡查询轮询 GetData 不带 DONOTFLUSH，可能每帧反复 Flush；等待时会自旋最长 1 s（S · G）

- **问题与证据**：已验证：0x11E4210 循环对每个遮挡查询结果调 0x1631790。该函数先取自旋锁 cmdctx+0x29F0（lock cmpxchg，0x163180A，失败 Sleep），再 0x1631860 ID3D11DeviceContext::GetData(query,&r,8,flags=0)（vt+0xE8，0x163184C 写 0）。flags=0 时只要结果没就绪，每次调用都会 Flush 命令缓冲（D3D11 文档语义）。Umbra 要求等待（getWaitForResult，0x11E422B）时，0x16317F3..0x16318AB 无退避地重复 GetData，直到成功或满 1000 ms（常量 0x1765A80=1000、0x1791E98=1e7）。这是渲染线程上的 CPU-GPU 同步点，触发频率要运行时才能知道。
- **方案**：先用第 2 条统计每帧 GetData 次数、S_FALSE 次数、等待分支累计时间。如果非等待轮询的 S_FALSE 明显：把非等待路径的 flags 改成 D3D11_ASYNC_GETDATA_DONOTFLUSH(1)，等待路径保留第一次 flags=0 以保证提交。如果等待分支常触发：查是哪类查询要求同步（Umbra 的首帧 / 镜头切换？），从源头处理，不加超时。
- **收益**：未知（L）。如果每帧有数十次查询未就绪，可省对应次数的驱动 Flush；等待分支若常触发，直接就是渲染线程的停顿时间。
- **风险**：低到中。DONOTFLUSH 只影响非等待路径的提交时机。等待路径不改语义。

### gpu-6. 蒙皮调色板每个 pass 都重算并整块 UpdateSubresource（M · G）

- **问题与证据**：已验证：0x1104100（LOD 绘制入口，每个 pass 调一次）无条件先调 0x11009A0：对实例的 n 根骨逐根算 逆绑定×pose（0x1100A01..0x1100B41，48 KB 栈缓冲），再 0x113F860 → 0x113D240 → ID3D11DeviceContext::UpdateSubresource(cb,0,NULL,…)（vt+0x180，pDstBox=NULL，整块缓冲）。fp-render-re.md §1 / model-lod.md 表明同一模型每帧会在 pass 0（遮挡体）、1（G-buffer）、3×级联（阴影，级联切分 0.1/27.5/120/200）、可能还有 2 里各提交一次，而一帧内骨骼姿态不变（姿态由游戏线程写，0x1100F10）。
- **方案**：在 0x1104100 → 0x11009A0 处加一层「本帧、本实例、本姿态版本」缓存：同一渲染帧内第二次起跳过计算和上传，直接复用 inst+0x48 的常量缓冲。帧号取渲染线程自有计数，以 Present 命令为界。前提是同一常量缓冲在帧内没有被别的绘制改写；inst+0x48 是每实例一份（M，需核实）。第一人称模块已链在这条路径上（fp-render-re.md:341），要一起改。
- **收益**：每个蒙皮模型每帧少 1–4 次骨骼矩阵运算和常量缓冲上传。渲染线程 CPU 减少多少取决于同屏蒙皮数量，例如大群虫 / 士兵。只在渲染线程成为关键路径时（不限帧率）体现为帧率，推测为个位数百分比。
- **风险**：中。第一人称按 pass 改姿态的逻辑（fp-render-re.md §2、§17）依赖每 pass 重新上传，缓存必须把 pass 相关的姿态覆盖算进键里。

### gpu-7. 小物体按屏幕尺寸设剔除距离（Umbra cull distance 默认 FLT_MAX）（M · G）

- **问题与证据**：已验证（view-distance-re.md:70,80 / scope-pip-re.md §4.5）：渲染节点构造 0x11B2400 时 +0x60（剔除距离²）默认 FLT_MAX；只有 0x16EE60（模型数据自带）、0x965DB0（200 km）等少数路径会设。cmd3 收集 0x11E3E1C 才按 d² > +0x60 剔除。所以碎片、小道具、弹药箱等小物体在近景视锥里一律进 Umbra 并逐 pass 绘制（G-buffer 加最多 3 级阴影）。
- **方案**：在 Object::create 的调用点（0x11B27D0 / 0x16FC10 之后）按包围半径算屏幕尺寸阈值，例如 far = k·radius，k 由 ini 配。调 setCullDistance（thunk 0x12D609E）并同步写 node+0x60=far²。只对没有显式设过距离的节点生效，载具、角色、建筑等主体不设。
- **收益**：推测：大量碎片 / 道具的场景里 draw 数下降，主要省渲染线程 CPU 和 Umbra 处理量，GPU 侧也略有收益。要第 2 条的按 pass 计数来定 k。
- **风险**：中。远处小物体会提前消失（可见变化），要和 view-distance-re.md 的远景双相机分段一起回归。剔除只影响渲染，不影响碰撞或联机同步。

### gpu-8. 阴影 pass 用低一级 LOD / 全局 LOD 距离系数（M · G）

- **问题与证据**：已验证（model-lod.md §1）：0x6BF9F0 从基础 LOD 顺序遍历，+0xE8（距离²）> d² 就停，所有 pass（含阴影）共用同一选择，d = model+0x2C（主镜头距离减包围半径）。所以阴影图里的物体与主视角同精度，原版 155 个带 LOD 的 SGO（兵、虫、UFO）在 3 级级联里都按全精度投影。
- **方案**：钩 0x6BF9F0 的 LOD 比较：pass==3 时把 d² 乘一个系数（ini，如 4 = 距离×2），或直接跳到下一级 LOD。可选再加一个全局 LodDistanceScale 给低端 GPU 用。插件生成的载具 LOD（model_lod.py）自动受益。
- **收益**：阴影 pass 三角形数下降，只在 GPU 受限的机器上有意义（本机 GPU 3D 3–17%，收益可能看不出）。属于低端机选项，不是普遍提速。
- **风险**：低到中。阴影轮廓和本体网格不一致，可能自阴影错位（acne）。LOD 共用骨架实例（model-lod.md），所以蒙皮不受影响。

### gpu-9. 不建议：用 deferred context / 多线程并行提交渲染（XL）

- **问题与证据**：已验证：全游戏只有 0x111B595 创建的一个立即上下文（[cmdctx+0x88]，0x113CEE0 存进 nativeCtx+8）。所有录制都进单一命令环 cmdctx+0x28A8（0x51D00/0x51D60），只由渲染线程 0x111B8E0 消费。nativeCtx+0x20 的状态缓存（0x1158DD0 / 0x11597E0 脏刷新）是单线程设计。immediate context 还被 cmdctx+0x29F0 自旋锁保护，跨线程用（0x163180A）。实测 GPU 3D 3–17%，渲染线程不在关键路径上（stress-re.md:20-38）。
- **方案**：不做。要并行化，只能先把 Umbra processVisibility（0x11B5A70，渲染线程）按视图拆开，这又牵涉 Umbra Commander 回调的线程安全，库内部不可审计。不限帧率需要省渲染线程时间时，先做第 5、6、7 条这类减量。
- **收益**：避免一项大改造：D3D11 deferred context 在不少驱动上是模拟实现，收益不确定，又要重写整条命令录制链。
- **风险**：如果硬做：状态缓存、插件的 thermal/scope/fp 模块（都假定立即上下文，fp_skin_gpu.cpp:55 显式拒绝 deferred）全部要重写，高风险。

### gpu-10. Umbra resolveVisibility 在游戏线程上同步执行（只量不改）（S · G）

- **问题与证据**：已验证：0x11B6AA0（主视角查询，0x1198655 调）在游戏线程上先 Camera::setProperties（0x12D611C），再 resolveVisibility（0x12D6110，0x11B6B1D），之后才录 processVisibility 命令（0x11B6B48 起，渲染线程 0x11B5A70 执行）。另有 0x11B6808 / 0x11B7001 / 0x11B71D1 三处 resolve（远景 / 阴影级联），一帧约 5 次，全在游戏线程。游戏线程正是重场景的瓶颈（stress-re.md:26）。Umbra 自带线程：initThreads(0,4,{0,1,2,3},1)（0x11B4848；表 0x1765B00 = 0,1,2,3）。umbra_sandlot.dll 不导入 SetThreadAffinityMask / SetThreadIdealProcessor，所以这张表不是钉核（已验证导入表）。第三方说降频 / 跳过 resolve 会卡加载、黑闪（unlimited-fps-re.md:129）。
- **方案**：先用第 2 条量出 resolve 在游戏线程帧时里的占比。占比明显时，下一步只考虑把阴影级联 3（120–200 m）的 resolve 与 G-buffer 录制重排，或评估远景相机按 bitmask 只在有远景对象时查询。不做降频、不跳帧（已被否决）。
- **收益**：未知。如果 resolve 占游戏线程数毫秒，就直接是重场景帧时；如果不到 1 ms 就结案。
- **风险**：测量本身低风险。后续改动风险高（Umbra 非线程安全，view-distance-re.md:106）。

## 内存与资源（mem）

现状：大地图内存的大头已由 PR #79 的 TerrainShare 解决（src/fieldshare.cpp；memory 里 RM015 实测原版 4.7 GB、无共享 16.7 GB、有共享 5.3 GB），不再重复提。分配器方面，游戏 operator new（0x12D85B0）经 IAT 0x1756300 调 api-ms-win-crt-heap 的 malloc，用的是进程堆；Havok 另有 hkFreeListMemorySystem。frame-pacing-re.md 已实测：提交约 4 GB 里常驻只有 0.5–0.9 GB，堆本身不值得动。插件按资源组 2（场景组）预载，任务结束时由 FreeResourceGroup(2) 释放，插件容器里也没找到只增不减的大块（npcai、edf41 系列、fieldshare 在重置或对象消失时都会清理）。
最大的瓶颈在插件这边，原因是「无条件预载」叠加「同一任务里同时驻留的档案各带一份相同贴图」：每个任务都把 38 种机体、它们的武器、无畏舰和炮台弹药、潜艇母舰、传送激光、终极呼叫、支援兵、空投箱全部预载（M001 日志里每项都是 =1）。这批常驻的飞机和无畏舰档案里，贴图共 266 MB，其中 134 MB 是重复的副本，因为引擎的贴图表按档案各建一份。
另有两条只有实测数据、来源还没定的现象：同一局游戏里反复进出测试场 19 轮，进任务时的工作集从 616 MB 单调涨到 785 MB（约 9 MB/轮）；第一次任务之后，进任务时的提交基线不回落，多出 0.7–0.9 GB。
建议先做：1）给预载加一个开发用开关，用 autopilot 对 M001 / RM015 做 A/B，量出插件预载的真实成本（S）；2）把同一任务里一定同时驻留的档案合并，消掉 134 MB 重复贴图（M）；3）用 UMDH 在原版和插件下各跑一遍任务循环，给内存爬升归因（M）。然后再决定预载是改按需加载，还是改按房间并集加载（L，涉及联机）。

### mem-1. 插件每个任务无条件预载全部呼叫资源，改为按需异步加载或按房间实际可呼叫的并集加载（L · G · P）

- **问题与证据**：已验证：MissionStart（src/mission.cpp:183-203）每个任务都调用 PreloadJets / PreloadPlayerJets / PreloadSub / PreloadLaser / PreloadSupportVehicles / PreloadAirdrop / PreloadSupportSoldiers 等。jet_spawn.cpp:563-612 对 kBodies（38 种，src/jet_internal.h:120 起）里所有已安装机体都调用 0x7A3780(mgr,sgo,2,-1)，排除的只有敌方 / Primer 四种，同时预载这些机体的武器。:609 只要装了 EDF6VC_DN_DREADNOUGHT.MRAB 就预载无畏舰的炮台弹药；subcarrier.cpp:1209-1215 只要文件在就预载潜艇母舰（EDF6VC_SUB.MRAB 47 MB）；playerjet.cpp:1891 在 twins 开启时全部预载。是否预载只看文件在不在、ini 开没开，不看本任务、本房间是否有人装备了对应呼叫。实测：M001 日志（EDF6VehicleCrew.log 第 135342-135400 行）中 JET preload 的每一项都是 =1，dreadnought rounds 111，SUB carrier=1。从 mission start entered 到 mission start，提交从 1513 MB 涨到 2328 MB，但这一段同时包含原版加载线程的异步加载，不能全算在插件头上。memory 记录的 RM015 是原版 4.7 GB、插件加 TerrainShare 5.3 GB（10-08 测量），之后预载集合又加了无畏舰、XM、C-130 等。按 SGO 引用汇总，这批常驻飞机档案在盘上约 162 MB，贴图另见下一条。已有可行性证据：任务中途调用 0x7A3780 仍会被加载线程处理（docs/feedback-2026-10-19-mapui.md:41）；debug_spawn 已在用「loading on demand … once the game reports it loaded」（日志第 120889 行起），完成检查用 0x7A3600（docs/feature-npc-weapon-tier.md:52）。
- **方案**：第一步（S）：在 MissionStart 里给上述 Preload* 加一个仅供开发用的总开关（ini 或 autopilot 参数），用 tests/autopilot drive.py 跑 RM015 / M001 做 A/B，比较 MEMORY the map in 一行的提交和工作集，量出插件预载的真实成本。第二步（L）：把飞机、弹药、潜艇、激光等呼叫的资源改为呼叫时排队 0x7A3780，再用 0x7A3600 查完成，就绪后才生成（呼叫本身有飞抵延迟，可以掩盖加载时间）。武器 SGO 要和机体一起排队，避免再出现「发射时建武器卡 1.6 s」（jet_spawn.cpp:588 的注释）。联机时每台机器收到呼叫后各自加载，沿用 NpcArmsLoaded / PrepareRangeEnemy 那套「各端就绪才提交」的模式；或者退一步，改为按房间成员已公布的呼叫装备并集预载（support_dispatch 已有 RoomMemberConfig）。原版任务的空袭接管（airstrike takeover）要扫任务脚本，确认该任务确实会用到才预载。
- **收益**：推测：每个任务少常驻数百 MB（RM015 上插件比原版多出的 0.6 GB 大部分可能来自这里，A/B 之后才能确定）。8 GB 内存的机器上大地图的余量随之变大，任务加载阶段插件占用的游戏线程时间也会缩短（M001 中插件各阶段合计 4.1 s，见第 5 条）。
- **风险**：呼叫到生成的时序要改：资源没就绪时必须让呼叫等待，而不是退回原版飞机（jet_spawn.cpp:562 的注释说没预载就出原版飞机，这本身就违反「不做回退」）。联机时各端就绪时间不一致，要加就绪确认。任务中途排队加载会不会和原版加载线程冲突，只有 debug_spawn 的实测，没有覆盖全部机体。

### mem-2. 同一任务里一定同时驻留的插件档案各带一份相同贴图（飞机 + 无畏舰这一组重复 134 MB）（M · G）

- **问题与证据**：已验证：pylib/mdb.py:309-318 的 insert_member 注释说明，引擎按档案建贴图表（0x110b182 / 0x110b2aa），模型按文件名绑定本档案里的贴图（0x110f640），所以不同档案里的同一张贴图会加载两次。用 pylib/mdb.rab_read 对游戏 Mods/OBJECT 里每次都预载的 16 个 EDF6VC_*.MRAB 逐个哈希贴图成员：贴图共 266.4 MB，其中重复副本 134.4 MB。最大的几组：HELI_TRANSPORT / INTERCEPTOR / JET / MULTIROLE 共用 helicopter_body_* 等，重复 40.2 MB；DN_DREADNOUGHT / EMP_LONG / EMP_MID / EMP_SHORT，重复 32.1 MB 加 10.7 MB；C130_GUNSHIP / C130_TRANSPORT / CARRIER / GUNSHIP，重复 14.2 MB 加 12.1 MB。把全部 EDF6VC 档案算上，重复贴图字节共 613.9 MB / 478 张，其中驾驶舱套件的 edf6vc_cpit_tank_gunner_metal_dark.dds 出现在 50 个档案里，cpit_tank_gunner_station0/2/3 各出现在 8 个档案里。颜色变体之间的重复（如 Begaruta 7 色共用 nm）只在同一任务出现多个颜色时才有代价。
- **方案**：在生成器 / 安装链里，把一定同时驻留的模型合并进同一个档案：多个 MDB 加一份共享贴图，各 SGO 的 animation_model 指向 [共享档案, 各自 mdb]。原版和插件早期都有一个档案放多个 mdb 的先例，EDF6VC_JET.MRAB 曾同时含 bomber501 和 bomber501_2（docs/model-optimisation-2026-10-07.md:29）。先合并飞机组（JET / INTERCEPTOR / MULTIROLE / HELI_TRANSPORT）、无畏舰组（DN_DREADNOUGHT + EMP_*）、C-130 组。驾驶舱套件贴图要看引擎能不能跨档案引用：不能的话，就按「同一任务常见同时出现的载具」分组合并，不要每辆车各塞一份。合并后在 tools/*_check 里加一项「同组档案之间没有重复贴图」的离线检查。
- **收益**：已验证的上限：每个任务少加载约 134 MB 贴图数据（显存，可能还有一份系统内存副本），加载时少解码和上传这么多数据，模型包也会变小。全集重复 614 MB 主要影响安装包体积和同场出现多种载具的情况。
- **风险**：合并档案会改变 SGO 的引用路径和安装归属账本（.edf6vc_files.json 的引用计数）。insert_member 要求成员按「贴图 → 模型 → HD 贴图」的顺序排列，否则模型会变黑。要实机确认多 mdb 档案在引擎里按路径缓存、第二个 SGO 不会重新加载。

### mem-3. 任务循环中内存持续上涨（约 9 MB/轮），第一次任务后基线多出 0.7–0.9 GB，需要先归因（M · G）

- **问题与证据**：已验证（日志数据），来源未定：EDF6VehicleCrew.log.1 中同一进程连续进出测试场（13:53-14:00）共 19 轮，mission start entered 时的工作集依次为 616→626→629→645→658→670→680→695→706→710→741→755→752→751→770→767→779→780→775→785 MB，单调上涨约 9 MB/轮；同一时间峰值提交从 6199 MB 涨到 7022 MB。第一次进任务时提交为 1370 MB，之后每次进任务都在 2.2–2.5 GB（偶尔 3.1–3.6 GB）。EDF41 M104 连续三次的 the map in 提交为 3581→4265→4287 MB。插件的预载在资源组 2，离开任务时由 FreeResourceGroup(2) 释放（docs/mission-cycle-re.md:15-33），fieldshare 也没有残留（ResetTerrainShare 的 still shared 日志为 0 条）。所以上涨来自原版缓存、原版泄漏，还是插件某处，目前都不能确定。
- **方案**：用 tests/autopilot 跑同一任务进出 20 轮，做两组：原版（不装插件）和全插件。每组在第 2 轮和第 20 轮用 UMDH（gflags +ust）或 ETW heap 快照做差，按调用栈的模块归因；同时在 LogMemory 里加上进程堆 HeapSummary 和 Havok 内存统计。定位到插件栈就修对应的生命周期；定位到原版，就逆向它的缓存（例如 MODE 组资源、Umbra、音效节点：edf41sound.cpp 注释提到 0x7B2500 会把空节点追加进 SeManager 列表）是否该在任务结束释放。
- **收益**：推测：长时间游戏（50+ 个任务）可少涨约 0.5 GB 工作集，峰值提交也不再越跑越高，主要关系到低内存机器的长局稳定性。
- **风险**：只是测量和归因，本身没有风险。最后的修复如果落在原版代码，需要额外的 hook，风险另行评估。

### mem-4. EDF6VehicleCrew.dll 的 .data 段为 120 MB，大部分是敌人操控 / 支援的静态网络缓冲（M）

- **问题与证据**：已验证：pefile 读游戏目录里的 EDF6VehicleCrew.dll，.data 段 VirtualSize 120.42 MB、RawSize 15.0 MB，DLL 文件共 24 MB，相比之下 EDF6Coop 的 .data 只有 3.96 MB。用 llvm-pdbutil 读 D:/APP/edf6-wt-1124-ui-rewrite/build 的 PDB，按符号地址差排序：enemy_native::walking_net::streams 23.9 MB（src/enemy_control_walking_net.cpp:50，Stream 含 Part guns[256] 和 Event queue[4096]），walking_parts::streams 16.6 MB，monster_net::streams 14.5 MB，enemy_native::transactions 13.2 MB（enemy_control_native_pose.cpp:45），crew::deployments 9.4 MB，deiroi_net 7.9 MB，merman_net 4.8 MB，boss_gun_net::streams 3.85 MB。boss_gun_net 那一份带非零默认初始化，进了文件里的已初始化数据，这类数据也是 DLL 大到 24 MB 的原因。这些都是可写的映像段，加载时就按整段计入提交（推测，按 Windows 映像映射的常规行为，本机没有单独测量）；没进入敌人操控的玩家也要付这笔。
- **方案**：把 Stream / Transaction 这类大数组改成在第一次安装或第一次进入敌人操控会话时，用 std::unique_ptr<Stream[]> 一次性分配，之后不释放，指针稳定性和现在一样。现有的 `for(auto& s:streams)` 遍历改成遍历这块分配。带非零默认值的大结构改成全零初始化（或首次分配时再填值），让它退回 bss、不再撑大 DLL 文件。在 tests 里加一条守卫：PE 的 .data VirtualSize 不得超过阈值（例如 16 MB）。
- **收益**：约 −105~120 MB 进程提交（没用敌人操控的会话里全部省下），DLL 文件约 −15 MB（下载和加载都更快）。工作集收益小，因为这些页大多没被触碰过。
- **风险**：涉及 7 个以上文件，生命周期本身简单，但要保证安装失败、分配失败时整个功能关闭并记一行日志，不能留半初始化状态。原有的零初始化语义要保持一致。

### mem-5. 插件预载阶段单次排队就阻塞游戏线程 1.4–3.4 s，需要确认会不会拉长任务加载（S · G）

- **问题与证据**：已验证（日志）：M001 的 MISSION phase 计时中，airdrop container 2344 ms（PreloadAirdrop 只调用一次 0x7A3780，airdrop.cpp:154），support soldiers 1719 ms，测试场的 jets 2796 ms、debug spawn 3406 ms、support soldier weapons 1406 ms。0x7A3780 一开头就在 mgr+0x98 上加锁（0x7A37C8 lea rdx,[rcx+0x98]，接着 call 0x50bc0），推测是和原版加载线程抢同一把锁。插件这段在 MissionStart 里同步执行，M001 从 entered 到 mission start 为 4.1 s，测试场为 9.1 s。这段阻塞会不会拉长总加载时间（从 entered 到 the map in：M001 12.9 s）没有测过，因为加载线程本来就在忙。
- **方案**：先测量：和第 1 条的 A/B 一起记录 entered → the map in 的时长。如果确实会拉长加载，就把插件的排队移到原版加载线程空闲时，或者改成第 1 条的按需加载；不要用延迟去绕开锁。
- **收益**：推测：0–4 s / 任务的加载时间，需要实测确认。
- **风险**：只测量没有风险。改动时机则要遵守「0x7A3780 只在加载期安全」的旧结论（docs/decoy-blast-re.md:8），这条结论和 mapui 文档里「任务中途也会被处理」的说法冲突，需要先核实。
- **依赖**：插件每个任务无条件预载全部呼叫资源，改为按需异步加载或按房间实际可呼叫的并集加载

### mem-6. EDF6AutoTurret 的日志没有大小上限（本机已到 70 MB）（S）

- **问题与证据**：已验证：游戏目录 Mods/Plugins/EDF6AutoTurret.log 有 70,880,606 字节、37 万行，Debug=1 时大量 GUNNER / ON 行。common/append_log.h 只管追加写、从不轮转。VehicleCrew 有 kLogMax=32 MB，超过后改名为 .log.1（src/plugin.cpp:1179-1212），AutoTurret 没有。ini 默认 Debug=0（autoturret/EDF6AutoTurret.ini:11），所以主要是测试机和开了 Debug 的玩家受影响。
- **方案**：把 VehicleCrew 的轮转逻辑挪进 common/append_log.h：写入时累计字节数，超过上限就关闭句柄、改名为 .log.1、重新打开。两个插件共用这一份，删掉 VehicleCrew 里的私有实现。
- **收益**：磁盘占用有上限（每个插件 ≤64 MB），也避免玩家回传日志时单个文件过大。
- **风险**：轮转期间别的进程可能占着 .log.1，要沿用 VehicleCrew 现有「改名失败就记日志、继续写」的处理。

### mem-7. edf41dragon 的 customCopies 每生成一条龙就加一份副本，不去重也不释放（S）

- **问题与证据**：已验证：src/edf41dragon.cpp:170 的 customCopies 注释写明「never freed」，CopyOf（:201-206）每次 Configure（:245，每条 4.1 龙）都 push 一份新副本。而同类需求的 edf41classes.cpp:189-372 Persist 已经按 FNV 哈希按内容去重（floatgun、robo、mother 都走它）。单份只有几百字节，量很小，但属于只增不减的容器。
- **方案**：CopyOf 改为调用 edf41c::Persist（内容相同就复用同一份），删掉 customCopies。
- **收益**：长局里不再按刷龙次数线性增长（量级 KB），同时统一成一套持久节点实现。
- **风险**：Persist 用的节点布局（SgoNode）要和 dragon 的 Node 布局一致，需要核对字段和偏移；不一致就只给 CopyOf 加同样的哈希去重。

## 工程可维护性与开发效率（eng）

现状：插件 246 个 .cpp（约 16 万行）。CMakeLists.txt 有 2754 行，里面 302 个可执行目标、468 个 add_test。tools/selftest.py 一个文件 16050 行。全套检查的墙钟由 ctest 决定：最近一次是 506 s，同时跑的 selftest 是 360 s。冷编译 CPU 约 2760 s，其中测试目标的编译和链接约 1700 s，比插件本身的 891 s 还多。
最大的问题有两类。
(1) 测试门有漏洞，而且很慢：需要 EDF.dll 的 108 个 native 审计，build.cmd 和 check.py 都从不传 EDF6_NATIVE_DLL，所以本机和 CI 永远是 Skipped。少数几个长测试（tracked_range 328 s、spawn_catalog 分片不均 119–357 s、cockpit_build 8 片各约 190 s）决定了整套检查要等多久。
(2) 改动热点集中在几个大文件上：近一个月 831 个提交里，CMakeLists 被改 182 次，selftest 173 次，crew.h 95 次。crew.h 被 190 个 TU 包含，改一次就要全量重编。
裸 RVA 没有集中登记：同一地址最多被 6 个不同名字定义在 25 个文件里。配置有 472 个键，同一个键要在 crew.h、plugin.cpp、ini、README 四处手工同步。
建议先做：① check.py 用 pylib/gamedir 自动给 ctest 传 EDF6_NATIVE_DLL / EDF6_GAME_DIR（S，立刻多出 108 个真校验）；② 长测试共享缓存并按耗时分片（M）；③ 把 Config 拆出 crew.h，用一张配置表生成（M）。之后再收拢 CMake 测试 helper 和 RVA 登记表。
以上数字都来自本 worktree 的 grep 和 D:\APP\edf6-wt-1124-ui-rewrite\build 下的 check-*.log、.ninja_log。收益里标了「推测」的没有实测。

### eng-1. check.py 自动提供 EDF6_NATIVE_DLL / EDF6_GAME_DIR：解除 108 个永远跳过的 native 审计（S）

- **问题与证据**：已验证：在 D:\APP\edf6-wt-1124-ui-rewrite\build 一次真实的 check.py 运行里，CMakeCache.txt:234/237 中 EDF6_GAME_DIR 和 EDF6_NATIVE_DLL 都为空，check-ctest.log 里有 108 个用例 Skipped（占 661 个的 16%），包括 enemy_control_native_audit、stab_native_readback、mission_participants_native 等。CMakeLists.txt 里有 129 处传入 ${EDF6_NATIVE_DLL}，有 128 处设了 SKIP_RETURN_CODE 77。build.cmd:6 配置时不带 -D；tools/check.py 全文不出现 EDF6_NATIVE_DLL；CI（.github/workflows/build.yml:63）没有游戏。所以这些「源码签名逐字节对 EDF.dll」的审计在任何常规门禁里都不跑，只有手工设缓存变量时才跑。selftest 那边已经通过 pylib/rootcpk.py:16 DEFAULT_GAME=gamedir.find_or_dev() 自动找到游戏，所以 spawn_catalog 跑了 350 s；ctest 却不找。两边口径不一致。另外 CMakeLists.txt:1380 和 :1525 把同一个缓存变量 set 了两次。
- **方案**：check.py 在调用 build.cmd 前用 pylib/gamedir.find() 定位游戏目录，再以 -DEDF6_GAME_DIR=<dir> -DEDF6_NATIVE_DLL=<dir>/EDF.dll 配置（build.cmd 接受透传参数）。先校验 TimeDateStamp 0x678CCB46，不符就报错退出，不悄悄跳过。删掉 CMakeLists.txt:1525 的重复 set。check.py 结尾打印「native 审计实际执行数 / 跳过数」；本机有游戏时 Skipped>0 视为失败。这些审计只读 EDF.dll 文件，不启动游戏，不受游戏是否运行影响。
- **收益**：本机门禁立刻多出 108 个签名 / 原版字节校验，覆盖率从 84% 到接近 100%。补丁地址漂移、签名表与源码不一致这类问题能在推送前发现，不用等实机日志里出现 REFUSED。耗时增加不多：从日志看单个 native 审计 0.1–数秒，个别 havok 模拟另算（推测）。
- **风险**：首次打开后可能有一批审计已经腐化而变红。这正是要暴露的东西，但需要预留一轮修复。pefile 必须装在本机的 Python 3.12 里。

### eng-2. 长测试拆尾巴：共享生成载具的构建缓存，按实测耗时分片（M）

- **问题与证据**：已验证（check-ctest.log / check-selftest.log）：ctest 墙钟 506 s，由单个用例决定：tracked_range 328.6 s（tests/test_tracked_range.py，没有分片）、edf5_campaign_lifecycle 263 s、wt_tracked_0..3 为 150–240 s、cockpit_build_0..7 为 81–216 s。selftest 墙钟 360 s，就是 spawn_catalog_archives_recorded 分片中最长的一片：4 片分别是 357/348/175/119 s。分片方式是 tools/selftest.py:3345 的 rows[shard::4]，按名字轮转发牌，生成载具在各片之间不均。docs/agent-rules-detail.md:20 规定「超过约 3 分钟就拆分片」，以上至少 5 个用例违反。spawn_catalog 的 docstring 说明每次都要「building its kind」重建生成载具；cockpit_build / wt_tracked / tracked_range 也走生成载具的安装路径。几个测试是否在重复构建同一批载具，属于推测，需要 profile 确认。docs/feedback-2026-11-21-landing-slide.md:102 记录过一次 check.py 用时 1376 s。
- **方案**：① 在 pylib 加一个按内容寻址的构建缓存，键为生成器源码哈希、输入文件（Root.cpk 内条目大小 / 哈希）和参数，产物放 build/cache/，供 spawn_catalog、cockpit_build、wt_tracked、tracked_range 共用。缓存只对确定性纯函数生效，键不全就不缓存。② 分片改成按 selftest 已记录的上次耗时做 LPT 装箱（selftest 已有按耗时排序的数据，tools/selftest.py:15848），不再轮转。③ tracked_range 和 edf5_campaign_lifecycle 按用例拆成 ctest 分片（照 EDF6_TEST_SHARD 的现成写法）。
- **收益**：推测：ctest 墙钟可从约 506 s 降到约 250 s（最长单例降到约 200 s 以内），selftest 从 360 s 降到约 200 s。缓存命中时，重复构建的 CPU 可省下数百秒，check.py 排队时间也会随之下降（全机只跑一套，check.py:11）。
- **风险**：缓存键漏了输入会掩盖真回归，所以键必须包含生成器全部源码，并另设一个定期全量 --no-cache 的入口。不允许用墙钟判断时效（规则禁止），只能用内容哈希。

### eng-3. 把 Config 从 crew.h 拆出，并瘦身伞形头 crew.h（可选再加 PCH）（M）

- **问题与证据**：已验证：190 个 .cpp 包含 crew.h（全部 246 个）。crew.h 有 1631 行，开头包含 <Windows.h>，在 1609–1623 行又包含 core/ground/heli/hud/nix/vhud/payload/map/mapcmd/proteus/npcai 等 24 个功能头。struct Config 定义在 crew.h:21，大约 520 行，所以每加一个 ini 键都要改 crew.h。近 30 天 crew.h 被 95 个提交修改。.ninja_log 显示插件 250 个 TU 共 891 CPU-s，按比例推算，改一次 crew.h 要重编约 190 个 TU，约 680 CPU-s，还没算包含 crew.h 的测试目标。全仓库没有 target_precompile_headers 或 UNITY_BUILD（grep 无结果）。
- **方案**：① 新建 src/config.h 只放 Config 和 Cfg()，用到配置的 TU 显式包含它，最好由第 4 条的配置表生成。② 删掉 crew.h 底部的 24 个功能头包含，改为各 .cpp 按需包含，用 include-what-you-use 或一次编译报错清单机械补齐。③ 插件目标加 target_precompile_headers(EDF6VehicleCrew PRIVATE <Windows.h> <cstdint> <cstring> <vector> <string>)，只放系统头，不放 crew.h。
- **收益**：加配置键 / 改功能头时，增量重编从约 190 个 TU 降到只编直接使用它的 TU（推测是个位数到几十个）。改动迭代时每次少等 30–60 s（16 核，推测）。PCH 预计再省掉 10–20% 的冷编译 CPU（推测，没测）。
- **风险**：机械改动面大，会和在飞的功能分支冲突，应选合并低谷期一次做完。/W4 /WX 下缺包含会直接报错，不会悄悄改变行为。

### eng-4. 配置项单一事实源：一张配置表生成 Config 字段、读取、范围校验、退役键和通用一致性检查（M）

- **问题与证据**：已验证：EDF6VehicleCrew.ini 有 1416 行（124 KB），472 个键，全部在 [VehicleCrew] 一节。src/plugin.cpp 的 LoadConfig（:534）有 ReadFloat 191 次、ReadBool 147 次、ReadInt 123 次，而 Fix/FixInt 等范围钳制只有 117 处，大部分浮点键没有范围校验。ReadBool 用 GetPrivateProfileIntW（plugin.cpp:120），'yes' 之类的非数字值会悄悄变成默认值，也不记日志。同一个键要手工写在 crew.h 的字段、plugin.cpp 的读取、ini 和 README 四处。selftest 里为单个功能逐一写了「键在 plugin / ini / README 都有」的正则检查（例如 tools/selftest.py:1856–1863，共引用 README 73 次），没有一条通用检查。近 30 天 ini 被改 70 次，README 193 次。另外，每次 ini 变化，ReloadConfigIfChanged 都在载具输入钩子里、也就是游戏线程上执行（src/crew.cpp:942），一次 LoadConfig 约 470 次 GetPrivateProfile*，每次都重新解析 124 KB 的文件。可能造成卡顿，这一点属于推测，没测。AutoTurret 在 autoturret/src/plugin.cpp:830–873 重复了同样的模式。
- **方案**：新增 src/config_keys.inc（X-macro 或 Python 表加生成器，照 gen_calls.py 的 --check 做法），每行写明类型、字段、键名、默认值、上下限和 README 小节。由它生成 Config 字段、LoadConfig 的读取与钳制、IgnoreRetired 列表。读取改成整份文件只解析一次到内存表，再逐键取值。bool 严格解析（0/1/true/false），其余值记 CONFIG 日志。selftest 换成一条通用检查：表里的键和 ini 双向一致、和 README 一致、和退役表互斥，然后删掉逐功能的同类检查。AutoTurret 复用同一套读取器。
- **收益**：加一个键从改 4 处变成改 1 行。约 350 个缺范围校验的数值键得到统一的钳制和日志。可以删掉数十条手写的 selftest 正则（推测 30–60 条）。热重载时 I/O 从约 470 次文件解析降到 1 次。
- **风险**：生成后的默认值必须和现有字段初始化逐个对齐，需要一个对照测试：新旧 LoadConfig 对同一份 ini 结果按字节相同。改 ini 里的键名会影响玩家已装的 ini，所以这次只改结构，不改键名。

### eng-5. CMake 收拢：测试 helper 函数加 tests/CMakeLists.txt，插件源码改 glob（M）

- **问题与证据**：已验证：CMakeLists.txt 有 2754 行，近 30 天被 182 个提交修改（同期共 831 个），是冲突热点，有 121 个提交信息涉及 merge/冲突。`/W4 /WX /utf-8 /permissive-` 重复了 300 次，add_dependencies(offline_checks …) 133 处，target_include_directories(... src …) 113 处。现在只有一个 helper：edf6_feedback_fixture（:2434，71 处使用）。插件源码分散在 120 条 target_sources(EDF6VehicleCrew …) 里，例如 :2177 夹在测试定义之间；:13 的注释写明「每个功能分支各加一行」以减少冲突。find_package(Python3) 在 :982 和 :1291 出现两次。核对结果：246 个 src/*.cpp 全部属于插件目标，没有例外，因此可以直接 glob。
- **方案**：① 加 function(edf6_cpp_test name SOURCES … [ARGS …] [NATIVE] [TIMEOUT n])，统一处理编译选项、include、链接 edf6common、加入 offline_checks、add_test，以及 NATIVE 时的 SKIP_RETURN_CODE 77 和 ${EDF6_NATIVE_DLL} 参数。Python 用例同理用 edf6_py_test。② 全部测试移到 tests/CMakeLists.txt（add_subdirectory），按领域 include 子文件。③ 插件用 file(GLOB CONFIGURE_DEPENDS src/*.cpp)。④ 去掉重复的 find_package / set。
- **收益**：测试定义从平均约 5 行降到 1 行，根 CMakeLists 预计从 2754 行降到约 1000 行（推测）。新增功能不再碰插件源码列表，CMakeLists 的合并冲突基本消失。漏挂 offline_checks 或漏设 SKIP 77 这类错误由 helper 从结构上杜绝。
- **风险**：一次性大改，需要校验 ctest -N 的用例名集合和改前完全一致（脚本比对）。glob 会把误放进 src/ 的临时 .cpp 编进插件，靠 /WX 和评审把关。

### eng-6. 测试目标别再 #include 生产 .cpp 和其他测试 .cpp：生产代码编一次成 OBJECT 库，按领域合并测试 exe（L）

- **问题与证据**：已验证：166 个测试 / 工具 .cpp 用 #include "../src/x.cpp" 引入生产代码，共 176 处，涉及 138 个不同文件。另有测试互相包含，例如 sub/carrier/support_vehicle_route 这 3 个测试包含 support_dispatch_test.cpp，用 #define main 改名（tests/sub_support_dispatch_test.cpp:2–4）。结果，.ninja_log 里最慢的 5 个 TU 是 support_dispatch_test 一族，每个 14.6–18.6 s，同一份代码编了 4 次。冷编译中测试对象占 1112 CPU-s，链接 389 次占 437 CPU-s，生产 src 在测试里重编 61 个 TU 占 152 CPU-s，合计超过插件本身的 891 CPU-s。
- **方案**：① 需要测内部细节的模块，把匿名命名空间里的内容移到 x_internal.h 的 detail 命名空间（已有 *_logic.h 的先例），测试只包含头文件，生产 .cpp 编进一个 OBJECT 库 edf6vc_objs，插件和测试共同链接（不需要符号的测试链接 logic 子集）。② 测试之间共享 fixture 时，改为公共 fixture 头加 .cpp 库，删除 #define main 的套娃写法。③ 同领域的小测试合并成一个 exe，用 --case 参数供 ctest 逐例调用，减少链接次数。
- **收益**：推测：冷编译 CPU 减少 30–40%（约 800–1100 CPU-s），offline_checks 的增量构建也变快，最慢的几个 TU 不再出现 4 倍重复。
- **风险**：OBJECT 库里的全局状态会被多个测试 exe 共享初始化，需要逐模块核对静态初始化和钩子安装副作用。工作量大，应按领域分批迁移，每批对照 ctest 通过数。

### eng-7. 裸 RVA 集中登记：src/edf_rva.h 加棘轮检查（M）

- **问题与证据**：已验证（脚本扫描 src/ 和 autoturret/src/ 中 kXxx=0x… 的定义）：共 997 个具名 RVA 常量。其中 148 个地址在多个文件里重复定义，97 个在不同文件里名字不同。0x20B2958 出现在 25 个文件，有 kObjectManager、kManagerGlobal、kBulletMgr、kCameraMgr、kHavokGlobal、kManager、kSystem（crew.cpp:63）、kSceneAt 等名字；src/scene_object.h:17 已经有 kObjectManager，但多数文件没有用它。0x20B29A8 在 airdrop / debug_spawn 里叫 kPreloadMgr，在 outfit41.cpp:73、worldmap.cpp:62、keyconfig_menu.h:93 里叫 kTextTable；0x1194280 被叫作 kEffectCreate / kFactory / kSpawn / kUnit。语义是否矛盾是推测，需要用 vanilla-code 核实。0x20B2978 在 20 个文件里，0x20B2890 在 19 个文件里。src/code_signatures.h 只被 4 个文件使用。
- **方案**：新增 src/edf_rva.h（命名空间 edf::rva），按全局对象、函数、vtable 分组，名字取自 D:\APP\edf6-all-forces\vanilla-code 索引里的符号，每条附一行文档出处。加一个离线检查 tools/rva_registry_check.py：扫描 src 里 0x1000–0x2200000 范围的字面量，登记表外的新出现一律报错，已有的进 allowlist 并只许减少（棘轮）。优先迁移上述高频全局（约 10 个地址覆盖上百处）。
- **收益**：换游戏版本或者核查一个地址时，只需看一处。同址异名的困惑被消除，0x20B29A8 / 0x1194280 这类可能的误名会暴露出来。新代码不能再散写 RVA。
- **风险**：迁移纯属改名，可以分批做。棘轮 allowlist 的初值大约有几千行，需要生成而不是手写。

### eng-8. 补丁框架补上补丁所有权和重叠检测，加一条汇总签名审计（M · G）

- **问题与证据**：已验证：common/patch.cpp 的 PatchCode / PatchVtableSlot 只比对 expected 字节，不记录是谁打的补丁。plugin.cpp 的初始化（:1346 起）手工排了约 110 个 Install*() 调用，依赖关系只靠注释说明，例如「before the jets」「after the airdrop」。65 个文件调用 PatchCode / VtableSlot；8 个文件直接调用 VirtualProtect，绕开了 common 层，例如 enemy_control_pvp_berserker.cpp（4 处）、edf41natives.cpp、physics.cpp、viewdiag.cpp。用统一 CheckSignatures 表的只有 4 个文件，其余模块各自内联 Matches。tests/ 和 selftest 里没有「不同模块的补丁区间不重叠」的检查（grep 无结果），也没有一个用例一次性对 EDF.dll 校验全部模块的签名。两个模块写同一段代码时，现在的行为是第二个模块 expected 不匹配后自己拒绝，日志里只写 REFUSED，看不出和谁冲突（推测：靠实机日志才能发现）。
- **方案**：① edf::PatchCode / PatchVtableSlot 增加 owner 标签参数，在进程内登记表里记录 (rva, size, owner)，新补丁与已有区间重叠时拒绝，并记录「与 <owner> 冲突」。卸载按登记表逆序恢复。② 8 处直接 VirtualProtect 改用 common 原语。③ 各模块的签名都收进 CodeSignature 表，导出一个只读枚举接口。加一个 native 用例，把所有表对 EDF.dll 校验一遍，并检查区间两两不重叠（依赖第 1 条）。④ 把 Install 顺序的依赖写成表（模块、前置模块），启动时校验拓扑序。
- **收益**：补丁冲突从实机日志前移到离线门禁。新增模块不用再读 100 多行注释来判断插在哪里。卸载 / 关闭单个功能时有可靠的恢复路径。
- **风险**：有意为之的多模块串联（ChainVtableSlot）要在登记表里允许链式共存，不能误判为冲突。改的是插件启动路径，至少需要一次实机或 autopilot 冒烟。
- **依赖**：条目 1（native 审计在门禁中实际运行）

### eng-9. 拆分 16050 行的 tools/selftest.py，清理源码字符串断言（M）

- **问题与证据**：已验证：tools/selftest.py 有 16050 行、422 个函数，近 30 天被 173 个提交修改（冲突热点）。约 459 行是 `assert '…' in code/plugin/hud…` 这种逐字源码断言，另有 38 处 re.search(…, code)，再加 23 个 tests/*_guard.py。这类断言把具体写法焊死，例如 tools/selftest.py:3312–3321 断言 debug_spawn.cpp 里某一行循环的原文，纯重构也会变红，抬高改动成本。
- **方案**：① 改成 tools/selftest/ 包，按领域分文件（install_chain、calls、models、edf41、guards …），沿用现有的 @test 注册和按耗时调度，入口不变。② 能被第 4、7、8 条的通用结构检查覆盖的逐字断言直接删掉。只留真正的契约断言（例如 ABI 常量），并尽量改成编译期 static_assert 或行为测试。③ 新规则：禁止新增「整行源码原文」断言。
- **收益**：合并冲突明显减少。纯重构不再被字符串守卫拦下。推测可删 150–250 条低价值断言。
- **风险**：删断言前要逐条判断它守护的是契约还是实现细节，宁可先改成结构检查再删。拆文件后 selftest 的耗时调度（上次耗时记录）需要保留。

### eng-10. 统一游戏目录定位与 Python 路径引导（S）

- **问题与证据**：已验证：pylib/gamedir.py 已经实现完整的查找（$EDF6_DIR → 程序所在目录 → Steam libraryfolders → DEV_DIR），但约 20 个文件仍硬编码 D:\steam\…\EARTH DEFENSE FORCE 6，并混用三个环境变量名：tools/atlas_fuzz.py:20 与 tools/hud_view.py:33 用 EDF6_DIR；tests/test_edf41bike.py:26、test_edf41heli.py:27、test_goldcoat.py:36、test_xm_maps.py:329 用 EDF6_GAME_DIR；tools/edfre.py:10、tools/make_worldmap.py:33、tools/make_edf5_dlc.py:62 完全写死。202 个 py 文件各写 sys.path.insert 样板，前几种写法各重复 70/43/32/23/21/17 次。
- **方案**：所有游戏路径一律走 gamedir.find() 或 find_or_dev()，只认 EDF6_DIR 这一个环境变量（CMake 缓存变量由 check.py 从它派生，见条目 1）。ctest 用 ENVIRONMENT PYTHONPATH=pylib;tools;testrange 统一注入，check.py / selftest 同样处理，并删掉各文件的 sys.path.insert。加一条守卫：禁止再出现字面量 'EARTH DEFENSE FORCE 6'（gamedir.py 除外）。
- **收益**：换机器或换盘符时不再有一批测试悄悄跳过或读到错误目录。删除约 200 处样板。
- **风险**：PyInstaller 冻结的安装器依赖 sys._MEIPASS 路径，要保证 installer.py 的引导不受影响（gamedir 本身已处理 frozen）。

### eng-11. 日志系统收进 common：一套异步写入器，带标签和限频（S）

- **问题与证据**：已验证：EDF6VehicleCrew 的 Log（src/plugin.cpp:1247）是异步缓冲写线程，时间戳精确到毫秒。EDF6AutoTurret 的 Log（autoturret/src/plugin.cpp:138）通过 common/append_log.h 在调用线程上同步 WriteFile，时间戳只到秒，所以两份日志无法按时间对齐。没有等级和类别，限频各写各的：edf41insects.cpp:73 和 edf41smalldragon.cpp:58 各有一个 LogLimited。src 里有 2171 处 Log 调用。
- **方案**：把 plugin.cpp 里的 logBuffer / LogWriter / CrashLog 移到 common/log.{h,cpp}，两个插件共用（AutoTurret 也得到异步写入和毫秒时间戳）。提供 LogOnce(tag)、LogEvery(tag, ms) 和 Debug 等级（受 ini Debug 控制），替换各模块自造的限频。
- **收益**：两个插件的日志可以按毫秒对齐排查。AutoTurret 不再在游戏线程上同步写盘。去掉重复的限频代码。
- **风险**：崩溃时的落盘路径（CrashLog / AppendCrashLine）必须保持原有语义，迁移后用 tests/crash_dump_test.cpp 回归。

### eng-12. 文档与代码交叉引用守卫（S）

- **问题与证据**：已验证（脚本比对）：代码里引用了不存在的文档 docs/debug-spawn-re.md 和 docs/member-attribute-api.md，见 src/coop_attr.h、src/coop_member_attribute_api.h、src/room_sync.h、src/scene_object.h。docs/*.md 引用了约 21 个本仓库不存在的代码路径，例如 src/hitauth.cpp、src/networld.cpp、src/spawn.cpp、pylib/cas.py、tests/keyclaim_test.cpp、tools/sgo_write.py。其中一部分属于其他仓库（docs/enemy-control-network-contract.md:11 指向 multislot/），没有标明仓库；一部分是改名或删除后遗留的。项目规则要求以 docs 中的逆向笔记作为依据，坏链接会把人引到错误位置。
- **方案**：在 check.py 的 gen 阶段加 tools/doc_links_check.py：检查代码里的 docs/*.md 引用必须存在；docs 里的 src/、tools/、tests/、pylib/ 路径必须存在，或者带仓库前缀（例如 multislot/、edf-coop-stable:）。修复现有约 23 处。
- **收益**：文档和代码的错位在提交时就能发现，后续改名、删除文件不会再留下死链。
- **风险**：很低。历史反馈文档（feedback-*，用户原话存档）可以加入白名单，不改原文。

