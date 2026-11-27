# 光环载具（指挥车 / 维修车 / 弹药车）

三辆新增车辆，是新增的选项，不顶替原版的槽位。每辆车给半径内的友军持续加成：

| 车 | SGO | 标记骨骼 | 效果 | ini |
|---|---|---|---|---|
| 指挥车 | `EDF6VC_AURA_CMD` | `edf6vc_aura_cmd` | 攻击加成（默认 +30%） | `AuraCommandAttack` |
| 维修车 | `EDF6VC_AURA_REP` | `edf6vc_aura_rep` | 每秒回复最大耐久的一定比例（默认 2%/秒） | `AuraRepairShare` |
| 弹药车 | `EDF6VC_AURA_AMO` | `edf6vc_aura_amo` | 每秒补给弹匣容量的一定比例；正在装填的武器按同一比例缩短装填（默认 10%/秒） | `AuraAmmoShare` |

公共开关：
- `AuraVehicles`：总开关。设为 0 时光环关闭，车照样能开。
- `AuraRadius`：作用半径，默认 60 m，范围 0~500。
- `AuraParkSpeed`：无人驾驶时，车速不超过这个值才算停着，默认 1 m/s。

这些键都在 `EDF6VehicleCrew.ini` 里，`plugin.cpp` 读取时会钳到合法范围。

## 规则（`src/aura_logic.h`，纯逻辑）

- **何时生效**：车没毁，并且满足下面任一条件：
  - 驾驶位（seat 0）有人，玩家或 NPC 都算；
  - 无人驾驶，但车速 ≤ `AuraParkSpeed`，即停着。

  无人驾驶又在移动（比如被撞飞、在滑坡）时不生效。
- **谁受益**：和车同阵营的士兵、NPC、载具，并且在 `AuraRadius` 之内。
- **叠加**：同类光环不叠加，取最强的一个；不同类光环可以同时生效。
- **攻击加成和原版支援装置的关系**：两者取较高者，不相乘（`AttackMultiplier` = max）。
- 一局最多同时识别 `kMaxSources` = 16 个光环源，超出的部分取最先找到的 16 个。

## 原生实现（`src/aura.cpp`，EDF.dll TimeDateStamp `0x678CCB46` 的 RVA）

- **识车**：
  - `AuraVehicleFrame` 在 crew 的载具帧里，按模型的标记骨骼（BoneRecord506）认出是哪种光环车；
  - 同时记录这辆车的位置、速度和驾驶状态。
- **攻击**：使用原生的每帧伤害倍率 `obj+0x390`：
  - `0x54BE8D` 读取它；
  - `0x54BEE5` 计算 `+0x398 = +0x390 × +0x388`；
  - `0x54BF13` 把它复位成 1。

  士兵的子弹在 `0x59B536` 读 `+0x398`，载具的子弹在 `0x6303C4` 读（这条只做了静态验证）。插件按原版支援装置的做法，用 max（`0x2AC8CA`）抬高 `+0x390`。所有机器都写：开火的那台机器上算出的子弹才有效。
- **维修**：
  - 只回复本机拥有的对象的 HP（`+0x2F8`，范围 `+0x2F0..+0x2F4`），规则同 `proteus_field.inc`；
  - 联机时，把回复量同时加进原生的 HP 差值累加器 `+0x5B4`：`0x54AA06` 会把伤害事件的差值加到这里，slot 40 在 `0x54CD06` 把它发出去，所以每台机器上的副本都会同步。
- **弹药**：
  - 补每把武器的弹匣（`+0xBE8`，容量 `+0x248`）；
  - 正在装填的武器缩短装填进度（`+0xE68`，共 `+0x20C` 帧。`0x693F3B` 倒数自动装填，`0x693F41` 在计数到 0 时完成装填）；
  - 跳过 ReloadType 2，即空袭兵按 pt 装填的呼叫类武器；
  - 武器状态是每台机器各自的，所以每台机器都写。
- **扫描范围**：`0x5E11D0` 遍历队伍。
- **签名校验**：所有挂钩点都先做字节签名校验（`kSigs`），不符就写日志，光环不出动。

## 联机

- 所有功能都要支持联机。由房主判定当前有哪些光环生效，数值也用房主的 ini。
- **通道和格式**：走 plugin_net 通道 `AFAU`，状态流 `auraVehicles = 6`，object 0。
  - 一个源的线格式：kind 1 字节 + team 4 + radius 4 + strength 4 + pos 12；
  - 一帧最多 16 个源。
