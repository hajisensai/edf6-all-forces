# 2026-10-09 用户反馈：M 地图指挥界面（map 组）

分支 `fix/fb1009-map`，基于 `origin/main` b89d26a（#92）。行号：「原」指 b89d26a，其余指本分支。
**本文所有改动只做了离线验证（编译、离线 check、`hud_view` 布局图），没有进游戏实测。**

## 1. 「载具上的npc还标着可以招募的标记」

### 根因

M 地图把「坐在载具里的士兵」当作一支步行的自由小队来画、来列、来提供招募，三处都没有把「在座位上」放进判据：

- 标记：`src/map_marks.h:37`（原）`FriendlyMark` 对非载具对象只看阵营，坐在座位上的士兵照样画一个本队 / 友军方块，叠在它的载具上（`src/map.cpp:296` 原 `WalkVisit` 的友方遍历本来就会走到座位上的士兵）。
- 面板与按钮：`src/npcai.cpp:1241`（原）`StatusOf` 只看控制状态，乘车小队显示「自由行动」(`FREE`)；`src/mapcmd.cpp:220`（原）`Takes` 对小队一律返回 true，选中乘车小队时「招募」按钮亮着。
- 执行：`src/npcai.cpp:2054-2057`（原）`recruit` / `follow` 不查是否在座位上，对乘车小队直接 `SetFollow`。

另查了原版：EDF.dll 里读士兵 `+0x540`（可自动入队）字节的只有 `0x5956CF`、`0x596BD8`（士兵 Think 的「走近玩家自动入队」）和 `0x59B4CF`，HUD 区（`0x7C0000..0x900000`）没有读 `+0x540` / `+0x548` 的代码，所以没找到「原版画招募标记」这条路径——用户在 M 里看到的就是上面插件自己的这几样。原版 Think 对座位上的士兵是否还会跑 `0x596BD8` 那段自动入队，**没有查清（L）**。

### 修法

- 一个判据 `mapcmd::OffersRecruit(玩家的, 脚本的, 解散冷却中, 乘车中)`（`src/mapcmd_logic.h`），npcai 用它给每个小队算 `recruitable`（`CommandUnit::recruitable`），`Takes` 的 RECRUIT 只认它；乘车小队还不接 follow / 交战 / 集火 / 点位命令（这些交给它的载具），只接「下车」和「解除」。
- `StatusOf` 乘车时显示 `RIDING`（乘车中，`hudtext.h` 词表加了这个词）。
- `NpcSquadCommandForRequester` 的 recruit / follow 对乘车小队返回新原因 `NpcCommandReason::riding`（追加在枚举末尾，提示「该小队在载具上：请指挥它的载具（或先下令下车）」）。
- `map_marks.h` `Seen` 加 `riding`：座位上的士兵不再单独画标记，由载具标记代表（`map.cpp WalkVisit` 用已有的 `Body()` 判定）。

### 测试

- `map_marks_check`：乘车士兵（本队 / 友军）无标记、载具照常。
- `map_cmd_check`：`OffersRecruit` 各组合。
- `map_command_runtime_check RtsClicks`：乘车小队 `Takes` 不给 recruit / follow / move / guard，给 dismount / 解除。
- `npc_core_check`：桩车里的士兵 recruit 被拒为 `riding`、`SquadRecruitable` 为假、状态 `RIDING`，move 不接；下车后恢复可招募。

## 2. 「支援招募弄成一列带图标的 HUD 并且可以点击……显示太复杂……参考 RTS」

### 原状

支援在底部按钮流里只有「上一支援 / 下一支援 / 呼叫 %ls」三个长文字按钮（`src/hud.cpp:3573-3575` 原），要一项项翻；底部按钮 17 个，每个都写着按键；底部还有两行按键说明、左侧 15 行常驻图例。

### 修法（只改地图 UI 与调用接口，不改支援后端逻辑）

