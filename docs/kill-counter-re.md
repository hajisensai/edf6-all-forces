# 击杀计数与战斗记录（戦績）：EDF6 的实现与 4.1 移植单位的独立计数（静态逆向）

EDF.dll TimeDateStamp `0x678CCB46`，地址全是 RVA。H = 反汇编里直接读到；M = 由代码结构推出；L = 推测。全部是静态分析
（`tools/edfre.py` + capstone、`pylib/sgo.py` 读 Root.cpk），**没有实机核对**。用户原话（2026-10-18）：「4.1导入单位的sgo里的
gameobjectkillcount写入单独的类别，同时把新添加的怪物击杀计数写入战斗记录里，让玩家能看到模组新加入生物的对应击杀数。如设置赛克托、
飞龙、四足要塞、brain、earth eater等的击杀计数」。

实现：`pylib/edf41kills.py`（计数名、文字、存档槽位规则）、`tools/make_edf41_objects.py` `kill_counters`（4.1 单位的 SGO）、
`tools/make_edf5_campaign.py`（`Mods/ETC/ACHIEVEMENT.SGO` 与文本表）、`src/killcount.cpp` + `src/killcount_logic.h`（战斗记录页）。
离线核对：`tests/killcount_test.cpp`（ctest `killcount`）、`tests/killcount_native_audit.py`（ctest `killcount_native`，需
`EDF6_NATIVE_DLL`）、`tools/selftest.py` `edf41_kill_counters_*`。

## 0. 更正

`tools/make_edf41_objects.py` 旧注释说「EDF6 的计数表在 EDF+0x893C40」，**不对**：`0x893C40` 是战斗记录页 `ui::HUiHQHistory`
（vtable `0x18031A0`）的构造函数，它只是在栈上排了一张**显示用**的 90 行表（H）。计数本身是 `APP:/ETC/Achievement.sgo` 里定义的
变量（§1），表里没有的名字不计数，跟页面上列没列无关。

## 1. 计数存在哪：Achievement.sgo 的变量表（H）

- `0xCDD30` 读 `APP:/ETC/Achievement.sgo`（`0xCDDDF`），取 `AchievementEvent`（`0xCE142`）与 `AchievementVariable`（`0xCE158`）。
  每个变量是 `[名字, 'int'|'float', 初值]`（类型字符串按 `INT` / `FLOAT` 认，`0xCE19F` / `0xCE296`）。原版 91 个变量、39 个事件
  （Steam 成就条件，如 `Conquest5: AllClearRatio >= 500`）。由 `0xD62C0`（启动）与 `0xDB580` / `0xDB880`（重置）调用。
- 存放：`[EDF+0x20B2890] + 0x14B50`（下称 store）。`+0` 是 `std::map<std::wstring, 变量>` 的头结点，`+0x18` / `+0x28` 是事件数组
  （每项 0x38 字节，`+0x34` 已解锁字节）。结点（MSVC 布局）：`+0/+8/+0x10` 左/父/右，`+0x19` is-nil，`+0x20` 键 wstring（`+0x30`
  长度、`+0x38` 容量），`+0x40` 类型（0 int / 1 float），`+0x44` 值，`+0x50` 引用它的事件下标数组。
- 查找 `0xCFAB0(store, &out, wstring*)`：按 UTF-16 码元逐个无符号比较、再比长度（`0xCFB73..0xCFBAF`，即 `std::wstring::compare`），
  找不到返回头结点（`== [store]`）。

## 2. 击杀怎么计（H，联机部分 M）

- `SoldierBase` 的消息处理（vtable 槽 9 `0x598FD0`，`AssultSoldier` / `Engineer` / `PaleWing` 共用）收到 `0x10000006`（伤害结算
  `0x547C30` 在目标 HP 首次 ≤ 0 时发给攻击者，见 `docs/damage-stats-re.md` §5）后，若 `soldier+0x340`（手柄对象）非空，就在
  `0x599101` 调 `0x5499F0(soldier, 死者)`。另一个调用者是 `EDF6_Berserker_Equipment_Bomb` 槽 18（`0x34A8B4`）。
- `0x5499F0`：从死者 `+0x100` 的 SGO 取 `game_object_kill_counter`（字符串，`0x549A53`），交给
  `0xCF640(store, 死者, 名字, 1, true)`（`0x549B50`）。
