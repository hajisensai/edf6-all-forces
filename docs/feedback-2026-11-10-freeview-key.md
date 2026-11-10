# 2026-11-10 反馈：格雷普喇叭与自由视角同键

## 原话

维护者（2026-11-10）：

> 格雷普喇叭和自由视角同键 默认的自由视角改成c吧

测试站（https://edf6.fushi.moe）截至报告 #47 没有相关条目（`tools/testhub.py state` 查过，无「喇叭 / 自由视角 / 观察键 / 中键」字样）。

## 核对

- 插件观察键 `FreeLookKey` 原默认 `0x04`（鼠标中键）。
- 格雷普是 Car 类车：键盘上让它鸣笛的是原版驾驶页标着「副炮」的那一行（`0x36`，默认鼠标中键），不是标着「喇叭」的那一行
  （`0x4D`，默认右键，Car 上不做事）。逆向见 `keyconfig-re.md` §8（证据级别 H，`tests/car_input_native_audit.py` 钉住）。
  所以按住中键观察时格雷普会同时鸣笛，与原话一致。
- C 原来是高视角 `HighCamKey`。

## 处理

- `FreeLookKey` 默认改 **C（0x43）**，`HighCamKey` 默认改 **L（0x4C）**（C 让给观察键）。
- C / L 在载具情境里的占用（`tests/keybind_test.cpp` 冲突矩阵，含原版各页默认键与 EDF6AutoTurret 的 ini）：改后没有新增重叠；
  原有的「观察键中键 ↔ 原版副炮中键」这条已知重叠随之消失。地图打开时 C 是地图里的支援呼叫（`mapcmd.cpp`），这时插件载具键
  全部让开（`InputAllowed`），与原来高视角在 C 时一样。
- 沙扎比：观察键不再与锁定（中键）同键，沙扎比上也提供键鼠观察（手柄 L3 与沙扎比锁定 R3 不同键，本来就提供）。
- 已有安装的迁移：
  - ini：安装器 `NEW_DEFAULTS_1110`，仍是旧出厂值的 `FreeLookKey=0x04` → `0x43`、`HighCamKey=0x43` → `0x4C`，各只迁一次，玩家自己改过的不动。
  - 游戏按键设置（COMMON.CFG 里的插件页）：`common/edf/keybind.h kCrewSteps`，播种标记里记下已走的默认值步数；仍是旧默认的槽
    才挪，C 被玩家给了别的动作时观察键不挪（不造成两个动作同键），玩家自己设的键不动。

## 未处理（待定）

- 手柄观察键 `FreeLookButton` 默认 L3，格雷普上 L3 同样是喇叭（座位按键位 0x40）。原话只说键鼠默认改 C，这里没改；
  可选做法见 `keyconfig-re.md` §8 末尾。（同日后续已处理：`feedback-2026-11-10-grape-l3.md`。）
- 实机待确认：格雷普上按住 C 观察不再鸣笛、按中键照旧鸣笛；按 L 切高视角；旧 COMMON.CFG 启动后日志有
  `KEYCONFIG default steps 1..1 on the seeded store: 2 key(s) ...`，插件页「载具通用」显示 自由视角 C、高视角 L。