- **支援栏**（`hud.cpp MapSupportBar`，纯布局 `map_buttons.h GroupSupport / Column`）：地图左侧、小队面板下方一列。目录名按「·」前的部分分组（`截击机·守点 / ·跟随` 一行），每行一个矢量图标 + 名称，变体在行右端做成小图标块（守点=盾、跟随=箭头、有人=人头、空车交付=空框）。点行或块 = 武装这项支援（亮起），下一次左键点地图就是目的地，右键取消；标题行显示调度状态（冷却 N 秒 / 调度中 / 不可用，不可用时整列变暗）；鼠标悬停显示完整名称和用法。键盘 `[` `]` `C` 照旧。
- **支援的调用接口**（`src/support_call.h`，实现在 `support_dispatch.cpp` 末尾，只读）：`SupportCallIcon(index)`（按 `SupportAircraftSpec` 的机型 / 步兵 / 地面车种给图标种类）、`SupportCallVariant(index)`（守点 / 跟随取自 `SupportAircraftSpec.follow`，有人 / 空车交付取自地面目录的 `SupportCrewMode`；变体图标不靠匹配目录文字，HUD 源码里不留字面文字，`selftest hud_text_localized`）、`SupportCallReadiness()`（读 `SupportCallAt` 本来就查的 `offlinePending` / `callAt` 冷却 / 联机，不改任何状态）。
- **命令卡**（`hud.cpp MapButtons`）：按钮 = 图标 + 短词，**只显示当前选中单位能执行的命令**（`mapbtn::Shown`，没选中时只剩「拾取箱子」「回复箱」），按键与完整说明移到悬停提示（`MapTipSet / MapTipDraw`，最后画、盖在最上面）。
- **精简**：底部按键说明从两行并成一行（没选中时是一句操作提示）；图例收成右下角「图例 ▾」小标签，悬停展开。
- 文字行上限 `kMaxLines` 96 → 160（地图的面板、命令卡、支援栏、提示全在同一帧）。

### 测试

- `map_buttons_check`：命令卡 `OrderOf / Arms / Shown`（载具选中 6 个、没选中 2 个、选中小队才有小队工具），支援目录分组（13 项 → 8 行，无「·」的不合并），`Column` 行与块的位置、互不重叠、块在行内，点击命中块 / 行 / 行间空隙。
- `map_command_runtime_check RtsClicks`：点支援行 = 武装（已发布给 HUD）、再点 = 取消、武装后左键点地图 = 在该处呼叫这一项，选择不丢。`SupportInput`：目录、当前项和结果发布给 HUD。
- `hud_view`（CTest `hud_layout`）：各分辨率 / 语言下按钮数等于该选择应显示的数目、每项支援都有可点区域、支援栏不压命令卡、面板文字不重叠。离线图 `build/hud-review/*/map_*.txt`（`python tools/hud_view.py`）。

## 3. 「没办法让 npc 移动攻击，只有守点，在打怪就没办法移动；图标为什么在 npc 竖直顶上」

### 根因

- 只有「守点」：守点把小队的锚点换成目标点（`npcai.cpp OrdersOf`），但 `Drive` 的移动按顺序取第一个适用的（`src/npcai.cpp:917` 原 `Evade` 躲避在最前，`:932` 有目标就去「战斗位」，`:935` 没目标才回守点）。在打怪时士兵一直在躲 / 站战斗位，命令点只影响战斗位的夹取范围，玩家命令排在士兵自主交战之后。用户 10-09 11:16:57 的日志就是这样：`MAPCMD GUARD (62,24,425)` 之后 20 多秒同一批 Fencer 位置一动不动、`move=combat spot`，用户随后连续重复下了几十次 GUARD。
- 图标位置：地图指挥圈、选择角框和点选判定都放在图钉顶上（`src/mapcmd.cpp:263-267` 原 `Marks` 给地面单位加 `PinHeight`，`src/hud.cpp:3644` 原用 `MapPin` 画），离士兵本体竖直一大截。

### 修法

- 新命令（`mapcmd_logic.h`，追加在 `Order` 末尾，线协议值不变）：**移动** `move`（强制移动：去目标点，路上不躲避、不后撤、不站战斗位，但照常转向并射击目标；到点后变成守点），**攻击移动** `attackMove`（路上有目标就就地交战，没有就继续前进，到点后变成守点）。纯逻辑 `PointOrder / PursuitOf / Arrive / RightClickOrder` 可离线测。
- `npcai.cpp`（改动集中）：`Drive` 在躲避之前先看 `PursuitOf`：`move` 或「没目标的 `attackMove`」走 `Pursue`（编队位或目标点，`FormationMove` 多给出到位距离），队长到位（6 米 + 无编队时守点半径一半）时 `Arrive` 把命令转成守点；路上的选目标改为围绕士兵自己（`npcLeash`）。命令处理里 move / attackMove 和守点同一套（`GuardOrder`、编队）。
- 地图操作（`mapcmd.cpp`）：**右键单击**（不拖动；右键拖动仍是旋转）地面 = 选中单位移动到那里，点在敌人上 = 标记并集火它；**Z** = 攻击移动到指针处；命令卡的「移动 / 攻击移动 / 守点」按钮按下后等下一次左键点地图，右键取消。G / V / X / H 等原有按键不变。载具单位收到 move / attackMove 时按守点执行（`VehicleCommandOf`）。
- 联机：命令协议 `kVersion` 2 → 3（新命令值、`riding` 原因、所有点位命令都带编队位），旧版本互相不接受，不会误执行。
- 图标位置：指挥圈 / 选择角框 / 点选判定统一用 `BodyPoint`（地面单位脚下 + 1.2 米，飞行单位就在本身）；地图上的本队 / 友军士兵方块也画在身上、不再竖杆（载具、飞机、大型敌人的图钉不变）。

