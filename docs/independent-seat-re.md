# V603（Vehicle603_Flak）副座位独立炮塔：原版支持到哪一步（逆向笔记）

2026-10-30。EDF.dll TimeDateStamp `0x678CCB46`，地址全是 RVA，全部静态（`tools/edfre.py` + capstone），**没有进游戏实测**。
置信度：H = 指令里直接读到；M = 由代码结构推出；L = 猜测。
目的：`docs/multi-weapon-keys.md`「独立座位没有做」一节留下的问题——V603 对座位 ≥1 的武器建造、瞄准、输入、联机是否支持，
以及插件自己摆骨骼时炮弹是否跟着骨骼走。对照类：403 Blacker（`0x17D8FA0`）、404 Titan（`0x17D9458`）。

## 结论

| 环节 | V603 原版 | 403 / 404 原版 | 插件要不要补 |
|---|---|---|---|
| holder 进座位 i 的列表 | 通用，按行的座位号（VehicleBase） | 同左 | 不用 |
| 建枪（`0x633330`） | 只建 holder 0、1，**按行号不按座位号**，都传座位 0 的 Aim | 403 循环 3 次传座位 i 的 Aim；404 展开 6 次 | 要（第 2 行以后） |
| 座位 i 的 Aim 轴映射（`0x669BA0 → 0x5FB790`） | 只给座位 0 | 3 座 × 2 轴 | 要 |
| 每帧给 Aim 步进（Aim slot 2） | 只座位 0 | 循环 3 座 | 要 |
| 输入（扳机、右摇杆） | 只读座位 0 | 循环 3 座 | 要 |
| 摆骨骼（slot 45） | 只按座位 0 的俯仰摆 `cannon_r/_l` | 每座一对骨骼 | 要 |
| 联机 yaw / pitch | 通用的 CarBase NetObject，按座位遍历；**前提是该座 Aim 有映射** | 同左 | 补映射即可 |
| 炮口方向跟骨骼 | 是：holder 的骨骼世界行每帧拷进 `weapon+0x150` | 同左 | 不用 |

一句话：V603 把「一座、两门炮」写死在 slot 4 / 45 / 46 / 55 和构造里；holder 归座、NetObject 收发、控制权同步、炮口取向这些通用层
本来就按座位做。插件补的是类专属的那五处。

## 1. 建枪

- **holder 归座是通用的（H）**。VehicleBase 构造 `0x629450` 逐行读 `vehicle_weapon_setting`（名字引用 `0x629DA3`）：
  `0x62A4A8 call 0x11001F0` 按行 `[0]` 的骨骼名找骨骼记录，`0x62A516` 写进 `holder+0x18`、`0x62A51B` `holder+0x38 = veh`；
  行 `[1]`（`ebx`）≥ 0 时 `0x62A52E..0x62A57E` 把 holder 追加进 `seat[ebx]+0xC8`（数量 `+0xD8`）；< 0 时 `0x62A587..0x62A60D` 加进每个座位。
  与类无关：`['gun_x', 1]` 的 holder 一定在 1 号座位的列表里。
- **武器对象只由类的 slot 46 建（H）**。V603 slot 46 `0x621A50` 展开两次：`0x621B1E call 0x633330(veh, seat0+0xE0, &item0, 0)`、
  `0x621B97 call 0x633330(veh, seat0+0xE0, &item1, 1)`（`rdx = [veh+0x608]+0xE0`，两次都是座位 0）。`0x633330` 把武器写进
  `holder[r9]`（`0x633837` `lea rdi,[r12+r12*8]`、`0x63393C mov [rsi+rdi*8+0x10], rbx`）；Aim 参数只在 `0x633804` 交给 `0x5FB810`
  初始化转速（项数 > 2 才走，`0x6335D1`）。
  - 所以 V603 没有 Car 那样的「座位号门」（对照 `docs/edf41-bike-re.md` 的 `0x65B14B` / `0x65B31B`）：**门是行号**——第 0、1 行不论座位号都建，
    第 2 行以后原版一律不建。插件 `stores.cpp InstallBuilds` 已在 slot 46 之后补建后面的行（`docs/stock-payload-re.md` §4.1），但传的 Aim 是 0，
    转速不会初始化。
  - 若把 holder 0 / 1 本身划给座位 1，枪会建出来、进座位 1 的列表，但转速写进的是**座位 0** 的 Aim。