- `0xCF640`：`0xCFAB0` 查名字；**找不到（== end）什么也不做**；找到且类型是 int（`+0x40 == 0`）就 `+0x44 += 1`（`0xCF66A`），再
  `0xCF710` 检查挂在这个变量上的成就事件。所以：
  - SGO 没有这个键、或名字不在 Achievement.sgo 里：**静默不计**，不崩（H）。这正是 4.1 单位迁移前的状态：`HectorKillCount`、
    `4LegTankKillCount`、`MotherShip401KillCount` 等 4.1 名字 EDF6 没有，打死了哪儿都不记。
  - 只有带手柄对象的士兵（本机玩家，含分屏 2P）的击杀才记（H：`+0x340` 是手柄对象，见 `docs/itembox-re.md`）。NPC、载具本身的击杀
    不经士兵的消息处理，不记（M，与原版一致）。
- 联机：store 是每台机器自己的，没有任何网络同步；每台机器只记本机玩家的击杀（M：客机上敌人的死亡消息是否发到客机玩家的士兵，
  未逐条核实，`docs/damage-stats-re.md` §8 同一个未知）。插件不改这条路径，联机行为与原版计数相同。

## 3. 存档：TROPHY.DAT，按位置存（H）

- 写：`0x7C2950` 把 `0xCFC50(store, buf)` 的结果作为 `TROPHY.DAT` 存盘。`0xCFC50`：先写事件的已解锁字节（`+0x34`）到
  `buf[0..]`，补零到 **0xC8（200）** 字节；再按 **map 顺序**（名字的 UTF-16 序）把每个变量的 `+0x44` 写到 `buf+0xC8+4i`，补零到
  200 个。**写变量那一段没有上界检查**（`0xCFCE0..0xCFD42`），buf 是调用者栈上的 0x3F0 字节（`0x7C29A1`，cookie 在 `+0x480`）：
  变量超过 200 个会写穿栈。
- 读：`0x8CD510` / `0x715A90` 把文件内容拷到栈上（0x3F0 字节），`0xD00B0(store, buf)`：事件状态重新从 Steam 查（`0x7C4DD0`），
  变量**按 map 顺序依次取 `buf+0xC8+4i`**，同样不检查个数、不认名字（`0xD0179..0xD01E5`）。
- 推论：
  1. 存档不认名字、只认位置。往 Achievement.sgo 里加一个排在原版名字**中间**的变量，读旧档时它后面所有原版计数都会错位一格
     （不崩，但数全乱）；卸载后再读也一样乱。
  2. 新名字全部排在所有原版名字**之后**，原版变量的位置就不变：
     - 装了：旧档读进来，新计数读到的是原来的补零 = 0；之后存档时新计数写在原版之后的槽位。
     - 卸载（Achievement.sgo 还原）后读档：只按位置读前 91 个，全是原版的、全对；多出的槽位被忽略。**卸载后再存档，4.1 计数
       会被写成 0（丢失）**，原版计数不受影响。
  3. 上限 200 个变量（原版 91 + 移植 18 = 109）。
- 所以新计数**可以直接进原版存档**，不需要插件自己另存文件。原版名字都以大写字母开头，移植的名字以小写 `edf41_` 开头（UTF-16
  码元大于任何大写字母），全部排在后面（`pylib/edf41kills.py` `add_variables` 检查：Mods 里别的 MOD 的 Achievement.sgo 若有排在
  `edf41_` 之后的名字，或总数超 200，或读不了，就原样保留那份、只是不加 4.1 计数，任务包照装，安装摘要里提示：
  `make_edf5_campaign.achievement_or_base`）。

## 4. 战斗记录页 `ui::HUiHQHistory`（H）

- 工厂 `0x893BD0`（指针在 `0x1803170`）分配 0x780 字节，在 `0x893BF3` 调构造 `0x893C40`（唯一调用点）。
- 构造在栈上 `rbp+0x270 .. rbp+0x810` 排 90 行 `{const wchar_t* 名字; u8 格式; float 系数}`（16 字节一行，`0x893CD7..0x8947C3`
  直写；`rbp+0x810` 紧接着就是栈 cookie，`0x893C69`，**表不能原地加长**）。格式：0 `%d`、1 时:分:秒、2 `%d`+`StatusInfo_Count`、
  3 `%d%%`、4 `%dm`（`0x895263` 的 switch）；击杀行都是格式 0、系数 1.0。
- 第一遍（`0x8947E0..0x8949A6`，`r14` 指当前行的系数，`[r14-0xC]` 名字，上界 `0x8949A2: cmp r12d,0x5A`）：对每行查 store 取值，
  名字含 `KillCount` 且值为 0 的不显示，数出要显示的行数，建同样多的格子（`BattleRecord_Cells` / `CellBase01|02`）。