### 测试

- `npc_core_check`（直接跑 `npcai.cpp Drive`）：同样的近身敌人，守点时士兵在躲 / 站战斗位（旧行为，证明根因）；移动命令时 `move order` 且目标仍锁定；攻击移动有敌人时交战、没敌人时 `attack-move` 前进；队长到点后两种命令都变成守点。
- `map_cmd_check`：move / attackMove 需要地面点、优先级、`PursuitOf`、`Arrive`、`RightClickOrder`、载具换成守点、线协议值（recruit 仍是 8）、`BodyPoint`。
- `map_command_runtime_check RtsClicks`：右键地面 = 移动、右键拖动不下令、右键敌人 = 标记并集火、Z = 攻击移动、移动按钮武装后左键给点、右键取消武装、没选中时右键无事。
- `command_protocol`：新版本号，更高版本的包被接收但不执行。

## 4. 「视角出了地图边界以后会一闪一闪的」

### 根因（静态推断，L）

地图镜头的焦点没有任何边界：跟随、平移、拖动、Tab 居中都直接写焦点（`src/map.cpp:625`、`:640`、`:545`、`:562` 原），镜头可以开到地图地面之外任意远。地面之外，游戏没有为这里准备画面：`playarea.h` 测得的「地面边缘」以外地图射线什么都打不到，能看到的只有远景 pass 才画的远景层（`view_clip.h`：远景 pass 从 500 米起画，镜头贴着它们时近于 500 米的部分被裁掉，移动 / 缩放时一块块出现消失），以及游戏可见性数据从未覆盖的位置。插件自己的每帧计算（`GroundAt` 焦点高度的缓动、`TopAt` 眼睛抬升）离线逐帧推演都是收敛的，没有在两个值之间来回跳的反馈，所以闪烁来自镜头所处的位置本身。**这一点没能在游戏里确认，是推断。**

### 修法

- 视图加唯一的边界（`map_cam.h Bounds / Around / Keep`）：地图真实地面（`MapPlayArea` 的墙再外扩 `kVoidMargin` = 地面边缘；测量完成前是物理正方形），再扩到包住玩家（玩家在边外时跟随不被拉回）。`Keep` 把焦点夹在边界内；边界够大时连眼睛（焦点后方 `height / tan(pitch)`）也保持在地面上方，做法是把焦点按眼睛的越界量移回来，不越过对边；不够大（小地图上拉到很高）时只保焦点。
- 在 `map.cpp Frame` 里所有写焦点的操作之后各 `Keep` 一次（跟随之后、指挥居中与平移 / 拖动 / 转向 / 缩放之后），不做缓动、不加阈值。`Keep` 幂等：已经合规的视图原样不动，所以不会出现两帧之间被拉来拉去。

### 测试

- `map_cam_check`：约 1.5 万个视图（各高度、俯角、朝向、焦点在地图内外）——焦点在边界内；边界容得下时眼睛也在边界内；对结果再 `Keep` 一次不变，且只改焦点 x / z；连续 180 帧顶着边缘平移再松开：位置从不来回反向、停在边内；玩家在边外时边界扩到包住他，跟随落在他身上。变异测试：去掉眼睛那一步，「眼睛在地图上方」大量失败。

## 构建与测试

- `cmake --build build -j 4`、`--target offline_checks`：通过，无警告。
- `ctest -j 3`：179 项中本分支相关全部通过；`support_soldier_native` 在基线（b89d26a）上同样失败（Python 原生审计里访问冲突，属支援组），`mission_crew` 在全量并发时偶发失败一次、单独重跑 3 次均通过（与本组改动无关）。

## 还没在游戏里实测的（如实）

1. 地图边界后是否还闪（若仍闪，说明原因在别处，要 `Debug=1` 日志和录像）；边界大小是否合适（会不会挡住想看的边缘区域）。
2. 右键单击与右键拖动旋转的区分手感（阈值 6 个鼠标单位）；Z 在原版里无其它用途（地图打开时人物输入本来被保持）。
3. 移动 / 攻击移动在真实寻路下能否走到（`GroundNavigate` 每帧 4096 次查询的共享预算在几十个士兵同时改目标时可能排队，日志里的「战斗位却不动」也可能有这部分原因，属寻路，未改）；到点转守点的判定距离。
4. 座位上的士兵：原版 Think 对他们是否还会「走近自动入队」（`0x596BD8`）未查清；用户看到的「可招募标记」若其实是原版画的，本修复不覆盖。
5. 支援栏的图标在游戏字体 / 分辨率下的观感；联机时调度状态显示（联机一律显示可呼叫，由房主决定）。