- 对照（H）：403 slot 46 `0x5FEFB0` 循环 `0x5FF040..0x5FF0D7` 三次，`0x5FF08E lea rdx,[r15+0xE0]; add rdx,rsi`（`rsi += 0x340`）传座位 i 的 Aim；
  404 slot 46 `0x6000E0` 展开 6 次（`docs/stock-payload-re.md` §4.1）。

## 2. 瞄准（Aim）

- **轴映射只给座位 0（H）**。V603 构造 `0x621251..0x6212B0` 两次 `0x669BA0(veh, edx=0, 约束 0/1, 轴 1/0)`；`0x669BA0` 以
  `seat = [veh+0x608] + edx*0x340`（`0x669BC1`）、`0x669C35 call 0x5FB790(seat+0xE0, 轴, &{骨, lo, hi})` 登记映射。
  对照：403 `0x5FE920..0x5FE964`、404 `0x5FF9A0..0x5FF9E8` 都是外层 3 座 × 内层 2 轴，约束号 = `2i + j`，骨骼号表在 `veh+0x2B00`。
- **每帧只步进座位 0（H）**。V603 slot 4 `0x621800`：`0x621872 lea rdx,[veh+0x2AA0]; 0x621879 lea rcx,[seat0+0xE0]; 0x621880 call [vt+0x10]`（Aim slot 2），
  前后 `0x6696A0` / `0x669630(veh, 座位 0, 约束 0/1, …)` 按 Aim 驱动约束马达。整个函数没有座位循环。
  对照：403 slot 4 `0x5FEE90` / 404 slot 4 `0x5FFFC0`：`0x5FEED0..0x5FEF85` 循环 3 次，输入 `veh + (0x2AA + i)*0x10`（`0x5FEF21`），
  Aim `seat_i+0xE0`，马达约束 `2i / 2i+1`。
- **V603 没有给副座位留输入槽（H）**：构造 `0x620F6A..0x620F88` 只清 `+0x2AA0..+0x2AAF`（一个 `{x,y,0,1}`），`+0x2AB0` 是 slot 5 交给
  `0x632BD0` 的另一块（`0x621C7F`），`+0x2AC0..+0x2AE8` 是动画 / 骨骼号。403 / 404 的 slot 55 清 3 个 0x10 槽（`0x5FEBF9..0x5FEC2B`）。
- **摆骨骼只按座位 0（H）**。V603 slot 45 `0x621510`：`0x6215F4 call 0x661C00(veh, [+0x2ACC], [+0x2AD0])`（炮塔偏航，取物理），
  `0x62161C` / `0x621644 call 0x661810` 用 `[[veh+0x608]+0x158]+0xC`（座位 0 Aim 的俯仰轴映射条目）摆 `cannon_r` / `cannon_l`，再 `0x621654 call 0x1100B90`。
  对照：403 slot 45 → `0x5FF2F0` 对三座各摆一对骨骼，俯仰分别取 `seat0+0x158`、`seat1+0x158`（`[rax+0x498]`，`0x5FF386`）、`seat2+0x158`（`[rax+0x7D8]`，`0x5FF3C3`）。
- 控制权 / 远端插值是通用的（H）：slot 51 `0x672AD0 → 0x673161 call 0x62E6C0`，`0x62E6F5..0x62E744` 遍历 `veh+0x618` 个座位，
  每座 `0x630DF0` 判权限后调 `0x5FBA20`（远端插值）或 `0x5FB880`（本机）。V603 与 403 / 404 / Car 共用 slot 51。

## 3. 输入

