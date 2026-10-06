# NPC 自制 AI 框架设计（WIP，2026-10-06 暂停）

> 状态：阶段 1 逆向刚开始，用户要求暂停。本文件只记已确认的逆向结果和接续点，设计正文未写。
> EDF.dll TimeDateStamp 0x678CCB46，地址为 RVA。

## 需求修订（协调者转达，2026-10-06）

- 泰坦「后退」= 主炮后坐力把车往后推（玩家版有，NPC 版和部分坦克 NPC 版没有）。恢复后坐力由 fix/npc-tank-recoil 做，本分支不碰。
- 本分支的泰坦验收：被后坐力或其他外力推离目标点后自动开回（倒车或掉头，按距离和朝向选）。倒车保留为回点和避障手段。

## 已确认：原版坦克 AI action 0x661440（CarBase slot 72，所有坦克共用）

由 CarBase slot 6 `0x6731C0` 注册进 veh+0x25A0 ActionTable，edx==1 时执行更新：

1. `0x609650(veh+0x608)`：0 号座不是 AI 乘员就整个跳过。
2. `+0x25D4` = 卡住标志：`+0x1970==0` 且 |`+0x182C`| ≤ 1.19e-7 视为不动；再结合速度 < 1.0 与 `+0x74`（矩阵上向量 y）< min(`+0x1B20`, 0.42)。
3. 有路线：`+0x4A8`（AiRouteExplorer shared_ptr）且 `+0x4D0`（有下一点）→ `0x661220(veh, &+0x4C0)`：
   - `+0x25E0` 存上一帧目标点，`+0x25F0` 记距离、`+0x25F4` 计数 0xE10（3600 帧）；目标点不变、距离没缩短超过 2 m、explorer+0x28（速度）≥0.5 时倒数，归零后（存活）走 `0x7748F0`/`0x630DF0`/`0x6329B0` —— 推断是「路线卡死 60 s 后的处理」，待细看。
   - `+0xE10/+0xE18` weak 引用存在时 `0xE65A0` 把它拷到 `+0x25E0`。
4. `0x660490(veh, 有路线)`：行驶控制器（413 行，未读）。
5. 0 号座有武器（seat+0xD8>0）：`+0x518/+0x520` 是目标 weak 引用；`+0xE08`/`+0xE09` 是攻击许可类标志（推断对应脚本 SetAiAttack 等）；
   无路线且 `0x5FC6E0(seat+0xF0 射程)` 判不在射程时 → `0x661020(veh, 目标位置, r8b=1, 0)` 朝目标开；然后 vtable slot 70 `0x65F6F0(veh, 0, 目标)` 开火，`+0x25D0=0`。
   无目标时 `+0x25D0` 递增到 90 帧后 slot 70(null) 停火。
6. **驾驶输出 = 写 0 号座摇杆块**（`0x661020`）：`seat+0x2C0` = −clamp(偏角×10/…, −1, 1)（转向），`seat+0x2C4` = −油门×(explorer+0x28 或 1)×`+0x4B8`（SetAiMoveSpeed 倍率）。
   - 偏角 = atan2 局部坐标（`0x4E1F0`）。r8b==0（走路线）时 |偏角| > 2.199 rad（126°）就把偏角加 π 归一化、油门取 −1 → **原版走路线时会倒车**；r8b==1（追目标）从不倒车。
   - |偏角| ≥ 0.314 rad（18°）时油门为 0（原地转向）。
   - 结论：此前 docs/ground-ai-re.md 「RideAi 乘员从不写座位摇杆块」只对乘员成立；**车辆 AI action 自己写 seat0+0x2C0/+0x2C4**，插件自制 AI 可以在 slot 55 输入钩里、调用原版输入之前覆盖同一块（与 DrillInput 同位置），不必改 ActionTable。

## 接续点（下次从这里做）

1. 读 `0x660490`（行驶控制器）、`0x65F6F0`（slot 70 开火）、`0x661220` 后半（`0x630DF0`/`0x6329B0`）。
2. 确认 ActionTable 更新与 slot 55 输入的先后顺序（决定覆盖摇杆块的时机）。
3. 脚本 NPC 识别：CreateFriend `0x1B0310`→`0x1D8900`、SetAiRouteNavigate `0x1C2370`（explorer 写 `+0x4A8`）、Vehicle_SetAiAttack `0x1CC2A0`（推断写 `+0xE08/E09`）、`+0xE30`=1 RideAi 生成；候选判据：`+0x4A8` 有路线 / 脚本写过 `+0xE10` / 非插件 Crew() 配的乘员（crew.cpp State::crewedAt）。
4. 联机：docs/online-re.md §3.4/§5（DummyVehicleRider 无网络身份，各端自认权威）→ 自制 AI 只在房主算。
5. 相关分支：feat/map-command-npcs（已 push，mapcmd 驻守/跟随接口）；fix/npc-air-soft-boundary、fix/npc-attack-runs 只有未提交工作区改动，无法合并。