- **发送节奏**：
  - 列表变化时立即发，最多每秒 10 次（`kGapMs` = 100）；
  - 列表不变时每秒发一次心跳（`kHeartbeatMs`），用来补丢包和照顾中途加入的成员；
  - 客人 3.5 秒收不到（`kSilentMs`）就认为当前没有光环。
- 每台机器按收到的列表，给自己控制的单位结算加成。
- **房间协议升到 23**（`src/mod_room.h`）：旧版插件既不发也不收这条消息，要求全员同版本，不做兼容。

## 显示

- **地面范围圈**：车离玩家 400 m 以内时，在车下的地面上画出作用半径的圈，每 500 ms 重新投一次射线贴地。
- **HUD 提示**：只对正在受益的玩家显示一个「光环」块，列出各类效果：
  - 「攻击 +N%」
  - 「维修 x%/秒」
  - 「补弹 x%/秒」

  文本在 `src/hudtext.inc`，有四种语言。

## 车辆管线（`tools/make_aura_vehicles.py`）

- **派生方式**：派生自 `V512_KEITRUCK`（轻卡），走派生载具的统一管线，不手工改 SGO：
  - 复制原版 MRAB，把 mdb 成员改名为 `<stem>.mdb`；
  - 在 `body` 下加一根标记骨骼（位置 0,1,0）；
  - 给唯一的不透明材质的 diffuse 着色（指挥车橙、维修车绿、弹药车蓝），用来区分三辆车；
  - SGO 的 `animation_model` 指向自己的 MRAB；
  - 耐久改为 1000；
  - 没有武器；
  - 驾驶舱通过 `cockpits.graft_files` 嫁接，再经过 `model_lod.finish` 处理。
- **登记**：
  - `pylib/vcobjects.py` 的 `GROUND_VEHICLES`；
  - `pylib/cockpit_fits_plugin.py`，复用 V512 的座位；
  - `pylib/ledger.py`、`pylib/buildcache.py` 的组 `aura_vehicles`；
  - `tools/installer.py`：资源项、安装、卸载、自检；
  - `tools/build_release.py`。
- **呼叫和测试场**：
  - `tools/calls.py` 新增 `EDF6VC_CALL_AURA_CMD/REP/AMO`，共 3000 pt，每种有四语说明；生成到 `src/calls.inc`，`RELEASED` 记 `aura trucks (2026-11-27)`；
  - 测试场 `testrange/catalog.py` 的 `INSTALLER_VEHICLES` 新增三行；`catalog_archives.json` 和 `catalog_row_seconds.json` 同步更新。

## 测试

- `aura_logic`（`tests/aura_logic_test.cpp`）覆盖以下内容：
  - 生效条件（驾驶 / 停车 / 移动中无人驾驶 / 车已毁）；
  - 半径和阵营筛选；
  - 同类取最高、异类共存；
  - 攻击取 max、回血和补弹的数值；
  - ReloadType 2 不补；
  - AFAU 编解码、越界拒收、发送节奏、静默超时。
- `aura_vehicles_guard`（`tests/aura_vehicles_guard.py`）：
  - 检查各登记表一致：标记骨骼与 `aura::Kind` 的顺序、vcobjects、calls / calls.inc / KINDS / notes、驾驶舱座位、测试场行、ledger、buildcache、installer、build_release；
  - 本机有 Root.cpk 时，还会在内存里只读地构建三辆车，检查标记骨骼的位置、驾驶员视点、没带别车的标记骨骼、`animation_model`，以及没有武器。
- `support_net_runtime_test`、`hud_view`、`crew_first_use_test` 已补上光环的桩函数和通道注册。

## 实机待验（一次运行内完成）

1. 在测试场同一处依次生成三辆车，确认颜色可区分，地面圈的半径是 60 m。
2. 走进圈内，HUD 出现「光环」块；走出圈后 HUD 块消失。
3. 指挥车：在圈内和圈外各对同一个目标开火，比较伤害（士兵和载具各测一次）。
4. 维修车：受伤后站进圈内，确认 HP 按每秒 2% 回复；载具也会回复。
5. 弹药车：打空弹匣后站进圈内，确认弹匣在补；正在装填的武器装填变快；空袭兵的呼叫武器不补。
6. 依次测停车（生效）、有人驾驶（生效）、无人驾驶但在移动（不生效）、车被摧毁（不生效）。
7. 放两辆指挥车，确认加成只取一份。
8. 联机：客人看到范围圈和 HUD 块，并且在客人那边上面 3~5 的效果都成立；客人驾驶光环车时，光环同样生效。