- **V603 只读座位 0（H）**。slot 55 `0x621460` → `0x62149D mov rbx,[veh+0x608]`（座位 0），`0x6214A4` 读 `+0x2E4`，`0x6214B3 call 0x62DE50`（≥ 0.8），
  命中则 `0x6214C3` / `0x6214D3` 拉 holder 0、1；`0x6214D8..0x6214FA` 把座位 0 的 `+0x2D0`（取负）/ `+0x2D4` 写进 `veh+0x2AA0/+0x2AA4`。
  `+0x2E0`（副扳机）不读（与 `docs/feedback-2026-10-19-bmpt-secondary.md` 一致）。
- 骑手写自己座位的 `+0x2D0/+0x2D4/+0x2E0/+0x2E4` 是通用的（`docs/camera-re.md` §4：`0x56DBC0` / `0x572DF0`），所以座位 i 的输入**有数**，只是 V603 不读。
- 对照：403 slot 55 `0x5FEC38..0x5FECC0` 循环 3 座：`seat_i+0x2E4` → `0x62C000(holder i)`（`0x5FEC93`），右摇杆 → `veh+0x2AA0+i*0x10`；
  404 `0x5FFCA8..0x5FFD52` 另加 `seat_i+0x2E0` → holder `i+3`（`0x5FFD2B`）。
- 乘员镜头：本次没追（L）。镜头不驱动 Aim，Aim 只吃右摇杆经 slot 55 / 4 的转发。

## 4. 联机

- **V603 用的就是 CarBase 的 NetObject（H）**：V603 次 vtable `0x17DC8B8` 第 7 / 8 槽 = `0x676B40` / `0x66BA60`，与 Car、403、404、TankBase、CarBase 相同（14 个类共用）。
- 发送 `0x676F50`：`0x677065` 取座位数 `veh+0x618`，逐座 `0x6770A4 call 0x630DF0`（本机有权）且 `0x6770B9 call 0x5FB7E0`（任一轴 `+0x38` 映射非空）才置位，
  `0x677119 call 0x5FBBC0` 发该座 yaw / pitch。接收 `0x66BAC0`：`0x66BBFC..0x66BC4B` 按位图逐座 `0x66BC33 call 0x5FAAE0(seat_i+0xE0, …)`。
- 所以座位 i 的同步**只缺映射**：补上 `0x5FB790` 登记后，原版会自己发收、远端经 `0x62E6C0 → 0x5FBA20` 插值。开火不靠这条（武器自己的同步，见 `docs/fp-soldier-re.md`）。

## 5. 骨骼与炮口方向

- **方向（H）**：VehicleBase slot 5 `0x630250`：`0x63028C call [vt+0x168]`（slot 45 摆骨骼并 `0x1100B90` 合成世界）之后，
  `0x6303C0..0x630498` 遍历**全部** holder（`veh+0x638`，步长 0x48，数 `+0x648`，与座位无关）：`0x6303E2..0x630413` 把 `holder+0x18` 骨骼记录的世界行
  `+0xB0..+0xEF` 拷进 `weapon+0x150`，再 `0x630441 call 0x68FD00` 放置武器。枪口 mode 0 的弹道方向就是 `weapon+0x150` 的第 2 行（`common/edf/weapon.h`、
  `0x6969A0`）；mode 1 是「局部 × 枪口骨骼」。两者都取该帧合成后的骨骼。
- **插件摆的骨骼会被跟随（M，强类比）**：原版 V603 自己就是在 `0x621654` 之前写 `cannon_r/_l` 的局部（`0x661810`），弹道随之俯仰；插件在同一窗口写
  局部（`vulcan_rotor.cpp` 已把 `0x621654` 重定向到 `Build`，`barrel_recoil` 也在那里写），与原版写法无法区分。`0x1100B90` 只重建 `rec+8 = 1` 的骨骼
  （`src/drill.cpp` 注释、`0x1110FC0`），所以按 `drill.cpp WriteLocal` 的方式置脏标记。
- **注意（H）**：若该骨骼是动态物理代理，`0x6215D6 call 0x6EDCA0` 会先用刚体覆盖它的世界行、清 `+8`；副炮塔骨骼要么不建代理，要么
  `car_base_simulation_node` 给 0（`docs/edf41-bike-re.md`「悬空」一节）。
