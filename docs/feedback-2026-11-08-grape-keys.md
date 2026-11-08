# 2026-11-08 反馈：格雷普前进就开同轴机枪、倒车就切挂载

**离线逆向与离线测试，没有在游戏里实测。**

## 用户原话

英文玩家玩完 4.1 离线任务后的反馈（「Textures」一节）：

- 「For armored vehicle types (such as the Grape), since LT and LB are assigned to forward and reverse movement, respectively, you cannot move forward unless you keep firing the coaxial machine gun, and you cannot move backward unless you change your weapon loadout. (could this perhaps be fixed by modifying the ini file?)」

## 原因

逆向见 `docs/stock-payload-re.md` §2.1（`tests/car_input_native_audit.py` 固定）。

- 格雷普是原版 Car 类（vtable `0x17E01B0`）。它的玩家输入（第 55 槽 `0x65A390` 的 `0x65A548..0x65A611`）：
  座位 `+0x2E0`（手柄 LT；键盘是驾驶页的油门键）= **油门**；按键位 0x10（LB；键盘刹车键）= **刹车 / 倒车**；A = 手刹；
  L3 = 喇叭（车有喇叭时）；RT = 主炮；RB（0x20）调的第 83 槽在 Car 上是 `ret 0`，什么都不做。
- 插件（`src/payload.cpp`）的 `Class` 没有 Car，格雷普落进 `other`；`FreeSecondary` 把所有 `other` 都当作「副扳机原版不读」，
  于是格雷普的同轴机枪挂在 `+0x2E0` 上：一踩油门就开机枪（键盘按 W 也一样）。
- 挂载切换在手柄上固定读 LB（座位按键位 0x10）：格雷普一倒车就在主炮弹链之间轮换。
- 根因：给原版车分配附加武器的按键时，没有按每一类原版实际读取的输入做占用判断，而是把「没逆向过的类」默认当成空闲。

## 能不能改 ini 解决

旧版不能完全解决：同轴机枪的扳机和手柄切换键都写在代码里；`StockVehicleStores=0` 再运行安装器能去掉同轴机枪和挂载，冲突随之消失，
但挂载也没了。新版不需要改 ini。

## 修法

- `src/payload.cpp`：`Class` 按 vtable 认出 505、601 与 Car（`kVtCar = 0x17E01B0`，与 `src/edf41bike.cpp kCarVtable` 同一判据）。
  `FreeSecondary` 改成白名单：只有逆向确认不读 `+0x2E0` 的 505 / 601 把副扳机给同轴机枪；Car 与没读过的类都不给。
  没有自己开火键的同轴机枪在这些类上没有控制（`PayloadFire::other`），也不会混进主炮的切换轮。
  Car 的 `+0x2E0` 不再当作副扳机读（火控视图不会因踩油门切到「副武器」）。
  手柄的主炮切换键按类给出（`SwitchButton`）：Car 用 X（Car 不读），其它类仍是 LB。
- 格雷普的同轴机枪改走开火键机制：`pylib/vcobjects.py STOCK_MOUNTS['ifv']` 加 `'MG401': Mounting(4)`，
  安装器生成派生种类 `EDF6VC_MG401_K4_2200.SGO`（`tools/make_stock_stores.py` 的同轴机枪挂载也经 `_mounted`），
  `src/stores.inc` 里它的 `fireKey = 4`（`ControlOf` → `Source::key`）。NPC 车组照旧按同轴机枪的射程 / 锥角开火（`HasShot` 按 `IsCoax` 判）。
- 开火键 4 的手柄默认改为 RB（`VehicleFire4Button = 0x200`，`src/crew.h` 与 `EDF6VehicleCrew.ini`）。原来是 0（手柄上没有开火键 4）。
  其它用开火键 4 的车（X-Morph 单位，V603 类）原版只读 `+0x2E4`，RB 也没有插件功能占用。

### 最终键位（格雷普）

| 动作 | 键盘 | 手柄 |
|---|---|---|
| 前进（原版） | 驾驶页油门键（默认 W） | LT |
| 刹车 / 倒车（原版） | 驾驶页刹车键（默认 S） | LB |
| 主炮（原版） | 鼠标左键 | RT |
| 主炮弹链切换（AP / HE） | R（`PlayerJetSwitchKey`） | **X**（原来 LB） |
| AGM-114 | 开火键 3（X） | 十字键左 |
| 同轴机枪 | **开火键 4（鼠标侧键 4）** | **RB**（原来 LT） |

键盘的开火键 3～6 可在原版按键设置的「插件：地面载具」页改；手柄在 ini 的 `VehicleFire4Button` 改。

## 测试

- `tests/car_input_native_audit.py`（ctest `car_input_native`，需要 EDF.dll，只读）：Car 读 `+0x2E0` 油门、LB 刹车、A 手刹、L3 喇叭、RB 走空的第 83 槽；
  505 / 601 / 603 的输入函数（连同两层被调函数）不读 `+0x2E0` 和按键位；再核对插件的 `FreeSecondary`、`SwitchButton`、`VehicleFire4Button` 默认值、
  `STOCK_MOUNTS` 里格雷普同轴机枪的键都不落在 Car 原版读取的输入上。
- `tests/payload_runtime_test.cpp`：Car 座位踩 LT 不开同轴机枪、RB 开同轴机枪、LB 不切换、X 切换；没读过的类上无键同轴机枪不被副扳机拉、也不进切换轮；
  505 的同轴机枪仍在副扳机。

## 测试站 #29「大部分新加载具按住前进或后退键以后松开仍然移动」

不是同一根因，这次没有改。#29 说的是插件新加的车（战雷模型 / X-Morph 单位等，都是 V603 类），它们的驾驶读左摇杆 / W·S 写的
`+0x2C4`（TankBase `0x67EF80`），不读 `+0x2E0`，也不读 LB；本次的冲突只发生在 Car 类。那条要单独查（例如键盘巡航档
`src/drivegear.cpp` 是否在这些车上接管了松手后的输入，或车辆本身的惯性），需要日志或实机复现。

## 待实机确认

- 手柄：格雷普 LT 前进时不再开机枪，RB 开同轴机枪，LB 倒车不再换弹链，X 换弹链；HUD 控制行显示「[RB] 同轴机枪」「[X] 切换」。
- 键盘：按 W 前进不再开机枪，鼠标侧键 4 开同轴机枪。
- 格雷普若有喇叭（L3），插件的自由视角键（`FreeLookButton` 默认也是 L3）在格雷普上与它重叠；本次没有改。