- 第二遍（`0x894D50..0x895164`，`r13 = 2×行号`，`[rbp+r13*8+0x270/0x278/0x27C]`，上界 `0x895161: cmp eax,0x5A`）：标签是文本表
  `StatusInfo_` + 名字（`0x894E42`，`0x7A3500` 查文本，找不到返回空串 `0x1762240`），值同上，跳过规则同上。
- 名字不在 store 里时，`0xCFAB0` 返回头结点，`0xCFC30` 读头结点 `+0x40` 当类型——头结点由 `0xCDD7C` 分配 0x70 字节、只初始化了
  指针与颜色，`+0x40` 是**未初始化的内存**：可能显示一行垃圾数字。所以页面上只能加 store 里真有的名字。
- 整个构造里对这张表的读只有这 5 处（`0x8947C9` 的 lea、`0x894D56`、`0x894F72`、`0x894F90`、`0x895263`），两遍的上界只有这两处
  `0x5A`（`tests/killcount_native_audit.py` 逐条核对）。

## 5. 方案与实现

1. **计数名**（`pylib/edf41kills.py` `COUNTERS`，18 个）：4.1 自己的计数表（EDF41.exe 0xA36D20 一带的 24 个名字）中移植单位用到的
   每一个，加前缀 `edf41_` 成为独立类别。EDF6 有同名的（`DragonKillCount` 在 EDF6 是「蝌蚪」、`Carrier1KillCount` 是传送飞船、
   `DeiroiKillCount`、`GiantAntKillCount` 等）也分开，避免 4.1 的飞龙记成 EDF6 的蝌蚪。4.1 的地下巢（故事版 `NestKillCount`、DLC
   版之前被映射成 EDF6 的 `AntHillKillCount`「传送装置」）统一为 `edf41_NestKillCount`。
2. **SGO**（`make_edf41_objects.py` `kill_counters`，build 的最后一步）：生成的每个 SGO / DSGO 若有 `game_object_kill_counter`，
   换成 `edf41kills.ported(名字)`；遇到没有类别的名字直接报错（不会悄悄漏计）。部件（地球吞噬者的面板 / 炮台、四足要塞的炮、
   迪洛伊的脚）在 4.1 里本来就没有计数键，保持不计：一只地球吞噬者 = 打掉核心的那一次。
3. **变量**（`make_edf5_campaign.py` `achievement_table`）：每次安装都写 `Mods/ETC/ACHIEVEMENT.SGO` = 原来那份（游戏的或别的 MOD 的，
   记在 `Mods/.edf5campaign.json`，卸载还原）+ 18 行 `[edf41_…, 'int', 0]`。
4. **文字**（同一工具的 `text_table`）：5 种语言的 `StatusInfo_edf41_…`，名字取 4.1 自己的（日 / 英文本表、社区简中补丁），格式照
   EDF6 的行（英文以「:」结尾），后缀「（4.1）」/「 (4.1)」与 EDF6 同名类别区分。
5. **页面**（`src/killcount.cpp`）：插件在 2 GB 以下申请一页放自己的行表（`[r13*8+disp32]` 无基址寻址要求地址能放进有符号 32 位），
   先按构造函数的直写指令把原版 90 行原样解码进去（`ParseRows`，不硬编码名字），再把上面 5 处读改成读这张表（同长度：
   `mov r14d, imm32; nop` 与 ModRM mod 10→00），此时上界仍是 90，页面与原版完全一样。然后把 `0x893BF3` 的调用重定向：每次构造前
   查 store（自己按 §1 的结点布局做二叉查找，不调游戏函数），把 store 里真有的 4.1 计数接在 90 行后面，两处上界改成实际行数
   （≤ 108 < 127，imm8 有符号）。任何一处字节不符就整套不装（日志 `KILLCOUNT …`），页面保持原版。

## 6. 各 4.1 单位最终的计数名