- 枪口位置用枪口条目自己的骨骼（`muzzle+0`）；它在载具骨架里挂在被摆骨骼之下时随父骨合成（M，没逐条追到 `muzzle+0` 的来源）。

## 6. 其它 V603 写死处（H）

- slot 48 `0x621700`（任一 holder 开火回调）：只请求 variant 0（BodyRecoil，`0x621751`）并播 `main` 的 Fire 动画（`0x621746`）。
  403 slot 48 `0x5FED92 call 0x62DB10` 先找出 holder 属于哪座，variant 1 作用在 `seat_i` 的 Aim（`0x5FEE40..0x5FEE5E`）。副座位开火在 V603 上
  会播主炮动画、无该座后坐。
- slot 4 尾部 `0x6218B9..0x621A1A` 按 `ragdoll_cannon_r/_l`（`+0x2ADC/+0x2AE0`）给左炮刚体角速度，只认座位 0 那对炮。

## 对插件实现的含义

要让 V603 的副座位各操作一座炮塔，最少补五处，其余（holder 归座、控制权、NetObject、炮口取向）原版已按座位做：

1. **建枪**：数据里副炮的行 `['gun_i', i]` 放在第 2 行以后，由 `stores.cpp InstallBuilds` 的补建路径建；补建时把 `seat_i+0xE0` 当 Aim 传给
   `0x633330`（或自己调 `0x5FB810`），用行第三项初始化转速，不写死。不需要像 Car 那样改门。
2. **轴映射**：对每个副座位调 `0x5FB790(seat_i+0xE0, 轴, AxisMap{骨, lo, hi})`（`edf41bike RegisterMappings` 的做法），或数据里给
   `car_base_constraint_data` 后调 `0x669BA0`。这一步同时让联机发送 `0x677065` 把该座纳入。
3. **帧里步进**：在 V603 slot 4 的原函数之后，对本机有权的副座位以 `(-seat+0x2D0, seat+0x2D4)` 调 Aim slot 2（`[vt+0x10]`），
   参照 `edf41bike` 对 Car 座位 1 的做法；远端座位不步进，交给原版 `0x5FBA20`。
4. **摆骨骼**：在 `0x621654` 的 `Build` 链里（与 `vulcan_rotor` / `barrel_recoil` 共用那次重定向），按 Aim 轴映射条目的角度写副炮塔
   yaw / pitch 骨骼的局部并置 `+8`。所有机器都摆（远端靠插值后的角度），弹道方向随之而来。
5. **拉扳机**：本机有权时 `seat_i+0x2E4 ≥ 0.8`（`0x62DE50`）→ 对 `seat_i` 列表里的 holder 调 `0x62C000`（走 `payload.cpp PullHook`，挂载切换照常），
   副扳机 `+0x2E0` 按 `multi-weapon-keys` 的键位规则。远端不拉。

**更省事的路线：不经原版 Aim，插件自己存 yaw / pitch、摆骨骼、调 `0x62C000`。** 能用（第 5 节：方向取自骨骼，H），能省掉第 1 步的转速和第 2、3 步；
直接写 `weapon+0x139` 等价于 `0x62C000` 但跳过存活检查和 `PullHook`，不建议，用 `0x62C000`。代价是联机：没有映射时原版不发这座的角度，
要像 `dreadnought_net` 那样在 tag 15（`seat_net.h`）上自发 yaw / pitch，远端自己摆骨骼。

**推荐**：走原版 Aim（1–5 全做）。多出的工作只是第 2 步一次登记和第 3 步一次调用，换来转速 / 限位 / 缓动、远端插值、控制权切换都是原版的，
不用再写一份联机消息。第 6 节两处（主炮开火动画、无该座后坐）先接受，需要时再在 slot 48 按 holder 归座分流。

## 待实机

- 副座位 Aim 由插件步进后，`0x5FBBC0` 是否真的把该座发出去（`0x630DF0` 对副座位乘员的权限判定）。
- 副炮塔骨骼被插件摆动后，mode 0 / mode 1 枪口的炮弹方向与位置（第 5 节的 M 项）。
- 乘员镜头（第 3 节 L）。
