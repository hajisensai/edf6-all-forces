# 2026-11-10 反馈：手柄 L3 在格雷普上是喇叭又是自由视角

**离线逆向与离线测试，没有在游戏里实测。**

## 原话

维护者（2026-11-10）对遗留问题「手柄 L3 在格雷普上仍和喇叭同键，要不要改」的回答：

> 都要改。

键鼠部分（自由视角默认改 C）已在同日的 `feedback-2026-11-10-freeview-key.md` 处理；本篇是手柄部分。

## 核对（证据级别 H，静态反汇编，`tests/car_input_native_audit.py` 钉住）

- 手柄观察键 `FreeLookButton` 默认 L3（座位按键位 0x40），由两处读：`src/vehicle_look.cpp`（各座位的观察）与
  `src/turretcam.cpp FreeHeld`（有炮塔的车，驾驶座的炮塔镜头自带的观察）。
- 格雷普是 Car 类（vtable `0x17E01B0`，第 55 槽 `0x65A390`）。玩家路径读的座位是 `[veh+0x608]`（`0x65A475`，`layout.h kSeats`
  的第 0 个，驾驶座），测试的按键位是 0x01 手刹、0x10 刹车、0x40 喇叭、0x20（空槽 83）；喇叭只在 `veh+0x29B0` 置位（车的 SGO
  有喇叭）时读（`0x65A5FC`）。`docs/stock-payload-re.md` §2.1、`docs/keyconfig-re.md` §8。
- 607 机器人卡车与 60X 卡车的第 55 槽也是 `0x65A390`（同一套输入）。
- 所以有喇叭的这些车，驾驶座上按住 L3 观察会同时鸣笛。乘客座、其它车不受影响。

## 选择

载具上八个座位按键位（A / B / X / Y / LB / RB / L3 / R3）在格雷普驾驶座都已有动作（原版：A 手刹、LB 刹车、L3 喇叭；
插件：B 换座、X 主炮弹链切换、RB 同轴机枪、R3 瞄具放大 / 高视角，Y 不确定是否被原版上下车占用），没有空键可以给观察换。

- 采用：**原版车自己读的按键优先**（与旋翼机 L3 悬停下降、沙扎比 R3 锁定的已有做法一致，方案 §3 规则 4「一个情境一个按键
  只给一个动作」）。手柄观察键在「该车原版输入在这个座位上读的按键」上不提供：有喇叭的格雷普 / 卡车驾驶座上 L3 只是喇叭，
  不观察、HUD 不提示观察键；乘客座、没有喇叭的车、其它车照常用 L3 观察。键鼠观察（C）不受影响。
- 没采用：
  - 全局把手柄观察键换到别的按钮：没有在所有载具上都空闲的按钮。
  - 观察开着时把座位位 0x40 对原版车隐藏：手柄上就再也没有喇叭；而且同一帧里插件别的模块（炮塔镜头、热像组合键）也读座位
    按键字，隐藏需要另给它们一份原值，离线无法确认没有漏读。
- 玩家在 ini 把 `FreeLookButton` 改到别的位时，同一判据决定：与该座位原版读的键重叠就不提供，不重叠照常。

## 实现

- `src/seat_switch_logic.h`：`kCarInputFn` / `kCarPadReads` / `kCarHornButton`、`CarPadReads(seat,horn)`、`PadActionFree(binding,stockReads)`。
- `src/crew.cpp StockPadReads(vehicle,seat)`：按 `kClasses` 的输入函数认出 Car 路径（Car / 607 / 60X），读 `veh+0x29B0` 喇叭标志。
- `src/vehicle_look.cpp VehicleLookOffered(vehicle,seat,keys)`（原 `Offered`，导出）：加上这条判据；`turretcam.cpp FreeHeld` 改为同样问它
  （炮塔镜头的观察以前只看按键，连旋翼机 / 沙扎比的让位也没走，现在一并统一）。
- 没有改任何默认值（`FreeLookButton` 仍是 64），所以不需要安装器的 ini 迁移（`NEW_DEFAULTS_*`）或 COMMON.CFG 迁移。

## 测试

- `tools/seat_switch_check.cpp`（`seat_switch_check`，offline_checks）：`CarPadReads` 只对驾驶座给 A / LB（有喇叭再加 L3）；
  `PadActionFree` 在共享任一按键时不提供。
- `tests/car_input_native_audit.py`（ctest `car_input_native`，需要 EDF.dll，只读）：新增座位 `[veh+0x608]`、喇叭标志 `+0x29B0`、
  两种卡车共用 `0x65A390` 的字节断言，并核对 `seat_switch_logic.h` 常量与 `StockPadReads` / `VehicleLookOffered` / `FreeHeld` 的接线。

## 待实机确认

- 手柄：有喇叭的格雷普驾驶座按住 L3 只鸣笛，不观察、屏幕底部不出现观察提示；坐到乘客座（换座 B）按 L3 可以观察。
- 键鼠：格雷普驾驶座按住 C 观察照旧。
- 不确定格雷普的 SGO 实际是否都带喇叭（`+0x29B0`）；不带的话手柄 L3 在驾驶座上仍是观察（这时不冲突）。
- 未处理的同类重叠：热像组合键 `VehicleThermalButton` 默认 L3+R3，在有喇叭的格雷普驾驶座上按组合键时也会短促鸣笛
  （本次只按问题 2 处理观察键）；机甲类（504 / 612 等）原版也读 0x40 等按键位（`stock-payload-re.md` 表，证据 M，未逆向到每个位
  对应什么），这里没有把它们加进 `StockPadReads`。