| 单位（SGO） | 4.1 原名 | 移植后 | 文字（简中） |
|---|---|---|---|
| 赫克托 HECTOR301×9 / HECTOR401×12（EDF6_Berserker_Large） | HectorKillCount（之前不计） | edf41_HectorKillCount | 赫克托击破数（4.1） |
| 四足要塞 ALIEN4LEGTANK301（MovingFortress） | 4LegTankKillCount（之前不计） | edf41_4LegTankKillCount | 四足要塞击破数（4.1） |
| 迪洛伊 DEIROI401 / 401L / 401S / 401_2 / 401_3 | DeiroiKillCount（之前并入 EDF6 迪洛伊） | edf41_DeiroiKillCount | 迪洛伊击破数（4.1） |
| 防护者 ALIENTRAILER401 ×5 | AlienTrailerKillCount（之前不计） | edf41_AlienTrailerKillCount | 防护者击破数（4.1） |
| 阿尔戈 UFOROBO401 | UfoRoboKillCount（之前不计） | edf41_UfoRoboKillCount | 阿尔戈击破数（4.1） |
| 地球吞噬者核心（主脑）UFOMOTHER401_COREPANEL、DLC 的 _DLC_COREPANEL | MotherShip401KillCount（之前不计） | edf41_MotherShip401KillCount | 主脑（地球吞噬者）击破数（4.1） |
| 母舰 UFOMOTHER301 | MotherShip301KillCount（之前不计） | edf41_MotherShip301KillCount | 母舰击破数（4.1） |
| 大型运输船 UFOCARRIER401 | Carrier2KillCount（之前不计） | edf41_Carrier2KillCount | 大型运输船击破数（4.1） |
| 运输船 UFOCARRIER301 | Carrier1KillCount（之前并入 EDF6 传送飞船） | edf41_Carrier1KillCount | 运输船击破数（4.1） |
| 飞行载具 UFO401、DLC UFO401ACE | UfoSmall401KillCount（之前不计） | edf41_UfoSmall401KillCount | 飞行载具击破数（4.1） |
| 飞行无人机 UFO301 | UfoSmall301KillCount（之前不计） | edf41_UfoSmall301KillCount | 飞行无人机击破数（4.1） |
| 红色无人机 UFO301ACE | UfoSmall301AceKillCount（之前不计） | edf41_UfoSmall301AceKillCount | 红色无人机击破数（4.1） |
| 神龙 DRAGONBIG401 | BigDragonKillCount（之前不计） | edf41_BigDragonKillCount | 神龙击破数（4.1） |
| 飞龙 EDF41/DRAGONSMALL401、REDSMALLDRAGON、YERROWSMALLDRAGON 及 DLC 的 _HS | DragonKillCount（之前并入 EDF6「蝌蚪」） | edf41_DragonKillCount | 双足飞龙击破数（4.1） |
| 地下巢 ANTHILL301、DLC ANTHILL301UG / UGMP02 / LARGEMP02 | NestKillCount（故事版不计；DLC 版并入 EDF6「传送装置」） | edf41_NestKillCount | 地下隧道出口摧毁数（4.1） |
| DLC 蚂蚁 GIANTANT04 / 04SB / 04SBMP02 | GiantAntKillCount（并入 EDF6） | edf41_GiantAntKillCount | 蚂蚁型巨型昆虫击破数（4.1） |
| DLC 死亡女王 GIANTBEEQUEEN02 | GiantBeeQueenKillCount（并入 EDF6） | edf41_GiantBeeQueenKillCount | 死亡女王击破数（4.1） |
| DLC 雷塔留斯 NEPHILA02L / 03L | NephilaKillCount（并入 EDF6） | edf41_NephilaKillCount | 雷塔留斯击破数（4.1） |

「brain」与「earth eater」：4.1 里是**同一个计数**（`MotherShip401KillCount`：日文「アースイーター撃破数」、英文「Brains Defeated」），
死的是 401 母舰的核心（`UfoMother401_CorePanel`；M201「坠落的旗舰」里只有核心的那一版就是「ブレイン」）。面板本身没有计数键，所以
没有能单独计的「地球吞噬者」对象；这里保留一个类别，文字两个名字都写。

## 7. 未核实 / 风险

- 全部静态，未实机（L）：页面加行后格子是否都能滚动到（格子由同一段代码按行数建，M）、`Mods/ETC/ACHIEVEMENT.SGO` 是否在
  `0xCDD30` 读取前已被 ModLoader 的重定向接管（其它 `Mods/ETC`、`Mods/DEFAULTPACKAGE` 文件同样在启动时读，M）。
- 联机：计数纯本地，与原版相同（M）；房间里其他人有没有装这个 MOD 不影响自己的计数。
- 卸载后存档会把 4.1 计数清零（§3），原版计数不受影响。
- 别的 MOD 若整份覆盖 `ETC/ACHIEVEMENT.SGO`：4.1 名字不在 store 里 → 不计、页面不显示（插件按 store 过滤，不会显示垃圾行）；重新
  运行安装器会在那份之上再加。
- 插件的行表在 2 GB 以下申请；申请不到就不改页面（日志说明），计数照常进存档，只是看不到。
